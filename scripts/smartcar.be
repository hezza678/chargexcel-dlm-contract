# ChargeXcel on-device DLM script: smartcar.com cloud API.
#
# Every `interval` seconds: if ChargeXcel has enough headroom for the car to
# draw at least a Level 2 minimum, the vehicle should be charging; otherwise
# it should be stopped. Policy in one line: Smartcar's third-party API has no
# amperage/speed control on this plan (or at all, for most makes) -- only
# start, stop, and a state-of-charge limit -- so the control surface is
# binary, the same shape a Tesla integration ends up with.
#
# The car is only TOLD when the answer changes (headroom crosses the line),
# plus once every `reassert_s` as a safety net for a command that was lost.
# Every tick still reads the headroom and reports; it just doesn't open an
# HTTPS session to say the same thing again. Commanding on every tick would
# mean 60 TLS sessions an hour, each a real "start charging" at the car, for
# a state that changes a few times a day.
#
# Needs four secrets on the /dlm page (all from the Smartcar developer
# dashboard for this app + the one vehicle it is connected to):
#   client_id       the app's OAuth client id
#   client_secret   the app's OAuth client secret
#   vehicle_id      the vehicle to control
#   user_id         sent as sc-user-id on every vehicle call
interval = 60
var reassert_s = 900

import json
import string

# Smartcar's client-credentials token lasts an hour; cached here and reused
# across ticks (the VM persists between ticks, so a global survives until an
# error reloads the script).
var token = ""
var token_expiry = 0

# What the car was last successfully told (true = start, false = stop, nil =
# nothing yet, or the last answer was rejected), and when.
var sent_charge = nil
var sent_at = 0

def refresh_token(now)
  var cid = dlm.secret("client_id")
  var csec = dlm.secret("client_secret")
  if cid == nil || csec == nil
    return false
  end
  var body = "grant_type=client_credentials&client_id=" + cid + "&client_secret=" + csec
  var hdr = {"Content-Type": "application/x-www-form-urlencoded"}
  var r = dlm.http("POST", "https://iam.smartcar.com/oauth2/token", hdr, body)
  if r[0] != 200
    dlm.log("token request -> " + str(r[0]))
    return false
  end
  var doc = json.load(r[1])
  var tok = doc.find('access_token')
  if tok == nil
    return false
  end
  token = tok
  var life = doc.find('expires_in')
  if life == nil life = 3600 end
  token_expiry = now + int(life) - 120  # refresh two minutes early
  return true
end

def tick()
  var t = dlm.telemetry()
  if !t['allowed_amps_valid']
    dlm.report(false, "no headroom figure yet")
    return
  end
  var vid = dlm.secret("vehicle_id")
  var uid = dlm.secret("user_id")
  if vid == nil || uid == nil
    dlm.report(false, "set vehicle_id and user_id")
    return
  end

  var min_amps = 6.0
  var want_charge = t['relay_permitted'] && t['allowed_amps'] >= min_amps
  var label = want_charge ? "charging: headroom ok" : "stopped: low headroom"

  # Same answer as last time, and told recently enough: report it, no HTTP.
  if sent_charge != nil && sent_charge == want_charge && t['epoch'] - sent_at < reassert_s
    dlm.report(true, label)
    return
  end

  if token == "" || t['epoch'] >= token_expiry
    if !refresh_token(t['epoch'])
      dlm.report(false, "auth failed")
      return
    end
  end

  var base = "https://vehicle.api.smartcar.com/v3/vehicles/" + vid + "/commands/charge/"
  var hdr = {"Authorization": "Bearer " + token, "sc-user-id": uid, "Content-Type": "application/json"}
  var r = dlm.http("POST", base + (want_charge ? "start" : "stop"), hdr, "{}")

  if r[0] == 401
    token = ""        # forces a fresh token next tick
    sent_charge = nil # and the command is re-sent with it
    dlm.report(false, "token rejected")
    return
  end
  if r[0] == 200 || r[0] == 202
    sent_charge = want_charge
    sent_at = t['epoch']
    dlm.report(true, label)
  else
    # Not recorded as sent, so the next tick tries again.
    dlm.log("charge command -> " + str(r[0]))
    dlm.report(true, string.format("api error %d", r[0]))
  end
end
