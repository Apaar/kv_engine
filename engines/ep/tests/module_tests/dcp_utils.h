/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2018 Couchbase, Inc
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

class DcpConsumer;
class MutationResponse;

/*
 * Calls DcpConsumer::handleResponse() when it is necessary for the
 * DcpConsumer::step() function to proceed (e.g., the last call to step()
 * has put the consumer in a state waiting for a response).
 *
 * @param connection The connection
 */
void handleProducerResponseIfStepBlocked(DcpConsumer& connection);

std::unique_ptr<MutationResponse> makeMutation(uint64_t seqno,
                                               uint16_t vbid,
                                               const std::string& value,
                                               uint64_t opaque);
