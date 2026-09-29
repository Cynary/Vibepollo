> For normal use, see [Direct game capture](../../docs/direct-game-capture.md). The environment-based setup below describes the earlier standalone experiments.

# Direct game capture prototype

This replaces desktop capture with a game's shared GPU textures while keeping the
same Vibepollo stream connected. It uses CaptureEngine v0.1.6772's MIT-licensed
hook and shared-memory interface, with the ABI 63 changes described below.
It is experimental and disabled by default.

## Enable or restore the prototype

Close any stream first. In an elevated PowerShell window running as the Windows
streaming user, run the setup script with the built host, helper, launcher, matching
ABI 63 hook, and the game's actual executable:

```powershell
.\setup.ps1 -Mode Enable `
  -TargetPath 'D:\Games\Example\Game.exe' `
  -HelperPath 'C:\DirectCapture\sunshine_wgc_capture-ce.exe' `
  -HostPath 'C:\DirectCapture\sunshine.exe' `
  -LauncherPath 'C:\DirectCapture\ce-stream-launcher.exe' `
  -HookPath 'C:\DirectCapture\capture_hook_x64.dll'
```

The default installation is `C:\Program Files\Apollo`, and the application is
`MoonDeckStream`. Use `-InstallDirectory` or `-ApplicationName` if yours differs.
Keep the launcher and matching hook files at those paths while enabled. For
32-bit games, keep the matching 32-bit hook beside the 64-bit hook.

The script backs up the installed host and helper, prefixes the existing application
command with the readiness launcher, and saves the environment values it changes.
It restarts ApolloService, so an active stream would be interrupted. Launch the
selected game through MoonDeck after setup; an already-running unhooked game
must be closed and relaunched.

```powershell
.\setup.ps1 -Mode Status
.\setup.ps1 -Mode Disable
```

Disable restores both binaries, the original application command, and environment.
It preserves unrelated application edits and refuses to overwrite a host, helper or
launch command changed since setup. Recovery files are kept under
`%LOCALAPPDATA%\Moonmachine\DirectCapture`; do not delete them while enabled.
This command does not install a matching hook build or change Windows services'
startup settings.

The fixture regression checks restoration and conflict handling without
controlling the real service. Run it with streaming closed because it briefly
changes, then restores, the current user's prototype environment values:

```powershell
.\tests\setup_test.ps1 -SetupScript .\setup.ps1
```

The fixture regression and a real enable → MoonDeck launch → 4K HDR capture →
game exit → disable cycle passed on Windows.

## Manual configuration

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
- Broaden startup/resize validation beyond the tested games. The matching hook
  now retains its frame-processing lock and releases tracked DX12 resources
  before a preserved-swapchain resize; repeated Stellar Blade launches passed.
- Validate longer controller-play sessions and additional frame-generation modes.
- Expand HDR validation beyond static colour patches and measure latency under gameplay.
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
`0ed5952`, on the v0.1.6772-derived tree. The later `dbb5e79` fix keeps
the frame-processing mutex owned through capture and drawing. Commit `7518a74`
also preserves the tracked-resource cleanup around swapchain resize. Three
fresh HDR launches and DLSS 2x on/off transitions passed in Stellar Blade;
two repeated startup logs confirmed that the resize cleanup ran. These checks
cover the reproduced failure, not every game's resize lifecycle.

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

The same 4K DLSS 2x fixture was subsequently recorded at Moonlight's accepted
HEVC-packet boundary on the client and decoded offline. A steady sample of 480
consecutive decoded frames had no repeated scanlines or bar positions. Median
bar movement was 16 pixels per received frame, versus about 32 pixels expected
at the independently reported 58.5 FPS base rate. The hook reported 117 output
FPS, and the received stream was 3840x2160 HEVC 4:4:4 10-bit with PQ/BT.2020
metadata. This verifies extra motion images through capture, encoding, network
delivery and decoding. The fixture content itself was SDR; native HDR colour
was checked separately with the patch-pattern test above. This does not measure
physical display presentation or promise that every generated frame survives
capture rate limiting.

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

### Rolling helper diagnostics

The helper includes the same rolling trace implementation used in these tests.
When `MOONMACHINE_HOST_FRAME_TRACE` is set, stage events retain the latest three
minutes (up to two million events). Create the helper's `.wgc.csv.snapshot` file
and wait for its removal before reading the CSV. Taking a snapshot does not stop
recording. The writer runs separately from the capture thread; capture only adds
to the in-memory queue. These diagnostics remain opt-in.

The standalone regression checks recording beyond the former 90-second cutoff,
removal of old events, and repeated snapshots. From this directory:

```sh
g++ -std=c++20 -O2 -static tests/rolling_trace_test.cpp -o rolling-trace-test.exe
./rolling-trace-test.exe
```

It passed on Windows and Linux. The two headers are byte-identical to those used
by the Windows helper in the matched capture and subsequent transition tests.

With `MOONMACHINE_CE_TRACE_TIMING=1`, bridge checkpoints also appear in the rolling
helper stage CSV. Their names begin with `ce_bridge_`. For these rows,
`callback_id` holds the producer's raw source QPC to join against the CE timing
CSV; `steady_us` is the measured checkpoint time. `source_100ns` is zero. The
checkpoints distinguish output-lock acquisition, shader setup/command recording,
GPU submission and completion wait, metadata publication, unlock and notification.
Do not subtract the raw source key from a checkpoint time: Frame Generation may
use a future presentation schedule for that key.

### Loaded-scene check

Stellar Blade ran unpaused in a loaded 4K HDR scene with Frame Generation off,
then DLSS 2x, and exited normally. Each matched steady sample covered about
51 seconds at 116 streamed FPS, with no client drops recorded. Outgoing frame
spacing P99 was 9.43 ms without FG and 9.14 ms with it. The off sample included
one 44.95 ms gap, localized to a 35.67 ms bridge conversion/publication interval.
The finer checkpoints subsequently located that stall in acquisition of the
shared output texture, rather than the colour-conversion shader.

Measured capture-to-publication averaged 2.43 ms without FG (P99 6.12 ms), versus
0.59 ms with FG (P99 0.74 ms). The callbacks occur at different rendering stages
and GPU utilization differed (94% and 70% spot readings), so these numbers do
not isolate FG's cost or prove a speedup over desktop capture. This was a loaded
scene with a stationary character, not a combat or controller-play test.

### Rare handoff stalls

The shared-texture acquisition occasionally took 35–42 ms even though the
conversion itself completed in roughly 0.16 ms. Adding an explicit D3D11
`Flush()` after the host released the texture did not help; that experiment
is not part of the bridge.

A Windows GPU/scheduler trace caught the helper, game, compositor and encoder
blocked at the same time while GraphicsPerfSvc enumerated graphics state.
The helper was scheduled within 7 microseconds once its wait ended. In a
same-scene comparison, temporarily disabling that service removed the large
wait; restoring it brought the wait back:

| GraphicsPerfSvc | Frames measured | Acquisition P99 | Largest acquisition wait | Largest whole-bridge interval |
| --- | ---: | ---: | ---: | ---: |
| Enabled | 13,951 | 0.058 ms | 36.216 ms | 36.503 ms |
| Disabled for the test | 13,834 | 0.057 ms | 0.284 ms | 0.595 ms |
| Restored | 13,925 | 0.057 ms | 35.927 ms | 36.269 ms |

Each row covers about two minutes after warmup, with 4K HDR and FG off. These
results identify the large spikes observed in this test; they do not explain
every source of streaming jitter. The service's original configuration was
restored afterward. The prototype does not change Windows services.

### HDR gameplay through the client decoder

The updated hook was also tested through the complete bridge with Stellar Blade
in a loaded scene, HDR enabled and DLSS Frame Generation set to 2x. After
disconnecting and reconnecting, the helper resumed direct final-output capture.
The video received by Moonlight was HEVC at 3840×2160, `yuv444p10le`, limited
range, PQ/BT.2020. The first 240 decoded frames had no decoding errors or
adjacent identical image hashes, and a decoded preview showed the expected
scene. Unique hashes establish changing video, not that every generated frame
survived encoding; the controlled moving-pattern test above checks interpolation.

Exiting the game returned the same stream to desktop capture. Disabling the
prototype restored the original helper and launch command. These recording runs
are correctness checks, not latency benchmarks.

## Disconnect and reconnect

Use the matching host build as well as the helper. The host closes the helper’s
control pipe and lets it exit before releasing shared GPU resources. It kills the
helper only if the bounded shutdown wait expires. An older host immediately
kills the helper, which produced shared-fence removal errors in testing.

The matching hook also keeps capture-resource destruction on the render thread
for games with a single-threaded D3D11 device. Overcooked 2 exposed the previous
background-release race: reconnect froze after 14–15 frames. The corrected build
passed three reconnects of the same game process, followed by input-driven exit
and return to desktop capture without closing the stream.

The final hook build, 0.1.31 (`997c23c`), passed the full native verification gate,
including unit tests, x64 ASan/UBSan and static warning limits. An x86 sanitizer
runtime was unavailable. The subsequent Stellar Blade check used this exact
build with HDR and DLSS 2x: reconnect resumed final-output capture, the received
video decoded as 4K 10-bit 4:4:4 PQ/BT.2020, and 240 decoded frames had no
adjacent duplicate hashes. Normal game exit returned to desktop capture. Setup
v2 then restored both original binaries, verified by their SHA-256 hashes.
The exact packaged 0.1.31 hook also passed an initial Overcooked capture and
three reconnects of the same game process, with at least 1,200 frames per session.
An initial test launched before helper readiness and correctly stayed on desktop
capture; that run was excluded and repeated after correcting the test sequence.
