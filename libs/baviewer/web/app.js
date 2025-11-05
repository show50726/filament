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

import {LitElement, html, css, unsafeCSS, nothing} from "https://unpkg.com/lit@2.8.0?module";

const FOREGROUND_COLOR = '#fafafa';
const INACTIVE_COLOR = '#6f6f6f';
const BACKGROUND_COLOR = '#5362e5';

const ALLOCATED_COLOR = '#d43232';
const FREE_COLOR = '#3ac224';
const GPU_LOCKED_COLOR = '#ffeb99';

class SidePanel extends LitElement {
    static get properties() {
        return {
            connected: {type: Boolean, attribute: 'connected'},
            info: {type: Object, state: true},
            isPaused: {type: Boolean, state: true},
        };
    }

    _togglePause() {
        this.dispatchEvent(new CustomEvent('toggle-pause', {
            bubbles: true,
            composed: true
        }));
    }

    dynamicStyle() {
        return `
            :host {
                position: fixed;
                background: ${this.connected ? BACKGROUND_COLOR : INACTIVE_COLOR};
                width: 250px;
                height: 100%;
                padding: 10px 20px;
                color: ${FOREGROUND_COLOR};
            }
            .title {
                color: white;
                width: 100%;
                text-align: center;
                margin: 0 0 10px 0;
                font-size: 20px;
            }
            .info-item {
                margin-bottom: 5px;
            }
            .pause-button {
                background-color: #f44336;
                color: white;
                border: none;
                padding: 10px;
                text-align: center;
                text-decoration: none;
                display: inline-block;
                font-size: 16px;
                margin: 4px 2px;
                cursor: pointer;
                border-radius: 8px;
            }
            .resume-button {
                background-color: #4CAF50;
            }
        `;
    }

    render() {
        return html`
            <style>${this.dynamicStyle()}</style>
            <div class="container">
                <div class="title">baviewer</div>
                <button class="pause-button ${this.isPaused ? 'resume-button' : ''}" @click="${this._togglePause}">
                    ${this.isPaused ? 'Resume' : 'Pause'}
                </button>
                ${this.info ? html`
                    <div class="info-item"><b>Frame ID:</b> ${this.info.frameId}</div>
                    <div class="info-item"><b>Total Size:</b> ${this.info.totalSize}</div>
                    <div class="info-item"><b>Slot Size:</b> ${this.info.slotSize}</div>
                ` : nothing}
            </div>
        `;
    }
}

customElements.define("bav-sidepanel", SidePanel);

class Visualizer extends LitElement {
    static get styles() {
        return css`
            :host {
                display: block;
                padding: 20px;
                flex-grow: 1;
            }
            .slot-container {
                display: flex;
                flex-wrap: wrap;
            }
            .slot {
                border: 1px solid black;
                margin: 2px;
                padding: 5px;
                font-size: 12px;
            }
        `;
    }

    static get properties() {
        return {
            info: {type: Object, state: true},
        };
    }

    _getSlotColor(slot) {
        if (slot.isAllocated) {
            return ALLOCATED_COLOR;
        }
        if (slot.gpuUseCount > 0) {
            return GPU_LOCKED_COLOR;
        }
        return FREE_COLOR;
    }

    render() {
        if (!this.info) {
            return nothing;
        }

        return html`
            <div class="slot-container">
                ${this.info.slots.map(slot => html`
                    <div class="slot" style="background-color: ${this._getSlotColor(slot)}">
                        <div>Offset: ${slot.offset}</div>
                        <div>Size: ${slot.size}</div>
                        <div>GPU Users: ${slot.gpuUseCount}</div>
                    </div>
                `)}
            </div>
        `;
    }
}

customElements.define("bav-visualizer", Visualizer);

class Timeline extends LitElement {
    static get styles() {
        return css`
            :host {
                display: block;
                height: 100px;
                background-color: #333;
                border-top: 1px solid #555;
                overflow-x: auto;
                white-space: nowrap;
            }
            .tick-container {
                display: flex;
                height: 100%;
                align-items: flex-end;
            }
            .tick-wrapper {
                display: flex;
                flex-direction: column;
                align-items: center;
                cursor: pointer;
                margin: 0 5px;
            }
            .tick {
                width: 2px;
                height: 20px;
                background-color: #888;
            }
            .tick-wrapper.changed .tick {
                background-color: #ffc107;
            }
            .tick-wrapper:hover .tick {
                background-color: #ccc;
            }
            .tick-wrapper.active .tick {
                background-color: #fff;
                height: 40px;
            }
            .tick-label {
                color: #fff;
                font-size: 10px;
                margin-bottom: 5px;
            }
        `;
    }

