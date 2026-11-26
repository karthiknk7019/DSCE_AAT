#!/usr/bin/env bash
# Role 3 owns this file.
#
# CLI smoke check: runs the built binary on every sample input and compares
# stdout byte-for-byte with the expected file (docs/CONTRACT.md section 3).
# This script never builds - run `make` first.
#
# Usage: bash tools/smoke.sh
set -u

cd "$(dirname "$0")/.." || exit 1

bin=./build/journey
[ -x "$bin" ] || bin=./build/journey.exe   # Windows builds add .exe

if [ ! -x "$bin" ]; then
  echo "smoke: ./build/journey not found - run 'make' first" >&2
  exit 1
fi

status=0
for pair in "samples/example1.txt:samples/expected1.txt" \
            "samples/demo.txt:samples/expected-demo.txt" \
            "samples/city.txt:samples/expected-city.txt" \
            "samples/city-large.txt:samples/expected-city-large.txt" \
            "samples/bmtc.txt:samples/expected-bmtc.txt"; do
  input=${pair%%:*}
  expected=${pair##*:}
  if "$bin" < "$input" | diff -u "$expected" -; then
    echo "smoke: $input matches $expected"
  else
    echo "smoke: $input does NOT match $expected" >&2
    status=1
  fi
done

exit $status
