#!/usr/bin/env python3
"""
run_lab.py - TinyUSB QEMU Lab launcher.

Launches both QEMU instances (device and host roles) plus the vusbd broker,
with GDB stubs on separate ports for simultaneous debugging.

Usage:
  python3 tools/qemu/run_lab.py --device-elf <path> --host-elf <path>
  python3 tools/qemu/run_lab.py --device-elf build/cdc_msc.elf \\
                                --host-elf build/cdc_msc_hid.elf \\
                                --pcap /tmp/vusb.pcap

Environment:
  QEMU_ARM    Path to qemu-system-arm (default: qemu-system-arm)
"""

import argparse
import os
import signal
import subprocess
import sys
import time

QEMU = os.environ.get("QEMU_ARM", "qemu-system-arm")

DEVICE_SERIAL_SOCK = "/tmp/vusb-dev.sock"
HOST_QMP_SOCK      = "/tmp/tinyusb-host.qmp"
CTRL_SOCK          = "/tmp/vusb-ctrl.sock"

GDB_PORT_DEVICE = 1234
GDB_PORT_HOST   = 1235


def cleanup_sockets():
    for p in [DEVICE_SERIAL_SOCK, HOST_QMP_SOCK, CTRL_SOCK]:
        if os.path.exists(p):
            os.unlink(p)


def launch_device_qemu(elf_path, extra_args=None):
    """Launch QEMU A: TinyUSB device role, PL011 on Unix socket."""
    cmd = [
        QEMU, "-M", "raspi0", "-kernel", elf_path,
        "-serial", f"unix:{DEVICE_SERIAL_SOCK},server=on,wait=off",
        "-serial", "mon:stdio",
        "-gdb", f"tcp::{GDB_PORT_DEVICE}",
        "-S",  # start paused, waiting for GDB
        "-nographic",
        "-d", "guest_errors",
    ]
    if extra_args:
        cmd.extend(extra_args)
    print(f"[DEVICE] {' '.join(cmd)}")
    return subprocess.Popen(cmd, stdin=subprocess.DEVNULL)


def launch_host_qemu(elf_path, extra_args=None):
    """Launch QEMU B: TinyUSB host role, DWC2 with emulated USB devices."""
    cmd = [
        QEMU, "-M", "raspi0", "-kernel", elf_path,
        "-serial", "null",
        "-serial", "mon:stdio",
        "-qmp", f"unix:{HOST_QMP_SOCK},server=on,wait=off",
        "-device", "usb-kbd",
        "-gdb", f"tcp::{GDB_PORT_HOST}",
        "-S",  # start paused, waiting for GDB
        "-nographic",
        "-d", "guest_errors",
    ]
    if extra_args:
        cmd.extend(extra_args)
    print(f"[HOST]   {' '.join(cmd)}")
    return subprocess.Popen(cmd, stdin=subprocess.DEVNULL)


def launch_broker(pcap_path=None, verbose=False):
    """Launch the vusbd broker."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    cmd = [
        sys.executable, os.path.join(script_dir, "vusbd.py"),
        "--device-sock", DEVICE_SERIAL_SOCK,
        "--ctrl-sock", CTRL_SOCK,
        "--backend", "loopback",
    ]
    if pcap_path:
        cmd.extend(["--pcap", pcap_path])
    if verbose:
        cmd.append("-v")
    print(f"[BROKER] {' '.join(cmd)}")
    return subprocess.Popen(cmd)


def main():
    parser = argparse.ArgumentParser(description="TinyUSB QEMU Lab launcher")
    parser.add_argument("--device-elf", required=True,
        help="Path to device-role firmware ELF")
    parser.add_argument("--host-elf",
        help="Path to host-role firmware ELF (optional, launches host QEMU)")
    parser.add_argument("--pcap",
        help="Write PCAP capture to this file")
    parser.add_argument("--no-broker", action="store_true",
        help="Skip launching the broker")
    parser.add_argument("--no-gdb-wait", action="store_true",
        help="Don't pause QEMUs waiting for GDB (-S removed)")
    parser.add_argument("-v", "--verbose", action="store_true")
    parser.add_argument("--trace",
        help="QEMU trace events pattern (e.g. 'dwc2_*,usb_*')")

    args = parser.parse_args()
    procs = []

    cleanup_sockets()

    try:
        # Launch broker first so the socket exists for QEMU
        if not args.no_broker:
            broker = launch_broker(args.pcap, args.verbose)
            procs.append(("broker", broker))
            time.sleep(0.3)

        # Device QEMU
        dev_extra = []
        if args.no_gdb_wait:
            dev_extra = []  # -S already in base cmd, we'd need to remove it
        if args.trace:
            dev_extra.extend(["-trace", f"enable={args.trace}"])

        dev_qemu = launch_device_qemu(args.device_elf, dev_extra)
        procs.append(("device-qemu", dev_qemu))

        # Host QEMU (optional)
        if args.host_elf:
            host_extra = []
            if args.trace:
                host_extra.extend(["-trace", f"enable={args.trace}"])
            host_qemu = launch_host_qemu(args.host_elf, host_extra)
            procs.append(("host-qemu", host_qemu))

        print()
        print("=" * 60)
        print("TinyUSB QEMU Lab is running!")
        print(f"  Device GDB: gdb-multiarch -ex 'target remote :{GDB_PORT_DEVICE}'")
        if args.host_elf:
            print(f"  Host GDB:   gdb-multiarch -ex 'target remote :{GDB_PORT_HOST}'")
        print(f"  Control:    python3 tools/qemu/vusbctl.py attach")
        if args.pcap:
            print(f"  PCAP:       wireshark {args.pcap}")
        print("  Press Ctrl+C to stop all processes")
        print("=" * 60)
        print()

        # Wait for any process to exit
        while True:
            for name, proc in procs:
                ret = proc.poll()
                if ret is not None:
                    print(f"[{name}] exited with code {ret}")
            time.sleep(1)

    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        for name, proc in procs:
            if proc.poll() is None:
                print(f"  Terminating {name}...")
                proc.terminate()
        for name, proc in procs:
            proc.wait(timeout=5)
        cleanup_sockets()
        print("Lab stopped.")


if __name__ == "__main__":
    main()
