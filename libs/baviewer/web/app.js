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
                background: ${this.connected ? BACKGROUND_COLOR : INACTIVE_COLOR};
                width: 250px;
                height: 100%;
                padding: 10px 20px;
                color: ${FOREGROUND_COLOR};
                box-sizing: border-box;
                flex-shrink: 0;
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
                background-color: #ffffff;
                color: #333333;
                overflow-y: auto;
                overflow-x: hidden;
            }
            .allocation-bar {
                display: flex;
                width: 100%;
                height: 60px;
                background-color: #333;
                border: 1px solid #555;
                border-radius: 4px;
                overflow: hidden;
                margin-bottom: 20px;
                box-shadow: 0 4px 6px rgba(0,0,0,0.3);
                transform-origin: left center;
                transition: transform 0.2s;
                min-width: 100%;
            }
            .allocation-bar-container {
                width: 100%;
                overflow-x: auto;
                margin-bottom: 20px;
            }
            .segment {
                height: 100%;
                transition: flex-grow 0.3s, background-color 0.3s;
                position: relative;
                cursor: pointer;
            }
            .segment:hover {
                filter: brightness(1.2);
                z-index: 10;
                outline: 2px solid #fff;
            }
            .segment-tooltip {
                position: absolute;
                bottom: 100%;
                left: 50%;
                transform: translateX(-50%);
                background: #f0f0f0;
                color: #333;
                padding: 8px;
                border-radius: 4px;
                font-size: 12px;
                white-space: nowrap;
                visibility: hidden;
                opacity: 0;
                transition: opacity 0.2s;
                pointer-events: none;
                z-index: 100;
                border: 1px solid #555;
            }
            .segment:hover .segment-tooltip {
                visibility: visible;
                opacity: 1;
            }
            .legend {
                display: flex;
                gap: 15px;
                margin-top: 20px;
                flex-wrap: wrap;
            }
            .legend-item {
                display: flex;
                align-items: center;
                gap: 5px;
                font-size: 14px;
            }
            .legend-color {
                width: 16px;
                height: 16px;
                border-radius: 4px;
            }
            .details-panel {
                margin-top: 20px;
                padding: 15px;
                background-color: #f5f5f5;
                border-radius: 4px;
                border: 1px solid #ddd;
            }
            .details-title {
                font-size: 16px;
                font-weight: bold;
                margin-bottom: 10px;
                color: #333;
            }
            .details-grid {
                display: grid;
                grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
                gap: 10px;
            }
            .details-item {
                display: flex;
                flex-direction: column;
            }
            .details-label {
                font-size: 12px;
                color: #666;
            }
            .details-value {
                font-size: 14px;
                color: #333;
            }
        `;
    }

    static get properties() {
        return {
            info: {type: Object, state: true},
            selectedSegment: { type: Object, state: true },
            zoomLevel: { type: Number, state: true },
        };
    }

    constructor() {
        super();
        this.selectedSegment = null;
        this.zoomLevel = 1;
        this._handleWheel = this._handleWheel.bind(this);
    }

    connectedCallback() {
        super.connectedCallback();
        this.addEventListener('wheel', this._handleWheel, { passive: false });
    }

    disconnectedCallback() {
        super.disconnectedCallback();
        this.removeEventListener('wheel', this._handleWheel);
    }

    _handleWheel(e) {
        if (e.ctrlKey || e.metaKey) {
            e.preventDefault();
            const delta = e.deltaY > 0 ? -0.1 : 0.1;
            this.zoomLevel = Math.min(Math.max(1, this.zoomLevel + delta), 10);
        }
    }

    _hashString(val) {
        const str = String(val);
        let hash = 0;
        for (let i = 0; i < str.length; i++) {
            hash = (hash << 5) - hash + str.charCodeAt(i);
            hash |= 0; // Convert to 32bit integer
        }
        return hash;
    }

    _getSlotColor(slot) {
        if (!slot.isAllocated) {
            return '#e0e0e0'; // Free (Light Grey)
        }
        // Generate a stable color based on materialId for allocated slots
        const hash = this._hashString(slot.materialId);
        const hue = Math.abs(hash % 360);
        const saturation = 60 + Math.abs((hash >> 8) % 20); // 60-80%
        const lightness = 60 + Math.abs((hash >> 16) % 15); // 60-75%
        return `hsl(${hue}, ${saturation}%, ${lightness}%)`;
    }

    _formatSize(bytes) {
        if (bytes < 1024) return `${bytes} B`;
        if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
        return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
    }

    render() {
        if (!this.info) {
            return nothing;
        }

        const totalSize = this.info.totalSize;

        return html`
            <h2>Memory Allocation</h2>
            <div class="allocation-bar-container">
                <div class="allocation-bar" style="transform: scaleX(${this.zoomLevel})">
                ${this.info.slots.map(slot => {
            const widthPct = (slot.size / totalSize) * 100;
                    return html`
                        <div
                            class="segment"
                            style="width: ${widthPct}%; background-color: ${this._getSlotColor(slot)}"
                            @click="${() => this.selectedSegment = slot}"
                        >
                            <div class="segment-tooltip">
                                <div><strong>Offset:</strong> ${slot.offset}</div>
                                <div><strong>Size:</strong> ${this._formatSize(slot.size)}</div>
                                <div><strong>Status:</strong> ${slot.isAllocated ? 'Allocated' : 'Free'}</div>
                                <div><strong>GPU Users:</strong> ${slot.gpuUseCount}</div>
                                ${slot.isAllocated ? html`<div><strong>Material ID:</strong> 0x${Number(slot.materialId).toString(16)}</div>` : nothing}
                            </div>
                        </div>
                    `;
                })}
                </div>
            </div>
            ${this.selectedSegment ? html`
                <div class="details-panel">
                    <div class="details-title">Segment Details</div>
                    <div class="details-grid">
                        <div class="details-item">
                            <div class="details-label">Offset</div>
                            <div class="details-value">${this.selectedSegment.offset}</div>
                        </div>
                        <div class="details-item">
                            <div class="details-label">Size</div>
                            <div class="details-value">${this._formatSize(this.selectedSegment.size)}</div>
                        </div>
                        <div class="details-item">
                            <div class="details-label">Status</div>
                            <div class="details-value">${this.selectedSegment.isAllocated ? 'Allocated' : 'Free'}</div>
                        </div>
                        <div class="details-item">
                            <div class="details-label">GPU Users</div>
                            <div class="details-value">${this.selectedSegment.gpuUseCount}</div>
                        </div>
                        ${this.selectedSegment.isAllocated ? html`
                            <div class="details-item">
                                <div class="details-label">Material ID</div>
                                <div class="details-value">0x${Number(this.selectedSegment.materialId).toString(16)}</div>
                            </div>
                        ` : nothing}
                    </div>
                </div>
            ` : nothing}
            <div class="legend">
                <div class="legend-item">
                    <div class="legend-color" style="background-color: #e0e0e0"></div>
                    <span>Free</span>
                </div>
                <div class="legend-item">
                    <div class="legend-color" style="background-image: linear-gradient(to right, hsl(0, 65%, 65%), hsl(120, 65%, 65%), hsl(240, 65%, 65%))"></div>
                    <span>Allocated (Varied)</span>
                </div>
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
                height: 60px;
                background-color: #f5f5f5;
                border-top: 1px solid #ccc;
                overflow-x: auto;
                white-space: nowrap;
                position: relative;
            }
            .tick-container {
                display: flex;
                height: 100%;
                align-items: flex-end;
                padding-bottom: 5px;
            }
            .tick-wrapper {
                display: flex;
                flex-direction: column;
                align-items: center;
                margin: 0 2px;
                min-width: 10px;
                flex-shrink: 0;
            }
            .tick-wrapper.clickable {
                cursor: pointer;
            }
            .tick {
                width: 4px;
                height: 15px;
                background-color: #ddd;
                border-radius: 2px;
                transition: height 0.2s, background-color 0.2s;
            }
            .tick-wrapper.changed .tick {
                background-color: #ffc107;
                height: 25px;
            }
            .tick-wrapper.clickable:hover .tick {
                background-color: #333;
                height: 30px;
            }
            .tick-wrapper.active .tick {
                background-color: #4CAF50;
                height: 40px;
                width: 6px;
            }
            .tick-label {
                color: #666;
                font-size: 9px;
                margin-bottom: 2px;
                user-select: none;
            }
            .tick-wrapper.active .tick-label {
                color: #000;
                font-weight: bold;
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
                        class="tick-wrapper ${this.selectedFrame === item.frameId ? 'active' : ''} ${item.hasChanged ? 'changed clickable' : ''}"
                        @click="${() => item.hasChanged && this._handleTickClick(item.frameId)}"
                        title="${item.hasChanged ? 'Click to view allocation change' : 'No change'}"
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
                min-width: 0;
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
                    try {
                        this.history = await fetchHistory();
                        if (this.selectedFrame === null && this.history.length > 0) {
                            this.selectedFrame = this.history[this.history.length - 1].frameId;
                        }
                        this.info = await fetchBufferAllocatorInfo(this.selectedFrame);
                    } catch (e) {
                        console.error("Failed to update info", e);
                    }
                }
            }
        );
        try {
            this.history = await fetchHistory();
            if (this.history.length > 0) {
                this.selectedFrame = this.history[this.history.length - 1].frameId;
            }
            this.info = await fetchBufferAllocatorInfo(this.selectedFrame);
        } catch (e) {
            console.error("Failed to initialize info", e);
        }
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
            try {
                this.info = await fetchBufferAllocatorInfo(this.selectedFrame);
            } catch (e) {
                console.error("Failed to select frame", e);
            }
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
            this._timeline.updateComplete.then(() => {
                this._timeline.scrollTo({ left: this._timeline.scrollWidth, behavior: 'smooth' });
            });
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
