#!/usr/bin/env bash
# Role 3 owns this file (UI tooling exception, AGENTS.md).
#
# One-command demo check (task U6): finds a Python that has Playwright and
# runs tools/ui_demo_check.py. Never builds - run `make` first.
#
# Usage: bash tools/ui_demo_check.sh [--headed] [--port N]
set -u

cd "$(dirname "$0")/.." || exit 1

for py in ${PYTHON:-} python3 /usr/bin/python3 python; do
  [ -n "$py" ] || continue
  if command -v "$py" >/dev/null 2>&1 && "$py" -c 'import playwright' >/dev/null 2>&1; then
    exec "$py" tools/ui_demo_check.py "$@"
  fi
done

echo "ui-demo: no Python with Playwright was found." >&2
echo "ui-demo: install it once with:" >&2
echo "  python3 -m pip install playwright && python3 -m playwright install chromium" >&2
exit 2
