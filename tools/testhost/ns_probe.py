#!/usr/bin/env python3
"""
ns_probe.py — headless NDJSON test driver for NanoSoul TEST mode (docs/13).

The browser test host (index.html) is the interactive rig; this is its
command-line twin: same USB-Serial-JTAG NDJSON protocol, but scriptable so the
whole board-on checklist runs as one command and prints a pass/fail report.

Board must be running TEST mode (IO48→GND strap, or a CONFIG_NANOSOUL_MODE_TEST
build). In TEST mode the USJ is the protocol channel and logs go to UART0, so
this talks clean JSON.

    pip install pyserial
    python ns_probe.py all                 # hwinfo + selftest + sense ranges (no motion)
    python ns_probe.py hwinfo
    python ns_probe.py selftest
    python ns_probe.py sense --sec 6 --hz 20
    python ns_probe.py motor 0 400 800     # spin wheel 0, duty +400, 800ms  (MOVES!)
    python ns_probe.py stop
    python ns_probe.py override current_a 0.6 --ttl 0   # force stall current
    python ns_probe.py override enc_rpm 0 0 0
    python ns_probe.py inject lifted
"""
import argparse
import base64
import glob
import json
import os
import socket
import struct
import sys
import threading
import time
import urllib.parse

I2C_EXPECT = {0x6A: "QMI8658", 0x23: "BH1750", 0x40: "INA219", 0x36: "MAX17048"}


class Chan:
    """Transport-agnostic NDJSON channel: buffers JSON frames, matches by id."""

    def __init__(self):
        self.frames = []
        self.lock = threading.Lock()
        self._id = 0
        self._stop = False

    def _ingest(self, raw):
        """raw = one bytes line (serial) or one text message (ws)."""
        s = raw.strip()
        if not s:
            return
        if s[:1] == b"{":
            try:
                obj = json.loads(s.decode("utf-8", "replace"))
                with self.lock:
                    self.frames.append(obj)
                return
            except json.JSONDecodeError:
                pass
        sys.stderr.write("  log| " + s.decode("utf-8", "replace") + "\n")

    def send(self, **kw):
        self._id += 1
        kw.setdefault("v", 1)
        kw["id"] = self._id
        self._write(json.dumps(kw))
        return self._id

    def wait(self, pred, timeout=3.0):
        deadline = time.time() + timeout
        seen = 0
        while time.time() < deadline:
            with self.lock:
                cur = list(self.frames[seen:])
                seen = len(self.frames)
            for f in cur:
                if pred(f):
                    return f
            time.sleep(0.02)
        return None

    def ack(self, cmd_id, timeout=3.0):
        return self.wait(lambda f: f.get("type") == "ack" and f.get("id") == cmd_id, timeout)

    def mark(self):
        """Frame count now; pass to wait_from so an assertion only sees frames
        that arrive AFTER the stimulus (not a stale earlier state)."""
        with self.lock:
            return len(self.frames)

    def wait_from(self, start, pred, timeout=6.0):
        deadline = time.time() + timeout
        seen = start
        while time.time() < deadline:
            with self.lock:
                cur = list(self.frames[seen:])
                seen = len(self.frames)
            for f in cur:
                if pred(f):
                    return f
            time.sleep(0.02)
        return None

    def _write(self, s):
        raise NotImplementedError

    def close(self):
        self._stop = True


class SerialChan(Chan):
    """USB-Serial-JTAG NDJSON transport (the board's NATIVE USB port, docs/13)."""

    def __init__(self, port, baud=115200):
        super().__init__()
        try:
            import serial  # pyserial — lazy so --help works without it
        except ImportError:
            sys.exit("need pyserial:  pip install pyserial")
        # Open with DTR/RTS deasserted: on USB-Serial-JTAG and CH34x auto-reset
        # boards, asserting them holds the chip in reset so it looks dead.
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = baud
        self.ser.timeout = 0.2
        self.ser.dtr = False
        self.ser.rts = False
        self.ser.open()
        try:
            self.ser.dtr = False
            self.ser.rts = False
        except Exception:
            pass
        self._buf = b""
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        while not self._stop:
            try:
                chunk = self.ser.read(256)
            except Exception:
                break
            if not chunk:
                continue
            self._buf += chunk
            while b"\n" in self._buf:
                line, self._buf = self._buf.split(b"\n", 1)
                self._ingest(line)

    def _write(self, s):
        self.ser.write((s + "\n").encode())

    def close(self):
        self._stop = True
        try:
            self.ser.close()
        except Exception:
            pass


