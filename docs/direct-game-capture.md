# Direct game capture

Direct capture takes a game's frame before Windows desktop composition. It can reduce capture latency and avoid uneven delivery from Windows Graphics Capture (WGC). It uses the bundled CaptureEngine hook; WGC remains available for the desktop and games that cannot be captured directly.

## Enable it

In Configuration, enable **Direct game capture**, save, and restart Vibepollo with the stream closed. Then launch games normally, including through MoonDeck, Steam, Epic or Ubisoft Connect. You do not need to enter each game's executable or change its launch command.

Vibepollo follows the **foreground fullscreen window on the streamed display**. It never searches behind that window for another game. Borderless and exclusive fullscreen windows qualify; ordinary maximized windows with a title bar or taskbar gap do not. Games can already be running or start through any launcher.

When focus moves to another eligible fullscreen application, direct capture follows it. When you switch to the desktop or a windowed application, capture uses WGC. WGC also covers the transition until the new target produces a frame. Returning to the game reactivates direct capture without reconnecting the stream.

**Always use WGC for these games** optionally excludes executable paths, separated by semicolons (up to 16). No inclusion list is required. The former additional-executables setting is retained only for configuration compatibility and no longer changes selection.

Before attaching, Vibepollo checks for known anti-cheat services, game-directory markers and known incompatible titles. Detected protection keeps the game on WGC. These checks cannot establish compatibility with every anti-cheat system; use the exclusion setting for games whose rules prohibit capture hooks. Enabling automatic capture does not make an unsigned hook approved by a game's anti-cheat provider.

WGC handles the desktop, excluded applications, missing hooks, and failed direct capture. When the game exits, capture follows the new foreground window. A game that stops producing frames for five seconds returns to WGC; a failed target is not repeatedly injected during the same stream. Reconnect the stream to retry it. Normal focus changes are not failures and allow immediate reactivation. Initial hook setup has a separate 30-second frame timeout.

Disable **Direct game capture** and restart Vibepollo to return to normal capture. Resolution, HDR, encoder, virtual-display, and controller settings are unchanged. Direct capture uses the WGC backend for fallback even if another backend was selected.

## Building the Windows package

The host, WGC helper and CaptureEngine hooks must be distributed together. Windows builds download the [pinned hook bundle](https://github.com/Cynary/capture-engine/releases/tag/vibepollo-auto-abi63-20260928) and verify its SHA-256 hash. For an offline build, configure CMake with `CAPTURE_ENGINE_BUNDLE_DIR` pointing to a directory containing:

- `capture_hook_x64.dll`
- `capture_hook_x86.dll`
- CaptureEngine's `LICENSE`

The package installs these under `tools/capture-engine`. CMake checks the DLL hashes against `cmake/packaging/windows_capture_engine.cmake`. The [pinned source revision](https://github.com/Cynary/capture-engine/commit/7d022c8585c1dbc2bcc60e2e44ff1426555f1771) is `7d022c8585c1dbc2bcc60e2e44ff1426555f1771` (shared-memory ABI 63). Set `SUNSHINE_BUNDLE_DIRECT_CAPTURE=OFF` to omit the download. A build without this bundle continues to support WGC and logs a warning if direct capture is enabled.

The old scripts under `tools/ce-prototype` are developer experiments. Normal configuration does not require their environment variables or a manually prefixed application command.

## Measurements and compatibility

In a stationary Stellar Blade test at 4K HDR, HEVC 4:4:4 and a 116 FPS target, direct capture reached the encoder about **5.9 ms earlier on average** than WGC. Encoded output was ready about **6.3 ms earlier**. Both measurements start at the game's Present call; they do not include network transfer or client display latency. This comparison used frame generation off; results depend on the game and system.

The prototype was tested with D3D11 and D3D12, HDR colour conversion, reconnects, and generated frames. Compatibility still depends on the game and graphics API. Logs identify when direct capture starts and when WGC resumes, so a working stream alone does not prove it is using direct capture.

## Integration checks (28 September 2026)

The earlier integration passed an initial Overcooked 2 capture and three reconnects to the same game process. Each reconnect delivered at least 1,200 direct frames, and each helper shut down cleanly. Ending the game returned to WGC on the same stream. With the hook directory temporarily removed, WGC continued delivering the game and the launcher did not block. Stellar Blade also switched into direct capture at 3840×2160 and detected its transition to HDR.

The Windows host, helper, launcher and both web interfaces built successfully. Target-list validation, stall timeouts, rolling traces, thread shutdown and launcher readiness were checked separately. Packaging checks verified the downloaded archive and both hook hashes. These checks cover the integration; the earlier numeric HDR and frame-generation tests are recorded in the developer notes under `tools/ce-prototype`.

### Automatic discovery

With the optional executable list empty, an already-running Overcooked 2 was selected and captured directly, then reconnected without restarting the game. Alan Wake Remastered launched through Epic was also selected automatically and delivered direct frames. Avatar: Frontiers of Pandora launched through Ubisoft Connect also delivered direct frames without an executable entry. Late-attachment testing exposed circular hook chains with other overlays; the bundled hook fixes resize interception and adds verified x86 body interception instead of requiring per-game launch wrappers.

Stellar Blade was also discovered with an empty list and transitioned from SDR to 4K HDR. The exclusion check kept Overcooked on WGC with no hook module loaded; clearing the exclusion allowed late attachment to the same process and delivered over 6,000 direct frames. Native hook builds and unit tests passed, and static analysis stayed within the project's existing warning baseline.

### Fullscreen focus switching

The 28 September update replaces background-window discovery with foreground fullscreen selection. Live testing switched repeatedly between an already-running Overcooked 2 (D3D11 SDR) and a separate D3D12 4K HDR application, with direct frames confirmed from both processes in the same stream. Minimizing the game returned to WGC, and restoring it resumed direct capture. Fullscreen geometry tests cover negative monitor coordinates, rounding, title-bar/taskbar gaps and spanning windows.

The helper retains its shared-memory mappings across focus changes because dormant hooks still hold references to them. It waits for the previous hook's dormant acknowledgment before resetting those mappings. If that acknowledgment fails, it keeps WGC rather than reusing resources an old producer might still access. A dormant hook stays loaded; unloading it could invalidate references retained by the game or other overlays. This is not a way to undo an anti-cheat rejection.
