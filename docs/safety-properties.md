# Safety properties

What this contract guarantees, each stated as the specific failure it
prevents — not as a vague assurance. The list is short because the
contract gives a plugin almost nothing to break.

## 1. A plugin cannot influence any decision

**Prevents:** a compromised, buggy, or simply overconfident plugin changing
what current ChargeXcel allows, or when it sheds load.

Nothing a plugin sends is read by anything that decides. The two fields in
the `Advisory` — `present` and `action` — are stored and shown on the
`/dlm` page; no code path carries them anywhere else. This is enforced in
the firmware's source by a validator that fails the build check if any plugin-facing file names the current-limit
arbiter, the safety supervisor, the relay driver, or a current-sensor
driver — and, in the other direction, if any of those files names anything
plugin-facing. That validator is itself mutation-tested: each of those
mistakes has been introduced deliberately and watched turn it red.

## 2. A plugin cannot suppress load shedding

**Prevents:** an overload going unhandled because "the DLM plugin was
supposed to be managing it."

ChargeXcel's own overload protection runs from its own current sensors on
its own 250 ms schedule, and has no code path that reads anything a plugin
submits. It runs the same way whether zero plugins or ten are talking to
ChargeXcel. A plugin that told the car to draw 48 A on a 40 A branch is
just another overload, and it is handled the same way a dryer is.

## 3. An off-board plugin cannot cost ChargeXcel anything but web-server time

**Prevents:** a looping or flooding plugin degrading the unit.

Submissions faster than 5 Hz are refused with `429` before anything is
copied. The one piece of free text a plugin may send is cut at 31
characters and stripped to printable ASCII before it is stored.

## 4. An on-device script cannot take the unit down with it

**Prevents:** a script bug (an endless loop, a runaway list, a hung
server) reaching the part of ChargeXcel that sheds load.

- **Time:** a tick is stopped after about 2.1 million instructions and
  fails with `timeout_error`. The script runs on its own low-priority
  task, so waiting on a slow server stalls only the script.
- **Memory:** everything the script allocates comes from its own fixed
  20 KB arena, which is set aside once when the unit starts. When it runs
  out, the tick fails. The unit's own memory is never touched.
- **Reach:** no files, no sockets, no `os` or `sys`. The only way out is
  `dlm.http()`: one request at a time, at most 16 per tick, at least a
  second apart, responses cut at 4 KB.
- **Output:** `dlm.report()` goes through exactly the same path, sanitiser
  and rate limit as an off-board plugin's report. Properties 1 and 2 hold
  for scripts unchanged.

The desktop runner in [`../runner/`](../runner/) uses the same VM code and
its tests exercise these limits. Each is also a source check in the
firmware's own build.

## What this means practically for you, as a plugin author

You cannot break any of the above by accident, no matter what your plugin
does — sending garbage, going silent, flooding requests — because the only
things you can do to ChargeXcel are put text on a page and use some of its
web server's time (or, for a script, its own set-aside memory and CPU budget). The worst outcome of a plugin bug is on the *car's*
side: your plugin told it the wrong number. ChargeXcel's own protection
still stands underneath that.
