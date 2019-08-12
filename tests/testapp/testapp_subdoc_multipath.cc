/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2015 Couchbase, Inc
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

/*
 * Sub-document API multi-path tests
 */

#include "testapp_subdoc_common.h"

// Test multi-path lookup command - simple single SUBDOC_GET
TEST_P(SubdocTestappTest, SubdocMultiLookup_GetSingle) {
    store_document("dict", "{\"key1\":1,\"key2\":\"two\", \"key3\":3.0}");

    SubdocMultiLookupCmd lookup;
    lookup.key = "dict";
    lookup.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_GET, SUBDOC_FLAG_NONE,
                            "key1"});
    std::vector<SubdocMultiLookupResult> expected{{PROTOCOL_BINARY_RESPONSE_SUCCESS, "1"}};
    expect_subdoc_cmd(lookup, PROTOCOL_BINARY_RESPONSE_SUCCESS, expected);

    // Attempt to access non-existent key.
    lookup.key = "dictXXX";
    expected.clear();
    expect_subdoc_cmd(lookup, PROTOCOL_BINARY_RESPONSE_KEY_ENOENT, expected);

    // Attempt to access non-existent path.
    lookup.key = "dict";
    lookup.specs.at(0) = {PROTOCOL_BINARY_CMD_SUBDOC_GET, SUBDOC_FLAG_NONE,
                          "keyXX"};
    expected.push_back({PROTOCOL_BINARY_RESPONSE_SUBDOC_PATH_ENOENT, ""});
    expect_subdoc_cmd(lookup, PROTOCOL_BINARY_RESPONSE_SUBDOC_MULTI_PATH_FAILURE,
                      expected);

    delete_object("dict");
}

// Test multi-path lookup command - simple single SUBDOC_EXISTS
TEST_P(SubdocTestappTest, SubdocMultiLookup_ExistsSingle) {
    store_document("dict", "{\"key1\":1,\"key2\":\"two\", \"key3\":3.0}");

    SubdocMultiLookupCmd lookup;
    lookup.key = "dict";
    lookup.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_EXISTS,
                            SUBDOC_FLAG_NONE, "key1"});
    std::vector<SubdocMultiLookupResult> expected{{PROTOCOL_BINARY_RESPONSE_SUCCESS, ""}};
    expect_subdoc_cmd(lookup, PROTOCOL_BINARY_RESPONSE_SUCCESS, expected);

    delete_object("dict");
}

/* Creates a flat dictionary with the specified number of key/value pairs
 *   Keys are named "key_0", "key_1"...
 *   Values are strings of the form "value_0", value_1"...
 */
unique_cJSON_ptr make_flat_dict(int nelements) {
    cJSON* dict = cJSON_CreateObject();
    for (int i = 0; i < nelements; i++) {
        std::string key("key_" + std::to_string(i));
        std::string value("value_" + std::to_string(i));
        cJSON_AddStringToObject(dict, key.c_str(), value.c_str());
    }
    return unique_cJSON_ptr(dict);
}

static void test_subdoc_multi_lookup_getmulti() {
    auto dict = make_flat_dict(PROTOCOL_BINARY_SUBDOC_MULTI_MAX_PATHS + 1);
    store_document("dict", to_string(dict));

    // Lookup the maximum number of allowed paths - should succeed.
    SubdocMultiLookupCmd lookup;
    lookup.key = "dict";
    std::vector<SubdocMultiLookupResult> expected;
    for (int ii = 0; ii < PROTOCOL_BINARY_SUBDOC_MULTI_MAX_PATHS; ii++)
    {
        std::string key("key_" + std::to_string(ii));
        std::string value("\"value_" + std::to_string(ii) + '"');
        lookup.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_GET,
                                SUBDOC_FLAG_NONE, key});

        expected.push_back({PROTOCOL_BINARY_RESPONSE_SUCCESS,
                            value});
    }
    expect_subdoc_cmd(lookup, PROTOCOL_BINARY_RESPONSE_SUCCESS, expected);

    // Add one more - should fail.
    lookup.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_GET, SUBDOC_FLAG_NONE,
                            "key_" + std::to_string(PROTOCOL_BINARY_SUBDOC_MULTI_MAX_PATHS)});
    expected.clear();
    expect_subdoc_cmd(lookup, PROTOCOL_BINARY_RESPONSE_SUBDOC_INVALID_COMBO,
                      expected);
    reconnect_to_server();

    delete_object("dict");
}
// Test multi-path lookup - multiple GET lookups
TEST_P(SubdocTestappTest, SubdocMultiLookup_GetMulti) {
    test_subdoc_multi_lookup_getmulti();
}

