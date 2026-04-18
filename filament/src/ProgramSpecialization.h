/*
 * Copyright (C) 2025 The Android Open Source Project
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
#ifndef TNT_FILAMENT_PROGRAMSPECIALIZATION_H
#define TNT_FILAMENT_PROGRAMSPECIALIZATION_H

#include <backend/Program.h>

#include <private/filament/Variant.h>
#include <private/filament/EngineEnums.h>
#include <array>

#include <utils/FixedCapacityVector.h>

#include <cstdint>

namespace filament {

struct DynamicSpecConstKey {
    using type_t = uint16_t;

    static constexpr size_t USED_BITS = CONFIG_NEXT_DYNAMIC_SPEC_CONSTANT - CONFIG_MAX_RESERVED_SPEC_CONSTANTS;
    static constexpr size_t COMBINATION_COUNT = 1 << USED_BITS;

    static auto getKeys() noexcept {
        std::array<DynamicSpecConstKey, COMBINATION_COUNT> keys;
        for (size_t i = 0; i < COMBINATION_COUNT; ++i) {
            keys[i] = DynamicSpecConstKey{uint16_t(i)};
        }
        return keys;
    }

    DynamicSpecConstKey() noexcept = default;
    DynamicSpecConstKey(DynamicSpecConstKey const& rhs) noexcept = default;
    DynamicSpecConstKey& operator=(DynamicSpecConstKey const& rhs) noexcept = default;
    constexpr explicit DynamicSpecConstKey(type_t key) noexcept : key(key) { }

    constexpr bool operator==(DynamicSpecConstKey rhs) const noexcept {
        return key == rhs.key;
    }

    constexpr bool operator!=(DynamicSpecConstKey rhs) const noexcept {
        return key != rhs.key;
    }

    constexpr DynamicSpecConstKey operator & (type_t rhs) const noexcept {
        return DynamicSpecConstKey(key & rhs);
    }

    constexpr DynamicSpecConstKey operator | (type_t rhs) const noexcept {
        return DynamicSpecConstKey(key | rhs);
    }

    constexpr DynamicSpecConstKey operator ~ () const noexcept {
        return DynamicSpecConstKey(~key);
    }

    constexpr bool hasDirectionalLight() const noexcept {
        return (key & 0x1) != 0;
    }

    type_t key = 0u;
};

// A program specialization is a collection of all properties which could yield a different compiled
// program object.
struct ProgramSpecialization {
    // TODO(exv): We would like to use the cache ID of the material instead of the CRC32. The cache
    // ID is supposed to uniquely identify shader programs, which may be shared across materials
    // (especially in the case of depth variants). However, due to runtime code generation, in
    // practice, these shader programs can end up differing significantly across materials.
    //
    // The proper fix for this problem is to identify every single concern that could cause a shader
    // program to possibly change at runtime and add those variables to ProgramSpecialization. Only
    // then can we switch from the CRC32 back to the cache ID and gain savings across material files
    // with identical shader programs.
    uint32_t materialCrc32;
    Variant variant;
    DynamicSpecConstKey dynamicSpecConstKey;
    utils::Slice<const backend::Program::SpecializationConstant> specializationConstants;

    size_t hash() const noexcept;
    bool operator==(ProgramSpecialization const& rhs) const noexcept;
};

} // namespace filament

namespace std {
template<>
struct hash<filament::ProgramSpecialization> {
    inline size_t operator()(const filament::ProgramSpecialization& lhs) const noexcept {
        return lhs.hash();
    }
};
} // namespace std

#endif  // TNT_FILAMENT_PROGRAMSPECIALIZATION_H
