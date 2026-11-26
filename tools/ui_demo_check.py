#!/usr/bin/env python3
"""End-to-end check of the Act 2c browser demo (task U6).

This is *tooling*, not part of the graded build: `make` stays dependency-free.
It starts the existing server on a private port, drives the six core demo steps
in headless Chromium, and asserts the same facts a presenter says out loud.
On failure it writes a screenshot under `build/ui-demo/` and exits non-zero.

Run it through the wrapper, which finds a Python that has Playwright:

    bash tools/ui_demo_check.sh

First-time setup on a machine that does not have it:

    python3 -m pip install playwright
    python3 -m playwright install chromium

Why `#net=city.txt`: the default sample can change as the team adds datasets;
Act 2c is about the fictional Bengaluru demo, so pin it in the URL.
"""

import argparse
import re
import socket
import subprocess
import sys
import time
import urllib.request
from contextlib import contextmanager
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "build" / "journey"
SHOTS = ROOT / "build" / "ui-demo"


class Failure(Exception):
    """A demo assertion failed; the message already names the screenshot."""


def free_port():
    """Ask the kernel for an unused loopback port, then release it."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_for_server(base, timeout=10.0):
    deadline = time.time() + timeout
    last_error = None
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(f"{base}/api/network", timeout=0.25):
                return
        except Exception as error:  # server still starting
            last_error = error
            time.sleep(0.1)
    raise Failure(f"server did not answer on {base}: {last_error}")


def start_server(port, web_root=None):
    if not BIN.is_file():
        raise Failure(f"{BIN} is missing - run `make` first")
    log = ROOT / "build" / "ui-demo-server.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    handle = log.open("w")
    command = [str(BIN), "--serve", "--port", str(port)]
    if web_root:
        command += ["--web", str(web_root)]
    process = subprocess.Popen(
        command,
        cwd=str(ROOT),
        stdout=handle,
        stderr=subprocess.STDOUT,
        text=True,
    )
    base = f"http://127.0.0.1:{port}"
    try:
        wait_for_server(base)
    except Failure:
        process.terminate()
        raise Failure(f"server failed to start; see {log}")
    return process, log


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=0, help="private port (default: pick one)")
    parser.add_argument(
        "--web",
        default=None,
        help="serve this web/ directory instead of ./web (e.g. a pristine checkout)",
    )
    parser.add_argument("--headed", action="store_true", help="show the browser")
    args = parser.parse_args()

    try:
        from playwright.sync_api import expect, sync_playwright
    except ImportError:
        print(
            "ui-demo: Playwright is not installed for this Python.\n"
            "  python3 -m pip install playwright\n"
            "  python3 -m playwright install chromium",
            file=sys.stderr,
        )
        return 2

    port = args.port or free_port()
    server, log = start_server(port, args.web)
    base = f"http://127.0.0.1:{port}"

    failures = []
    browser = None
    try:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch(headless=not args.headed)
            page = browser.new_page(viewport={"width": 1512, "height": 950})
            console_errors = []
            page.on(
                "console",
                lambda message: console_errors.append(message.text)
                if message.type == "error"
                else None,
            )
            page.on("pageerror", lambda error: console_errors.append(str(error)))

            def shot(name):
                SHOTS.mkdir(parents=True, exist_ok=True)
                path = SHOTS / f"{name}.png"
                page.screenshot(path=str(path), full_page=True)
                return path

            @contextmanager
            def step(name):
                # A failed Playwright assertion is opaque without the pixels.
                try:
                    yield
                except Exception as error:  # noqa: BLE001 - report, don't mask
                    path = shot(f"failure-{name}")
                    raise Failure(f"step '{name}' failed: {error}\n  screenshot: {path}")

            page.goto(f"{base}/#net=city.txt", wait_until="load")
            big = page.locator("#resultCard .big-number")

            with step("1-result"):
                # Act 2c step 1: 2 buses, 210 then V1, change at Majestic.
                expect(big).to_have_text("2", timeout=15000)
                pills = page.locator("#resultCard .it-leg .route-plate")
                expect(pills).to_have_count(2)
                assert pills.all_inner_texts() == ["210", "V1"], pills.all_inner_texts()
                expect(page.locator("#resultCard .card-foot .micro")).to_contain_text(
                    "1 change on the way"
                )

            with step("2-proof"):
                # Step 2: the destination is in layer 2 and not in 0-1.
                expect(page.locator("#proofCard .proof-note")).to_contain_text(
                    "first appears in layer 2"
                )
                expect(page.locator("#proofCard .layer-row.dest .layer-label")).to_have_text(
                    "2 buses"
                )
                # The histogram is drawn from the C++ stats, not recomputed here.
                expect(page.locator("#proofCard .layer-row").first).to_contain_text("0 buses")

            with step("3-layers"):
                # Step 3: one ring per bus; Whitefield sits on ring 2; hover works.
                page.locator('.seg button[data-mode="layers"]').click()
                expect(page.locator("#mapCaption")).to_contain_text("BFS from", timeout=10000)
                expect(page.locator("#mapLegend")).to_be_visible()
                expect(page.locator("#mapLegend .ml-title")).to_have_text("BFS layers")
                depth = page.evaluate("state.layerDepthByStop.get('Whitefield')")
                assert depth == 2, f"Whitefield layer={depth}, expected 2"
                assert page.locator(".layer-node").count() > 0, "layer map drew no nodes"
                # Journey lines and the travelling dot are drawn above the
                # nodes, so hover a layer-1 stop that is not on the journey.
                targets = page.evaluate(
                    """() => {
                      const drawn = new Set();
                      for (const leg of state.result.legs) {
                        drawn.add(leg.from);
                        drawn.add(leg.to);
                      }
                      const rect = document.querySelector('#map').getBoundingClientRect();
                      const out = [];
                      for (const [stop, point] of state.layerPositions) {
                        if (state.layerDepthByStop.get(stop) === 1 && !drawn.has(stop)) {
                          out.push({
                            stop,
                            x: rect.left + state.view.x + point.x * state.view.k,
                            y: rect.top + state.view.y + point.y * state.view.k,
                          });
                        }
                      }
                      return out;
                    }"""
                )
                assert targets, "no non-journey layer-1 stop to hover"
                hovered = False
                target = None
                for target in targets[:8]:
                    page.mouse.move(target["x"], target["y"])
                    page.wait_for_timeout(80)
                    if page.locator("#tooltip").is_visible():
                        hovered = True
                        break
                assert hovered and target is not None, "hovering a layer node did not open its tooltip"
                expect(page.locator("#tooltip b")).to_have_text(target["stop"])
                expect(page.locator("#tooltip")).to_contain_text("1 bus")

            with step("4-close-v1"):
                # Step 4: close V1 -> no journey, one closure filtered, no rebuild.
                page.locator('.seg button[data-mode="network"]').click()
                v1 = page.locator(".route-row", has=page.locator(".rname", has_text=re.compile(r"^V1$")))
                v1.click()
                expect(page.locator("#resultCard .big-label")).to_contain_text(
                    "no route", timeout=10000
                )
                expect(page.locator("#resultCard .transfer-note")).to_contain_text(
                    "1 closed route filtered"
                )

            with step("5-sweep"):
                # Step 5: reopen V1, sweep every route, V1 is the only critical one.
                v1.click()
                expect(big).to_have_text("2", timeout=10000)
                page.locator("button.sweep-btn").click()
                expect(page.locator(".impact-chip.bad")).to_have_text(
                    "1 critical route", timeout=15000
                )
                row = page.locator(".impact-row", has_text="V1")
                expect(row.locator(".istate")).to_have_text("stranded")
                expect(page.locator(".impact-table .impact-row")).to_have_count(7)

            with step("6-buses-not-stops"):
                # Step 6: Buses != stops. A -> F is 2 buses over 10 stops.
                page.locator("#openNetwork").click()
                expect(page.locator("#networkDialog")).to_be_visible()
                page.locator(".sample-card", has_text="Buses ≠ stops").click()
                expect(page.locator("#networkDialog")).to_be_hidden()
                first_plate = page.locator("#resultCard .it-leg .route-plate").first
                expect(first_plate).to_have_text("Outer", timeout=15000)
                expect(big).to_have_text("2", timeout=10000)
                pills = page.locator("#resultCard .it-leg .route-plate")
                assert pills.all_inner_texts() == ["Outer", "Outer2"], pills.all_inner_texts()

            with step("console"):
                # A demo with a console error is a demo that can fail on stage.
                assert not console_errors, "console errors:\n  " + "\n  ".join(console_errors)

            final = shot("pass")
            print(f"ui-demo: Act 2c steps 1-6 passed in headless Chromium")
            print(f"ui-demo: final screenshot {final.relative_to(ROOT)}")
            print(f"ui-demo: server log {log.relative_to(ROOT)}")
    except Failure as error:
        failures.append(str(error))
    finally:
        if browser is not None:
            # The Playwright context manager already stops the driver; closing
            # again after it exits raises "Event loop is closed" and hides the
            # real result, so this close is best-effort only.
            try:
                browser.close()
            except Exception:  # noqa: BLE001 - cleanup must not mask the result
                pass
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()

    if failures:
        for message in failures:
            print(f"ui-demo: {message}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
