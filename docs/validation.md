# Validation and provenance

## Evidence timeline

- 28 September 2026: compared existing community implementations and recovered
  control-packet construction from the official app. This was protocol research,
  not hardware validation.
- 2 October: captured real GO Open/Close commands and final-state reports from the
  official app. Golden packet vectors are retained in the unit tests; raw capture
  files, device identifiers and vendor-app analysis artifacts are not published.
- Subsequent gateway work established ESP32 feeder operation, notification/readback
  handling, latest-target queue behavior, Ethernet OTA and Indigo state delivery.
- 6 October: owner confirmed the ESP32 was installed in the coop and feeder
  functions were working.
- 7 October: extracted the feeder-only source, removed unrelated application
  subsystems, and rewrote deployment instructions for a new installation.

## Extraction verification

The extracted firmware is compiled with Arduino ESP32 core 3.3.0 and the target
16 MB/OPI PSRAM/custom-partition configuration. Protocol and latest-command C++
tests and eleven feeder-relevant Indigo receiver tests pass. Browser JavaScript
syntax and state selection are checked separately. Local test credentials are
ignored and are not shipped. The extracted image has **not** been flashed onto
the installed coop gateway; this avoids replacing the owner's working installation.

Protocol coverage: captured command bytes/CRC, final versus moving/invalid reports.
Queue coverage: active request and latest pending target, disconnect, completion.
Receiver coverage: validation, unknown values, unchanged updates, stable IDs,
rename/restart, collisions, heartbeat expiry, live timeout/verbosity and event
replay/logging. No raw test count implies exhaustive hardware validation.

## Reproduction boundary

This is a source extraction, not an import of another Git history: the provided
workspace had no `.git` directory. Original files remain in place. Paths,
private LAN addresses, Mesh exports, private credentials, installed Indigo IDs,
compiled binaries and unrelated product code are excluded.

The Arduino discovery branch is retained so a fresh board can enroll a feeder.
RFID tags/reader form an optional external automation layer. Actual successful
reader coverage and every possible radio/power failure cannot be inferred from
successful feeder commands. Compatibility beyond the observed GO unit is unverified.
