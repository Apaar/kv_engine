/*
 *     Copyright 2019 Couchbase, Inc.
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

#pragma once

#include <memory>
#include <string>

struct EngineIface;

typedef struct server_handle_v1_t SERVER_HANDLE_V1;

/**
 * The ServerBucketIface allows the EWB engine to create buckets without
 * having to load the shared object (and have to worry about when to release
 * it).
 */
struct ServerBucketIface {
    virtual ~ServerBucketIface() = default;

    /**
     * Create a new bucket
     *
     * @param module the name of the shared object containing the bucket
     * @param name the name of the bucket (only used for logging)
     * @param get_server_api the method to provide to the instance
     * @return the newly created engine, or {} if not found
     */
    virtual std::unique_ptr<EngineIface> createBucket(
            const std::string& module,
            const std::string& name,
            SERVER_HANDLE_V1* (*get_server_api)()) const = 0;
};
