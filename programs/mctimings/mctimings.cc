/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2017 Couchbase, Inc
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include "config.h"
#include "programs/hostname_utils.h"
#include "programs/getpass.h"

#include <getopt.h>
#include <memcached/protocol_binary.h>
#include <nlohmann/json.hpp>
#include <platform/string_hex.h>
#include <protocol/connection/client_connection.h>
#include <protocol/connection/client_mcbp_commands.h>
#include <utilities/terminate_handler.h>

#include <strings.h>
#include <array>
#include <cstdlib>
#include <gsl/gsl>
#include <iostream>
#include <stdexcept>

#define JSON_DUMP_INDENT_SIZE 4

// A single bin of a histogram. Holds the raw count and cumulative total (to
// allow percentile to be calculated).
struct Bin {
    uint32_t count = 0;
    uint64_t cumulative_count = 0;
};

class Timings {
public:
    explicit Timings(const nlohmann::json& json) {
        initialize(json);
    }

    uint64_t getTotal() const {
        return total;
    }

    void dumpHistogram(const std::string &opcode)
    {
        std::cout << "The following data is collected for \""
                  << opcode << "\"" << std::endl;

        uint32_t ii;

        dump("ns", 0, 999, ns);
        for (ii = 0; ii < 100; ++ii) {
            dump("us", ii * 10, ((ii + 1) * 10 - 1), us[ii]);
        }

        for (ii = 1; ii < 50; ++ii) {
            dump("ms", ii, ii, ms[ii]);
        }

        dump("ms", 50, 499, halfsec[0]);
        for (ii = 1; ii < 10; ++ii) {
            dump("ms", ii * 500, ((ii + 1) * 500) - 1, halfsec[ii]);
        }

        if (oldwayout) {
            dump("ms", (10 * 500), 0, wayout[0]);
        } else {
            dump("s ", 5, 9, wayout[0]);
            dump("s ", 10, 19, wayout[1]);
            dump("s ", 20, 39, wayout[2]);
            dump("s ", 40, 79, wayout[3]);
            dump("s ", 80, 0, wayout[4]);
        }
        std::cout << "Total: " << total << " operations" << std::endl;
    }

private:

    // Helper function for initialize
    static void update_max_and_total(uint32_t& max, uint64_t& total, Bin& bin) {
        total += bin.count;
        bin.cumulative_count = total;
        if (bin.count > max) {
            max = bin.count;
        }
    }

    void initialize(const nlohmann::json& root) {
        if (root.find("error") != root.end()) {
            // The server responded with an error.. send that to the user
            throw std::runtime_error(root["error"].get<std::string>());
        }

        ns.count = root["ns"].get<uint32_t>();

        auto arr = root["us"].get<std::vector<uint64_t>>();
        size_t i = 0;
        for (auto count : arr) {
            if (i >= us.size()) {
                throw std::runtime_error(
                        "Internal error.. too many \"us\" samples");
            }
            us[i].count = gsl::narrow<uint32_t>(count);
            ++i;
        }

        arr = root["ms"].get<std::vector<uint64_t>>();
        i = 0;
        for (auto count : arr) {
            if (i >= ms.size()) {
                throw std::runtime_error(
                    "Internal error.. too many \"ms\" samples");
            }
            ms[i].count = gsl::narrow<uint32_t>(count);
            ++i;
        }

        arr = root["500ms"].get<std::vector<uint64_t>>();
        i = 0;
        for (auto count : arr) {
            if (i >= halfsec.size()) {
                throw std::runtime_error(
                        R"(Internal error.. too many "halfsec" samples")");
            }
            halfsec[i].count = gsl::narrow<uint32_t>(count);
            ++i;
        }

        try {
            wayout[0].count = root["5s-9s"].get<uint32_t>();
            wayout[1].count = root["10s-19s"].get<uint32_t>();
            wayout[2].count = root["20s-39s"].get<uint32_t>();
            wayout[3].count = root["40s-79s"].get<uint32_t>();
            wayout[4].count = root["80s-inf"].get<uint32_t>();
        } catch (...) {
            wayout[0].count = root["wayout"].get<uint32_t>();
            oldwayout = true;
        }

        // Calculate total and cumulative counts, and find the highest value.
        total = max = 0;

        update_max_and_total(max, total, ns);
        for (auto &val : us) {
            update_max_and_total(max, total, val);
        }
        for (auto &val : ms) {
            update_max_and_total(max, total, val);
        }
        for (auto &val : halfsec) {
            update_max_and_total(max, total, val);
        }
        for (auto &val : wayout) {
            update_max_and_total(max, total, val);
        }
    }

