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
 * distributed under permissions and
 * limitations under the License.
 */

#include "BackendTest.h"
#include "PlatformRunner.h"

#include <array>
#include <iostream>
#include <algorithm>

namespace test {

test::NativeView getNativeView() {
    return {
            .ptr = nullptr,
            .width = WINDOW_WIDTH,
            .height = WINDOW_HEIGHT,
    };
}

}// namespace test

namespace {

std::array<test::Backend, 3> const VALID_BACKENDS{
    test::Backend::OPENGL,
    test::Backend::VULKAN,
    test::Backend::WEBGPU
};

}// namespace

int main(int argc, char* argv[]) {
    const auto arguments = test::parseArguments(argc, argv);
    const auto backend = arguments.backend;

    if (!std::any_of(VALID_BACKENDS.begin(), VALID_BACKENDS.end(),
                [backend](test::Backend validBackend) { return backend == validBackend; })) {
        std::cerr << "Specified an invalid backend. Only GL and Vulkan are available on Android" << std::endl;
        return 1;
    }

    const auto operatingSystem = arguments.isContinuousIntegration ?
            test::OperatingSystem::CONTINUOUS_INTEGRATION : test::OperatingSystem::LINUX;
    test::initTests(backend, operatingSystem, true, argc, argv);
    return test::runTests();
}
