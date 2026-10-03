# Capture comparison on Vibepollo 2.0.0

On this controlled 4K HDR fixture, 1000 Hz improves WGC, but direct capture still reaches NVENC substantially earlier.

Time from the application's Present call to the host's NVENC submission API call, in milliseconds:

| Capture | Average | Median | p99 | Matched unique frames | Duplicate presentations excluded |
|---|---:|---:|---:|---:|---:|
| WGC, 120 Hz | 7.66 | 7.73 | 11.59 | 2380 | 5 |
| WGC, 1000 Hz | 6.07 | 6.00 | 10.12 | 2334 | 123 |
| Direct, 1000 Hz | 1.27 | 1.22 | 2.55 | 2357 | 0 |

WGC1000 reduced average delay by 1.59 ms relative to WGC120. Direct capture was another 4.79 ms earlier on average. These are independent distributions, not paired copies of each identical rendered frame across runs. They do not include network, decoding, physical scanout or TV panel response. NVENC submission is an API boundary, not an observation of the encoder engine's physical start.

## Conditions

RTX 5090 Windows host, K17 Arc 130V client. Rebased Vibepollo 2.0.0 and Moonlight VRR18. Host logs confirm the new client requests VRR low-latency policy. Stream: 3840x2160, requested 120 FPS, HEVC P1 yuv444 10-bit HDR, 500 Mbps. The D3D12 fixture generates 116 FPS with a high-resolution timer. Direct capture selects its focused fullscreen window dynamically; WGC runs without the old MOONMACHINE_WGC_EVENT_CAPTURE experiment. All runs launched through MoonDeck's existing diagnostic shortcut; the independent graphics fixture takes focus while that shortcut remains running. No simultaneous streams or second encoder.

The fixture is a changing colour field with little GPU load. It establishes capture-path overhead, not Stellar Blade's behavior under GPU load or frame generation. That needs the next gameplay session.

## Trace association and limits

30-second captures with eight seconds excluded at the start and two at the end. About 20 seconds and 2,300 unique frames per result. Observer timestamps bracket Present. Direct capture source timestamps fall within those brackets. WGC association follows DXGI Present to Win32k composition identity and DWM selection, then the helper callback/publication trace. Callback association uses the preceding selected DWM event, as in the previous analysis; it is not a pixel-encoded identity check. Each host frame is joined to NVENC timing by frame number and encode-entry timestamp. No missing associations in the analyzed windows. Duplicate presentation identities are excluded and counted above, rather than treated as fresh game frames.

The initial Overcooked D3D11 attempt was rejected: composition events occurred on a different thread, so the existing association logic matched zero frames. Initial old-client runs also did not request the new host VRR policy and are controls only, not the table above. A first direct fixture recording had no observer events because it armed after hook attachment; it is excluded.

The comparison used Vibepollo commit `e9e452fac` and Moonlight commit `7d28f1ae`. A later client-only shutdown fix (`afebe564`) stops its signal worker before SDL teardown; it does not change the capture or rendering paths measured here. Normal gameplay is the next validation step, particularly under heavy GPU load and frame generation.