    static get properties() {
        return {
            history: {type: Array, state: true},
            selectedFrame: {type: Number, state: true},
        };
    }

    _handleTickClick(frameId) {
        this.dispatchEvent(new CustomEvent('select-frame', {
            detail: frameId,
            bubbles: true,
            composed: true
        }));
    }

    render() {
        if (!this.history) {
            return nothing;
        }

        return html`
            <div class="tick-container">
                ${this.history.map(item => html`
                    <div 
                        class="tick-wrapper ${this.selectedFrame === item.frameId ? 'active' : ''} ${item.hasChanged ? 'changed' : ''}" 
                        @click="${() => this._handleTickClick(item.frameId)}"
                    >
                        <div class="tick-label">${item.frameId}</div>
                        <div class="tick"></div>
                    </div>
                `)}
            </div>
        `;
    }
}

customElements.define("bav-timeline", Timeline);

class BufferAllocatorVisualizer extends LitElement {
    static get styles() {
        return css`
            :host {
                height: 100%;
                width: 100%;
                display: flex;
            }
            .main-panel {
                display: flex;
                flex-direction: column;
                flex-grow: 1;
            }
        `;
    }

    get _sidePanel() {
        return this.renderRoot.querySelector('#sidepanel');
    }

    get _visualizer() {
        return this.renderRoot.querySelector('#visualizer');
    }

    get _timeline() {
        return this.renderRoot.querySelector('#timeline');
    }

    async init() {
        const isConnected = () => this.connected;
        statusLoop(
            isConnected,
            async (status, data) => {
                this.connected = status !== STATUS_DISCONNECTED;
                if (status === STATUS_INFO_UPDATED && !this.isPaused) {
                    this.history = await fetchHistory();
                    if (this.selectedFrame === null && this.history.length > 0) {
                        this.selectedFrame = this.history[this.history.length - 1].frameId;
                    }
                    this.info = await fetchBufferAllocatorInfo(this.selectedFrame);
                }
            }
        );
        this.history = await fetchHistory();
        if (this.history.length > 0) {
            this.selectedFrame = this.history[this.history.length - 1].frameId;
        }
        this.info = await fetchBufferAllocatorInfo(this.selectedFrame);
    }

    constructor() {
        super();
        this.connected = false;
        this.info = null;
        this.history = [];
        this.selectedFrame = null;
        this.isPaused = false;
        this.init();

        this.addEventListener('select-frame', async (e) => {
            this.selectedFrame = e.detail;
            this.info = await fetchBufferAllocatorInfo(this.selectedFrame);
        });

        this.addEventListener('toggle-pause', async () => {
            this.isPaused = !this.isPaused;
            if (this.isPaused) {
                await pause();
            } else {
                await resume();
            }
        });
    }

    static get properties() {
        return {
            connected: {type: Boolean, state: true},
            info: {type: Object, state: true},
            history: {type: Array, state: true},
            selectedFrame: {type: Number, state: true},
            isPaused: {type: Boolean, state: true},
        }
    }

    updated(props) {
        if (props.has('info')) {
            this._sidePanel.info = this.info;
            this._visualizer.info = this.info;
        }
        if (props.has('history')) {
            this._timeline.history = this.history;
        }
        if (props.has('selectedFrame')) {
            this._timeline.selectedFrame = this.selectedFrame;
        }
        if (props.has('isPaused')) {
            this._sidePanel.isPaused = this.isPaused;
        }
    }

    render() {
        return html`
            <bav-sidepanel id="sidepanel" ?connected="${this.connected}"></bav-sidepanel>
            <div class="main-panel">
                <bav-visualizer id="visualizer"></bav-visualizer>
                <bav-timeline id="timeline"></bav-timeline>
            </div>
        `;
    }
}

customElements.define("bav-root", BufferAllocatorVisualizer);

// Add the root component to the body
const root = document.createElement('bav-root');
document.getElementById('visualizer-container').appendChild(root);
