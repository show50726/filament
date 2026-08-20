const timeline = document.querySelector('#timeline');
const context = timeline.getContext('2d');
const pauseButton = document.querySelector('#pause');
const statusElement = document.querySelector('#status');
const frameLabel = document.querySelector('#frameLabel');
const layoutSection = document.querySelector('#layoutSection');
const reallocationBanner = document.querySelector('#reallocationBanner');
const summary = document.querySelector('#summary');
const memoryViewport = document.querySelector('#memoryViewport');
const memoryTrack = document.querySelector('#memoryTrack');
const allocationInspector = document.querySelector('#allocationInspector');
const allocationList = document.querySelector('#allocationList');
const details = document.querySelector('#details');

let events = [];
let latestSequence = 0;
let selectedIndex = -1;
let paused = false;
let polling = false;
let pixelsPerEvent = 72;
let pan = 0;
let dragging = false;
let dragStart = 0;
let panStart = 0;
let layoutMode = 'readable';
let layoutZoom = 1;
let selectedAllocationId = null;

const colors = {
    background: '#ffffff', grid: '#d0d0d0', text: '#6f6f6f', accent: '#5362e5',
    reallocate: '#c94f5d', selected: '#2c3892'
};

function formatBytes(value) {
    if (value < 1024) return `${value} B`;
    if (value < 1024 * 1024) return `${(value / 1024).toFixed(value < 10240 ? 1 : 0)} KiB`;
    return `${(value / (1024 * 1024)).toFixed(2)} MiB`;
}

function formatSignedBytes(value) {
    if (value === 0) return '±0 B';
    return `${value > 0 ? '+' : '−'}${formatBytes(Math.abs(value))}`;
}

function escapeHtml(value) {
    return String(value).replace(/[&<>'"]/g, character => ({
        '&': '&amp;', '<': '&lt;', '>': '&gt;', "'": '&#39;', '"': '&quot;'
    })[character]);
}

function eventX(index) {
    return 54 + pan + index * pixelsPerEvent;
}

function fitLatest() {
    if (!events.length) return;
    pan = Math.min(0, timeline.clientWidth - 74 - (events.length - 1) * pixelsPerEvent);
}

function resizeTimeline() {
    const ratio = window.devicePixelRatio || 1;
    const rect = timeline.getBoundingClientRect();
    timeline.width = Math.round(rect.width * ratio);
    timeline.height = Math.round(rect.height * ratio);
    context.setTransform(ratio, 0, 0, ratio, 0, 0);
    renderTimeline();
}

function renderTimeline() {
    const width = timeline.clientWidth;
    const height = timeline.clientHeight;
    context.fillStyle = colors.background;
    context.fillRect(0, 0, width, height);
    context.strokeStyle = colors.grid;
    context.lineWidth = 1;
    context.beginPath();
    context.moveTo(0, 88.5);
    context.lineTo(width, 88.5);
    context.stroke();

    if (!events.length) {
        context.fillStyle = colors.text;
        context.fillText('No allocation changes received', 18, 34);
        return;
    }

    context.font = '11px ui-monospace, SFMono-Regular, Consolas, monospace';
    events.forEach((event, index) => {
        const x = eventX(index);
        if (x < -40 || x > width + 40) return;
        const selected = index === selectedIndex;

        if (index > 0) {
            const previousX = eventX(index - 1);
            if (previousX < width && x > 0) {
                context.strokeStyle = '#9aa7e6';
                context.beginPath();
                context.moveTo(Math.max(previousX, 0), 88.5);
                context.lineTo(Math.min(x, width), 88.5);
                context.stroke();
                if (x - previousX > 52) {
                    context.fillStyle = colors.text;
                    context.textAlign = 'center';
                    context.fillText(`+${event.frame - events[index - 1].frame} frames`,
                            (x + previousX) / 2, 108);
                }
            }
        }

        context.strokeStyle = selected ? colors.selected :
                (event.reallocated ? colors.reallocate : colors.accent);
        context.lineWidth = selected ? 3 : 2;
        context.beginPath();
        context.moveTo(x, event.reallocated ? 30 : 48);
        context.lineTo(x, 101);
        context.stroke();

        context.fillStyle = context.strokeStyle;
        context.beginPath();
        if (event.reallocated) {
            context.moveTo(x, 25);
            context.lineTo(x - 7, 37);
            context.lineTo(x + 7, 37);
        } else {
            context.arc(x, 45, selected ? 5 : 4, 0, Math.PI * 2);
        }
        context.fill();

        context.fillStyle = selected ? colors.selected : colors.text;
        context.textAlign = 'center';
        context.fillText(`F${event.frame}`, x, 126);
    });
    context.textAlign = 'start';
    context.lineWidth = 1;
}

function activeAllocations(event) {
    return event ? event.allocations.filter(item => item.state === 'allocated') : [];
}