// Test multi-path lookup - multiple GET lookups with various invalid paths.
TEST_P(SubdocTestappTest, SubdocMultiLookup_GetMultiInvalid) {
    store_document("dict", "{\"key1\":1,\"key2\":\"two\",\"key3\":[0,1,2]}");

    // Build a multi-path LOOKUP with a variety of invalid paths
    std::vector<std::pair<std::string, protocol_binary_response_status > > bad_paths({
            {"[0]", PROTOCOL_BINARY_RESPONSE_SUBDOC_PATH_MISMATCH},
            {"key3[3]", PROTOCOL_BINARY_RESPONSE_SUBDOC_PATH_ENOENT},
    });

    SubdocMultiLookupCmd lookup;
    lookup.key = "dict";
    std::vector<SubdocMultiLookupResult> expected;
    for (const auto& path : bad_paths) {
        lookup.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_GET,
                                SUBDOC_FLAG_NONE, path.first});
        expected.push_back({path.second, ""});
    }
    expect_subdoc_cmd(lookup,
                      PROTOCOL_BINARY_RESPONSE_SUBDOC_MULTI_PATH_FAILURE,
                      expected);

    delete_object("dict");
}

// Test multi-path lookup - multiple EXISTS lookups
TEST_P(SubdocTestappTest, SubdocMultiLookup_ExistsMulti) {
    auto dict = make_flat_dict(PROTOCOL_BINARY_SUBDOC_MULTI_MAX_PATHS + 1);
    store_document("dict", to_string(dict));

    // Lookup the maximum number of allowed paths - should succeed.
    SubdocMultiLookupCmd lookup;
    lookup.key = "dict";
    std::vector<SubdocMultiLookupResult> expected;
    for (int ii = 0; ii < PROTOCOL_BINARY_SUBDOC_MULTI_MAX_PATHS; ii++) {
        std::string key("key_" + std::to_string(ii));

        lookup.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_EXISTS,
                                SUBDOC_FLAG_NONE, key });
        expected.push_back({PROTOCOL_BINARY_RESPONSE_SUCCESS, ""});
    }
    expect_subdoc_cmd(lookup, PROTOCOL_BINARY_RESPONSE_SUCCESS, expected);

    // Add one more - should fail.
    lookup.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_EXISTS,
                            SUBDOC_FLAG_NONE,
                            "key_" + std::to_string(PROTOCOL_BINARY_SUBDOC_MULTI_MAX_PATHS)});
    expected.clear();
    expect_subdoc_cmd(lookup, PROTOCOL_BINARY_RESPONSE_SUBDOC_INVALID_COMBO,
                      expected);
    reconnect_to_server();

    delete_object("dict");
}

/******************* Multi-path mutation tests *******************************/

// Test multi-path mutation command - simple single SUBDOC_DICT_ADD
TEST_P(SubdocTestappTest, SubdocMultiMutation_DictAddSingle) {
    store_document("dict", "{}");

    SubdocMultiMutationCmd mutation;
    mutation.key = "dict";
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                              SUBDOC_FLAG_NONE, "key", "\"value\""});
    expect_subdoc_cmd(mutation, PROTOCOL_BINARY_RESPONSE_SUCCESS, {});

    // Check the update actually occurred.
    validate_object("dict", "{\"key\":\"value\"}");

    delete_object("dict");
}

