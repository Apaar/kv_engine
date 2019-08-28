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
#include <algorithm>
#include <cctype>
#include <openssl/ssl.h>
#include <stdexcept>
#include "ssl_utils.h"

long decode_ssl_protocol(const std::string& protocol) {
    /* MB-12359 - Disable SSLv2 & SSLv3 due to POODLE */
    long disallow = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3;

    std::string minimum(protocol);
    std::transform(minimum.begin(), minimum.end(), minimum.begin(), tolower);

    if (minimum.empty() || minimum == "tlsv1") {
        // nothing
    } else if (minimum == "tlsv1.1" || minimum == "tlsv1_1") {
        disallow |= SSL_OP_NO_TLSv1;
    } else if (minimum == "tlsv1.2" || minimum == "tlsv1_2") {
        disallow |= SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1;
    } else if (minimum == "tlsv1.3" || minimum == "tlsv1_3") {
        disallow |= SSL_OP_NO_TLSv1_2 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1;
    } else {
        throw std::invalid_argument("Unknown protocol: " + minimum);
    }

    return disallow;
}

void set_ssl_ctx_ciphers(SSL_CTX* ctx,
                         const std::string& list,
                         const std::string& suites) {
    if (list.empty()) {
        SSL_CTX_set_cipher_list(ctx, "");
    } else if (SSL_CTX_set_cipher_list(ctx, list.c_str()) == 0) {
        throw std::runtime_error(
                "Failed to select any of the requested TLS < 1.3 ciphers (" +
                list + ")");
    }

    if (suites.empty()) {
        SSL_CTX_set_ciphersuites(ctx, "");
    } else if (SSL_CTX_set_ciphersuites(ctx, suites.c_str()) == 0) {
        throw std::runtime_error(
                "Failed to select any of the requested TLS > 1.2 ciphers (" +
                suites + ")");
    }
}
