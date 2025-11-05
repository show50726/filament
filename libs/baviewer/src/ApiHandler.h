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

#ifndef BAVIEWER_APIHANDLER_H
#define BAVIEWER_APIHANDLER_H

#include <baviewer/DebugServer.h>

#include <CivetServer.h>

#include <condition_variable>
#include <mutex>

namespace filament::baviewer {

class DebugServer;

// Handles the following REST requests:
//
//    GET /api/info?frame={frameId}
//    GET /api/history
//    GET /api/status
//    GET /api/pause
//    GET /api/resume
//
class ApiHandler : public CivetHandler {
public:
    explicit ApiHandler(DebugServer* server)
        : mServer(server) {}
    ~ApiHandler() = default;

    bool handleGet(CivetServer* server, struct mg_connection* conn);

    void notify();

private:
    bool handleGetInfo(struct mg_connection* conn);
    bool handleGetHistory(struct mg_connection* conn);
    bool handleGetStatus(struct mg_connection* conn);
    bool handlePause(struct mg_connection* conn);
    bool handleResume(struct mg_connection* conn);

    DebugServer* mServer;

    std::mutex mStatusMutex;
    std::condition_variable mStatusCondition;
    uint64_t mCurrentStatus = 0;
};

} // namespace filament::baviewer

#endif // BAVIEWER_APIHANDLER_H
