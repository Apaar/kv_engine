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
#include "probabilistic_counter.h"

#include <folly/portability/GTest.h>
#include <limits>

/**
 * Define the increment factor for the ProbabilisticCounter being used for
 * the tests. 0.012 allows an 8-bit ProbabilisticCounter to mimic a uint16
 * counter.
 */
static const double incFactor = 0.012;

/*
 * Unit tests for the ProbabilisticCounter class.
 */

// Test that we can construct a ProbabilisticCounter and when we first call
// generateCounterValue on a counter initialised to zero it will return one.
TEST(ProbabilisticCounterTest, initialInc) {
    ProbabilisticCounter<uint8_t> probabilisticCounter(incFactor);
    uint8_t counter{0};
    EXPECT_EQ(1, probabilisticCounter.generateValue(counter));
}

// Test the a u16int_t counter is considered saturated when it reaches the max
// of uint8_t.
TEST(ProbabilisticCounterrTest, saturateCounter) {
    ProbabilisticCounter<uint8_t> probabilisticCounter(incFactor);
    uint16_t counter{0};
    while (counter != std::numeric_limits<uint8_t>::max()) {
        counter = probabilisticCounter.generateValue(counter);
    }
    EXPECT_TRUE(probabilisticCounter.isSaturated(counter));
}
