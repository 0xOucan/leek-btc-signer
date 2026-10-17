"""
Drive the s3cam-qemu firmware through the UART harness (docs/TESTING.md).

    with Device() as dev:
        dev.wait_screen("MainMenuView")
        dev.key("DOWN"); dev.key("OK")
        dev.wait_screen("LoadSeedView")
        dev.save_frame("load.ppm")
        dev.qr(b"...")              # a scanned frame, on a Scan screen

The device side is src/hal/qemu-harness.c.
"""
import base64
import os
import queue
import subprocess
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, ".pio", "build", "s3cam-qemu")
IMAGE = os.path.join(BUILD, "qemu-flash.bin")

# Must match src/core/palette.c, for frames rendered on the host side.
PALETTE = [
    0x000000, 0xFCFCFC, 0x777777, 0xFF9F0A, 0xFF9F0A, 0x000000, 0x2C2C2C, 0x414141,
    0x30D158, 0xFFD60A, 0xFF5700, 0xFF1B0A, 0x409CFF, 0x00F100, 0x00CAF1, 0xFFFFFF,
]
DARK_ROLES = {0, 5, 6}   # gfx_role_lit() is false for these


class Device:
    def __init__(self, timeout=60):
        self.timeout = timeout
        self.lines = queue.Queue()
        self.log = []
        self.screen = None
        self.frame = None   # (w, h, bytes)
        self.acks = 0
        self.notes = []

    def __enter__(self):
        self.proc = subprocess.Popen(
            ["qemu-system-xtensa", "-machine", "esp32s3", "-display", "none",
             "-serial", "stdio", "-monitor", "none",
             "-drive", "file=%s,if=mtd,format=raw" % IMAGE],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        threading.Thread(target=self._read, daemon=True).start()
        self.wait_line(lambda l: l == "LBS-READY")
        self.wait_line(lambda l: l == "A")     # the first frame is out
        return self

    def __exit__(self, *exc):
        self.proc.kill()
        self.proc.wait()

    def _read(self):
        for raw in self.proc.stdout:
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            self.lines.put(line)

    def _handle(self, line):
        self.log.append(line)
        if line == "A":
            self.acks += 1
        elif line.startswith("S "):
            self.screen = line[2:]
        elif line.startswith("F "):
            _, w, h, data = line.split(" ", 3)
            self.frame = (int(w), int(h), base64.b64decode(data))
        elif line.startswith("N "):
            self.notes.append(line[2:])
        elif line.startswith("E "):
            raise AssertionError("harness error: " + line)

    def wait_line(self, pred, timeout=None):
        end = time.time() + (timeout or self.timeout)
        while time.time() < end:
            try:
                line = self.lines.get(timeout=0.1)
            except queue.Empty:
                if self.proc.poll() is not None:
                    raise AssertionError("QEMU exited:\n" + "\n".join(self.log[-20:]))
                continue
            self._handle(line)
            if pred(line):
                return line
        raise AssertionError("timed out; last lines:\n" + "\n".join(self.log[-20:]))

    def pump(self, seconds=0.3):
        """Take in whatever the device prints for a moment."""
        end = time.time() + seconds
        while time.time() < end:
            try:
                self._handle(self.lines.get(timeout=0.05))
            except queue.Empty:
                pass

    def key(self, name):
        """Press a key and wait until the device has handled it and sent
        the frame that resulted."""
        want = self.acks + 1
        self.proc.stdin.write(("K %s\n" % name).encode())
        self.proc.stdin.flush()
        self.wait_line(lambda l: self.acks >= want)

    def qr(self, data):
        """Hand the device one decoded QR frame, as the camera would, and
        wait until the scan screen has taken it and redrawn. Returns the
        notes it printed meanwhile."""
        if isinstance(data, str):
            data = data.encode("utf-8")
        start = len(self.notes)
        want = self.acks + 1
        self.proc.stdin.write(b"Q " + data.hex().encode() + b"\n")
        self.proc.stdin.flush()
        self.wait_line(lambda l: self.acks >= want and
                       any(n.startswith("frame ") for n in self.notes[start:]))
        return self.notes[start:]

    def wait_screen(self, name, timeout=10):
        if self.screen == name:
            return
        self.wait_line(lambda l: l == "S " + name, timeout)

    def pixel(self, x, y):
        w, h, fb = self.frame
        b = fb[(y * w + x) // 2]
        return (b >> 4) if x & 1 else (b & 0x0F)

    def save_frame(self, path, scale=4):
        w, h, _ = self.frame
        mono = w == 128 and h in (64, 128)
        with open(path, "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (w * scale, h * scale))
            for y in range(h * scale):
                row = bytearray()
                for x in range(w * scale):
                    c = self.pixel(x // scale, y // scale)
                    rgb = (0 if c in DARK_ROLES else 0xFFFFFF) if mono else PALETTE[c]
                    row += bytes(((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255))
                f.write(row)