function allocationMap(event) {
    return new Map(activeAllocations(event).map(item => [String(item.owner), item]));
}

function allocationId(item) {
    return String(item.id);
}

function allocationTitle(item) {
    return `${item.name} · ${item.state} · ${formatBytes(item.size)} · offset ${item.offset}` +
            (item.requestedSize ? ` · requested ${formatBytes(item.requestedSize)}` : '') +
            (item.gpuUseCount ? ` · GPU uses ${item.gpuUseCount}` : '');
}

function inspectAllocation(item) {
    if (!item) {
        allocationInspector.innerHTML = '<span class="hint">Hover or click an allocation to inspect it</span>';
        return;
    }
    allocationInspector.innerHTML = `<span class="name">${escapeHtml(item.name)}</span>` +
            `<span class="value">${escapeHtml(item.state)}</span>` +
            `<span class="value">offset ${item.offset}</span>` +
            `<span class="value">physical ${formatBytes(item.size)}</span>` +
            `<span class="value">requested ${item.requestedSize ? formatBytes(item.requestedSize) : '—'}</span>`;
}

function updateAllocationSelection() {
    document.querySelectorAll('[data-allocation-id]').forEach(element => {
        element.classList.toggle('selected', element.dataset.allocationId === selectedAllocationId);
    });
}

function selectAllocation(item) {
    selectedAllocationId = allocationId(item);
    inspectAllocation(item);
    updateAllocationSelection();
}

function readableLayoutWidth(event) {
    const viewportWidth = Math.max(memoryViewport.clientWidth - 2, 1);
    const sizes = event.allocations.map(item => item.size).filter(size => size > 0);
    if (!sizes.length || !event.totalSize) return viewportWidth;
    const smallestBlock = Math.min(...sizes);
    const widthForSmallestBlock = event.totalSize / smallestBlock * 72;
    return Math.min(120000, Math.max(viewportWidth, widthForSmallestBlock));
}

function layoutWidth(event) {
    const viewportWidth = Math.max(memoryViewport.clientWidth - 2, 1);
    if (layoutMode === 'fit') return viewportWidth;
    return Math.max(viewportWidth, readableLayoutWidth(event) * layoutZoom);
}

function renderRuler(event) {
    const ruler = document.createElement('div');
    ruler.className = 'memory-ruler';
    for (let tick = 0; tick <= 10; ++tick) {
        const marker = document.createElement('span');
        marker.className = 'memory-tick';
        marker.style.left = `${tick * 10}%`;
        marker.textContent = formatBytes(Math.round(event.totalSize * tick / 10));
        ruler.append(marker);
    }
    memoryTrack.append(ruler);
}

function renderAllocationList(event) {
    allocationList.innerHTML = '<div class="allocation-row header"><span></span><span>Name</span>' +
            '<span>State</span><span>Offset range</span><span>Physical</span><span>Requested</span></div>';
    event.allocations.forEach(item => {
        const row = document.createElement('div');
        row.className = 'allocation-row';
        row.dataset.allocationId = allocationId(item);
        row.title = allocationTitle(item);

        const swatch = document.createElement('span');
        swatch.className = `swatch ${item.state}`;
        const name = document.createElement('span');
        name.className = 'name';
        name.textContent = item.name;
        const state = document.createElement('code');
        state.textContent = item.state;
        const range = document.createElement('code');
        range.textContent = `${item.offset}–${item.offset + item.size}`;
        const size = document.createElement('code');
        size.textContent = formatBytes(item.size);
        const requested = document.createElement('code');
        requested.textContent = item.requestedSize ? formatBytes(item.requestedSize) : '—';
        row.append(swatch, name, state, range, size, requested);
        row.addEventListener('mouseenter', () => inspectAllocation(item));
        row.addEventListener('mouseleave', () => inspectAllocation(
                event.allocations.find(allocation => allocationId(allocation) === selectedAllocationId)));
        row.addEventListener('click', () => selectAllocation(item));
        allocationList.append(row);
    });
}

