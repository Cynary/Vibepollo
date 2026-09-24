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
