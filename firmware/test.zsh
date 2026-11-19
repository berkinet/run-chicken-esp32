#!/bin/zsh
set -euo pipefail
cd "${0:A:h}/.."
mkdir -p build/tests
for name in protocol latest_command admin_password; do
  c++ -std=c++11 "firmware/tests/${name}_test.cpp" -o "build/tests/$name"
  "build/tests/$name"
done
python3 -m unittest discover -s indigo/tests -v