// Test multi-path mutation command - simple multiple SUBDOC_DICT_ADD
TEST_P(SubdocTestappTest, SubdocMultiMutation_DictAddMulti) {
    store_document("dict", "{}");

    SubdocMultiMutationCmd mutation;
    mutation.key = "dict";
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                              SUBDOC_FLAG_NONE, "key1", "1"});
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                              SUBDOC_FLAG_NONE, "key2", "2"});
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                              SUBDOC_FLAG_NONE, "key3", "3"});
    expect_subdoc_cmd(mutation, PROTOCOL_BINARY_RESPONSE_SUCCESS, {});

    // Check the update actually occurred.
    validate_object("dict", "{\"key1\":1,\"key2\":2,\"key3\":3}");

    delete_object("dict");
}

// Test multi-path mutation command - test maximum supported SUBDOC_DICT_ADD
// paths.
static void test_subdoc_multi_mutation_dict_add_max() {
    store_document("dict", "{}");

    SubdocMultiMutationCmd mutation;
    mutation.key = "dict";
    for (int ii = 0; ii < PROTOCOL_BINARY_SUBDOC_MULTI_MAX_PATHS; ii++) {
        std::string path("key_" + std::to_string(ii));
        std::string value("\"value_" + std::to_string(ii) + '"');

        mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                                  SUBDOC_FLAG_NONE, path, value});
    }
    expect_subdoc_cmd(mutation, PROTOCOL_BINARY_RESPONSE_SUCCESS, {});

    // Check the update actually occurred.
    auto dict = make_flat_dict(PROTOCOL_BINARY_SUBDOC_MULTI_MAX_PATHS);
    const auto expected_str = to_string(dict, false);
    validate_object("dict", expected_str.c_str());

    delete_object("dict");

    // Try with one more mutation spec - should fail and document should be
    // unmodified.
    store_document("dict", "{}");
    auto max_id = std::to_string(PROTOCOL_BINARY_SUBDOC_MULTI_MAX_PATHS);
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                              SUBDOC_FLAG_NONE, "key_" + max_id,
                              "\"value_" + max_id + '"'});
    expect_subdoc_cmd(mutation, PROTOCOL_BINARY_RESPONSE_SUBDOC_INVALID_COMBO,
                      {});

    reconnect_to_server();

    // Document should be unmodified.
    validate_object("dict", "{}");

    delete_object("dict");
}
TEST_P(SubdocTestappTest, SubdocMultiMutation_DictAddMax) {
    test_subdoc_multi_mutation_dict_add_max();
}

// Test attempting to add the same key twice in a multi-path command.
TEST_P(SubdocTestappTest, SubdocMultiMutation_DictAddInvalidDuplicate) {
    store_document("dict", "{}");

    SubdocMultiMutationCmd mutation;
    mutation.key = "dict";
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                              SUBDOC_FLAG_NONE, "key", "\"value\""});
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                              SUBDOC_FLAG_NONE, "key", "\"value2\""});
    // Should return failure, with the index of the failing op (1).
    expect_subdoc_cmd(mutation,
                      PROTOCOL_BINARY_RESPONSE_SUBDOC_MULTI_PATH_FAILURE,
                      {{1, PROTOCOL_BINARY_RESPONSE_SUBDOC_PATH_EEXISTS}});

    // Document should be unmodified.
    validate_object("dict", "{}");

    delete_object("dict");
}

// Test multi-path mutation command - 2x DictAdd with a Counter update
TEST_P(SubdocTestappTest, SubdocMultiMutation_DictAddCounter) {
    store_document("dict", "{\"count\":0,\"items\":{}}");

    SubdocMultiMutationCmd mutation;
    mutation.key = "dict";
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                              SUBDOC_FLAG_NONE, "items.foo", "1"});
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                              SUBDOC_FLAG_NONE, "items.bar", "2"});
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_COUNTER,
                              SUBDOC_FLAG_NONE, "count", "2"});
    expect_subdoc_cmd(mutation, PROTOCOL_BINARY_RESPONSE_SUCCESS,
                      {{2, PROTOCOL_BINARY_RESPONSE_SUCCESS, "2"}});

    // Check the update actually occurred.
    validate_object("dict",
                    "{\"count\":2,\"items\":{\"foo\":1,\"bar\":2}}");

    delete_object("dict");
}

