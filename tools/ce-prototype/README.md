# Direct game capture prototype

This replaces desktop capture with a game's shared GPU textures while keeping the
same Vibepollo stream connected. It uses CaptureEngine v0.1.6772's MIT-licensed
hook and shared-memory interface. It is experimental and disabled by default.

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

For testing, build `stream_launcher.cpp` as a Windows GUI executable and prefix
the existing MoonDeckStream application command with its quoted path. This
wrapper waits for the capture helper's readiness event before launching the
original MoonDeckStream command. Buddy therefore starts the game after the
helper is watching, rather than racing capture initialization. Without this
handshake, some launches precede the helper and remain on desktop capture.

Use this wrapper only with the prototype enabled. It fails after 45 seconds if
no helper signals readiness. Restore the original application command when
restoring the normal helper. It does not solve arbitrary late injection or
guarantee that process polling beats every game's graphics initialization.

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
- Validate complete HDR encode/decode output and measure latency under gameplay.
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

This checks ten GPU conversion vectors against CPU reference values. It does
not verify the subsequent video encoder, decoder, or TV output.

Typed sRGB input views already return linear values. The bridge preserves those
values for scRGB output and re-encodes them for an ordinary SDR output surface.
The GPU test covers both paths; forcing the earlier SDR shader reproduces the
double-conversion error (0.00405 instead of 0.05127 for one channel). These
checks validate the conversion shader, not the full encoded HDR stream.