class WsChan(Chan):
    """companion WebSocket transport ws://<ip>/ws?token=… — zero-dependency."""

    def __init__(self, ip, token="", port=80):
        super().__init__()
        self.sock = socket.create_connection((ip, port), timeout=6)
        key = base64.b64encode(os.urandom(16)).decode()
        path = "/ws?token=" + urllib.parse.quote(token, safe="") if token else "/ws"
        req = ("GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\n"
               "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
               "Sec-WebSocket-Version: 13\r\n\r\n" % (path, ip, key))
        self.sock.sendall(req.encode())
        resp = b""
        while b"\r\n\r\n" not in resp:
            d = self.sock.recv(1024)
            if not d:
                raise RuntimeError("ws: closed during handshake")
            resp += d
        if b" 101 " not in resp.split(b"\r\n", 1)[0]:
            raise RuntimeError("ws handshake failed: " + resp[:120].decode("latin1"))
        self._buf = b""
        self.sock.settimeout(0.3)
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        while not self._stop:
            try:
                d = self.sock.recv(4096)
                if not d:
                    break
                self._buf += d
                self._drain()
            except socket.timeout:
                continue
            except OSError:
                break

    def _drain(self):
        while len(self._buf) >= 2:
            opcode = self._buf[0] & 0x0F
            b1 = self._buf[1]
            masked = b1 & 0x80
            ln = b1 & 0x7F
            off = 2
            if ln == 126:
                if len(self._buf) < 4:
                    return
                ln = struct.unpack(">H", self._buf[2:4])[0]; off = 4
            elif ln == 127:
                if len(self._buf) < 10:
                    return
                ln = struct.unpack(">Q", self._buf[2:10])[0]; off = 10
            mask = b""
            if masked:
                if len(self._buf) < off + 4:
                    return
                mask = self._buf[off:off + 4]; off += 4
            if len(self._buf) < off + ln:
                return
            payload = self._buf[off:off + ln]
            self._buf = self._buf[off + ln:]
            if masked:
                payload = bytes(payload[i] ^ mask[i % 4] for i in range(len(payload)))
            if opcode == 0x1:      # text frame
                self._ingest(payload)
            elif opcode == 0x8:    # close
                self._stop = True
                return
            elif opcode == 0x9:    # ping -> pong
                self._frame(0xA, payload)
            # opcode 0x2 (binary jpeg snapshot) ignored

    def _frame(self, opcode, payload):
        n = len(payload)
        mask = os.urandom(4)
        hdr = bytes([0x80 | opcode])
        if n < 126:
            hdr += bytes([0x80 | n])
        elif n < 65536:
            hdr += bytes([0x80 | 126]) + struct.pack(">H", n)
        else:
            hdr += bytes([0x80 | 127]) + struct.pack(">Q", n)
        hdr += mask
        self.sock.sendall(hdr + bytes(payload[i] ^ mask[i % 4] for i in range(n)))

    def _write(self, s):
        self._frame(0x1, s.encode("utf-8"))
        # Pace: the companion WS drops under back-to-back client frames (its 1Hz
        # state broadcast races the sync reply on the same socket). ~0.3s is safe.
        time.sleep(0.3)

    def close(self):
        self._stop = True
        try:
            self.sock.close()
        except Exception:
            pass


def find_port():
    for pat in ("/dev/ttyACM*", "/dev/ttyUSB*", "/dev/tty.usb*", "/dev/cu.usb*"):
        hit = sorted(glob.glob(pat))
        if hit:
            return hit[0]
    return None


def do_hwinfo(lk):
    cid = lk.send(type="hwinfo_get")
    hw = lk.wait(lambda f: f.get("type") == "hwinfo", 4.0)
    lk.ack(cid, 1.0)
    if not hw:
        print("hwinfo: NO RESPONSE — is the board in TEST mode on this port?")
        return False
    fw = hw.get("fw", {})
    print(f"fw {fw.get('ver','?')}  idf {fw.get('idf','?')}  mode {fw.get('mode','?')}")
    i2c1 = hw.get("i2c1", {})
    scan = set(i2c1.get("scan", []))
    print(f"I²C1 {i2c1.get('pins','')}  scan={[hex(a) for a in sorted(scan)]}")
    ok = True
    for addr, name in I2C_EXPECT.items():
        present = addr in scan
        # MAX17048 is expected-but-unused; absence of the rest is a fail
        flag = "OK " if present else ("--  " if name == "MAX17048" else "MISS")
        if not present and name != "MAX17048":
            ok = False
        print(f"  {flag} {hex(addr):>4} {name}")
    rng = hw.get("range", {})
    if rng:
        print(f"range: {json.dumps(rng, ensure_ascii=False)}")
    return ok


