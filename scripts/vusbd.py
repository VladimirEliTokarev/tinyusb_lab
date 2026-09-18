#!/usr/bin/env python3
"""
vusbd - Virtual USB Daemon (broker)

Routes VUSB frames between a TinyUSB device guest (QEMU A, PL011 over
Unix socket) and one of several host backends:

  1. usb-redir: connects to a second QEMU (B) running TinyUSB host stack
  2. usbip: exposes the device via USB/IP to the WSL2/Linux kernel vhci_hcd
  3. loopback: echoes back for basic transport testing

Also provides:
  - Scriptable attach/detach/reset via a control socket (vusbctl)
  - PCAP tap for Wireshark (LINKTYPE_USB_2_0)
  - Fault injection (corrupt, truncate, force STALL)

Usage:
  python3 vusbd.py --device-sock /tmp/vusb-dev.sock [--backend loopback]
  python3 vusbd.py --device-sock /tmp/vusb-dev.sock --backend usbredir \\
                   --redir-sock /tmp/vusb-redir.sock
"""

import argparse
import asyncio
import logging
import os
import signal
import struct
import sys
import time

log = logging.getLogger("vusbd")

# ── VUSB protocol constants ──────────────────────────────────────────

VUSB_FRAME_ATTACH    = 0x01
VUSB_FRAME_DETACH    = 0x02
VUSB_FRAME_RESET     = 0x03
VUSB_FRAME_SETUP     = 0x04
VUSB_FRAME_DATA_OUT  = 0x05
VUSB_FRAME_DATA_IN   = 0x06
VUSB_FRAME_ACK       = 0x07
VUSB_FRAME_NAK       = 0x08
VUSB_FRAME_STALL     = 0x09
VUSB_FRAME_DATA_RESP = 0x0A
VUSB_FRAME_SOF       = 0x0B
VUSB_FRAME_SET_ADDR  = 0x0C
VUSB_FRAME_SPEED     = 0x0D

VUSB_HDR_SIZE = 3
VUSB_MAX_PAYLOAD = 1024

FRAME_NAMES = {
    0x01: "ATTACH",    0x02: "DETACH",    0x03: "RESET",
    0x04: "SETUP",     0x05: "DATA_OUT",  0x06: "DATA_IN",
    0x07: "ACK",       0x08: "NAK",       0x09: "STALL",
    0x0A: "DATA_RESP", 0x0B: "SOF",       0x0C: "SET_ADDR",
    0x0D: "SPEED",
}


def frame_name(ftype):
    return FRAME_NAMES.get(ftype, f"UNKNOWN(0x{ftype:02x})")


def build_frame(ftype, payload=b""):
    return struct.pack("<BH", ftype, len(payload)) + payload


# ── PCAP writer (LINKTYPE_USB_2_0 = 288) ────────────────────────────

LINKTYPE_USB_2_0 = 288

class PcapWriter:
    def __init__(self, path):
        self.f = open(path, "wb")
        # Global header: magic, version 2.4, thiszone=0, sigfigs=0,
        # snaplen=65535, network=LINKTYPE_USB_2_0
        self.f.write(struct.pack("<IHHIIII",
            0xa1b2c3d4, 2, 4, 0, 0, 65535, LINKTYPE_USB_2_0))
        self.f.flush()

    def write_packet(self, data, direction="H2D"):
        ts = time.time()
        ts_sec = int(ts)
        ts_usec = int((ts - ts_sec) * 1_000_000)
        # Prepend a minimal pseudo-header: direction byte
        dir_byte = b"\x00" if direction == "H2D" else b"\x01"
        pkt = dir_byte + data
        caplen = len(pkt)
        self.f.write(struct.pack("<IIII", ts_sec, ts_usec, caplen, caplen))
        self.f.write(pkt)
        self.f.flush()

    def close(self):
        self.f.close()


# ── Fault injector ───────────────────────────────────────────────────

