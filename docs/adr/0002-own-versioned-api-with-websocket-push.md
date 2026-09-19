---
status: accepted
date: 2026-09-19
---

# Own versioned HTTP API under /api/v1 with p3a's envelope and a WebSocket status push

p64's web UI replicates p3a's, which invites reusing p3a's routes verbatim. The user chose
an own, p3a-inspired API instead: versioned under `/api/v1/`, p3a's `{ok, data}` /
`{ok:false, error, code}` envelope, actions as POST that answer 202 when queued, settings
merged with PUT, and one WebSocket that pushes status changes and the live panel preview
(p3a polls every 4 s with ETags and has no status push). The web UI is therefore a port,
not a drop-in, and scripts written for p3a do not work unchanged on p64.

Why: p64's surface differs in kind (panel modes, brightness 1 to 255, widgets, streams,
a file manager, a live preview, an IMU) and p3a's API carries a touchscreen device's
assumptions and legacy routes; freezing p64 to p3a's route set would preserve names at the
cost of a clean contract. The API version number in status lets the UI detect mismatches
as p3a's does.