def do_selftest(lk):
    lk.send(type="selftest")
    print("selftest running (streaming results)…")
    results, seen, t0 = {}, 0, time.time()
    # collect selftest_item events until 2s of quiet
    last = time.time()
    while time.time() - last < 2.0 and time.time() - t0 < 60:
        with lk.lock:
            cur = list(lk.frames[seen:])
            seen = len(lk.frames)
        for f in cur:
            if f.get("type") == "event" and f.get("name") == "selftest_item":
                d = f.get("data", f)
                results[d.get("name", "?")] = d
                last = time.time()
        time.sleep(0.05)
    verdict = {0: "PASS", 1: "FAIL", 2: "SKIP"}
    npass = nfail = 0
    for name, d in results.items():
        r = d.get("result")
        v = verdict.get(r, str(r))
        if r == 0:
            npass += 1
        elif r == 1:
            nfail += 1
        detail = d.get("detail", "")
        print(f"  {v:<4} {name:<18} {detail}")
    print(f"selftest: {npass} pass / {nfail} fail / {len(results)} total")
    return nfail == 0 and len(results) > 0


def do_sense(lk, sec, hz):
    cid = lk.send(type="sense_rate", hz=hz)
    lk.ack(cid, 1.0)
    print(f"streaming sense @ {hz}Hz for {sec}s…")
    stats, seen, t0 = {}, 0, time.time()
    n = 0
    while time.time() - t0 < sec:
        with lk.lock:
            cur = list(lk.frames[seen:])
            seen = len(lk.frames)
        for f in cur:
            if f.get("type") != "sense":
                continue
            n += 1
            for k in ("lux", "cur_ma"):
                if isinstance(f.get(k), (int, float)):
                    lo, hi = stats.get(k, (f[k], f[k]))
                    stats[k] = (min(lo, f[k]), max(hi, f[k]))
            ac = f.get("accel")
            if isinstance(ac, list) and len(ac) == 3:
                for i, ax in enumerate("xyz"):
                    key = "accel_" + ax
                    lo, hi = stats.get(key, (ac[i], ac[i]))
                    stats[key] = (min(lo, ac[i]), max(hi, ac[i]))
            enc = f.get("enc")
            if isinstance(enc, list):
                for i, e in enumerate(enc):
                    if isinstance(e, list) and len(e) == 2:
                        key = f"enc{i}_rpm"
                        lo, hi = stats.get(key, (e[1], e[1]))
                        stats[key] = (min(lo, e[1]), max(hi, e[1]))
        time.sleep(0.02)
    lk.send(type="sense_rate", hz=0)   # stop the stream
    print(f"  {n} frames")
    for k in sorted(stats):
        lo, hi = stats[k]
        print(f"  {k:<10} min={lo:<10.3g} max={hi:<10.3g}")
    return n > 0


