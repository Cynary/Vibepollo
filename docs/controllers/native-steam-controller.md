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
stream teardown removed the HID device. Real hardware controls, haptics, local
input isolation and sleep recovery still require validation. This branch is a
development build, not a replacement release for all Vibepollo features.