// Test multi-path mutation command - 2x DictAdd with specific CAS.
TEST_P(SubdocTestappTest, SubdocMultiMutation_DictAddCAS) {
    store_document("dict", "{\"int\":1}");

    // Use SUBDOC_EXISTS to obtain the current CAS.
    BinprotSubdocResponse resp;
    BinprotSubdocCommand request(PROTOCOL_BINARY_CMD_SUBDOC_EXISTS);
    request.setPath("int").setKey("dict");
    subdoc_verify_cmd(request, PROTOCOL_BINARY_RESPONSE_SUCCESS, "", resp);
    auto cas = resp.getCas();

    // 1. Attempt to mutate with an incorrect CAS - should fail.
    SubdocMultiMutationCmd mutation;
    mutation.key = "dict";
    mutation.cas = cas - 1;
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                              SUBDOC_FLAG_NONE, "float", "2.0"});
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                              SUBDOC_FLAG_NONE, "string", "\"value\""});
    expect_subdoc_cmd(mutation, PROTOCOL_BINARY_RESPONSE_KEY_EEXISTS, {});
    // Document should be unmodified.
    validate_object("dict", "{\"int\":1}");

    // 2. Attempt to mutate with correct CAS.
    mutation.cas = cas;
    uint64_t new_cas = expect_subdoc_cmd(mutation,
                                         PROTOCOL_BINARY_RESPONSE_SUCCESS, {});

    // CAS should have changed.
    EXPECT_NE(cas, new_cas);

    // Document should have been updated.
    validate_object("dict", "{\"int\":1,\"float\":2.0,\"string\":\"value\"}");

    delete_object("dict");
}

// Test multi-path mutation command - create a bunch of dictionary elements
// then delete them. (Not a very useful operation but should work).
void test_subdoc_multi_mutation_dictadd_delete() {
    store_document("dict", "{\"count\":0,\"items\":{}}");

    // 1. Add a series of paths, then remove two of them.
    SubdocMultiMutationCmd mutation;
    mutation.key = "dict";
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                              SUBDOC_FLAG_NONE, "items.1", "1"});
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                              SUBDOC_FLAG_NONE, "items.2", "2"});
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                              SUBDOC_FLAG_NONE, "items.3", "3"});
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_COUNTER,
                              SUBDOC_FLAG_NONE, "count", "3"});
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DELETE,
                              SUBDOC_FLAG_NONE, "items.1"});
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DELETE,
                              SUBDOC_FLAG_NONE, "items.3"});
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_COUNTER,
                              SUBDOC_FLAG_NONE, "count", "-2"});
    expect_subdoc_cmd(mutation, PROTOCOL_BINARY_RESPONSE_SUCCESS,
                      {{3, PROTOCOL_BINARY_RESPONSE_SUCCESS, "3"},
                       {6, PROTOCOL_BINARY_RESPONSE_SUCCESS, "1"}});

    // Document should have been updated.
    validate_object("dict", "{\"count\":1,\"items\":{\"2\":2}}");

    // 2. Delete the old 'items' dictionary and create a new one.
    mutation.specs.clear();
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DELETE,
                              SUBDOC_FLAG_NONE, "items"});
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_UPSERT,
                              SUBDOC_FLAG_NONE, "count", "0"});
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                              SUBDOC_FLAG_MKDIR_P, "items.4", "4"});
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                              SUBDOC_FLAG_NONE, "items.5", "5"});
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_COUNTER,
                              SUBDOC_FLAG_NONE, "count", "2"});
    expect_subdoc_cmd(mutation, PROTOCOL_BINARY_RESPONSE_SUCCESS,
                      {{4, PROTOCOL_BINARY_RESPONSE_SUCCESS, "2"}});

    validate_object("dict", "{\"count\":2,\"items\":{\"4\":4,\"5\":5}}");

    delete_object("dict");
}

TEST_P(SubdocTestappTest, SubdocMultiMutation_DictAddDelete) {
    test_subdoc_multi_mutation_dictadd_delete();
}

TEST_P(SubdocTestappTest, SubdocMultiMutation_DictAddDelete_MutationSeqno) {
    set_mutation_seqno_feature(true);
    test_subdoc_multi_mutation_dictadd_delete();
    set_mutation_seqno_feature(false);
}