function renderMemoryLayout(event) {
    const hadLayout = memoryTrack.querySelector('.block') !== null;
    const oldWidth = Math.max(memoryTrack.scrollWidth, 1);
    const centerRatio = (memoryViewport.scrollLeft + memoryViewport.clientWidth / 2) / oldWidth;
    memoryTrack.innerHTML = '';
    memoryTrack.style.width = `${layoutWidth(event)}px`;
    renderRuler(event);

    event.allocations.forEach(item => {
        const block = document.createElement('div');
        block.className = `block ${item.state}`;
        block.dataset.allocationId = allocationId(item);
        block.style.left = `${100 * item.offset / event.totalSize}%`;
        block.style.width = `${100 * item.size / event.totalSize}%`;
        block.title = allocationTitle(item);

        const name = document.createElement('strong');
        name.textContent = item.name;
        const size = document.createElement('small');
        size.textContent = `${formatBytes(item.size)} @ ${item.offset}`;
        block.append(name, size);
        block.addEventListener('mouseenter', () => inspectAllocation(item));
        block.addEventListener('mouseleave', () => inspectAllocation(
                event.allocations.find(allocation => allocationId(allocation) === selectedAllocationId)));
        block.addEventListener('click', () => selectAllocation(item));
        memoryTrack.append(block);
    });

    renderAllocationList(event);
    updateAllocationSelection();
    inspectAllocation(event.allocations.find(item => allocationId(item) === selectedAllocationId));
    requestAnimationFrame(() => {
        memoryViewport.scrollLeft = hadLayout ?
                centerRatio * memoryTrack.scrollWidth - memoryViewport.clientWidth / 2 : 0;
    });
}

function describeChanges(index) {
    const current = events[index];
    const previous = events[index - 1];
    const currentMap = allocationMap(current);
    const previousMap = allocationMap(previous);
    const added = [];
    const removed = [];
    const changed = [];

    currentMap.forEach((item, owner) => {
        const old = previousMap.get(owner);
        if (!old) {
            added.push(item);
        } else if (old.offset !== item.offset || old.size !== item.size || old.id !== item.id) {
            changed.push({ old, item });
        }
    });
    previousMap.forEach((item, owner) => {
        if (!currentMap.has(owner)) removed.push(item);
    });
    return { added, removed, changed };
}

function changeRows(title, rows, formatter) {
    if (!rows.length) return '';
    return `<div class="change-group"><h3>${title}</h3>${rows.map(formatter).join('')}</div>`;
}

function selectEvent(index) {
    if (index < 0 || index >= events.length) return;
    selectedIndex = index;
    const event = events[index];
    const used = event.allocations.filter(item => item.state === 'allocated')
            .reduce((total, item) => total + item.size, 0);
    const retired = event.allocations.filter(item => item.state === 'retired')
            .reduce((total, item) => total + item.size, 0);
    const free = event.allocations.filter(item => item.state === 'free')
            .reduce((total, item) => total + item.size, 0);
    const previous = events[index - 1];
    const capacityDelta = previous ? event.totalSize - previous.totalSize : null;

    layoutSection.classList.toggle('reallocated', event.reallocated);
    memoryViewport.classList.toggle('reallocated', event.reallocated);
    reallocationBanner.hidden = !event.reallocated;
    if (event.reallocated) {
        if (capacityDelta === null) {
            reallocationBanner.innerHTML = '<strong>UBO reallocated</strong>' +
                    '<span>Previous capacity is outside retained history</span>' +
                    `<span class="delta">Now ${formatBytes(event.totalSize)}</span>`;
        } else {
            reallocationBanner.innerHTML = '<strong>UBO reallocated</strong>' +
                    `<span>${formatBytes(previous.totalSize)} → ${formatBytes(event.totalSize)}</span>` +
                    `<span class="delta">Δ ${formatSignedBytes(capacityDelta)}</span>`;
        }
    } else {
        reallocationBanner.innerHTML = '';
    }

    frameLabel.textContent = `Frame ${event.frame} · event ${event.sequence}`;
    summary.innerHTML = [
        ['Capacity', formatBytes(event.totalSize), event.reallocated && capacityDelta !== null ?
                `Δ ${formatSignedBytes(capacityDelta)}` : ''], ['Allocated', formatBytes(used), ''],
        ['Retired', formatBytes(retired), ''], ['Free', formatBytes(free), '']
    ].map(([label, value, delta]) => `<div class="metric"><span>${label}</span><strong>${value}</strong>` +
            `${delta ? `<span class="metric-delta">${delta}</span>` : ''}</div>`).join('');

    if (!event.allocations.some(item => allocationId(item) === selectedAllocationId)) {
        selectedAllocationId = null;
    }
    renderMemoryLayout(event);

    const changes = describeChanges(index);
    const allocationRow = item => `<div class="change"><code>${formatBytes(item.size)}</code><span>${escapeHtml(item.name)}</span></div>`;
    const changedRow = ({ old, item }) => `<div class="change"><code>${old.offset}→${item.offset}</code>` +
            `<span>${escapeHtml(item.name)}${old.size !== item.size ? ` · ${formatBytes(old.size)}→${formatBytes(item.size)}` : ''}</span></div>`;
    const body = changeRows('Added', changes.added, allocationRow) +
            changeRows('Removed', changes.removed, allocationRow) +
            changeRows('Moved / resized', changes.changed, changedRow);
    details.innerHTML = `<div class="event-heading"><strong>Frame ${event.frame}</strong>` +
            `<span class="badge ${event.reallocated ? 'reallocated' : ''}">${event.reallocated ? 'Reallocated' : 'Layout change'}</span></div>` +
            (body || '<div class="empty">Initial allocator state</div>');
    renderTimeline();
}

