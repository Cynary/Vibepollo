# Event-driven Windows capture

Set `MOONMACHINE_WGC_EVENT_CAPTURE=1` in the host process environment and restart
Vibepollo to let Windows Graphics Capture deliver each available frame without
waiting for a separate periodic capture slot. Unset it to use the existing
capture pacing. This option applies to the WGC path, not DXGI or `wgcc`.

A rate limiter still bounds sustained capture work to the negotiated frame rate.
It allows two frames close together after a delay, but cannot accumulate more
credit while idle. This is separate from Moonlight's experimental predictive
frame dropping, which can remain off.

The change preserves the existing timestamp policy and virtual-display backend.
It does not guarantee regular arrivals: game rendering, capture, encoding and
network delivery can still vary.

Validation: Windows build and live Overcooked 2 / Stellar Blade streams; a
standalone rate-policy test covers steady 116 FPS, delayed arrivals, burst limits,
idle recovery and an unlimited rate. Compile it with C++17:

```sh
c++ -std=c++17 tests/unit/test_wgc_event_rate_standalone.cpp -o /tmp/wgc-rate-test
/tmp/wgc-rate-test
```

The fork also includes bounded, opt-in capture/encode/send tracing for diagnosis.
The short sequential live comparisons are not a randomized performance benchmark.

### Capture recent gameplay rather than startup

`MOONMACHINE_HOST_FRAME_TRACE` enables a rolling, three-minute in-memory trace.
It must be set in both the Apollo service environment and the Windows user's
 environment: the WGC helper starts with a fresh user environment. Recording
continues for the session; it no longer stops after the first 90 seconds or
12,000 frames. Each recorder also has a one-million-row memory safety limit.

For a base path `C:\trace\host.csv`, create these empty files while playing:

```powershell
'host.csv', 'host.csv.capture.csv', 'host.csv.wgc.csv' | ForEach-Object {
    New-Item -ItemType File -Force (Join-Path 'C:\trace' ($_ + '.snapshot')) | Out-Null
}
```

A background worker writes each CSV and removes its `.snapshot` request only
when writing succeeds. Wait for all three request files to disappear before
copying the CSVs. Each file contains its most recent three minutes; use their
common timestamp interval for analysis. Saving does not stop recording, so the
same request can be repeated later. A graceful recorder shutdown also saves.
A forced process termination may lose the in-memory data.

The output columns are unchanged. Disk writes happen on the background worker;
snapshot copying briefly takes the recorder lock. Exclude the snapshot boundary
from latency comparisons. Remove the environment settings after diagnostics and
restart the host when the stream is closed.

The helper trace also records callback entry/exit, each `TryGetNextFrame` call,
`Surface()` access, timestamp retrieval, and frame-pool adjustment. The appended
`callback_id` and `call_index` columns associate events even when callbacks
interleave or a drain performs multiple retrievals. A `callback_frame` row ties
that callback to the WGC source timestamp. Empty callbacks and exceptions retain
entry/exit timing; an end marker alone does not imply a successful retrieval.
Compare callback-entry spacing first, then retrieval duration. The WGC source
timestamp can slightly precede or follow callback entry; do not interpret that
difference alone as an exact elapsed capture duration.

### Windows trace-worker lifetime

The sender's frame recorder must be owned by `videoBroadcastThread` as an ordinary
local object. Do not make it `thread_local`: its destructor joins a worker, and
MinGW invokes thread-local destructors during Windows loader-locked thread
cleanup. The worker also needs loader cleanup to finish exiting, so joining it
there deadlocks. Other exiting threads then stall behind the loader lock, and
stream teardown blocks the streaming API while the separate web UI can remain
responsive.

This occurred on September 27 with rolling tracing enabled. The saved process
dump contained `host_frame_trace::recorder::~recorder()` / `std::thread::join()`
in the video thread's TLS cleanup and `LdrShutdownThread` in the receive thread.
Function-local ownership joins the worker before the sender returns to Windows
thread cleanup, retaining final trace snapshots without detaching live workers.

`tests/unit/test_trace_thread_exit_standalone.cpp` exercises repeated sender
creation, final snapshot flushing, and worker shutdown. Its `--legacy-tls` mode
reproduces the unsafe lifetime and must only run in a separate process with an
external timeout on Windows.

### Direct-capture timestamps with upstream 2.0.0

The WGC helper can publish either compositor frames or game-hook frames. Each
frame now carries its origin with its QPC timestamp through the shared metadata,
encoder image and encoded packet. AMF retains that flag by encoded frame index;
PyroWave also carries it. A cached frame stamped with the current time does not
claim a game-hook timestamp.

Direct-origin frames bypass both nominal encoder timestamp snapping and WGC
send-time Present refinement. Their RTP timestamps come from the hook's original
capture time, subject only to the normal 90 kHz RTP conversion. WGC keeps both
upstream adjustments. Returning from direct capture clears the encoder's old
nominal prediction so the first WGC frame establishes a new one. Duplicate
packets retain the existing synthetic-timestamp path.

Update the host executable and `sunshine_wgc_capture.exe` together: shared
frame metadata now includes the origin field. No change to the injected capture
hook is required. Install with the stream stopped and restart the host service.
The standalone timestamp policy test covers WGC smoothing, direct preservation,
source transitions and discontinuities. Live latency improvement must be checked
with a new gameplay capture; timestamp correctness alone does not guarantee a
lower p99.
