/*
 *     Copyright 2019 Couchbase, Inc
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

#include "dcp_replicator.h"

#include "bucket.h"
#include "cluster.h"
#include "dcppipe.h"

#include <event2/event.h>
#include <mcbp/mcbp.h>
#include <memcached/vbucket.h>
#include <platform/strerror.h>
#include <protocol/connection/client_mcbp_commands.h>
#include <atomic>
#include <thread>

#ifndef WIN32
#include <netinet/tcp.h> // For TCP_NODELAY etc
#endif

namespace cb {

namespace test {

DcpReplicator::~DcpReplicator() = default;

class DcpReplicatorImpl : public DcpReplicator {
public:
    explicit DcpReplicatorImpl(DcpPacketFilter& packet_filter)
        : base(event_base_new()), packet_filter(packet_filter) {
    }

    ~DcpReplicatorImpl() override;

    void createPipes(const Cluster& cluster, Bucket& bucket);

    void start();

protected:
    static void thread_main(DcpReplicatorImpl& instance);

    void run();

    void create(const Cluster& cluster, Bucket& bucket, size_t me);

    struct BaseDeleter {
        void operator()(event_base* base) {
            event_base_free(base);
        }
    };
    std::atomic<size_t> num_ready{0};
    std::atomic_bool shutdown{false};
    std::unique_ptr<event_base, BaseDeleter> base;
    std::vector<std::unique_ptr<DcpPipe>> pipelines;
    std::unique_ptr<std::thread> thread;
    DcpPacketFilter& packet_filter;
};

DcpReplicatorImpl::~DcpReplicatorImpl() {
    shutdown = true;
    event_base_loopbreak(base.get());
    for (auto& pipe : pipelines) {
        pipe->close();
    }
    if (thread) {
        thread->join();
    }
}

void DcpReplicatorImpl::createPipes(const Cluster& cluster, Bucket& bucket) {
    for (size_t id = 0; id < cluster.size(); ++id) {
        create(cluster, bucket, id);
    }
}

void DcpReplicatorImpl::start() {
    thread = std::make_unique<std::thread>(
            [this] { return thread_main(*this); });
    // And wait for all of the streams to be set up
    while (num_ready < pipelines.size()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void DcpReplicatorImpl::thread_main(DcpReplicatorImpl& instance) {
    try {
        instance.run();
    } catch (const std::exception& e) {
        if (!instance.shutdown) {
            throw e;
        }
    }
}

void DcpReplicatorImpl::run() {
    event_base_loop(base.get(), 0);
}

std::array<SOCKET, 2> createNotificationPipe() {
    std::array<SOCKET, 2> ret{};
    if (cb::net::socketpair(SOCKETPAIR_AF, SOCK_STREAM, 0, ret.data()) ==
        SOCKET_ERROR) {
        throw std::runtime_error("Can't create notify pipe: " +
                                 cb_strerror(cb::net::get_socket_error()));
    }

    for (auto sock : ret) {
        int flags = 1;
        const auto* flag_ptr = reinterpret_cast<const void*>(&flags);
        cb::net::setsockopt(
                sock, IPPROTO_TCP, TCP_NODELAY, flag_ptr, sizeof(flags));
        cb::net::setsockopt(
                sock, SOL_SOCKET, SO_REUSEADDR, flag_ptr, sizeof(flags));

        if (evutil_make_socket_nonblocking(sock) == -1) {
            throw std::runtime_error("Failed to enable non-blocking: " +
                                     cb_strerror(cb::net::get_socket_error()));
        }
    }

    return ret;
}

void DcpReplicatorImpl::create(const Cluster& cluster,
                               Bucket& bucket,
                               size_t me) {
    const auto& map = bucket.getVbucketMap();

    // Locate all of the vbuckets I'm supposed to be contain a replica for
    std::vector<std::vector<size_t>> vbids(cluster.size());

    for (size_t vb = 0; vb < map.size(); ++vb) {
        for (size_t node = 1; node < map[vb].size(); ++node) {
            if (map[vb][node] == int(me)) {
                vbids[map[vb][0]].push_back(vb);
            }
        }
    }

    std::vector<cb::mcbp::Feature> features = {
            {cb::mcbp::Feature::MUTATION_SEQNO,
             cb::mcbp::Feature::XATTR,
             cb::mcbp::Feature::XERROR,
             cb::mcbp::Feature::SELECT_BUCKET,
             cb::mcbp::Feature::SNAPPY,
             cb::mcbp::Feature::JSON}};

    for (size_t node = 0; node < vbids.size(); ++node) {
        if (vbids[node].empty()) {
            // I don't have any connections towards this node
            continue;
        }

        auto connection = cluster.getConnection(node);
        connection->authenticate("@admin", "password", "PLAIN");
        connection->selectBucket(bucket.getName());
        std::string name("n_" + std::to_string(node) + "->n_" +
                         std::to_string(me));
        connection->setFeatures(name, features);

        // Create and send a DCP open
        auto rsp = connection->execute(BinprotDcpOpenCommand{
                name, 0, cb::mcbp::request::DcpOpenPayload::Producer});
        if (!rsp.isSuccess()) {
            throw std::runtime_error(
                    "DcpReplicatorImpl::start: Failed to set up "
                    "producer: " +
                    name);
        }

        // I need to create a connection to myself
        auto mine = cluster.getConnection(me);
        mine->authenticate("@admin", "password", "PLAIN");
        mine->selectBucket(bucket.getName());
        mine->setFeatures(name, features);
        BinprotDcpOpenCommand consumerOpenCommand{name};
        consumerOpenCommand.setConsumerName("n_" + std::to_string(me));
        rsp = mine->execute(consumerOpenCommand);
        if (!rsp.isSuccess()) {
            throw std::runtime_error(
                    "DcpReplicatorImpl::start: Failed to set up "
                    "consumer: " +
                    name);
        }

        pipelines.emplace_back(
                std::make_unique<DcpPipe>(base.get(),
                                          packet_filter,
                                          "n_" + std::to_string(node),
                                          "n_" + std::to_string(me),
                                          connection->releaseSocket(),
                                          mine->releaseSocket(),
                                          createNotificationPipe(),
                                          [this]() { this->num_ready++; }));
        pipelines.back()->addStreams(vbids[node]);
    }
}

std::unique_ptr<DcpReplicator> DcpReplicator::create(
        const Cluster& cluster,
        Bucket& bucket,
        DcpPacketFilter& packet_filter) {
    auto ret = std::make_unique<DcpReplicatorImpl>(packet_filter);
    ret->createPipes(cluster, bucket);
    ret->start();
    return ret;
}

} // namespace test
} // namespace cb