    void dump(const char *timeunit, uint32_t low, uint32_t high,
              const Bin& value)
    {
        if (value.count > 0) {
            char buffer[1024];
            int offset;
            if (low > 0 && high == 0) {
                offset = sprintf(buffer, "[%4u - inf.]%s", low, timeunit);
            } else {
                offset = sprintf(buffer, "[%4u - %4u]%s", low, high, timeunit);
            }
            offset += sprintf(buffer + offset, " (%6.2f%%) ",
                              double(value.cumulative_count) * 100.0 / total);

            // Determine how wide the max value would be, and pad all counts
            // to that width.
            int max_width = snprintf(buffer, 0, "%u", max);
            offset += sprintf(buffer + offset, " %*u", max_width, value.count);

            auto num = (int)(44.0 * (float)value.count / (float)max);
            offset += sprintf(buffer + offset, " | ");
            for (int ii = 0; ii < num; ++ii) {
                offset += sprintf(buffer + offset, "#");
            }

            std::cout << buffer << std::endl;
        }
    }

    /**
     * The highest value of all the samples (used to figure out the width
     * used for each sample in the printout)
     */
    uint32_t max = 0;

    /* We collect timings for <=1 us */
    Bin ns;

    /* We collect timings per 10usec */
    std::array<Bin, 100> us;

    /* we collect timings from 0-49 ms (entry 0 is never used!) */
    std::array<Bin, 50> ms;

    std::array<Bin, 10> halfsec;

    // [5-9], [10-19], [20-39], [40-79], [80-inf].
    std::array<Bin, 5> wayout;

    bool oldwayout = false;

    uint64_t total{};
};

std::string opcode2string(cb::mcbp::ClientOpcode opcode) {
    try {
        return to_string(opcode);
    } catch (const std::exception&) {
        return cb::to_hex(uint8_t(opcode));
    }
}

