#!/usr/bin/env python3
"""Reference DLM plugin for ChargeXcel — polls it once a second, decides how
much the car should draw, and reports what it did. Run it as-is to see the
contract in action, or fork it as a starting point for a real integration
(OCPP, Tesla, a tariff, etc.).

ChargeXcel is a load manager, not a charger: it publishes how much headroom
the service has, and it can cut the charging circuit off, but it cannot ask a
car to draw less. That is this program's job. Everything ChargeXcel needs
back is `present` (alive and managing) and a short `action` text for its
/dlm page -- nothing you send changes what ChargeXcel does.

This example's "policy" is intentionally trivial: it aims for 80% of whatever
headroom ChargeXcel reports, with a 6 A floor (below 6 A, most EVs just drop
the session, so it's not worth offering). A real plugin would replace
`decide()` with something that actually talks to your charging equipment's
own protocol (Tesla, OCPP, ...) -- everything else here (the HTTP loop, error
handling, the 1 Hz cadence) is meant to be reused as-is.

Usage:
    python3 dlm_reference_plugin.py --host chargexcel-abcd.local --key <your X-DLM-Key>

Get a key from ChargeXcel's own web UI: log in, open "Load Management
Plugins", and register a provider. See ../docs/http-api.md for the full
contract this script implements.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from urllib.parse import urlencode


@dataclass
class Telemetry:
    allowed_amps: float
    allowed_amps_valid: bool
    ct_valid: bool
    evse_branch_amps: float
    relay_permitted: bool
    safety_state: str
    solar_installed: bool
    raw: dict


class DlmClient:
    """Thin wrapper around POST /api/dlm/v1/tick. See ../docs/http-api.md."""

    def __init__(self, host: str, key: str, *, timeout: float = 5.0, use_https: bool = False):
        scheme = "https" if use_https else "http"
        self._url = f"{scheme}://{host}/api/dlm/v1/tick"
        self._key = key
        self._timeout = timeout

    def tick(self, *, present: bool = False, action: str | None = None) -> Telemetry:
        """Reports {present, action} and returns fresh telemetry. An empty
        call (all defaults) is "alive, nothing to report" and is also how
        you'd just read allowed_amps."""
        fields: dict[str, str] = {}
        if present:
            fields["present"] = "1"
        if action:
            fields["action"] = action[:31]

        body = urlencode(fields).encode("ascii")
        request = urllib.request.Request(
            self._url,
            data=body,
            method="POST",
            headers={
                "X-DLM-Key": self._key,
                "Content-Type": "application/x-www-form-urlencoded",
            },
        )
        try:
            with urllib.request.urlopen(request, timeout=self._timeout) as response:
                payload = json.loads(response.read())
        except urllib.error.HTTPError as error:
            if error.code == 401:
                raise PermissionError(
                    "ChargeXcel rejected the X-DLM-Key -- missing, wrong, or revoked. "
                    "Register a provider on ChargeXcel's Load Management Plugins page."
                ) from error
            if error.code == 429:
                raise RuntimeError("polling faster than 5 Hz; slow down to 1 Hz") from error
            raise

        telemetry = payload["telemetry"]
        return Telemetry(
            allowed_amps=telemetry["allowed_amps"],
            allowed_amps_valid=telemetry["allowed_amps_valid"],
            ct_valid=telemetry["ct_valid"],
            evse_branch_amps=telemetry["evse_branch_amps"],
            relay_permitted=telemetry["relay_permitted"],
            safety_state=telemetry["safety_state"],
            solar_installed=telemetry["solar_installed"],
            raw=payload,
        )


def decide(telemetry: Telemetry) -> tuple[float | None, str]:
    """Replace this with real policy. Returns (amps to set on the car, or
    None to stop, and a short action text for ChargeXcel's /dlm page).

    This example: aim for 80% of headroom, and stop below a 6 A floor. THIS
    is where a real plugin would then call Tesla's API / send an OCPP
    SetChargingProfile / write a Modbus register -- ChargeXcel never sees
    that part."""
    if not telemetry.allowed_amps_valid:
        return None, "waiting: no headroom figure"
    amps = round(telemetry.allowed_amps * 0.8, 1)
    if amps < 6.0:
        return None, "stopped: under 6 A floor"
    return amps, f"{amps:.1f} A to car"


def run(host: str, key: str, *, interval_s: float = 1.0, use_https: bool = False) -> None:
    client = DlmClient(host, key, use_https=use_https)
    print(f"Polling {host} once every {interval_s:g}s. Ctrl-C to stop.")

    action = ""
    managing = False
    while True:
        started = time.monotonic()
        try:
            # Report what we did last tick; read what to do this tick.
            telemetry = client.tick(present=managing, action=action)
            amps, action = decide(telemetry)
            managing = amps is not None
            # >>> a real plugin tells the car/EVSE `amps` here <<<
            print(
                f"headroom {telemetry.allowed_amps:.1f}A "
                f"(valid={telemetry.allowed_amps_valid}, state {telemetry.safety_state}, "
                f"branch {telemetry.evse_branch_amps:.1f}A) -> {action}"
            )
        except PermissionError as error:
            print(f"auth error: {error}", file=sys.stderr)
            return
        except (urllib.error.URLError, TimeoutError, RuntimeError) as error:
            # ChargeXcel shows this plugin as offline after 30 s of silence;
            # just try again next tick.
            print(f"tick failed, will retry: {error}", file=sys.stderr)

        elapsed = time.monotonic() - started
        time.sleep(max(0.0, interval_s - elapsed))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="e.g. chargexcel-abcd.local")
    parser.add_argument("--key", required=True, help="X-DLM-Key token from ChargeXcel's web UI")
    parser.add_argument("--https", action="store_true", help="use https:// instead of http://")
    parser.add_argument("--interval", type=float, default=1.0, help="seconds between ticks (default 1.0)")
    args = parser.parse_args()

    try:
        run(args.host, args.key, interval_s=args.interval, use_https=args.https)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
