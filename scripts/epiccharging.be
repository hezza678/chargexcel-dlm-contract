# ChargeXcel on-device DLM script: epiccharging.com cloud API.
#
# Every `interval` seconds: read the ports that are charging and give each
# one its share of ChargeXcel's headroom, in kW. Policy in one line: split
# the headroom equally across charging ports, clamped to each port's own
# max_power.
#
# Needs two secrets on the /dlm page:
#   tenant   the {tenant} in https://{tenant}.epiccharging.com
#   api_key  sent as Token-Authorization
interval = 60

import json
import string

def volts(t)
  return t['topology'] == 2 ? 208 : 240
end

def tick()
  var t = dlm.telemetry()
  if !t['allowed_amps_valid']
    dlm.report(false, "no headroom figure yet")
    return
  end
  var tenant = dlm.secret("tenant")
  var key = dlm.secret("api_key")
  if tenant == nil || key == nil
    dlm.report(false, "set tenant and api_key")
    return
  end
  var base = "https://" + tenant + ".epiccharging.com/api/external/v1/charger-port/"
  var hdr = {"Token-Authorization": key, "Accept": "application/json"}

  var r = dlm.http("GET", base + "?status__in=CHARGING", hdr, nil)
  if r[0] != 200
    dlm.report(false, "api GET " + str(r[0]))
    return
  end
  var doc = json.load(r[1])
  var ports = doc
  if type(doc) == 'instance' && doc.find('results') != nil
    ports = doc['results']
  end
  if type(ports) != 'instance' || size(ports) == 0
    dlm.report(true, "no port charging")
    return
  end

  var share_kw = t['allowed_amps'] * volts(t) / 1000.0 / size(ports)
  var sent = 0
  hdr['Content-Type'] = "application/json"
  for p: ports
    var id = p.find('id')
    if id == nil continue end
    var kw = share_kw
    var cap = p.find('max_power')
    if cap != nil && real(cap) < kw kw = real(cap) end
    var pr = dlm.http("PATCH", base + str(id) + "/", hdr, json.dump({"charging_speed": kw}))
    if pr[0] == 200
      sent += 1
    else
      dlm.log("PATCH " + str(id) + " -> " + str(pr[0]))
    end
  end
  if sent == size(ports)
    dlm.report(true, string.format("%d port(s): %.1f kW each", sent, share_kw))
  else
    dlm.report(true, string.format("%d/%d port(s): %.1f kW each", sent, size(ports), share_kw))
  end
end
