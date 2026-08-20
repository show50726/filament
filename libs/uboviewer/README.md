# UBO viewer

UBO viewer is a local web profiler for Filament's shared material-instance uniform buffer. It keeps
a bounded history of allocator changes and displays them on a zoomable event timeline. Frames that
do not change the allocator are intentionally omitted, while shared-buffer reallocations receive a
distinct marker.

Configure Filament with `FILAMENT_ENABLE_UBOVIEWER=ON`, set `FILAMENT_UBOVIEWER_PORT` (for example,
`8086`), then launch an application that enables material-instance uniform batching and open the
reported localhost URL. The Pause button stops browser queries; the engine and the server-side
bounded history continue running, so Resume can catch up without altering rendering behavior.

The HTTP endpoint is `GET /api/events?after={sequence}`. The response contains only changed
snapshots newer than the requested sequence. If the client falls behind the bounded history,
`reset` is true and the response starts at the oldest retained event.
