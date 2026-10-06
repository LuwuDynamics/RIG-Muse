#!/usr/bin/env python3
"""Configure a public RIG image over USB without putting a token in argv/history."""
import argparse
import getpass
import json
import re
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Puppy's USB UART port")
    parser.add_argument("--sdk-token", action="store_true", help="Prompt for your SDK token without echo")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--proxy", metavar="HOST:PORT", help="LAN HTTP CONNECT proxy; no authentication")
    group.add_argument("--direct", action="store_true", help="Disable the proxy")
    parser.add_argument("--clear", action="store_true", help="Remove saved SDK token and proxy settings")
    args = parser.parse_args()
    if args.clear and (args.sdk_token or args.proxy or args.direct):
        parser.error("--clear cannot be combined with other settings")
    changes = []
    if args.sdk_token:
        token = getpass.getpass("Muse SDK token (hidden): ").strip()
        if not re.fullmatch(r"mgst_[A-Za-z0-9_-]{42}[AEIMQUYcgkosw048]", token):
            parser.error("Invalid SDK token; copy the complete token from gadgets.muse.ai")
        changes.append(("setup.sdk_token", {"token": token}))
    if args.proxy:
        try:
            host, port_text = args.proxy.rsplit(":", 1)
            port = int(port_text)
            if not re.fullmatch(r"[A-Za-z0-9.-]{1,127}", host) or not 1 <= port <= 65535:
                raise ValueError()
        except ValueError:
            parser.error("Proxy must be an IPv4 address or hostname followed by :PORT")
        changes.append(("setup.proxy", {"host": host, "port": port}))
    if args.direct:
        changes.append(("setup.proxy", {"enabled": False}))
    if args.clear:
        changes.append(("setup.clear", {}))
    import serial
    with serial.Serial(args.port, 115200, timeout=.2) as uart:
        uart.dtr = False
        uart.rts = False
        time.sleep(3)  # Some USB bridges reset the board when the port opens.
        uart.reset_input_buffer()

        def command(name, params=None):
            data = ">" + name + (" " + json.dumps(params, separators=(",", ":")) if params is not None else "") + "\n"
            uart.write(data.encode())
            uart.flush()
            deadline = time.monotonic() + 12
            while time.monotonic() < deadline:
                line = uart.readline().decode(errors="replace")
                if "@setup " not in line:
                    continue
                response = json.loads(line.split("@setup ", 1)[1])
                if not response.get("ok"):
                    raise RuntimeError("Device rejected " + name + ": " + response.get("error", "unknown_error"))
                return response
            raise RuntimeError("No setup response. Check the port and install the public firmware.")

        for name, params in changes:
            command(name, params)
            print(name + ": saved")  # Never echo params or token fragments.
        if changes:
            command("setup.restart")
            time.sleep(3)
        print(json.dumps(command("setup.status"), indent=2))


if __name__ == "__main__":
    main()
