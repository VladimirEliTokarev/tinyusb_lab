#!/usr/bin/env python3
"""
vusbctl - Control client for the vusbd broker.

Connects to the broker's control Unix socket and sends commands.

Usage:
  vusbctl attach          - Connect the virtual USB device
  vusbctl detach          - Disconnect the virtual USB device
  vusbctl reset           - Bus reset the device
  vusbctl corrupt         - Corrupt the next frame (fault injection)
  vusbctl truncate        - Truncate the next frame (fault injection)
  vusbctl stall <ep>      - Force STALL on endpoint
  vusbctl replay <file>   - Replay raw VUSB frames from binary file
  vusbctl status          - Query broker status
  vusbctl quit            - Shut down the broker
"""

import socket
import sys

DEFAULT_CTRL_SOCK = "/tmp/vusb-ctrl.sock"


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    ctrl_sock = DEFAULT_CTRL_SOCK
    args = sys.argv[1:]

    # Allow --sock <path> before the command
    if args[0] == "--sock" and len(args) >= 3:
        ctrl_sock = args[1]
        args = args[2:]

    cmd_line = " ".join(args)

    try:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(ctrl_sock)
        sock.sendall((cmd_line + "\n").encode())

        # Read response
        resp = b""
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            resp += chunk
            if b"\n" in resp:
                break

        print(resp.decode().strip())
        sock.close()
    except FileNotFoundError:
        print(f"ERROR: Control socket not found: {ctrl_sock}", file=sys.stderr)
        print("Is vusbd running?", file=sys.stderr)
        sys.exit(1)
    except ConnectionRefusedError:
        print(f"ERROR: Connection refused on {ctrl_sock}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
