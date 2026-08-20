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

#include <uboviewer/DebugServer.h>
#include <uboviewer/UboInfo.h>

#include <civetweb.h>
#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>

namespace filament::uboviewer {
namespace {

constexpr int TEST_PORT = 18086;

UboInfo makeInfo(uint64_t frame, uint32_t totalSize = 256) {
    UboInfo info;
    info.frame = frame;
    info.totalSize = totalSize;
    info.allocations.push_back({
            .id = 1,
            .offset = 0,
            .size = totalSize,
            .state = AllocationState::FREE,
            .name = "<free>",
    });
    return info;
}

std::string download(int port, char const* path) {
    std::array<char, 256> error{};
    std::unique_ptr<mg_connection, decltype(&mg_close_connection)> connection(
            mg_download("127.0.0.1", port, 0, error.data(), error.size(),
                    "GET %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n", path),
            mg_close_connection);
    if (!connection) {
        return {};
    }

    std::string result;
    std::array<char, 1024> buffer{};
    for (int count = mg_read(connection.get(), buffer.data(), buffer.size()); count > 0;
            count = mg_read(connection.get(), buffer.data(), buffer.size())) {
        result.append(buffer.data(), count);
    }
    return result;
}

TEST(UboViewerTest, StoresOnlyChangesAndBoundsHistory) {
    DebugServer server(TEST_PORT, 2);
    ASSERT_TRUE(server.isReady());

    server.publish(makeInfo(1));
    UboInfo gpuCountOnly = makeInfo(2);
    gpuCountOnly.allocations[0].gpuUseCount = 7;
    server.publish(std::move(gpuCountOnly));

    auto query = server.getEventsAfter(0);
    ASSERT_EQ(query.events.size(), 1u);
    EXPECT_EQ(query.events[0].info.frame, 1u);

    UboInfo reallocated = makeInfo(3);
    reallocated.reallocated = true;
    server.publish(std::move(reallocated));
    server.publish(makeInfo(4, 512));

    query = server.getEventsAfter(0);
    EXPECT_TRUE(query.reset);
    EXPECT_EQ(query.oldestSequence, 2u);
    EXPECT_EQ(query.latestSequence, 3u);
    ASSERT_EQ(query.events.size(), 2u);
    EXPECT_TRUE(query.events[0].info.reallocated);
    EXPECT_EQ(query.events[1].info.totalSize, 512u);
}

TEST(UboViewerTest, ServesWebAppAndJsonApi) {
    DebugServer server(TEST_PORT);
    ASSERT_TRUE(server.isReady());
    server.publish(makeInfo(42));

    std::string const html = download(TEST_PORT, "/");
    EXPECT_NE(html.find("Filament UBO Profiler"), std::string::npos);

    std::string const json = download(TEST_PORT, "/api/events?after=0");
    EXPECT_NE(json.find("\"frame\":42"), std::string::npos);
    EXPECT_NE(json.find("\"state\":\"free\""), std::string::npos);
}

} // anonymous namespace
} // namespace filament::uboviewer

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
