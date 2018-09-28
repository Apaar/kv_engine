/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2016 Couchbase, Inc.
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

#include "cookie.h"
#include "function_chain.h"
#include <mcbp/protocol/opcode.h>
#include <memcached/rbac.h>
#include <array>

/**
 * The MCBP privilege chains.
 *
 * This class contains the privilege chains for each of the specified
 * opcodes to allow for a first defence to deny connections access to
 * certain commands. The implementation of certain commands may perform
 * additional checks.
 */
class McbpPrivilegeChains {
public:
    McbpPrivilegeChains();
    McbpPrivilegeChains(const McbpPrivilegeChains&) = delete;

    /**
     * Invoke the chain for the command. If no rule is set up for the command
     * we should fail the request (That would most likely help us not forget
     * to add new rules when people add new commands ;-))
     *
     * @param command the opcode of the command to check access for
     * @param cookie the cookie representing the connection / command
     * @return Ok - the connection holds the appropriate privilege
     *         Fail - the connection does not hold the privileges needed
     *         Stale - the authentication context is out of date
     */
    cb::rbac::PrivilegeAccess invoke(cb::mcbp::ClientOpcode command,
                                     Cookie& cookie);

protected:
    /*
     * Silently ignores any attempt to push the same function onto the chain.
     */
    void setup(cb::mcbp::ClientOpcode command,
               cb::rbac::PrivilegeAccess (*f)(Cookie&));

    std::array<FunctionChain<cb::rbac::PrivilegeAccess,
                             cb::rbac::PrivilegeAccess::Ok,
                             Cookie&>,
               0x100>
            commandChains;
};