static void request_cmd_timings(MemcachedConnection& connection,
                                const std::string& bucket,
                                cb::mcbp::ClientOpcode opcode,
                                bool verbose,
                                bool skip,
                                bool json) {
    BinprotGetCmdTimerCommand cmd;
    cmd.setBucket(bucket);
    cmd.setOpcode(opcode);

    connection.sendCommand(cmd);

    BinprotGetCmdTimerResponse resp;
    connection.recvResponse(resp);

    if (!resp.isSuccess()) {
        switch (resp.getStatus()) {
        case cb::mcbp::Status::KeyEnoent:
            std::cerr << "Cannot find bucket: " << bucket << std::endl;
            break;
        case cb::mcbp::Status::Eaccess:
            if (bucket == "/all/") {
                std::cerr << "Not authorized to access aggregated timings data."
                          << std::endl
                          << "Try specifying a bucket by using -b bucketname"
                          << std::endl;

            } else {
                std::cerr << "Not authorized to access timings data"
                          << std::endl;
            }
            break;
        default:
            std::cerr << "Command failed: " << to_string(resp.getStatus())
                      << std::endl;
        }
        exit(EXIT_FAILURE);
    }

    try {
        auto command = opcode2string(opcode);
        if (json) {
            auto timings = resp.getTimings();
            if (timings == nullptr) {
                if (!skip) {
                    std::cerr << "The server doesn't have information about \""
                              << command << "\"" << std::endl;
                }
            } else {
                timings["command"] = command;
                std::cout << timings.dump(JSON_DUMP_INDENT_SIZE) << std::endl;
            }
        } else {
            Timings timings(resp.getTimings());

            if (timings.getTotal() == 0) {
                if (skip == 0) {
                    std::cout << "The server doesn't have information about \""
                              << command << "\"" << std::endl;
                }
            } else {
                if (verbose) {
                    timings.dumpHistogram(command);
                } else {
                    std::cout << command << " " << timings.getTotal()
                              << " operations" << std::endl;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }
}

static void request_stat_timings(MemcachedConnection& connection,
                                 const std::string& key,
                                 bool verbose,
                                 bool json_output) {
    std::map<std::string, std::string> map;
    try {
        map = connection.statsMap(key);
    } catch (const ConnectionError& ex) {
        if (ex.isNotFound()) {
            std::cerr <<"Cannot find statistic: " << key << std::endl;
        } else if (ex.isAccessDenied()) {
            std::cerr << "Not authorized to access timings data" << std::endl;
        } else {
            std::cerr << "Fatal error: " << ex.what() << std::endl;
        }

        exit(EXIT_FAILURE);
    }

    // The return value from stats injects the result in a k-v pair, but
    // these responses (i.e. subdoc_execute) don't include a key,
    // so the statsMap adds them into the map with a counter to make sure
    // that you can fetch all of them. We only expect a single entry, which
    // would be named "0"
    auto iter = map.find("0");
    if (iter == map.end()) {
        std::cerr << "Failed to fetch statistics for \"" << key << "\""
                  << std::endl;
        exit(EXIT_FAILURE);
    }

    // And the value for the item should be valid JSON
    nlohmann::json json = nlohmann::json::parse(iter->second);
    if (json.is_null()) {
        std::cerr << "Failed to fetch statistics for \"" << key << "\". Not json"
                  << std::endl;
        exit(EXIT_FAILURE);
    }
    try {
        if (json_output) {
            json["command"] = key;
            std::cout << json.dump(JSON_DUMP_INDENT_SIZE) << std::endl;
        } else {
            Timings timings(json);
            if (verbose) {
                timings.dumpHistogram(key);
            } else {
                std::cout << key << " " << timings.getTotal() << " operations"
                          << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }
}

void usage() {
    std::cerr << "Usage mctimings [options] [opcode / statname]\n"
              << R"(Options:

  -h or --host hostname[:port]   The host (with an optional port) to connect to
  -p or --port port              The port number to connect to
  -b or --bucket bucketname      The name of the bucket to operate on
  -u or --user username          The name of the user to authenticate as
  -P or --password password      The passord to use for authentication
                                 (use '-' to read from standard input)
  -s or --ssl                    Connect to the server over SSL
  -4 or --ipv4                   Connect over IPv4
  -6 or --ipv6                   Connect over IPv6
  -v or --verbose                Use verbose output
  -S                             Read password from standard input
  -j or --json[=pretty]          Print JSON instead of histograms
  --help                         This help text

)" << std::endl

              << std::endl
              << "Example:" << std::endl
              << "    mctimings --user operator --bucket /all/ --password - "
                 "--verbose GET SET"
              << std::endl;
}

int main(int argc, char** argv) {
    // Make sure that we dump callstacks on the console
    install_backtrace_terminate_handler();

    int cmd;
    std::string port{"11210"};
    std::string host{"localhost"};
    std::string user{};
    std::string password{};
    std::string bucket{"/all/"};
    sa_family_t family = AF_UNSPEC;
    bool verbose = false;
    bool secure = false;
    bool json = false;

    /* Initialize the socket subsystem */
    cb_initialize_sockets();

    struct option long_options[] = {
            {"ipv4", no_argument, nullptr, '4'},
            {"ipv6", no_argument, nullptr, '6'},
            {"host", required_argument, nullptr, 'h'},
            {"port", required_argument, nullptr, 'p'},
            {"bucket", required_argument, nullptr, 'b'},
            {"password", required_argument, nullptr, 'P'},
            {"user", required_argument, nullptr, 'u'},
            {"ssl", no_argument, nullptr, 's'},
            {"verbose", no_argument, nullptr, 'v'},
            {"json", optional_argument, nullptr, 'j'},
            {"help", no_argument, nullptr, 0},
            {nullptr, 0, nullptr, 0}};

    while ((cmd = getopt_long(
                    argc, argv, "46h:p:u:b:P:sSvj", long_options, nullptr)) !=
           EOF) {
        switch (cmd) {
        case '6' :
            family = AF_INET6;
            break;
        case '4' :
            family = AF_INET;
            break;
        case 'h' :
            host.assign(optarg);
            break;
        case 'p':
            port.assign(optarg);
            break;
        case 'S':
            password.assign("-");
            break;
        case 'b' :
            bucket.assign(optarg);
            break;
        case 'u' :
            user.assign(optarg);
            break;
        case 'P':
            password.assign(optarg);
            break;
        case 's':
            secure = true;
            break;
        case 'v' :
            verbose = true;
            break;
        case 'j':
            json = true;
            if (optarg && strcasecmp(optarg, "pretty") == 0) {
                verbose = true;
            }
            break;
        default:
            usage();
            return cmd == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }

    if (password == "-") {
        password.assign(getpass());
    } else if (password.empty()) {
        const char* env_password = std::getenv("CB_PASSWORD");
        if (env_password) {
            password = env_password;
        }
    }

    try {
        in_port_t in_port;
        sa_family_t fam;
        std::tie(host, in_port, fam) = cb::inet::parse_hostname(host, port);

        if (family == AF_UNSPEC) { // The user may have used -4 or -6
            family = fam;
        }
        MemcachedConnection connection(host, in_port, family, secure);

        connection.connect();

        // MEMCACHED_VERSION contains the git sha
        connection.hello("mctimings",
                         MEMCACHED_VERSION,
                         "command line utitilty to fetch command timings");
        connection.setXerrorSupport(true);

        if (!user.empty()) {
            connection.authenticate(user, password,
                                    connection.getSaslMechanisms());
        }

        if (!bucket.empty() && bucket != "/all/") {
            connection.selectBucket(bucket);
        }

        if (optind == argc) {
            for (int ii = 0; ii < 256; ++ii) {
                request_cmd_timings(connection,
                                    bucket,
                                    cb::mcbp::ClientOpcode(ii),
                                    verbose,
                                    true,
                                    json);
            }
        } else {
            for (; optind < argc; ++optind) {
                try {
                    const auto opcode = to_opcode(argv[optind]);
                    request_cmd_timings(
                            connection, bucket, opcode, verbose, false, json);
                } catch (const std::invalid_argument&) {
                    // Not a command timing, try as statistic timing.
                    request_stat_timings(
                            connection, argv[optind], verbose, json);
                }
            }
        }
    } catch (const ConnectionError& ex) {
        std::cerr << ex.what() << std::endl;
        return EXIT_FAILURE;
    } catch (const std::runtime_error& ex) {
        std::cerr << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
