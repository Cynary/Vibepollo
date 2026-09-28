# Direct game capture prototype

This replaces desktop capture with a game's shared GPU textures while keeping the
same Vibepollo stream connected. It uses CaptureEngine v0.1.6772's MIT-licensed
hook and shared-memory interface, with the ABI 63 changes described below.
It is experimental and disabled by default.

Set `MOONMACHINE_CE_TARGET_PATH` to the full path of the game's executable and
`MOONMACHINE_CE_HOOK` to `capture_hook_x64.dll` before the capture helper starts.
These are helper environment variables, not configuration-file options. Start the
stream first, then launch the game. The helper uses ordinary WGC desktop capture
until it finds the configured game, switches to direct capture, and returns to
WGC when the game exits. No capture source changes are made to unrelated games.

An already-running game without the hook is skipped: attaching after D3D12 device
creation caused a device-removal failure in testing. A game whose hook is already
resident can reconnect. The older `MOONMACHINE_CE_TARGET_PID` option is retained
for diagnostics and takes precedence; leave it unset for automatic selection.

## MoonDeck startup

The Windows build includes `ce-stream-launcher.exe`; its install rule puts it
in `tools` beside the capture helper. Prefix the existing MoonDeckStream
application command with its quoted path. For example:

```text
"C:\Program Files\Apollo\tools\ce-stream-launcher.exe" "C:\Users\YourName\AppData\Local\Programs\MoonDeckBuddy\bin\MoonDeckStream.exe"
```

The existing command and arguments after the prefix stay unchanged. This
wrapper waits for the capture helper's readiness event before launching the
original MoonDeckStream command. Buddy therefore starts the game after the
helper is watching, rather than racing capture initialization. Without this
handshake, some launches precede the helper and remain on desktop capture.

Use this wrapper only with the prototype enabled. It fails after 45 seconds if
no helper signals readiness. Restore the original application command when
restoring the normal helper. It does not solve arbitrary late injection or
guarantee that process polling beats every game's graphics initialization.

