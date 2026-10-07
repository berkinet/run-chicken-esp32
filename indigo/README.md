# Optional Indigo automation

The gateway also works without Indigo. These scripts use Python's standard
library and run as Indigo Execute Python Script actions or from a shell.

## Open and Close

Copy `feeder_open.py` and `feeder_close.py` to your Indigo Scripts folder
(on macOS normally `/Library/Application Support/Perceptive Automation/Scripts/`).
Replace `FEEDER-GATEWAY-IP` and set X-API-Key to match `config.h`. Run as a script
file in an Indigo action, or paste its contents into an embedded Python action.
Inside Indigo it logs through the injected `indigo` API; outside it prints to
stdout/stderr and returns exit code 1 on request failure. It logs acceptance,
not movement confirmation. Monitor the state API or receiver below for completion.

## Receive state variables

Copy `remote_states.py` into Indigo's `Python3-includes` directory, together with
`remote_states_config.json` made from the example. Choose a unique source ID and
at least 24-character token; set exactly the same source/token in the gateway's
`remote_states_credentials.h`. Set its endpoint to
`http://INDIGO-SERVER:8786/remote-states/v1`, rebuild, and allow the listener through
the local firewall. This endpoint is trusted-LAN HTTP, not an internet service.

Create an Indigo Server Startup trigger containing:

```python
import importlib
import remote_states
remote_states.stop()
importlib.reload(remote_states)
remote_states.start()
```

Save and execute the trigger's **Execute Actions Only** action once to start now.
Repeat it after changing configuration or module code; it stops the prior listener
before reloading. There is no need to restart all of Indigo.

The receiver creates `remoteStatesFolder` and variables with prefix `ChickenFeeder`:
DoorState, RequestedDoorState, BLEConnected, BLEBusy, BLEError, LastResult,
ChipTemperatureC, ChipTemperatureMinC, ChipTemperatureMaxC, UptimeMs, LastSeen,
Online. For example `ChickenFeeder_DoorState`. It also creates a heartbeat timeout
setting (the example uses `Set_HeartbeatTimeout`) and a verbosity setting.
Saved IDs in `remote_states_ids.json` preserve references when variables are
renamed or moved; keep that file across upgrades. Do not copy an ID registry from
a different Indigo installation.

Values are strings. Booleans become `true`/`false`, JSON null becomes `unknown`.
Online describes receipt of gateway updates, not BLE connection or physical hatch
position. It defaults to false at startup and after a 90-second heartbeat timeout;
other values retain their last values while offline. The gateway sends changes,
a heartbeat every 30 seconds, and a full snapshot after a delivery failure.
A receiver reload alone does not force an unchanged snapshot; reboot the gateway
if newly configured fields remain unknown. Do so only when appropriate for your
installation.

## RFID automation used in the original installation

Phidgets22 presence events drive two Indigo triggers: Presence Start → feeder
Open; Presence End → feeder Close, with delayed loss-of-presence handled by the
reader/plugin. There is no independent hold-open timer in the ESP32. This repository
contains no Phidgets plugin or tag-enrollment logic. Validate stationary tag
presence, antenna coverage and loss delay before leaving that automation unattended.
The manual controls remain useful without RFID.
