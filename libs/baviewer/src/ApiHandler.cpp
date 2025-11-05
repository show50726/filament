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

#include "ApiHandler.h"

#include <baviewer/DebugServer.h>
#include <baviewer/JsonWriter.h>

#include <utils/Log.h>
#include <utils/ostream.h>

#include <CivetServer.h>

#include <chrono>
#include <cstring>
#include <mutex>

namespace filament::baviewer {

using namespace std::chrono_literals;

namespace {

auto const& kSuccessHeader = DebugServer::kSuccessHeader;

auto const error = [](int line, std::string const& uri, const char* message = "Not Found") {
    utils::slog.e << "[baviewer] DebugServer: 404 at line " << line << ": " << uri << " (" << message << ")" << utils::io::endl;
    return false;
};

} // anonymous

bool ApiHandler::handleGet(CivetServer* server, struct mg_connection* conn) {
    struct mg_request_info const* request = mg_get_request_info(conn);
    std::string const& uri = request->local_uri;

    if (uri.find("/api/status") == 0) {
        return handleGetStatus(conn);
    }

    if (uri == "/api/info") {
        return handleGetInfo(conn);
    }

    if (uri == "/api/history") {
        return handleGetHistory(conn);
    }

    if (uri == "/api/pause") {
        return handlePause(conn);
    }

    if (uri == "/api/resume") {
        return handleResume(conn);
    }

    return error(__LINE__, uri);
}

void ApiHandler::notify() {
    std::unique_lock const lock(mStatusMutex);
    mCurrentStatus++;
    mStatusCondition.notify_all();
}

bool ApiHandler::handleGetInfo(struct mg_connection* conn) {
    struct mg_request_info const* request = mg_get_request_info(conn);
    char frameIdStr[21] = {};
    bool frameRequested = false;
    if (request->query_string) {
        if (mg_get_var(request->query_string, strlen(request->query_string), "frame", frameIdStr, sizeof(frameIdStr)) > 0) {
            frameRequested = true;
        }
    }

    std::unique_lock const lock(mServer->mHistoryMutex);
    if (mServer->mHistory.empty()) {
        return error(__LINE__, "/api/info", "History is empty");
    }

    BufferAllocatorInfo const* infoToReturn = nullptr;

    if (frameRequested) {
        uint64_t frameId = strtoull(frameIdStr, nullptr, 10);
        // Find the latest info that is less than or equal to the requested frameId
        for (auto it = mServer->mHistory.rbegin(); it != mServer->mHistory.rend(); ++it) {
            if (it->frameId <= frameId) {
                infoToReturn = &(*it);
                break;
            }
        }
    } else {
        infoToReturn = &mServer->mHistory.back();
    }

    if (infoToReturn) {
        JsonWriter writer;
        if (!writer.writeBufferAllocatorInfo(*infoToReturn)) {
            return error(__LINE__, "/api/info", "JSON serialization failed");
        }
        mg_printf(conn, kSuccessHeader.data(), "application/json");
        mg_write(conn, writer.getJsonString(), writer.getJsonSize());
        return true;
    }

    return error(__LINE__, "/api/info", "Frame not found");
}

bool ApiHandler::handleGetHistory(struct mg_connection* conn) {
    std::unique_lock const lock(mServer->mHistoryMutex);
    mg_printf(conn, kSuccessHeader.data(), "application/json");
    mg_printf(conn, "[");
    bool first = true;
    for (const auto& info : mServer->mHistory) {
        if (!first) {
            mg_printf(conn, ",");
        }
        mg_printf(conn, "{\"frameId\":%llu, \"hasChanged\":%s}", info.frameId, info.hasChanged ? "true" : "false");
        first = false;
    }
    mg_printf(conn, "]");
    return true;
}

bool ApiHandler::handleGetStatus(struct mg_connection* conn) {
    std::unique_lock<std::mutex> lock(mStatusMutex);
    uint64_t const currentStatusCount = mCurrentStatus;
    if (mStatusCondition.wait_for(lock, 10s, [this, currentStatusCount] {
        return currentStatusCount < mCurrentStatus;
    })) {
        mg_printf(conn, kSuccessHeader.data(), "application/json");
        std::string const status = "{\"status\": " + std::to_string(mCurrentStatus) + "}";
        mg_write(conn, status.c_str(), status.length());
    } else {
        mg_printf(conn, kSuccessHeader.data(), "application/json");
        mg_write(conn, "{\"status\": \"no_update\"}", 20);
    }
    return true;
}

bool ApiHandler::handlePause(struct mg_connection* conn) {
    mServer->setPaused(true);
    mg_printf(conn, kSuccessHeader.data(), "application/json");
    mg_write(conn, "{\"status\": \"paused\"}", 19);
    return true;
}

bool ApiHandler::handleResume(struct mg_connection* conn) {
    mServer->setPaused(false);
    mg_printf(conn, kSuccessHeader.data(), "application/json");
    mg_write(conn, "{\"status\": \"resumed\"}", 20);
    return true;
}

} // namespace filament::baviewer
