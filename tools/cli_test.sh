#!/usr/bin/env bash
# Role 2 & Role 3 CLI contract test suite.
# Verifies batch stdin, batch file argument, --help, unknown flags,
# malformed networks/queries, unknown inactive warnings, routes 0,
# repeated stops, k=0 routes, and --interactive mode.
set -u

cd "$(dirname "$0")/.." || exit 1

bin=./build/journey
[ -x "$bin" ] || bin=./build/journey.exe
if [ ! -x "$bin" ]; then
  echo "cli-test: ./build/journey not found - run 'make' first" >&2
  exit 1
fi

failures=0

check_case() {
  local name=$1
  local expected_rc=$2
  local expected_out=$3
  local expected_err_substr=$4
  local stdin_data=$5
  shift 5

  local out_file err_file rc
  out_file=$(mktemp)
  err_file=$(mktemp)

  printf '%s' "$stdin_data" | "$bin" "$@" >"$out_file" 2>"$err_file"
  rc=$?

  if [ "$rc" -ne "$expected_rc" ]; then
    echo "FAIL [$name]: exit code $rc != expected $expected_rc" >&2
    cat "$err_file" >&2
    failures=$((failures + 1))
  elif [ -n "$expected_out" ] && [ "$(cat "$out_file")" != "$expected_out" ]; then
    echo "FAIL [$name]: stdout mismatch" >&2
    diff -u <(printf '%s\n' "$expected_out") "$out_file" >&2 || true
    failures=$((failures + 1))
  elif [ -z "$expected_out" ] && [ -s "$out_file" ]; then
    echo "FAIL [$name]: expected empty stdout, got:" >&2
    cat "$out_file" >&2
    failures=$((failures + 1))
  elif [ -n "$expected_err_substr" ] && ! grep -qF "$expected_err_substr" "$err_file"; then
    echo "FAIL [$name]: stderr missing '$expected_err_substr'" >&2
    cat "$err_file" >&2
    failures=$((failures + 1))
  elif [ -z "$expected_err_substr" ] && [ -s "$err_file" ]; then
    echo "FAIL [$name]: unexpected stderr:" >&2
    cat "$err_file" >&2
    failures=$((failures + 1))
  fi

  rm -f "$out_file" "$err_file"
}

# 1. File-argument batch mode matches stdin mode byte-for-byte
if ! diff -u samples/expected1.txt <("$bin" samples/example1.txt); then
  echo "FAIL [file_arg_example1]: output mismatch" >&2
  failures=$((failures + 1))
fi

# 2. --help exits 0 and prints usage on stdout (stderr empty)
help_err=$(mktemp)
out_help=$("$bin" --help 2>"$help_err")
help_rc=$?
if [ "$help_rc" -ne 0 ] || [ -s "$help_err" ]; then
  echo "FAIL [help_flag]: rc=$help_rc, stderr=$(cat "$help_err")" >&2
  failures=$((failures + 1))
fi
rm -f "$help_err"
case "$out_help" in
  *"usage: journey"*"--interactive"*"--serve"*) ;;
  *)
    echo "FAIL [help_flag]: missing usage text" >&2
    failures=$((failures + 1))
    ;;
esac

# 3. Unknown flag exits 1 with usage on stderr and empty stdout
check_case "unknown_flag" 1 "" "usage: journey" "" --bogus

# 4. Missing input file exits 1
check_case "missing_file" 1 "" "ERROR: cannot open" "" does_not_exist_xyz.txt

# 5. Invalid --threads range exits 1
check_case "bad_threads_0" 1 "" "ERROR: invalid thread count" "" --serve --threads 0
check_case "bad_threads_99" 1 "" "ERROR: invalid thread count" "" --serve --threads 99

# 6. Empty input -> malformed network (exit 1)
check_case "empty_input" 1 "" "ERROR: missing 'routes <n>' header" ""

# 7. Header out of range (>400) -> exit 1
check_case "routes_over_limit" 1 "" "ERROR:" $'routes 401\n'

# 8. Duplicate route name -> exit 1
check_case "duplicate_route" 1 "" "duplicate route" $'routes 2\nR1 1 A\nR1 1 B\n'

# 9. Stop count mismatch -> exit 1
check_case "stop_count_mismatch" 1 "" "declares 2 stops" $'routes 1\nR1 2 A\n'

# 10. Unexpected EOF in routes block -> exit 1
check_case "eof_in_routes" 1 "" "unexpected EOF" $'routes 2\nR1 1 A\n'

# 11. Second routes block rejected with exit 1
check_case "second_routes_block" 1 "JOURNEY 1 buses
  1: R1 A -> B" "may appear only once" $'routes 1\nR1 2 A B\nquery A B\nroutes 1\n'

# 12. routes 0 with comments/blank lines and 0-bus + NO JOURNEY queries exits 0
check_case "routes_zero" 0 "JOURNEY 0 buses
  already at X

NO JOURNEY" "" $'# header comment\n\nroutes 0\nquery X X\nquery X Y\n'

# 13. k=0 route + repeated stops on a route + unknown inactive warning + bad query recovery
check_case "k0_repeated_warn_recovery" 0 "JOURNEY 1 buses
  1: Loop A -> C

JOURNEY 1 buses
  1: Loop A -> C" "WARNING:" $'routes 2\nEmpty 0\nLoop 4 A B A C\nnot_a_query\nquery A\nquery A C GHOST\nquery A C\n'

# 14. Interactive mode with file and quit
inter_out=$(printf 'Jayanagar\nWhitefield\n\nquit\n' | "$bin" --interactive samples/city.txt)
case "$inter_out" in
  *"Network ready: 7 routes"*'JOURNEY 2 buses'*'Summary: 2 buses'*) ;;
  *)
    echo "FAIL [interactive_file]: unexpected interactive output" >&2
    echo "$inter_out" >&2
    failures=$((failures + 1))
    ;;
esac

# 15. Interactive mode from stdin prompts for network
inter_stdin_out=$(printf 'routes 1\nR1 2 A B\nA\nB\n\nquit\n' | "$bin" --interactive)
case "$inter_stdin_out" in
  *"Enter network"*'JOURNEY 1 buses'*) ;;
  *)
    echo "FAIL [interactive_stdin]: unexpected interactive stdin output" >&2
    echo "$inter_stdin_out" >&2
    failures=$((failures + 1))
    ;;
esac

if [ "$failures" -ne 0 ]; then
  echo "cli-test: $failures case(s) failed" >&2
  exit 1
fi

echo "cli-test: all 15 CLI contract checks passed"