// Test support for expiration on multi-path commands.
TEST_P(SubdocTestappTest, SubdocMultiMutation_Expiry) {
    // Create two documents; one to be used for an exlicit 1s expiry and one
    // for an explicit 0s (i.e. never) expiry.
    store_document("ephemeral", "[\"a\"]");
    store_document("permanent", "[\"a\"]");

    // Expiry not permitted for MULTI_LOOKUP operations.
    SubdocMultiLookupCmd lookup;
    lookup.key = "ephemeral";
    lookup.expiry = 666;
    lookup.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_EXISTS,
                            SUBDOC_FLAG_NONE, "[0]" });
    expect_subdoc_cmd(lookup, PROTOCOL_BINARY_RESPONSE_EINVAL, {});
    reconnect_to_server();

    // Perform a MULTI_REPLACE operation, setting a expiry of 1s.
    SubdocMultiMutationCmd mutation;
    mutation.key = "ephemeral";
    mutation.expiry = 1;
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_REPLACE,
                              SUBDOC_FLAG_NONE, "[0]", "\"b\""});
    expect_subdoc_cmd(mutation, PROTOCOL_BINARY_RESPONSE_SUCCESS, {});

    // Try to read the document immediately - should exist.
    auto result = fetch_value("ephemeral");
    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_SUCCESS, result.first);
    EXPECT_EQ("[\"b\"]", result.second);

    // Perform a REPLACE on permanent, explicitly encoding an expiry of 0s.
    mutation.key = "permanent";
    mutation.expiry = 0;
    mutation.encode_zero_expiry_on_wire = true;
    expect_subdoc_cmd(mutation, PROTOCOL_BINARY_RESPONSE_SUCCESS, {});

    // Try to read the second document immediately - should exist.
    result = fetch_value("permanent");
    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_SUCCESS, result.first);
    EXPECT_EQ("[\"b\"]", result.second);

    // Sleep for 2s seconds.
    // TODO: it would be great if we could somehow accelerate time from the
    // harness, and not add 2s to the runtime of the test...
    usleep(2 * 1000 * 1000);

    // Try to read the ephemeral document - shouldn't exist.
    result = fetch_value("ephemeral");
    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_KEY_ENOENT, result.first);

    // Try to read the permanent document - should still exist.
    result = fetch_value("permanent");
    EXPECT_EQ(PROTOCOL_BINARY_RESPONSE_SUCCESS, result.first);
    EXPECT_EQ("[\"b\"]", result.second);
}

// Test statistics support for multi-lookup commands
TEST_P(SubdocTestappTest, SubdocStatsMultiLookup) {
    // A multi-lookup counts as a single operation, irrespective of how many
    // path specs it contains.

    // Get initial stats
    auto stats = request_stats();
    auto count_before = extract_single_stat(stats, "cmd_subdoc_lookup");
    auto bytes_before_total = extract_single_stat(stats, "bytes_subdoc_lookup_total");
    auto bytes_before_subset = extract_single_stat(stats, "bytes_subdoc_lookup_extracted");

    // Perform a multi-lookup containing >1 path.
    test_subdoc_multi_lookup_getmulti();

    // Get subsequent stats, check stat increased by one.
    stats = request_stats();
    auto count_after = extract_single_stat(stats, "cmd_subdoc_lookup");
    auto bytes_after_total = extract_single_stat(stats, "bytes_subdoc_lookup_total");
    auto bytes_after_subset = extract_single_stat(stats, "bytes_subdoc_lookup_extracted");
    EXPECT_EQ(1, count_after - count_before);
    EXPECT_EQ(373, bytes_after_total - bytes_before_total);
    EXPECT_EQ(246, bytes_after_subset - bytes_before_subset);
}