def do_test(lk):
    """Automated non-motion mechanism battery: inject a stimulus over the NDJSON
    channel, assert the resulting soul state / event. One line PASS/FAIL each."""
    passed = []

    def check(name, ok, detail=""):
        passed.append(bool(ok))
        print(f"  [{'PASS' if ok else 'FAIL'}] {name:<26} {detail}")

    def wait_soul(mark, want, timeout=8):
        want = {want} if isinstance(want, str) else set(want)
        f = lk.wait_from(mark, lambda f: f.get("type") == "state" and f.get("soul") in want, timeout)
        return f.get("soul") if f else None

    def cur_soul(timeout=3):
        f = lk.wait(lambda f: f.get("type") == "state" and "soul" in f, timeout)
        return f.get("soul") if f else None

    lk.send(type="hwinfo_get")
    hw = lk.wait(lambda f: f.get("type") == "hwinfo", 5)
    if not hw:
        print("  hwinfo NO RESPONSE — board not in TEST mode on this port?")
        return False
    scan = set(hw.get("i2c1", {}).get("scan", []))
    check("i2c QMI8658/IMU", 0x6A in scan or 0x6B in scan, "0x6a/0x6b")  # board uses 0x6B
    check("i2c BH1750/light", 0x23 in scan, "0x23")
    check("i2c INA219/current", 0x40 in scan, "0x40")
    print(f"  (soul baseline: {cur_soul()})")

    # inject dark/bright directly (the override-lux path needs the 30s dark_hold).
    m = lk.mark(); lk.ack(lk.send(type="inject_event", name="dark"))
    s = wait_soul(m, "DOZE", 8); check("inject dark -> DOZE", s == "DOZE", f"got {s}")
    m = lk.mark(); lk.ack(lk.send(type="inject_event", name="bright"))
    s = wait_soul(m, {"IDLE", "ENGAGE", "APPROACH", "GAZED"}, 8)
    check("inject bright -> wake", s not in (None, "DOZE"), f"got {s}")

    m = lk.mark(); lk.ack(lk.send(type="inject_event", name="lifted"))
    s = wait_soul(m, "LIFTED", 6); check("inject lifted -> LIFTED", s == "LIFTED", f"got {s}")
    m = lk.mark(); lk.ack(lk.send(type="inject_event", name="placed"))
    s = wait_soul(m, {"IDLE", "ENGAGE", "APPROACH", "DOZE"}, 8)
    check("inject placed -> release", s not in (None, "LIFTED"), f"got {s}")

    # face override drives soul only if vision runs; camera's disabled here, so
    # this is best-effort (report, don't hard-fail).
    m = lk.mark()
    lk.ack(lk.send(type="override_set", ch="face", v=[1.0, 0.0, 0.0, 0.12, 0.85], ttl_ms=8000))
    s = wait_soul(m, {"ENGAGE", "APPROACH", "GAZED", "RETREAT"}, 8)
    print(f"  [{'PASS' if s else 'n/a '}] face present -> engage   got {s} (vision off w/o camera)")
    lk.ack(lk.send(type="override_clear", ch="face"))

    # estop deterministically drives soul FAULT (inject 'stall' is a motor event,
    # not a soul fault trigger).
    m = lk.mark(); lk.ack(lk.send(type="estop"))
    s = wait_soul(m, "FAULT", 6); check("estop -> FAULT", s == "FAULT", f"got {s}")
    m = lk.mark(); lk.ack(lk.send(type="clear_fault"))
    s = wait_soul(m, {"IDLE", "ENGAGE", "DOZE"}, 8)
    check("clear_fault -> recover", s not in (None, "FAULT"), f"got {s}")

    for nm in ("tap", "loud", "touch"):
        a = lk.ack(lk.send(type="inject_event", name=nm))
        check(f"inject {nm}", bool(a and a.get("ok")))

    print("  (asking the cloud LLM — Ark chat E2E, up to ~25s…)")
    lk.send(type="ask", text="用一句话介绍你自己")
    ev = lk.wait(lambda f: f.get("type") == "event" and f.get("name") == "llm_reply", 25)
    reply = (ev.get("data") or {}).get("text") if ev else None
    check("ask -> llm_reply (Ark chat)", bool(reply),
          repr(reply)[:70] if reply else "no reply — check chat key / 2.4G wifi")

    n, ok = len(passed), sum(passed)
    print(f"\n== mechanism battery: {ok}/{n} PASS ==")
    return ok == n


def do_monitor(lk, sec):
    """Live state+sense readout while YOU do physical stimuli by hand."""
    print("物理清单（边看数值边做）：遮光/照光 · 敲一下 · 抬起再放下 · 倾斜 · 摸屏 · "
          "手拨三个轮子 · 拍手")
    lk.ack(lk.send(type="sense_rate", hz=15))
    t0 = time.time()
    seen = lk.mark()
    last_se = None
    while time.time() - t0 < sec:
        with lk.lock:
            cur = list(lk.frames[seen:])
            seen = len(lk.frames)
        for f in cur:
            t = f.get("type")
            if t == "sense":
                a = f.get("accel") or [0, 0, 0]
                enc = [e[0] for e in (f.get("enc") or []) if isinstance(e, list)]
                lux = f.get("lux") or 0
                sys.stdout.write(f"\r  lux={lux:>7.1f} cur={f.get('cur_ma'):>5}mA "
                                 f"acc=[{a[0]:+.1f},{a[1]:+.1f},{a[2]:+.1f}] enc={enc} "
                                 f"lift={f.get('lifted')} tilt={f.get('tilted')}    ")
                sys.stdout.flush()
            elif t == "event":
                print(f"\n  event: {f.get('name')} {f.get('data', '')}")
            elif t == "state":
                se = (f.get("soul"), f.get("emotion"))
                if se != last_se:
                    print(f"\n  soul={se[0]} emotion={se[1]}")
                    last_se = se
        time.sleep(0.05)
    lk.send(type="sense_rate", hz=0)
    print("\n  monitor done.")


