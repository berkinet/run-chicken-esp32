# Lessons and troubleshooting

## Findings that made this work

- Product names were misleading: the purchased GO feeder advertised F-80. The
  observed name and GATT services were more useful than guessing from the SKU.
- A T50 packet was not a justified default. Official-app packet analysis and a
  GO capture established the actual 32-byte timestamp/action/CRC layout.
- A write acknowledgement arrives well before motion completes. Decode final
  position reports and require a fresh matching report for success.
- Connection and output state are separate. Reconnect can succeed while hatch
  position is still unknown. Never equate connected with closed.
- Phone app connections can interfere with gateway access. Provide explicit
  Connect/Disconnect controls and release the phone app before reconnecting.
- Long BLE and HTTP publication work belongs on worker tasks, not the web-server
  loop. Network delivery uses a bounded latest-snapshot queue.
- Feeder requests need timestamps. NTP synchronization must precede movement.
- A request queue should not replay a backlog of contradictory Open/Close actions.
  We retain one active operation and only the newest pending target.
- In Indigo, preserve variable IDs across renames. Startup triggers must actually
  execute in the server context; the action editor's Run button was not a reliable
  way to leave the long-running receiver active in the original installation.
- A valid OTA upload is not proof of recovery from a crashing application. Keep
  the USB recovery path and exact partition layout documented.

## Common symptoms

| Symptom | Check |
|---|---|
| `feeder_not_found` | Feeder power, range, app connection, saved address, F-80 advertisement |
| `multiple_feeders` | Isolate the intended feeder for first enrollment |
| `connection_or_services_failed` | Phone released connection? Correct model and UUIDs? Radio range? |
| `clock_synced=false` / rejected request | DHCP, DNS and outbound NTP; wait for time synchronization |
| `confirmation_timeout` | Observe actual motion; inspect report prefix/length, battery/power and range |
| State `unknown` | No valid final position report, disconnect, moving, or failed confirmation |
| Indigo variables stop changing | Receiver startup trigger, matching source/token, LAN/firewall and Online |
| Console login works but API is unauthorized | API key is separate from browser credentials |
| OTA rejected | Application binary only, correct partition/core settings, same-origin browser session |

## Deliberate limits and further work

The gateway does not decode battery, schedule, motor mode, obstruction or jam
states. Incoming packets are interpreted only for the observed final-position
fields. Recovery, RF congestion, multiple feeders and long-duration operation
need more validation before broad compatibility claims.

The user suspects other F80 feeders and coop doors may work. That is a useful
research lead, not a supported-device claim: establish advertisement/GATT identity,
command format and final-state semantics on each model before sending movement.

No automatic factory reset, address reassignment, feeder firmware update or
command replay is performed. A stale/unknown state must not be treated as a
verified closed hatch by higher-level automation.
