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

#ifndef BAVIEWER_DEBUGSERVER_H
#define BAVIEWER_DEBUGSERVER_H

#include <baviewer/BufferAllocatorInfo.h>

#include <utils/CString.h> 
#include <utils/Mutex.h>

#include <atomic>
#include <deque>
#include <vector>

class CivetServer;

namespace filament::baviewer {

/**
 * Server-side buffer allocator debugger.
 *
 * This class manages an HTTP server. It receives buffer allocator packages from the Filament C++ engine.
 */
class DebugServer {
public:
    static std::string_view const kSuccessHeader;
    static std::string_view const kErrorHeader;

    explicit DebugServer(int port, size_t historySize = 100);
    ~DebugServer();

    /**
     * Updates the information for the buffer allocator.
     */
    void update(BufferAllocatorInfo info);

    void setPaused(bool paused);

    bool isReady() const { return mServer; }

private:
    CivetServer* mServer;

    std::deque<BufferAllocatorInfo> mHistory;
    size_t mMaxHistorySize;
    uint64_t mFrameCounter = 0;
    std::atomic<bool> mPaused = false;
    mutable utils::Mutex mHistoryMutex;

    class FileRequestHandler* mFileHandler = nullptr;
    class ApiHandler* mApiHandler = nullptr;

    friend class ApiHandler;
};

} // namespace filament::baviewer

#endif  // BAVIEWER_DEBUGSERVER_H
