import sys


def _coop_log(message, *, type, isError=False):
    """Use Indigo's injected API in actions; ordinary console output otherwise."""
    indigo_api = globals().get("indigo")
    if indigo_api is not None:
        indigo_api.server.log(message, type=type, isError=isError)
    else:
        print("{}: {}".format(type, message), file=sys.stderr if isError else sys.stdout)
        if isError:
            raise SystemExit(1)


import urllib.request

try:
    request = urllib.request.Request(
        "http://FEEDER-GATEWAY-IP/api/v1/open",  # ESP32 DHCP reservation.
        data=b"", method="POST",
        headers={"X-API-Key": ""},  # Match firmware/config.h if configured.
    )
    with urllib.request.urlopen(request, timeout=5) as response:
        response.read()

except Exception as error:
    _coop_log(
        "Feeder OPEN request failed: {}".format(error),
        type="Chicken Feeder",
        isError=True,
    )

else:
    _coop_log(
        "Feeder OPEN request accepted by gateway",
        type="Chicken Feeder",
    )
