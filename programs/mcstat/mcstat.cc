/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2017 Couchbase, Inc.
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

#include <getopt.h>
#include <nlohmann/json.hpp>
#include <programs/getpass.h>
#include <programs/hostname_utils.h>
#include <protocol/connection/client_connection.h>
#include <utilities/terminate_handler.h>

#include <iostream>

/**
 * Request a stat from the server
 * @param sock socket connected to the server
 * @param key the name of the stat to receive (empty == ALL)
 * @param json if true print as json otherwise print old-style
 */
static void request_stat(MemcachedConnection& connection,
                         const std::string& key,
                         bool json,
                         bool format) {
    try {
        if (json) {
            auto stats = connection.stats(key);
            std::cout << stats.dump(format ? 1 : -1, '\t') << std::endl;
        } else {
            connection.stats(
                    [](const std::string& key,
                       const std::string& value) -> void {
                        std::cout << key << " " << value << std::endl;
                    },
                    key);
        }
    } catch (const ConnectionError& ex) {
        std::cerr << ex.what() << std::endl;
    }
}

static void usage() {
    std::cout << "Usage: mcstat [options] statkey ..." << std::endl
              << "  -h hostname[:port]  Host (and optional port number) to retrieve stats from"
              << std::endl
              << "                      (for IPv6 use: [address]:port if you'd like to specify port)"
              << std::endl
              << "  -p port      Port number" << std::endl
              << "  -u username  Username (currently synonymous with -b)"
              << std::endl
              << "  -b bucket    Bucket name" << std::endl
              << "  -P password  Password (if bucket is password-protected)"
              << std::endl
              << "  -S stdin     Read password from stdin (if bucket is password-protected)"
              << std::endl
              << "  -s           Connect to node securely (using SSL)"
              << std::endl
              << "  -j           Print result as JSON (unformatted)"
              << std::endl
              << "  -J           Print result in JSON (formatted)"
              << std::endl
              << "  -4           Use IPv4 (default)" << std::endl
              << "  -6           Use IPv6" << std::endl
              << "  -C certfile  Use certfile as a client certificate"
              << std::endl
              << "  -K keyfile  Use keyfile as a client key" << std::endl
              << "  statkey ...  Statistic(s) to request" << std::endl;
}

int main(int argc, char** argv) {
    // Make sure that we dump callstacks on the console
    install_backtrace_terminate_handler();

    int cmd;
    std::string port{"11210"};
    std::string host{"localhost"};
    std::string user{};
    std::string password{};
    std::string bucket{};
    std::string ssl_cert;
    std::string ssl_key;
    sa_family_t family = AF_UNSPEC;
    bool secure = false;
    bool json = false;
    bool format = false;

    /* Initialize the socket subsystem */
    cb_initialize_sockets();

    while ((cmd = getopt(argc, argv, "46h:p:u:b:P:SsjJC:K:")) != EOF) {
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
        case 'b' :
            bucket.assign(optarg);
            break;
        case 'u' :
            user.assign(optarg);
            break;
        case 'P':
            password.assign(optarg);
            break;
        case 'S':
            password.assign(getpass());
            break;
        case 's':
            secure = true;
            break;
        case 'J':
            format = true;
            // FALLTHROUGH
        case 'j':
            json = true;
            break;
        case 'C':
            ssl_cert.assign(optarg);
            break;
        case 'K':
            ssl_key.assign(optarg);
            break;
        default:
            usage();
            return EXIT_FAILURE;
        }
    }

    if (password.empty()) {
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
        connection.setSslCertFile(ssl_cert);
        connection.setSslKeyFile(ssl_key);

        connection.connect();

        // MEMCACHED_VERSION contains the git sha
        connection.hello("mcstat", MEMCACHED_VERSION,
                         "command line utility to fetch stats");
        try {
            connection.setXerrorSupport(true);
        } catch(std::runtime_error& e) {
            std::cerr << e.what() << std::endl;
        }

        if (!user.empty()) {
            connection.authenticate(user, password,
                                    connection.getSaslMechanisms());
        }

        if (!bucket.empty()) {
            connection.selectBucket(bucket);
        }

        if (optind == argc) {
            request_stat(connection, "", json, format);
        } else {
            for (int ii = optind; ii < argc; ++ii) {
                request_stat(connection, argv[ii], json, format);
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