// Test statistics support for multi-mutation commands
TEST_P(SubdocTestappTest, SubdocStatsMultiMutation) {
    // A multi-mutation counts as a single operation, irrespective of how many
    // path specs it contains.

    // Get initial stats
    auto stats = request_stats();
    auto count_before = extract_single_stat(stats, "cmd_subdoc_mutation");
    auto bytes_before_total = extract_single_stat(stats, "bytes_subdoc_mutation_total");
    auto bytes_before_subset = extract_single_stat(stats, "bytes_subdoc_mutation_inserted");

    // Perform a multi-mutation containing >1 path.
    test_subdoc_multi_mutation_dict_add_max();

    // Get subsequent stats, check stat increased by one.
    stats = request_stats();
    auto count_after = extract_single_stat(stats, "cmd_subdoc_mutation");
    auto bytes_after_total = extract_single_stat(stats, "bytes_subdoc_mutation_total");
    auto bytes_after_subset = extract_single_stat(stats, "bytes_subdoc_mutation_inserted");
    EXPECT_EQ(count_before + 1, count_after);
    EXPECT_EQ(301, bytes_after_total - bytes_before_total);
    EXPECT_EQ(150, bytes_after_subset - bytes_before_subset);
}

// Test support for multi-mutations returning values - maximum spec count
TEST_P(SubdocTestappTest, SubdocMultiMutation_MaxResultSpecValue) {
    // Create an array of PROTOCOL_BINARY_SUBDOC_MULTI_MAX_PATHS counters.
    std::string input("[");
    std::string expected_json("[");
    for (int ii = 0; ii < PROTOCOL_BINARY_SUBDOC_MULTI_MAX_PATHS; ii++) {
        input += "0,";
        expected_json += std::to_string(ii + 1) + ",";
    }
    input.pop_back();
    input += ']';
    store_document("array", input);
    expected_json.pop_back();
    expected_json += ']';

    SubdocMultiMutationCmd mutation;
    mutation.key = "array";
    std::vector<SubdocMultiMutationResult> expected_results;
    for (uint8_t ii = 0; ii < PROTOCOL_BINARY_SUBDOC_MULTI_MAX_PATHS; ii++) {
        std::string value("[" + std::to_string(ii) + "]");
        mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_COUNTER,
            SUBDOC_FLAG_NONE, value,
            std::to_string(ii + 1)});

        expected_results.push_back({ii, PROTOCOL_BINARY_RESPONSE_SUCCESS,
            std::to_string(ii + 1)});
    }

    expect_subdoc_cmd(mutation, PROTOCOL_BINARY_RESPONSE_SUCCESS,
                      expected_results);

    validate_object("array", expected_json);

    delete_object("array");

}

// Test that flags are preserved by subdoc multipath mutation operations.
TEST_P(SubdocTestappTest, SubdocMultiMutation_Flags) {
    const uint32_t flags = 0xcafebabe;
    store_object_w_datatype("array", "[]", flags, 0, cb::mcbp::Datatype::Raw);

    SubdocMultiMutationCmd mutation;
    mutation.key = "array";
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_ARRAY_PUSH_LAST,
                              SUBDOC_FLAG_NONE, "", "0"});
    expect_subdoc_cmd(mutation, PROTOCOL_BINARY_RESPONSE_SUCCESS, {});

    // Check the update actually occurred.
    validate_object("array", "[0]");
    validate_flags("array", flags);

    delete_object("array");
}

// Test that you can create a document with the Add doc flag
TEST_P(SubdocTestappTest, SubdocMultiMutation_AddDocFlag) {
    SubdocMultiMutationCmd mutation;
    mutation.addDocFlag(mcbp::subdoc::doc_flag::Add);
    mutation.key = "AddDocTest";
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_UPSERT,
                              SUBDOC_FLAG_NONE,
                              "test",
                              "56"});
    expect_subdoc_cmd(mutation, PROTOCOL_BINARY_RESPONSE_SUCCESS, {});
    validate_object("AddDocTest", "{\"test\":56}");

    delete_object("AddDocTest");
}

// Test that a command with an Add doc flag fails if the key exists
TEST_P(SubdocTestappTest, SubdocMultiMutation_AddDocFlagEEXists) {
    store_document("AddDocExistsTest", "[1,2,3,4]");

    SubdocMultiMutationCmd mutation;
    mutation.addDocFlag(mcbp::subdoc::doc_flag::Add);
    mutation.key = "AddDocExistsTest";
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_UPSERT,
                              SUBDOC_FLAG_NONE,
                              "test",
                              "56"});
    expect_subdoc_cmd(mutation, PROTOCOL_BINARY_RESPONSE_KEY_EEXISTS, {});

    delete_object("AddDocExistsTest");

    // Now the doc is deleted, we should be able to Add successfully
    expect_subdoc_cmd(mutation, PROTOCOL_BINARY_RESPONSE_SUCCESS, {});
    validate_object("AddDocExistsTest", "{\"test\":56}");

    delete_object("AddDocExistsTest");
}

