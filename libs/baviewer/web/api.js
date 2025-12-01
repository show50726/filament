/*
* Copyright (C) 2025 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License
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

// api.js encapsulates all the REST endpoints that the server provides

async function _fetchJson(uri) {
    const response = await fetch(uri);
    try {
        return await response.json();
    } catch (e) {
        const text = await response.text();
        console.error(`Failed to parse JSON from ${uri}. Response: ${text}`);
        throw e;
    }
}

async function fetchBufferAllocatorInfo(frameId) {
    let url = "api/info";
    if (frameId !== undefined) {
        url += `?frame=${frameId}`;
    }
    return await _fetchJson(url);
}

async function fetchHistory() {
    return await _fetchJson("api/history");
}

async function pause() {
    return await _fetchJson("api/pause");
}

async function resume() {
    return await _fetchJson("api/resume");
}

const STATUS_LOOP_TIMEOUT = 3000;

const STATUS_CONNECTED = 1;
const STATUS_DISCONNECTED = 2;
const STATUS_INFO_UPDATED = 3;

// Status function should be of the form function(status, data)
async function statusLoop(isConnected, onStatus) {
    try {
        const response = await _fetchJson("api/status");
        if (response.status !== 'no_update') {
            onStatus(STATUS_INFO_UPDATED, response.status);
        }
        statusLoop(isConnected, onStatus);
    } catch {
        onStatus(STATUS_DISCONNECTED);
        setTimeout(() => statusLoop(isConnected, onStatus), STATUS_LOOP_TIMEOUT)
    }
}
