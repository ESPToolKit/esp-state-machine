#!/usr/bin/env bash
set -euo pipefail

find src examples test -type f \( -name '*.h' -o -name '*.cpp' -o -name '*.ino' \) -print0 \
  | xargs -0 clang-format -i