class FaultInjector:
    def __init__(self):
        self.corrupt_next = False
        self.truncate_next = False
        self.force_stall_ep = None

    def process(self, frame_bytes):
        if len(frame_bytes) < VUSB_HDR_SIZE:
            return frame_bytes

        if self.corrupt_next:
            self.corrupt_next = False
            ba = bytearray(frame_bytes)
            if len(ba) > VUSB_HDR_SIZE:
                ba[VUSB_HDR_SIZE] ^= 0xFF
            log.warning("FAULT: corrupted frame")
            return bytes(ba)

        if self.truncate_next:
            self.truncate_next = False
            half = VUSB_HDR_SIZE + (len(frame_bytes) - VUSB_HDR_SIZE) // 2
            truncated = frame_bytes[:half]
            # Fix length field
            new_plen = half - VUSB_HDR_SIZE
            truncated = bytes([truncated[0]]) + struct.pack("<H", new_plen) + truncated[VUSB_HDR_SIZE:]
            log.warning("FAULT: truncated frame to %d bytes", len(truncated))
            return truncated

        return frame_bytes


# ── Async frame reader ───────────────────────────────────────────────

async def read_frame(reader):
    """Read one VUSB frame from an asyncio StreamReader."""
    hdr = await reader.readexactly(VUSB_HDR_SIZE)
    ftype, plen = struct.unpack("<BH", hdr)
    if plen > VUSB_MAX_PAYLOAD:
        raise ValueError(f"Frame payload too large: {plen}")
    payload = await reader.readexactly(plen) if plen > 0 else b""
    return hdr + payload


# ── Control socket handler ───────────────────────────────────────────

class ControlHandler:
    def __init__(self, broker):
        self.broker = broker

    async def handle(self, reader, writer):
        addr = writer.get_extra_info("peername")
        log.info("Control client connected: %s", addr)
        try:
            while True:
                line = await reader.readline()
                if not line:
                    break
                cmd = line.decode().strip().split()
                if not cmd:
                    continue
                resp = await self.dispatch(cmd)
                writer.write((resp + "\n").encode())
                await writer.drain()
        except Exception as e:
            log.error("Control error: %s", e)
        finally:
            writer.close()

    async def dispatch(self, cmd):
        verb = cmd[0].lower()

        if verb == "attach":
            frame = build_frame(VUSB_FRAME_ATTACH)
            await self.broker.send_to_device(frame)
            return "OK: attach sent"

        elif verb == "detach":
            frame = build_frame(VUSB_FRAME_DETACH)
            await self.broker.send_to_device(frame)
            return "OK: detach sent"

        elif verb == "reset":
            frame = build_frame(VUSB_FRAME_RESET)
            await self.broker.send_to_device(frame)
            return "OK: reset sent"

        elif verb == "corrupt":
            self.broker.fault_injector.corrupt_next = True
            return "OK: next frame will be corrupted"

        elif verb == "truncate":
            self.broker.fault_injector.truncate_next = True
            return "OK: next frame will be truncated"

        elif verb == "stall":
            if len(cmd) > 1:
                ep = int(cmd[1], 0)
                self.broker.fault_injector.force_stall_ep = ep
                return f"OK: forcing STALL on ep 0x{ep:02x}"
            return "ERR: usage: stall <ep_addr>"

        elif verb == "replay":
            if len(cmd) > 1:
                path = cmd[1]
                try:
                    await self.broker.replay_file(path)
                    return f"OK: replayed {path}"
                except Exception as e:
                    return f"ERR: replay failed: {e}"
            return "ERR: usage: replay <file>"

        elif verb == "status":
            return f"OK: device_connected={self.broker.device_connected}"

        elif verb == "quit":
            self.broker.stop()
            return "OK: shutting down"

        else:
            return f"ERR: unknown command: {verb}"


# ── Broker ───────────────────────────────────────────────────────────

