# Wi-Fi shell flows

`wifi search` scans and presents one entry per SSID, ordered by strongest RSSI.
Use Up/Down to navigate, Enter to select, and Escape to cancel. Open networks
connect immediately; saved credentials are reused, while a new protected
network asks for a password displayed only as asterisks. A configuration is
persisted only after the station reports both association and an IP address.
Attempts expire after 15 seconds and late scan/connection callbacks are ignored
by generation tokens.

## Event dispatch and persistence

The `GOT_IP` callback performs no heavy work in `sys_evt`. It publishes an event
snapshot to the Wi-Fi dispatch, and a dedicated coordinator worker serializes
network-side effects and persistence. This keeps blocking I/O, long mutex
holds, and retries out of both the system event task and the LVGL thread.

Ownership is transferred explicitly when an asynchronous operation is queued.
The dispatch/coordinator acknowledges or releases each item, including queue
overflow and retry paths. Connection, scan, and persistence tokens identify the
current generation, so stale callbacks from cancelled, timed-out, or superseded
attempts are ignored.

Scanning is asynchronous. Its callback copies the result snapshot before the
UI-side flow deduplicates and orders SSIDs. Cancellation and timeout invalidate
the generation. Credentials are persisted only after `connected` and `has_ip`;
failure paths roll back transient state. The forget flow performs an explicit
wipe of the persisted credential and never displays or logs the password.

This dispatch/coordinator boundary fixes the reboot observed immediately after
`GOT_IP`, which was caused by heavy work running in `sys_evt`.

`wifi saved` lists saved SSIDs without exposing credentials. Use Up/Down to
navigate, Enter to request forgetting the selected network, and Escape to exit.
Forgetting requires a second Enter to confirm; Escape cancels the confirmation.
