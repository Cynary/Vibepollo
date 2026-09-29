# Direct game capture

Direct capture takes a game's frame before Windows desktop composition. It can reduce capture latency and avoid uneven delivery from Windows Graphics Capture (WGC). It uses the bundled CaptureEngine hook; WGC remains available for the desktop and games that cannot be captured directly.

## Enable it

In Configuration, enable **Direct game capture** and enter the full paths of the games you want to use under **Direct capture game executables**. Separate multiple paths with semicolons:

```text
C:\Games\Example\Game.exe;D:\SteamLibrary\steamapps\common\Example2\Game2.exe
```

Use the actual game executable, rather than Steam, a launcher, or a shortcut. Up to 16 executables are supported. Leave games with anti-cheat protection off this list: they may prohibit injected capture hooks.

Save and restart Vibepollo with the stream closed. Launch a new stream, then launch the game. For ordinary configured application commands, Vibepollo waits for capture to be ready before starting the command. An already-running game without a hook stays on WGC until you restart that game. A previously hooked game can reconnect without restarting.

WGC handles unlisted applications, missing hooks, and failed direct capture. When the game exits, capture returns to WGC. A game that stops producing frames for five seconds also returns to WGC; reconnect the stream to retry that game. Initial hook setup has a separate 30-second frame timeout.

Disable **Direct game capture** and restart Vibepollo to return to normal capture. Resolution, HDR, encoder, virtual-display, and controller settings are unchanged. Direct capture uses the WGC backend for fallback even if another backend was selected.

## Building the Windows package

The host, WGC helper, launcher and CaptureEngine hooks must be distributed together. Windows builds download the [pinned hook bundle](https://github.com/Cynary/capture-engine/releases/tag/vibepollo-abi63-20260928) and verify its SHA-256 hash. For an offline build, configure CMake with `CAPTURE_ENGINE_BUNDLE_DIR` pointing to a directory containing:

- `capture_hook_x64.dll`
- `capture_hook_x86.dll`
- CaptureEngine's `LICENSE`

The package installs these under `tools/capture-engine`. CMake checks the DLL hashes against `cmake/packaging/windows_capture_engine.cmake`. The [pinned source revision](https://github.com/Cynary/capture-engine/commit/c13145ecefabdb18a48b245b3861fd5bdaeb8551) is `c13145ecefabdb18a48b245b3861fd5bdaeb8551` (shared-memory ABI 63). Set `SUNSHINE_BUNDLE_DIRECT_CAPTURE=OFF` to omit the download. A build without this bundle continues to support WGC and logs a warning if direct capture is enabled.

The old scripts under `tools/ce-prototype` are developer experiments. Normal configuration does not require their environment variables or a manually prefixed application command.

## Measurements and compatibility

In a stationary Stellar Blade test at 4K HDR, HEVC 4:4:4 and a 116 FPS target, direct capture reached the encoder about **5.9 ms earlier on average** than WGC. Encoded output was ready about **6.3 ms earlier**. Both measurements start at the game's Present call; they do not include network transfer or client display latency. This was a short comparison with frame generation off, rather than a guarantee for every game.

The prototype was tested with D3D11 and D3D12, HDR colour conversion, reconnects, and generated frames. Compatibility still depends on the game and graphics API. Logs identify when direct capture starts and when WGC resumes, so a working stream alone does not prove it is using direct capture.

## Integration checks (28 September 2026)

The installed host/helper pair passed an initial Overcooked 2 capture and three reconnects to the same game process. Each reconnect delivered at least 1,200 direct frames, and each helper shut down cleanly. Ending the game returned to WGC on the same stream. With the hook directory temporarily removed, WGC continued delivering the game and the launcher did not block. Stellar Blade also switched into direct capture at 3840×2160 and detected its transition to HDR.

The Windows host, helper, launcher and both web interfaces built successfully. Target-list validation, stall timeouts, rolling traces, thread shutdown and launcher readiness were checked separately. Packaging checks verified the downloaded archive and both hook hashes. These checks cover the integration; the earlier numeric HDR and frame-generation tests are recorded in the developer notes under `tools/ce-prototype`.
