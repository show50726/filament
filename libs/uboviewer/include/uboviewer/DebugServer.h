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

#ifndef UBOVIEWER_DEBUGSERVER_H
#define UBOVIEWER_DEBUGSERVER_H

#include <uboviewer/UboInfo.h>

#include <utils/Mutex.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

class CivetServer;

namespace filament::uboviewer {

class DebugServer {
public:
    struct Event {
        uint64_t sequence = 0;
        UboInfo info;
    };

    struct EventQuery {
        uint64_t oldestSequence = 0;
        uint64_t latestSequence = 0;
        bool reset = false;
        std::vector<Event> events;
    };

    explicit DebugServer(int port, size_t historyCapacity = 2048);
    ~DebugServer();

    DebugServer(DebugServer const&) = delete;
    DebugServer(DebugServer&&) = delete;

    void publish(UboInfo&& info);

    [[nodiscard]] EventQuery getEventsAfter(uint64_t sequence) const;
    [[nodiscard]] bool isReady() const noexcept { return mServer != nullptr; }

private:
    CivetServer* mServer = nullptr;
    class FileRequestHandler* mFileHandler = nullptr;
    class ApiHandler* mApiHandler = nullptr;

    size_t const mHistoryCapacity;
    mutable utils::Mutex mEventsMutex;
    std::deque<Event> mEvents UTILS_GUARDED_BY(mEventsMutex);
    uint64_t mLatestSequence UTILS_GUARDED_BY(mEventsMutex) = 0;
};

} // namespace filament::uboviewer

#endif // UBOVIEWER_DEBUGSERVER_H
