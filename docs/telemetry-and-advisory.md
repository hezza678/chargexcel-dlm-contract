# Telemetry and Advisory: the two structs

Every tick, ChargeXcel sends you a **Telemetry** struct and you send back an
**Advisory**. That's the entire contract, for both ways of plugging in:

- An off-board plugin gets them as JSON and form fields over HTTP. See
  [`http-api.md`](http-api.md) for the exact spelling.
- An on-device script gets the telemetry as a Berry map from
  `dlm.telemetry()`, and sends its advisory with `dlm.report()`. See
  [`on-device-scripts.md`](on-device-scripts.md).

The names below are the firmware's. On the wire and in scripts they are
snake_case (`allowedAmps` → `allowed_amps`).

**Nothing in the Advisory is an input to anything ChargeXcel decides.** A
plugin reports whether it is active and, in a few words, what it is doing;
ChargeXcel shows that on its `/dlm` page and dashboard. That is all a plugin
can say, by design: ChargeXcel is a load manager, not a charger. It cannot
ask a car to draw less, and it does not want to be told to cut power — that
is its own decision, made from its own current sensors.

## Telemetry (ChargeXcel → plugin)

What ChargeXcel tells you, every tick.

| Field | Type | Meaning |
|---|---|---|
| `epochSeconds` | uint32 | Wall-clock time, or `0` if ChargeXcel hasn't synced time yet. |
| `timeTrusted` | bool | Whether `epochSeconds` is real. If you're doing time-of-use logic, check this first. |
| `serviceLegAAmps`, `serviceLegBAmps` | float | The two service legs' current draw, in amps. Deliberately not called "L1"/"L2" — an installer can wire either sensor to either leg, so don't assume which is which. A net-zero plugin needs these, not just the headroom. |
| `evseBranchAmps` | float | What the EV charging circuit itself is currently drawing. |
| `ctReadingsValid` | bool | `false` means the three values above are placeholders, not real measurements — a failed read reads as zero, which looks exactly like a genuinely idle service otherwise. |
| **`allowedAmps`** | float | **The number to plan against.** How much the EV branch may draw right now without pushing either service leg past its continuous limit. |
| `allowedAmpsValid` | bool | `false` means "no opinion," not "unlimited" — an uncommissioned unit, or CT readings not currently trustworthy. With `ctReadingsValid` you can tell which: valid readings and no headroom figure means not commissioned. |
| `relayPermitted`, `relayClosed` | bool | Read-only context: is the charging circuit allowed to be live, and is it actually live right now. Useful for explaining to a user "charging stopped, but it wasn't me." |
| `safetyState` | name | ChargeXcel's own state, sent as its name (e.g. `RUNNING`, `SHED_OFF`, `TRIP_LATCHED`, `THERMAL_OFF`). Context only. |
| `serviceRatingAmps`, `evseBreakerRatingAmps` | float | The electrician's commissioning figures. |
| `continuousCapacityAmps` | float | `min(breaker × 80% or 100%, the max charge rate the electrician entered)` — the most the EV branch may ever be asked for on this installation. |
| `topology` | uint8 | `1` = split-phase 120/240, `2` = split-phase 120/208. Both are two-legged; the value only matters for turning amps into watts. |
| `solarInstalled` | bool | With solar, the service sensors cannot tell export from import, so a plugin doing net-zero charging has to reason about the legs differently. |
| `disconnectDelaySeconds` | float | How long ChargeXcel tolerates a sustained near-limit condition (service current above its trip threshold) before shedding load on its own — the electrician's per-installation setting, typically 1-5 minutes. The gentlest of the three shed rules; informational, so a plugin doing time-of-use or net-zero work knows how much runway it has to react on its own first. |
| `severeOverloadDelaySeconds` | float | The one hardcoded delay behind **both** of the two harder overload rules: total service current over its rating, or (should it ever happen) the EV branch over its own commissioned cap. Currently 30 s for either. Not per-installation, and not two different numbers — see below. |

### On the three shed rules and their delays

ChargeXcel has three internal load-shed rules, and none of them acts
instantly — every one requires a sustained breach first, which is exactly
what these two fields expose:

1. Total service current over its trip threshold → `disconnectDelaySeconds`
   (electrician-configured, ~1-5 min).
2. Total service current over its full rating → `severeOverloadDelaySeconds`.
3. The EV branch over its own commissioned cap (`continuousCapacityAmps`) →
   also `severeOverloadDelaySeconds`, the same number as (2).

`severeOverloadDelaySeconds` reports the current figure, so don't hardcode
30 s in your plugin.

The two delay fields are published to off-board plugins only; a script's
`dlm.telemetry()` map doesn't carry them.

## Advisory (plugin → ChargeXcel)

What you report. Both fields are shown on `/dlm` and the dashboard, and go nowhere else.

| Field | Type | Meaning |
|---|---|---|
| `present` | bool | "I am alive and actively managing." `false` means running but idle. Over HTTP the poll arriving is already the heartbeat, so this mostly distinguishes idle from active. |
| `action` | string ≤ 31 chars | A few words on what you are doing right now — `24 A to Tesla`, `paused: peak tariff`. Printable ASCII; anything else becomes `?`. |