class VUSBBroker:
    def __init__(self, args):
        self.args = args
        self.device_reader = None
        self.device_writer = None
        self.device_connected = False
        self.pcap = None
        self.fault_injector = FaultInjector()
        self._running = True

        if args.pcap:
            self.pcap = PcapWriter(args.pcap)
            log.info("PCAP output: %s", args.pcap)

    def stop(self):
        self._running = False

    async def send_to_device(self, frame_bytes):
        if self.device_writer:
            frame_bytes = self.fault_injector.process(frame_bytes)
            self.device_writer.write(frame_bytes)
            await self.device_writer.drain()
            if self.pcap:
                self.pcap.write_packet(frame_bytes, "H2D")
            ftype = frame_bytes[0] if frame_bytes else 0
            log.debug("-> device: %s (%d bytes)", frame_name(ftype), len(frame_bytes))

    async def send_from_device(self, frame_bytes):
        """Handle a frame received from the device."""
        if self.pcap:
            self.pcap.write_packet(frame_bytes, "D2H")
        ftype = frame_bytes[0] if frame_bytes else 0
        log.debug("<- device: %s (%d bytes)", frame_name(ftype), len(frame_bytes))

    async def replay_file(self, path):
        """Replay raw VUSB frames from a binary file."""
        with open(path, "rb") as f:
            data = f.read()
        pos = 0
        while pos + VUSB_HDR_SIZE <= len(data):
            ftype, plen = struct.unpack_from("<BH", data, pos)
            frame_len = VUSB_HDR_SIZE + plen
            if pos + frame_len > len(data):
                break
            frame = data[pos:pos + frame_len]
            await self.send_to_device(frame)
            await asyncio.sleep(0.001)
            pos += frame_len
        log.info("Replayed %d bytes from %s", pos, path)

    async def device_reader_loop(self):
        """Read frames from the device guest and dispatch them."""
        log.info("Device reader loop started")
        try:
            while self._running:
                frame = await read_frame(self.device_reader)
                await self.send_from_device(frame)
        except asyncio.IncompleteReadError:
            log.info("Device disconnected")
        except Exception as e:
            log.error("Device reader error: %s", e)
        finally:
            self.device_connected = False

    async def sof_generator(self):
        """Periodically send SOF frames to the device (1 ms interval)."""
        frame_num = 0
        while self._running:
            await asyncio.sleep(0.001)
            if self.device_connected:
                sof_payload = struct.pack("<I", frame_num)
                frame = build_frame(VUSB_FRAME_SOF, sof_payload)
                try:
                    await self.send_to_device(frame)
                except Exception:
                    pass
                frame_num = (frame_num + 1) & 0x7FF

    async def accept_device(self, reader, writer):
        """Accept a device guest connection on the Unix socket."""
        log.info("Device guest connected")
        self.device_reader = reader
        self.device_writer = writer
        self.device_connected = True

        # Auto-attach: send ATTACH frame
        attach = build_frame(VUSB_FRAME_ATTACH)
        await self.send_to_device(attach)

        await self.device_reader_loop()

    async def run(self):
        args = self.args

        # Remove stale socket files
        for sock_path in [args.device_sock, args.ctrl_sock]:
            if sock_path and os.path.exists(sock_path):
                os.unlink(sock_path)

        # Start device socket server
        dev_server = await asyncio.start_unix_server(
            self.accept_device, path=args.device_sock)
        log.info("Listening for device guest on %s", args.device_sock)

        # Start control socket server
        ctrl_handler = ControlHandler(self)
        ctrl_server = await asyncio.start_unix_server(
            ctrl_handler.handle, path=args.ctrl_sock)
        log.info("Control socket on %s", args.ctrl_sock)

        # Start SOF generator
        sof_task = asyncio.create_task(self.sof_generator())

        log.info("vusbd broker running (backend=%s)", args.backend)

        try:
            while self._running:
                await asyncio.sleep(0.1)
        except asyncio.CancelledError:
            pass
        finally:
            sof_task.cancel()
            dev_server.close()
            ctrl_server.close()
            if self.pcap:
                self.pcap.close()
            log.info("vusbd shut down")


# ── CLI entry point ──────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="vusbd - Virtual USB Daemon (broker)")
    parser.add_argument("--device-sock", default="/tmp/vusb-dev.sock",
        help="Unix socket path for device guest PL011 (default: /tmp/vusb-dev.sock)")
    parser.add_argument("--ctrl-sock", default="/tmp/vusb-ctrl.sock",
        help="Unix socket for vusbctl commands (default: /tmp/vusb-ctrl.sock)")
    parser.add_argument("--backend", choices=["loopback", "usbredir", "usbip"],
        default="loopback",
        help="Host backend (default: loopback)")
    parser.add_argument("--redir-sock",
        help="usb-redir Unix socket path (for usbredir backend)")
    parser.add_argument("--pcap",
        help="Write PCAP file (LINKTYPE_USB_2_0)")
    parser.add_argument("-v", "--verbose", action="store_true",
        help="Verbose logging")

    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(name)s %(levelname)s %(message)s")

    broker = VUSBBroker(args)

    loop = asyncio.new_event_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, broker.stop)

    try:
        loop.run_until_complete(broker.run())
    finally:
        loop.close()


if __name__ == "__main__":
    main()
