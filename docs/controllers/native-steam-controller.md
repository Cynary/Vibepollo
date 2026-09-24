# Native Steam Controller (experimental)

The matching Moonmachine client can send 2026 Steam Controller reports over the
existing encrypted streaming connection. Vibepollo forwards them to the
experimental Windows virtual HID driver, which presents an emulated Valve
28DE:1304 device to Steam Input. USB remains on the client. SSH and USB/IP are
not used for controller traffic.

This requires Cynary/libvirtualgamepad's `experimental/steam-controller` driver
and Cynary/moonlight-qt's `experimental/native-steam-controller` branch. Enable
`MOONMACHINE_NATIVE_CONTROLLER=1` in the host process environment for development;
otherwise the extension is not advertised or accepted. This is not enabled in a
normal release or automatically enabled by installing the driver.

Only one native controller is supported per host. Controller permissions apply
to these messages. Request IDs, lengths, directions and command allowlists are
checked. Feature requests expire after two seconds. Stopping a stream closes
the driver handle and removes its virtual device, including on disconnect.

The host's shared protocol dependency stays on its existing base revision with
only the wire-format header added. The client has its own transport-enabled
revision. Their protocol headers must match.

Builds and wire-format tests pass. An end-to-end synthetic test delivered 4,920
reports to the Windows viewer with zero malformed reports; a longer count at
stream teardown reported 7,368 accepted inputs and zero rejected inputs. Normal
stream teardown removed the HID device. Physical testing received 1,344 real reports in five seconds with zero malformed
reports. Steam Input identified controller type 17 and supplied orientation.
Left and right haptic output was felt on the physical controller. Reconnecting
received another 4,721 reports with zero malformed reports without re-pairing.
A finite-pulse comparison produced three matching bursts on each side, confirmed
by the tester. Local input isolation and sleep recovery still require validation. This branch is a
development build, not a replacement release for all Vibepollo features.

The tester also confirmed local Steam menu navigation immediately after ending
the stream, without pairing or reconnecting. Concurrent local input isolation
while streaming, sleep recovery, and the complete guided control checklist
have not been fully validated.

## Write completion and input latency

Validated output and setting writes complete after forwarding to the bounded
transport queue. They do not wait for a client round trip. Physical replies still
retire outstanding requests and report delivery failures. Read queries remain
pending until the real reply arrives; reads are not cached because a preceding
setting command may select which information the controller returns.

The client executes commands in order, so a read follows earlier writes even
though Windows has already received write completion. At most 32 requests are
outstanding, with two-second expiry. Input reports do not wait for acknowledgements.

The Windows driver-poll thread uses a high-resolution one-millisecond wait.
`Sleep(1)` previously produced a roughly 15.6 ms polling cadence on the test host,
causing Steam's haptic worker to accumulate seconds of delay. The corrected
300-command, 250-Hz finite-pulse test completed in 1.22 seconds: mean write
completion 1.78 ms, median 1.18 ms, p99 9.45 ms, maximum 10.95 ms. Before correcting
the wait, the same test took 4.71 seconds with mean completion 15.43 ms.
These measure Windows write-call completion, not the time a motor physically
moves. The run had no command-delivery failures or new Steam haptic backlog
warnings. In-game trackpad behavior still requires user confirmation.
