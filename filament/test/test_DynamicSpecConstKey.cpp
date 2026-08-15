/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "../src/DynamicSpecConstKey.h"

#include <gtest/gtest.h>

#include <stddef.h>

namespace filament {

namespace {

constexpr bool matchesValidKeyPredicate(
        Variant const variant, MaterialDomain const domain, bool const isLit) noexcept {
    auto const actual = DynamicSpecConstKey::getValidKeys(variant, domain, isLit);
    size_t output = 0;

    for (size_t i = 0; i < DYNAMIC_SPEC_CONST_KEY_COUNT; ++i) {
        DynamicSpecConstKey const candidate{
                static_cast<DynamicSpecConstKey::type_t>(i) };
        if (!DynamicSpecConstKey::isValidProgramSpecKey(
                    variant, candidate, domain, isLit)) {
            continue;
        }
        if (output >= actual.size || actual.keys[output] != candidate) {
            return false;
        }
        ++output;
    }
    return output == actual.size;
}

static_assert(matchesValidKeyPredicate(Variant{}, MaterialDomain::SURFACE, true));
static_assert(matchesValidKeyPredicate(
        Variant{ Variant::SPECIAL_SSR_VARIANT }, MaterialDomain::SURFACE, true));
static_assert(matchesValidKeyPredicate(
        Variant{ Variant::DEPTH_VARIANT }, MaterialDomain::SURFACE, true));
static_assert(matchesValidKeyPredicate(Variant{}, MaterialDomain::SURFACE, false));
static_assert(matchesValidKeyPredicate(Variant{}, MaterialDomain::POST_PROCESS, false));

TEST(DynamicSpecConstKeyTest, LitSurfaceSupportsDynamicLighting) {
    auto const keys = DynamicSpecConstKey::getValidKeys(
            Variant{}, MaterialDomain::SURFACE, true);
    bool hasDynamicLighting = false;
    bool hasNoDynamicLighting = false;
    for (auto const key: keys) {
        hasDynamicLighting |= key.hasDynamicLighting();
        hasNoDynamicLighting |= !key.hasDynamicLighting();
    }
    EXPECT_TRUE(hasDynamicLighting);
    EXPECT_TRUE(hasNoDynamicLighting);
}

TEST(DynamicSpecConstKeyTest, UnsupportedVariantsExcludeDynamicLighting) {
    struct Input {
        Variant variant;
        MaterialDomain domain;
        bool isLit;
    };
    Input const inputs[] = {
        { Variant{ Variant::SPECIAL_SSR_VARIANT }, MaterialDomain::SURFACE, true },
        { Variant{ Variant::DEPTH_VARIANT }, MaterialDomain::SURFACE, true },
        { Variant{}, MaterialDomain::SURFACE, false },
        { Variant{}, MaterialDomain::POST_PROCESS, false },
    };

    for (auto const& input: inputs) {
        auto const keys = DynamicSpecConstKey::getValidKeys(
                input.variant, input.domain, input.isLit);
        ASSERT_GT(keys.size, 0u);
        for (auto const key: keys) {
            EXPECT_FALSE(key.hasDynamicLighting());
        }
    }
}

} // anonymous namespace

} // namespace filament