def main():
    # --port on a shared parent so it works before OR after the subcommand.
    base = argparse.ArgumentParser(add_help=False)
    # SUPPRESS so a --port before the subcommand isn't clobbered by the subparser default.
    base.add_argument("--port", default=argparse.SUPPRESS, help="serial port (auto if omitted)")
    base.add_argument("--ws", default=argparse.SUPPRESS,
                      help="board IP for the companion WebSocket transport (native USB not needed)")
    base.add_argument("--token", default=argparse.SUPPRESS, help="companion.token for --ws")
    ap = argparse.ArgumentParser(description="NanoSoul TEST-mode serial probe", parents=[base])
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("all", parents=[base])
    sub.add_parser("test", parents=[base])
    p = sub.add_parser("monitor", parents=[base]); p.add_argument("--sec", type=float, default=45)
    sub.add_parser("hwinfo", parents=[base])
    sub.add_parser("selftest", parents=[base])
    p = sub.add_parser("sense", parents=[base]); p.add_argument("--sec", type=float, default=6); p.add_argument("--hz", type=int, default=20)
    p = sub.add_parser("motor", parents=[base]); p.add_argument("m", type=int); p.add_argument("duty", type=int); p.add_argument("ms", type=int)
    sub.add_parser("stop", parents=[base])
    p = sub.add_parser("override", parents=[base]); p.add_argument("ch"); p.add_argument("vals", nargs="+", type=float); p.add_argument("--ttl", type=int, default=0)
    p = sub.add_parser("clear", parents=[base]); p.add_argument("ch", nargs="?", default="*")
    p = sub.add_parser("inject", parents=[base]); p.add_argument("name"); p.add_argument("text", nargs="?", default=None)
    args = ap.parse_args()

    ws_ip = getattr(args, "ws", None)
    if ws_ip:
        token = getattr(args, "token", "")
        print(f"# ws ws://{ws_ip}/ws")
        lk = WsChan(ws_ip, token)
    else:
        port = getattr(args, "port", None) or find_port()
        if not port:
            sys.exit("no serial port found (pass --port or --ws <ip>). "
                     "note: the NDJSON link is on the board's NATIVE USB port")
        print(f"# port {port}")
        lk = SerialChan(port)
    time.sleep(0.3)
    try:
        if args.cmd == "all":
            a = do_hwinfo(lk); print()
            b = do_selftest(lk); print()
            c = do_sense(lk, 6, 20)
            print(f"\n== hwinfo {'OK' if a else 'FAIL'} · selftest {'OK' if b else 'FAIL'} · sense {'OK' if c else 'FAIL'} ==")
        elif args.cmd == "test":
            do_test(lk)
        elif args.cmd == "monitor":
            do_monitor(lk, args.sec)
        elif args.cmd == "hwinfo":
            do_hwinfo(lk)
        elif args.cmd == "selftest":
            do_selftest(lk)
        elif args.cmd == "sense":
            do_sense(lk, args.sec, args.hz)
        elif args.cmd == "motor":
            cid = lk.send(type="motor_test", m=args.m, duty=args.duty, ms=args.ms)
            print("ack:", lk.ack(cid))
        elif args.cmd == "stop":
            cid = lk.send(type="motor_stop"); print("ack:", lk.ack(cid))
        elif args.cmd == "override":
            v = args.vals if len(args.vals) > 1 else args.vals[0]
            cid = lk.send(type="override_set", ch=args.ch, v=v, ttl_ms=args.ttl)
            print("ack:", lk.ack(cid))
        elif args.cmd == "clear":
            cid = lk.send(type="override_clear", ch=args.ch); print("ack:", lk.ack(cid))
        elif args.cmd == "inject":
            kw = {"type": "inject_event", "name": args.name}
            if args.text:
                kw["text"] = args.text
            cid = lk.send(**kw); print("ack:", lk.ack(cid))
    finally:
        lk.close()


if __name__ == "__main__":
    main()