To build only the launcher from this directory on Windows:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
powershell -NoProfile -File tests/readiness_test.ps1 -Launcher build/ce-stream-launcher.exe
```

Run the readiness test with streaming stopped. It checks the 45-second timeout
and that a launched child's exit code reaches the caller. The launcher does
not include the CaptureEngine hook; use the matching ABI 63 hook build.

## Timing diagnostics

`MOONMACHINE_CE_TRACE_TIMING=1` keeps the latest 65,536 frames in memory and writes
`%TEMP%\ce-timing-<helper PID>-<game PID>.csv` when that capture ends. The measured
columns cover metadata observation, producer-fence completion, and completion of
the conversion/copy into Vibepollo's shared texture. No file writes occur in
the frame loop. Frame-generation source timestamps can be synthetic; don't
interpret source-to-observation differences as measured capture latency.

## What has been checked

- Automatic launch, SDR startup followed by 4K HDR, exit through the game menu,
  and relaunch in Stellar Blade, without restarting the helper or Moonlight.
- Disconnecting and reconnecting while the game remains running.
- Shared texture/fence replacement across resolution changes. The consumer
  compares NT object identity because Windows can reuse a numeric handle for a
  different resource. Comparing the numbers alone caused a reproducible timeout.
- Forty GPU colour vectors for SDR, sRGB texture views and HDR10 conversion, including values
  outside the Rec.709 gamut.
- Three consecutive Steam/MoonDeck launches with the readiness wrapper reached
  direct HDR capture and shut down normally after exiting the game.
- Early attachment to 32-bit Overcooked 2 captured SDR at 4K. Keyboard input
  through Moonlight reached the game, and audio reached the client HDMI sink.
  The 32-bit loader lookup now waits for the module to become available, just
  like the 64-bit path.
- With DLSS FG 2X enabled, 128 final-output pixel samples contained no consecutive
  duplicates (127 distinct hashes). This is evidence of changing final-output
  pixels, not proof of every generated frame reaching the client. Pixel readback
  was enabled only for this check and must not be used for latency measurements.
- A controlled target which never publishes a frame returns an error after
  30 seconds and remains alive. Desktop delivery is retained until a direct
  frame is ready. A streamed no-frame fixture also returned to the existing
  WGC path after 30 seconds without restarting the helper.

These checks were on one NVIDIA Windows host. They are not a compatibility claim
for other games, anti-cheat systems, drivers, or frame-generation implementations.

## Remaining work

- Replace 10 ms process discovery polling with a reliable launch/injection
  lifecycle. Polling does not guarantee injection precedes device creation.
- Investigate the intermittent preserved-swapchain resize assertion; subsequent
  successful launches do not prove it is fixed.
- Validate controller gameplay and generated-frame completeness through the client.
- Expand HDR validation beyond static colour patches and measure latency under gameplay.
- Audit CaptureEngine's generated-frame timestamps: they include a synthetic
  presentation schedule and cannot be treated as measured capture times.
- Improve resize scaling, which currently uses point sampling with letterboxing.
  Native-resolution output is unaffected.

Direct capture currently requires the game's GPU to match the stream GPU. An HDR
source requires an FP16 output surface; incompatible configurations return to WGC.
The producer is stopped before the consumer releases its imported GPU resources.
WGC delivery is stopped and joined before direct capture writes to the same output
texture, so the two paths never publish concurrently.

## Reproducing the colour check

From this directory in an MSYS2 UCRT64 environment on Windows:

```sh
g++ -std=c++20 -O2 -static tests/colour_test.cpp -ld3d11 -ld3dcompiler -o colour-test.exe
./colour-test.exe
```

This checks forty GPU conversion vectors against CPU reference values. It does
not verify the subsequent video encoder, decoder, or TV output.

Typed sRGB input views already return linear values. The bridge preserves those
values for scRGB output and re-encodes them for an ordinary SDR output surface.
The GPU test covers both paths; forcing the earlier SDR shader reproduces the
double-conversion error (0.00405 instead of 0.05127 for one channel). These
checks validate the conversion shader, not the full encoded HDR stream.

### HDR stream validation

A D3D12 pattern was captured directly, converted to scRGB, encoded by Vibepollo,
received by Moonlight, and decoded from the received HEVC packets for numeric
comparison. The stream was 3840×2160, YUV 4:4:4 10-bit, limited range, PQ/BT.2020.
Eight neutral and eight coloured patches matched the expected YUV values within
0.474 of a 10-bit code value. The coloured set includes one-code RGB differences.
This verifies the tested colour conversion and encoded stream; it does not
measure the TV's tone mapping or establish gameplay latency or complete frame
generation capture. Recording and readback runs are excluded from benchmarks.

Both fixture runs completed their SDR-to-HDR resize and returned to desktop
capture on the existing stream. These runs used the helper built from `397b2b4`.

### Measured capture clock

The matched hook/helper builds use shared-memory ABI 63. `captureObservedQpc`
is separate from the recording presentation schedule: DX12 and Vulkan generated
outputs carry the QPC sampled in their final-output callback. Ordinary capture
paths retain their existing measured capture time. A generated output without
that measurement reports zero, not an invented capture time.

The helper timing CSV includes `capture_observed_qpc` alongside `source_qpc`
(the original presentation schedule), observed, ready and published times.
This instrumentation does not change the timestamps sent to the client or the
frame admission policy. Do not mix these headers with the unmodified release
hook; the versioned mapping names and signature intentionally reject that mix.

The ABI 63 live check captured 11,410 frames with no missing measured timestamps
or clock-order violations. In the steady 2× FG menu portion, callback-to-publish
mean/p99 was 0.560/0.662 ms, while the presentation schedule led the callback by
33.6 ms. These timings exclude encoding, network and client work. This is a
measurement check, not a gameplay latency benchmark. Matching hook source commit:
`0ed5952`, on the v0.1.6772-derived tree.

### Generated-image validation

The native DX12 DLSS-G moving-bar fixture completed a direct-capture run at 2× FG.
A full-width centre scanline readback sampled 240 consecutive final-output frames:
all 240 had distinct pixel hashes, with no consecutive duplicates and the bar
present in every sample. The hook reported approximately 58.5 base / 117 output
FPS. This supports capturing generated images rather than repeating base images;
it is not an exhaustive claim about every FG mode or workload. The fixture and
capture shut down normally and desktop capture resumed on the existing stream.

The optional pixel diagnostic now samples a full-width centre scanline instead
of a centre crop. Its CSV includes bright-pixel position/count for RGBA8 sources;
other formats retain hashes and report position -1. It remains synchronous,
bounded to the requested sample count, and unsuitable for latency measurements.

#### Matched frame-generation capture comparison

A 4K DLSS 2x moving-scene fixture was run through normal desktop capture and
this bridge with the same workload and no pixel readback. Across two focused
runs, the 99th-percentile gap between outgoing frames was about 10.8 ms for
normal capture and 9.2 ms for direct capture, at 116 streamed frames per second.
Encoding averaged about 4.4 ms in both paths. The direct hook's measured
callback-to-publish interval averaged 0.52 ms (p99 0.67 ms).

This is a controlled fixture result, not evidence that demanding game workloads
have the same improvement. Client timing varied between repeats; it does not
yet establish an end-to-end latency reduction. Generated-frame scheduling
timestamps and measured callback times remain separate in the trace.

Helper stage traces use a process-specific filename so the host and helper do
not overwrite each other's snapshots. Request the helper snapshot before
ending the stream when investigating shutdown failures.
