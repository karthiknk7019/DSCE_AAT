#!/usr/bin/env bash
# Role 3 owns this file.
#
# UI smoke check: starts the optional HTTP server on a private port, loads a
# network through the same JSON API the browser uses, then checks a journey, a
# closure and the closure sweep. Never builds - run `make` first.
#
# Usage: bash tools/ui_smoke.sh
set -u

cd "$(dirname "$0")/.." || exit 1

bin=./build/journey
[ -x "$bin" ] || { echo "ui-smoke: ./build/journey not found - run 'make' first" >&2; exit 1; }

port=${UI_SMOKE_PORT:-8139}
log=$(mktemp)
"$bin" --serve --port "$port" >"$log" 2>&1 &
server_pid=$!

cleanup() {
  kill "$server_pid" 2>/dev/null
  wait "$server_pid" 2>/dev/null
  rm -f "$log"
}
trap cleanup EXIT

base="http://127.0.0.1:$port"
ready=0
for _ in $(seq 1 50); do
  if curl -sf "$base/api/network" >/dev/null 2>&1; then
    ready=1
    break
  fi
  sleep 0.1
done
[ "$ready" = 1 ] || { echo "ui-smoke: server did not start on $port" >&2; cat "$log" >&2; exit 1; }

fail() {
  echo "ui-smoke: $*" >&2
  exit 1
}

body='{"name":"smoke","routes":[{"name":"R1","stops":["1","2","7"]},{"name":"R2","stops":["3","6","7"]}]}'
built=$(curl -sf -X POST "$base/api/build" -H 'Content-Type: application/json' -d "$body") \
  || fail "POST /api/build failed"
case "$built" in
  *'"routes":2'*) ;;
  *) fail "build response did not report 2 routes" ;;
esac

journey=$(curl -sf -X POST "$base/api/journey" -d '{"source":"1","destination":"6"}') \
  || fail "POST /api/journey failed"
case "$journey" in
  *'"buses":2'*) ;;
  *) fail "expected 2 buses for 1 -> 6" ;;
esac
case "$journey" in
  *'"from":"7"'*) ;;
  *) fail "expected a transfer at stop 7" ;;
esac
case "$journey" in
  *'"layers":'*) ;;
  *) fail "journey response is missing BFS layers" ;;
esac

closed=$(curl -sf -X POST "$base/api/journey" \
  -d '{"source":"1","destination":"6","inactive":["R2"]}') \
  || fail "closed-route query failed"
case "$closed" in
  *'"found":false'*) ;;
  *) fail "closing R2 should remove the only journey" ;;
esac

impact=$(curl -sf -X POST "$base/api/impact" -d '{"source":"1","destination":"6"}') \
  || fail "POST /api/impact failed"
case "$impact" in
  *'"critical_count":2'*) ;;
  *) fail "both routes should be critical in the two-route network" ;;
esac

curl -sf "$base/" | grep -q 'Wayfinder' || fail "index.html was not served"
curl -sf "$base/samples/city.txt" | grep -q 'routes 7' || fail "sample file was not served"

code=$(curl -s -o /dev/null -w '%{http_code}' "$base/api/does-not-exist")
[ "$code" = "404" ] || fail "unknown API path returned $code, expected 404"

echo "ui-smoke: build, journey, closure, sweep and static files all OK"
