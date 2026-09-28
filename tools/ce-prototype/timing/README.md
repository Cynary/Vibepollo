# Capture timing experiment

These are opt-in developer tools, not an installed capture backend. They answer
where a frame waits between the application's Present call and WGC delivery.
They do not infer compositor latency from whichever two timestamps are closest.

Build on Windows with the matching CaptureEngine observer source and hook:

```powershell
cmake -S tools/ce-prototype -B build-timing -DCE_BUILD_TIMING_TOOLS=ON -DCE_SOURCE_DIR=C:/src/CaptureEngine
cmake --build build-timing --parallel
```

The native MinGW build needs C++/WinRT headers. The timestamp observer and injected
hook must agree on the diagnostic layout, independently of the capture ABI.
Run both in the same interactive Windows session. Keep generated traces private.

## Controlled identity check

`ce-frame-probe.exe --validate --present0` displays a 30-second 4K HDR pattern on
the first active 3840×2160 monitor. Its window stays above other windows. Every
frame contains a 16-bit ID and its complement. WGC captures the same monitor,
reads a narrow strip back, and records the decoded ID. Run it without another
game occupying that display. A streaming virtual display can supply the monitor.

Outputs are `frame-probe-present.csv`, `frame-probe-wgc.csv` and
`frame-probe-result.txt` in the working directory. `--present0` requests
nonblocking Present and targets 116 submissions/s; driver-level limiting may
still block it. Without that flag, Present uses sync interval 1. `--sdr` selects
SDR. `--no-capture` disables the probe's WGC session.

Record graphics events around the run:

```powershell
wpr -start FrameTiming.wprp!FrameTiming -filemode
# Run the probe interactively; wait for its normal exit.
wpr -stop host.etl "Frame identity validation" -skipPdbGen
ce-etl-frame-window.exe host.etl START_QPC END_QPC PROCESS_ID > events.csv
python analyze.py RUN_DIRECTORY
```

Use the actual process ID and QPC interval from the probe, not example constants.
The analyzer links the fixture's frame count, the DXGI call inside its measured
Present span, and Win32k/DWM surface and present-count records. It then checks the
selected count against the ID read from the captured pixels. Invalid and
ambiguous observations remain visible in the report.

## Timestamp-only hook

```powershell
ce-timestamp-observer.exe GAME_PID C:\matching\capture_hook_x64.dll 30 observer.csv
```

This creates an explicit per-process diagnostic mapping before injection and
keeps the hook's control channel alive. It does **not** request recording or
injected video capture, set a capture frame rate, create a capture GPU device,
import a texture, or wait for GPU completion. The hook records Present boundaries
and, where that route executes, final-output callback observations. The existing
nonblocking ring retains up to 16,384 events; output includes total, retained and
contended-drop counts. Long/high-rate runs can overwrite old events. Use a fresh
target process for each run: the diagnostic mapping stays mapped in the hook
until process exit, preventing a concurrent reader from replacing live storage.

The controller fails if the video-capture ring unexpectedly receives a frame.
No per-frame file writes occur in the hook. A callback timestamp means the CPU
reached that callback, **not** that GPU work had finished. Final-output records
include measured QPC and frequency separately; they are not scheduled display
times. Base and generated-frame routes must be validated separately.

## Performance and interpretation

After pixel validation, omit `--validate` to remove readback. Compare fresh
unhooked and timestamp-observed processes under otherwise identical conditions.
Both runs must use the same WGC and ETW instrumentation. Tests of the controlled
fixture can use the validated DWM selection association; that does not establish
pixel identity in arbitrary games, overlapping windows, or Frame Generation.

Do not treat WGC `SystemRelativeTime` as a capture-completion timestamp or a frame
identifier. It can lie in the future relative to callback entry. Keep the outer
application Present span separate from DXGI's inner Present event: waits before
that inner event are not automatically compositor delay. PresentHistory handoff
is also an observation, not a timestamp of pure GPU execution.

For a live Vibepollo run, snapshot the helper's own rolling trace while it is
still running. A host that forcibly ends its helper may lose an unsaved trace.
Check file timestamps, process IDs and measurement-window coverage before
calculating distributions. Keep first-packet-to-flip and complete-frame-to-flip
client measurements separate.

Regression checks: `python test_analysis.py`, CaptureEngine's `PresentObserver.*`
unit tests, and the live pixel-ID/empty-capture-ring checks described above.

## Present to NVENC submission

`ce-timestamp-observer.exe PID --listen SECONDS output.csv` creates the diagnostic
mapping without taking ownership of CaptureEngine's control channel. Start it
before the real helper injects the hook. The named readiness event is
`Global\\CEPresentObserverReady-PID`. An inactive existing mapping can be reopened
without resetting its ring; filter by the new measurement window. This does not
rearm a dormant hook's own tracing: a fresh game process is the reproducible
choice when changing capture modes. Never run two observers for one target.

An opt-in `MOONMACHINE_NVENC_SUBMIT_TRACE` path records encoder entry, the CPU
timestamp immediately before `nvEncEncodePicture`, and bitstream availability.
It retains 60,000 samples and writes at encoder-thread exit. Each thread has its
own output suffix so startup capability checks cannot overwrite the stream.
No files are written in the per-frame path. Match by frame index **and** encoder
entry time; frame numbers restart between sessions. API submission is not proof
that the NVENC hardware engine has begun processing.

For direct capture without generated frames, link the measured source QPC in
the helper timing CSV to its containing Present span, then follow that source
timestamp to the host frame record. For WGC use the validated fixture's ETW
association, noting that latest compositor selection remains an inference for
arbitrary games. Report missing/duplicate associations and reject impossible
timestamp ordering. Do not subtract unrelated callback distributions.

The standalone `test_submission_trace.cpp` verifies that simultaneous encoder
threads produce separate files. Compile with C++20 and pass a new temporary
directory. It needs no GPU or timing sleeps.
