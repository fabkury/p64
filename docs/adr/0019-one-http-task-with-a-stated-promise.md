---
status: accepted (decided 2026-11-11; the firmware side implemented the same day, the pages' side not yet)
date: 2026-11-11
---

# One HTTP task stays, with a stated promise to other clients

The web server serves one request at a time, to completion. Probes on 2026-11-11 showed
what that costs: ten half-sent requests made the device unreachable for about 90 s, a
refused 8 MB upload for 27 s, and a burst of navigation overflowed the queues behind
the handlers until the show stopped playing artwork. We keep the single task and bound
its worst case instead of adding concurrency: internal RAM is the scarce resource (a
worker's stack would come out of the 48 KB floor), and every handler would have to be
re-examined for running beside another.

The promise, which the specification states and the tests assert:

- While one client stalls or sends garbage, any other client is answered within 5 s:
  receive and send timeouts of 5 s, a refused body is not drained (the connection is
  closed), nothing slow is done inline beyond that (a like, which takes about 3 s, waits up to 5 s
  for the worker, then is queued and reported through the status). The owner chose 5 s over the 3 s first written here
  on 2026-11-11, to be kinder to uploads over weak Wi-Fi. The promise is per stalled
  client: they are timed out one after the other, so six stalled at once cost about
  30 s once (measured 29.7 s; 90 s before).
- **Except during an upload**: a legitimate upload holds the server for its duration
  (about 18 s for the 5 MB maximum at the measured 290 KB/s); the pages say so and wait.
- Four open pages are supported; beyond that the page idle longest loses its WebSocket
  and falls back to polling.
- A command the device cannot take is never dropped silently: navigation is "latest
  wins" (a burst keeps one load in flight and the most recent request), internal
  results have their own path and are never dropped, and anything else answers 503 with
  Retry-After.
- The live preview is each page polling the frame once a second, not a push.

Considered and rejected: moving uploads and file downloads to a worker task (meets the
3 s promise always, costs a stack and new concurrency); a pool of workers for every
handler (the most robust, the most RAM and the most to make safe).

Consequence: if uploads ever need to stop blocking the pages, the worker task is the
next step, and this ADR is the place to reverse.
