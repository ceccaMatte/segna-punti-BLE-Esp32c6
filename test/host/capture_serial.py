"""Bounded serial capture that always releases the port."""
import argparse
import time
import serial

parser = argparse.ArgumentParser()
parser.add_argument("port")
parser.add_argument("--seconds", type=float, default=15)
parser.add_argument("--reset", action="store_true")
args = parser.parse_args()
with serial.Serial(args.port, 115200, timeout=0.25) as port:
    port.dtr = False
    port.rts = False
    if args.reset:
        from esptool.reset import HardReset
        HardReset(port, uses_usb=True)()
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        line = port.readline()
        if line:
            print(line.decode("utf-8", errors="replace").rstrip(), flush=True)
