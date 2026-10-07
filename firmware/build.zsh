#!/bin/zsh
set -euo pipefail
cd "${0:A:h}/.."
cli=${ARDUINO_CLI:-arduino-cli}
args=()
[[ -n ${ARDUINO_CONFIG:-} ]] && args+=(--config-file "$ARDUINO_CONFIG")
for name in admin_credentials remote_states_credentials; do
  [[ -f firmware/feeder_gateway/$name.h ]] || { print -u2 "Create firmware/feeder_gateway/$name.h from its example first."; exit 1; }
done
"$cli" "${args[@]}" compile \
 --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=custom \
 --build-property upload.maximum_size=6553600 \
 --build-path "$PWD/build" firmware/feeder_gateway