// An Addd doesn't make sense with a cas, check that it's rejected
TEST_P(SubdocTestappTest, SubdocMultiMutation_AddDocFlagInavlidCas) {
    SubdocMultiMutationCmd mutation;
    mutation.addDocFlag(mcbp::subdoc::doc_flag::Add);
    mutation.key = "AddDocCas";
    mutation.cas = 123456;
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_UPSERT,
                              SUBDOC_FLAG_NONE,
                              "test",
                              "56"});
    expect_subdoc_cmd(mutation, PROTOCOL_BINARY_RESPONSE_EINVAL, {});
}

// MB-30278: Perform a multi-mutation with two paths with backticks in them.
TEST_P(SubdocTestappTest, MB_30278_SubdocBacktickMultiMutation) {
    store_document("dict", "{}");

    SubdocMultiMutationCmd mutation;
    mutation.key = "dict";
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                              SUBDOC_FLAG_NONE,
                              "key1``",
                              "1"});
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                              SUBDOC_FLAG_NONE,
                              "key2``",
                              "2"});
    mutation.specs.push_back({PROTOCOL_BINARY_CMD_SUBDOC_DICT_ADD,
                              SUBDOC_FLAG_NONE,
                              "key3``",
                              "3"});
    expect_subdoc_cmd(mutation, PROTOCOL_BINARY_RESPONSE_SUCCESS, {});

    validate_object("dict", R"({"key1`":1,"key2`":2,"key3`":3})");

    delete_object("dict");
}

// Test that when a wholedoc set is performed as part of a multi mutation
// the datatype is set JSON/RAW based on whether the document is actually valid
// json
TEST_P(SubdocTestappTest, WholedocMutationUpdatesDatatype) {
    // store an initial json document
    store_document("item", "{}");

    bool jsonSupported = hasJSONSupport() == ClientJSONSupport::Yes;

    // check datatype is json (if supported)
    validate_datatype_is_json("item", jsonSupported);

    // do a wholedoc mutation setting a non-json value
    {
        SubdocMultiMutationCmd mutation;
        mutation.key = "item";
        mutation.specs.push_back({uint8_t(cb::mcbp::ClientOpcode::Set),
                                  SUBDOC_FLAG_NONE,
                                  "",
                                  "RAW ;: NO JSON HERE"});

        expect_subdoc_cmd(mutation, uint16_t(cb::mcbp::Status::Success), {});
    }

    // check the datatype has been updated
    validate_datatype_is_json("item", false);

    // wholedoc mutation to set a json doc again
    {
        SubdocMultiMutationCmd mutation;
        mutation.key = "item";
        mutation.specs.push_back({uint8_t(cb::mcbp::ClientOpcode::Set),
                                  SUBDOC_FLAG_NONE,
                                  "",
                                  R"({"json":"goodness"})"});

        expect_subdoc_cmd(mutation, uint16_t(cb::mcbp::Status::Success), {});
    }

    // check datatype is back to json
    validate_datatype_is_json("item", jsonSupported);
    validate_object("item", R"({"json":"goodness"})");

    delete_object("item");
}

TEST_P(SubdocTestappTest, TestSubdocOpAfterWholedocSetNonJson) {
    // store an initial json document
    store_document("item", "{}");

    bool jsonSupported = hasJSONSupport() == ClientJSONSupport::Yes;

    // check datatype is json (if supported)
    validate_datatype_is_json("item", jsonSupported);

    std::vector<cb::mcbp::ClientOpcode> mutationOpcodes = {
            cb::mcbp::ClientOpcode::SubdocDictAdd,
            cb::mcbp::ClientOpcode::SubdocDictUpsert,
            cb::mcbp::ClientOpcode::SubdocReplace,
            cb::mcbp::ClientOpcode::SubdocArrayPushLast,
            cb::mcbp::ClientOpcode::SubdocArrayPushFirst,
            cb::mcbp::ClientOpcode::SubdocArrayAddUnique};

    // expect that any Subdoc mutation following a non-json wholedoc will fail
    for (size_t i = 0; i < mutationOpcodes.size(); i++) {
        SubdocMultiMutationCmd mutation;
        mutation.key = "item";

        std::vector<SubdocMultiMutationResult> expected;

        // whole doc non-json
        mutation.specs.push_back({uint8_t(cb::mcbp::ClientOpcode::Set),
                                  SUBDOC_FLAG_NONE,
                                  "",
                                  "RAW ;: NO JSON HERE"});

        // subdoc op on any path should fail - doc is not json
        mutation.specs.push_back({uint8_t(mutationOpcodes.at(i)),
                                  SUBDOC_FLAG_NONE,
                                  "path-" + std::to_string(i),
                                  "anyvalue"});

        expect_subdoc_cmd(
                mutation,
                uint16_t(cb::mcbp::Status::SubdocMultiPathFailure),
                {{1, uint16_t(cb::mcbp::Status::SubdocDocNotJson), ""}});
    }

    // doc should be unchanged
    validate_object("item", "{}");

    delete_object("item");
}

TEST_P(SubdocTestappTest,
       TestSubdocOpInSeperateMultiMutationAfterWholedocSetNonJson) {
    // store an initial json document
    store_document("item", "{}");

    bool jsonSupported = hasJSONSupport() == ClientJSONSupport::Yes;

    // check datatype is json (if supported)
    validate_datatype_is_json("item", jsonSupported);

    // do a wholedoc mutation setting a non-json value
    {
        SubdocMultiMutationCmd mutation;
        mutation.key = "item";
        mutation.specs.push_back({uint8_t(cb::mcbp::ClientOpcode::Set),
                                  SUBDOC_FLAG_NONE,
                                  "",
                                  "RAW ;: NO JSON HERE"});

        expect_subdoc_cmd(mutation, uint16_t(cb::mcbp::Status::Success), {});
    }

    // check the datatype has been updated
    validate_datatype_is_json("item", false);

    // expect that any Subdoc lookup op will fail
    {
        SubdocMultiLookupCmd lookup;
        lookup.key = "item";
        for (auto opcode : {cb::mcbp::ClientOpcode::SubdocGet,
                            cb::mcbp::ClientOpcode::SubdocExists}) {
            lookup.specs.push_back(
                    {uint8_t(opcode), SUBDOC_FLAG_NONE, "anypath"});
        }

        expect_subdoc_cmd(lookup,
                          uint16_t(cb::mcbp::Status::SubdocMultiPathFailure),
                          {{uint16_t(cb::mcbp::Status::SubdocDocNotJson), ""},
                           {uint16_t(cb::mcbp::Status::SubdocDocNotJson), ""}});
    }

    std::vector<cb::mcbp::ClientOpcode> opcodes = {
            cb::mcbp::ClientOpcode::SubdocDictAdd,
            cb::mcbp::ClientOpcode::SubdocDictUpsert,
            cb::mcbp::ClientOpcode::SubdocReplace,
            cb::mcbp::ClientOpcode::SubdocArrayPushLast,
            cb::mcbp::ClientOpcode::SubdocArrayPushFirst,
            cb::mcbp::ClientOpcode::SubdocArrayAddUnique};

    // expect that any Subdoc mutation will fail
    for (size_t i = 0; i < opcodes.size(); i++) {
        SubdocMultiMutationCmd mutation;
        mutation.key = "item";

        std::vector<SubdocMultiMutationResult> expected;

        mutation.specs.push_back({uint8_t(opcodes.at(i)),
                                  SUBDOC_FLAG_NONE,
                                  "path-" + std::to_string(i),
                                  "anyvalue"});

        expect_subdoc_cmd(
                mutation,
                uint16_t(cb::mcbp::Status::SubdocMultiPathFailure),
                {{0, uint16_t(cb::mcbp::Status::SubdocDocNotJson), ""}});
    }

    validate_object("item", "RAW ;: NO JSON HERE");

    delete_object("item");
}