async function poll() {
    if (paused || polling) return;
    polling = true;
    try {
        const response = await fetch(`/api/events?after=${latestSequence}`, { cache: 'no-store' });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const result = await response.json();
        const wasAtLatest = selectedIndex === events.length - 1;
        if (result.reset) events = [];
        if (result.events.length) {
            events.push(...result.events);
            if (events.length > 2048) events.splice(0, events.length - 2048);
            latestSequence = result.latestSequence;
            if (wasAtLatest || selectedIndex < 0) {
                selectedIndex = events.length - 1;
                fitLatest();
                selectEvent(selectedIndex);
            } else {
                renderTimeline();
            }
        }
        statusElement.textContent = paused ? 'Paused' : 'Live';
        statusElement.classList.add('connected');
    } catch (error) {
        statusElement.textContent = 'Disconnected';
        statusElement.classList.remove('connected');
    } finally {
        polling = false;
        if (!paused) window.setTimeout(poll, 250);
    }
}

function zoom(factor, anchor = timeline.clientWidth / 2) {
    const oldScale = pixelsPerEvent;
    pixelsPerEvent = Math.max(18, Math.min(240, pixelsPerEvent * factor));
    const eventAtAnchor = (anchor - 54 - pan) / oldScale;
    pan = anchor - 54 - eventAtAnchor * pixelsPerEvent;
    renderTimeline();
}

pauseButton.addEventListener('click', () => {
    paused = !paused;
    pauseButton.textContent = paused ? 'Resume' : 'Pause';
    pauseButton.classList.toggle('active', paused);
    statusElement.textContent = paused ? 'Paused' : 'Live';
    if (!paused) {
        if (events.length) {
            selectedIndex = events.length - 1;
            fitLatest();
            selectEvent(selectedIndex);
        }
        poll();
    }
});
document.querySelector('#zoomIn').addEventListener('click', () => zoom(1.3));
document.querySelector('#zoomOut').addEventListener('click', () => zoom(1 / 1.3));
document.querySelector('#layoutZoomIn').addEventListener('click', () => {
    layoutMode = 'readable';
    layoutZoom = Math.min(8, layoutZoom * 1.4);
    document.querySelector('#layoutFit').classList.remove('active');
    document.querySelector('#layoutReadable').classList.add('active');
    if (selectedIndex >= 0) renderMemoryLayout(events[selectedIndex]);
});
document.querySelector('#layoutZoomOut').addEventListener('click', () => {
    layoutMode = 'readable';
    layoutZoom = Math.max(0.1, layoutZoom / 1.4);
    document.querySelector('#layoutFit').classList.remove('active');
    document.querySelector('#layoutReadable').classList.add('active');
    if (selectedIndex >= 0) renderMemoryLayout(events[selectedIndex]);
});
document.querySelector('#layoutFit').addEventListener('click', () => {
    layoutMode = 'fit';
    document.querySelector('#layoutFit').classList.add('active');
    document.querySelector('#layoutReadable').classList.remove('active');
    if (selectedIndex >= 0) renderMemoryLayout(events[selectedIndex]);
});
document.querySelector('#layoutReadable').addEventListener('click', () => {
    layoutMode = 'readable';
    layoutZoom = 1;
    document.querySelector('#layoutFit').classList.remove('active');
    document.querySelector('#layoutReadable').classList.add('active');
    if (selectedIndex >= 0) renderMemoryLayout(events[selectedIndex]);
});
timeline.addEventListener('wheel', event => {
    event.preventDefault();
    const rect = timeline.getBoundingClientRect();
    zoom(event.deltaY < 0 ? 1.12 : 1 / 1.12, event.clientX - rect.left);
}, { passive: false });
timeline.addEventListener('pointerdown', event => {
    dragging = true;
    dragStart = event.clientX;
    panStart = pan;
    timeline.classList.add('dragging');
    timeline.setPointerCapture(event.pointerId);
});
timeline.addEventListener('pointermove', event => {
    if (!dragging) return;
    pan = panStart + event.clientX - dragStart;
    renderTimeline();
});
timeline.addEventListener('pointerup', event => {
    const moved = Math.abs(event.clientX - dragStart);
    dragging = false;
    timeline.classList.remove('dragging');
    if (moved < 5 && events.length) {
        const rect = timeline.getBoundingClientRect();
        const index = Math.round((event.clientX - rect.left - 54 - pan) / pixelsPerEvent);
        selectEvent(index);
    }
});

new ResizeObserver(resizeTimeline).observe(timeline);
new ResizeObserver(() => {
    if (selectedIndex >= 0) renderMemoryLayout(events[selectedIndex]);
}).observe(memoryViewport);
resizeTimeline();
poll();
