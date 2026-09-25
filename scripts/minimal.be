# The smallest useful ChargeXcel on-device script: every minute, tell one
# charger (at a made-up API) to limit itself to ChargeXcel's headroom in kW.
#
# Needs one secret on the /dlm page:
#   api_key  sent as a bearer token
interval = 60
import json
import string

def tick()
  var t = dlm.telemetry()
  if !t['allowed_amps_valid']
    dlm.report(false, "no headroom figure yet")
    return
  end
  var key = dlm.secret("api_key")
  if key == nil
    dlm.report(false, "set api_key")
    return
  end
  var kw = t['allowed_amps'] * (t['topology'] == 2 ? 208 : 240) / 1000.0
  var r = dlm.http("PATCH", "https://example.invalid/charger/1",
                   {"Authorization": "Bearer " + key, "Content-Type": "application/json"},
                   json.dump({"limit_kw": kw}))
  if r[0] == 200
    dlm.report(true, string.format("limit %.1f kW", kw))
  else
    dlm.log("PATCH -> " + str(r[0]))
    dlm.report(true, "api error " + str(r[0]))
  end
end
