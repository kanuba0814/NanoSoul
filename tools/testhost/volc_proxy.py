#!/usr/bin/env python3
"""
volc_proxy.py — 火山引擎 (Volcengine) adapter for the NanoSoul voice chain.

The firmware speaks OpenAI-style batch HTTP (llm.c):
    POST /v1/chat/completions       JSON, Bearer          -> choices[0].message.content
    POST /v1/audio/transcriptions   multipart WAV, Bearer -> {"text": ...}
    POST /v1/audio/speech           JSON, Bearer          -> WAV bytes

Volcengine's real endpoints don't match (Ark chat lives at /api/v3, speech uses
appid/token headers + base64 JSON), so this proxy runs on the PC and translates:

    chat  -> Ark  POST https://ark.cn-beijing.volces.com/api/v3/chat/completions
    stt   -> 大模型录音识别·极速版  POST .../api/v3/auc/bigmodel/recognize/flash
    tts   -> 语音合成 v1 HTTP      POST .../api/v1/tts  (pcm out, wrapped to WAV here)

Zero pip deps (stdlib only). Credentials come from env, never the repo:

    export VOLC_ARK_API_KEY=...      # 方舟控制台 API Key（chat）
    export VOLC_APP_ID=...           # 语音技术控制台 appid（TTS+ASR 共用）
    export VOLC_ACCESS_TOKEN=...     # 语音技术控制台 access token
    export VOLC_TTS_VOICE=zh_female_cancan_mars_bigtts   # 可选：覆盖固件 voice 字段
                                     # （固件 voice[16] 装不下大模型音色全名）

Usage:
    python3 volc_proxy.py                 # serve on 0.0.0.0:8600
    python3 volc_proxy.py --check 你好     # PC-local TTS->ASR round-trip (validates creds)
    python3 volc_proxy.py --print-config  # emit the SD config.json chat/stt/tts block

Then on the SD card, point chat/stt/tts base_url at http://<this-PC-LAN-IP>:8600 .
LAN plaintext HTTP — same trust model as the companion WS token (docs/09).
"""
import argparse
import base64
import json
import os
import socket
import struct
import sys
import threading
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib import request as urlreq
from urllib.error import HTTPError, URLError

ARK_URL = os.environ.get("VOLC_ARK_URL",
                         "https://ark.cn-beijing.volces.com/api/v3/chat/completions")
TTS_URL = os.environ.get("VOLC_TTS_URL", "https://openspeech.bytedance.com/api/v1/tts")
ASR_URL = os.environ.get("VOLC_ASR_URL",
                         "https://openspeech.bytedance.com/api/v3/auc/bigmodel/recognize/flash")

ARK_KEY = os.environ.get("VOLC_ARK_API_KEY", "")
APP_ID = os.environ.get("VOLC_APP_ID", "")
TOKEN = os.environ.get("VOLC_ACCESS_TOKEN", "")
TTS_CLUSTER = os.environ.get("VOLC_TTS_CLUSTER", "volcano_tts")
TTS_VOICE_OVERRIDE = os.environ.get("VOLC_TTS_VOICE", "")
TTS_RATE = int(os.environ.get("VOLC_TTS_RATE", "24000"))
TIMEOUT = 30

# Short aliases so a big-model voice can be named inside the firmware's char
# voice[16]. Env VOLC_TTS_VOICE still wins over everything.
VOICE_ALIASES = {
    "cancan": "zh_female_cancan_mars_bigtts",
    "qingcang": "zh_male_qingcang_mars_bigtts",
    "shuangkuai": "zh_female_shuangkuaisisi_moon_bigtts",
}


def http_json(url, payload, headers, timeout=TIMEOUT):
    """POST JSON, return (status, body_bytes, resp_headers). Raises nothing."""
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    req = urlreq.Request(url, data=data, method="POST")
    req.add_header("Content-Type", "application/json")
    for k, v in headers.items():
        req.add_header(k, v)
    try:
        with urlreq.urlopen(req, timeout=timeout) as r:
            return r.status, r.read(), dict(r.headers)
    except HTTPError as e:
        return e.code, e.read(), dict(e.headers)
    except URLError as e:
        return 0, str(e).encode(), {}


def wav_wrap(pcm, rate):
    """Canonical 44-byte WAV header + mono 16-bit PCM — exactly the layout
    llm.c's minimal parser expects (rate at offset 24, data from byte 44)."""
    n = len(pcm)
    return (b"RIFF" + struct.pack("<I", 36 + n) + b"WAVEfmt " +
            struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16) +
            b"data" + struct.pack("<I", n) + pcm)


def multipart_file(body, ctype):
    """Extract (fields, file_bytes) from a multipart/form-data body.
    Minimal parser (cgi module is gone in modern Python); tolerant of the
    firmware's fixed shape and of curl's."""
    b = None
    for part in ctype.split(";"):
        part = part.strip()
        if part.startswith("boundary="):
            b = part[len("boundary="):].strip('"')
    if not b:
        return {}, None
    delim = ("--" + b).encode()
    fields, file_bytes = {}, None
    for chunk in body.split(delim):
        chunk = chunk.strip(b"\r\n")
        if not chunk or chunk == b"--":
            continue
        head, _, payload = chunk.partition(b"\r\n\r\n")
        head_s = head.decode("utf-8", "replace")
        name = None
        for line in head_s.split("\r\n"):
            if "content-disposition" in line.lower():
                for tok in line.split(";"):
                    tok = tok.strip()
                    if tok.startswith("name="):
                        name = tok[5:].strip('"')
        if name == "file" or b"filename=" in head:
            file_bytes = payload
        elif name:
            fields[name] = payload.decode("utf-8", "replace").strip()
    return fields, file_bytes


# ---------------- Volcengine calls ----------------

def volc_chat(body_bytes, incoming_auth):
    key = ARK_KEY or (incoming_auth or "").removeprefix("Bearer ").strip()
    if not key:
        return 500, b'{"error":"no ark key: set VOLC_ARK_API_KEY"}'
    st, body, _ = http_json(ARK_URL, json.loads(body_bytes.decode("utf-8")),
                            {"Authorization": "Bearer " + key})
    return (st or 502), body


def volc_tts(text, voice):
    if not (APP_ID and TOKEN):
        return None, "no speech creds: set VOLC_APP_ID / VOLC_ACCESS_TOKEN"
    voice = TTS_VOICE_OVERRIDE or VOICE_ALIASES.get(voice, voice) or "zh_female_cancan_mars_bigtts"
    payload = {
        "app": {"appid": APP_ID, "token": TOKEN, "cluster": TTS_CLUSTER},
        "user": {"uid": "nanosoul"},
        "audio": {"voice_type": voice, "encoding": "pcm", "rate": TTS_RATE},
        "request": {"reqid": str(uuid.uuid4()), "text": text, "operation": "query"},
    }
    st, body, _ = http_json(TTS_URL, payload, {"Authorization": "Bearer; " + TOKEN})
    try:
        j = json.loads(body)
    except json.JSONDecodeError:
        return None, f"tts http {st}: {body[:200]!r}"
    if "data" not in j or not j["data"]:
        return None, f"tts error: code={j.get('code')} msg={j.get('message')} ({st})"
    return wav_wrap(base64.b64decode(j["data"]), TTS_RATE), None


def volc_asr(wav_bytes):
    if not (APP_ID and TOKEN):
        return None, "no speech creds: set VOLC_APP_ID / VOLC_ACCESS_TOKEN"
    payload = {
        "user": {"uid": "nanosoul"},
        "audio": {"format": "wav", "data": base64.b64encode(wav_bytes).decode()},
        "request": {"model_name": "bigmodel", "enable_punc": True},
    }
    st, body, hdr = http_json(ASR_URL, payload, {
        "X-Api-App-Key": APP_ID,
        "X-Api-Access-Key": TOKEN,
        "X-Api-Resource-Id": "volc.bigasr.auc_turbo",
        "X-Api-Request-Id": str(uuid.uuid4()),
        "X-Api-Sequence": "-1",
    })
    code = hdr.get("X-Api-Status-Code", "")
    try:
        j = json.loads(body)
    except json.JSONDecodeError:
        return None, f"asr http {st}: {body[:200]!r}"
    if code and code != "20000000":
        return None, f"asr status {code}: {hdr.get('X-Api-Message','')} {body[:200]!r}"
    res = j.get("result") or {}
    text = res.get("text")
    if text is None and isinstance(res.get("utterances"), list):
        text = "".join(u.get("text", "") for u in res["utterances"])
    if text is None:
        return None, f"asr: no text in {body[:200]!r}"
    return text, None


# ---------------- HTTP server ----------------

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *a):
        sys.stderr.write("%s %s\n" % (self.address_string(), fmt % a))

    def _send(self, status, body, ctype="application/json"):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/healthz":
            info = {"ok": True, "ark_key": bool(ARK_KEY),
                    "speech_creds": bool(APP_ID and TOKEN)}
            self._send(200, json.dumps(info).encode())
        else:
            self._send(404, b'{"error":"not found"}')

    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n)
        try:
            if self.path == "/v1/chat/completions":
                st, out = volc_chat(body, self.headers.get("Authorization"))
                print(f"  chat -> ark {st}")
                self._send(st, out)
            elif self.path == "/v1/audio/transcriptions":
                _, wav = multipart_file(body, self.headers.get("Content-Type", ""))
                if not wav:
                    self._send(400, b'{"error":"no file part"}')
                    return
                text, err = volc_asr(wav)
                if err:
                    print("  asr FAIL:", err)
                    self._send(502, json.dumps({"error": err}).encode())
                else:
                    print(f"  asr {len(wav)}B -> {text!r}")
                    self._send(200, json.dumps({"text": text}, ensure_ascii=False).encode())
            elif self.path == "/v1/audio/speech":
                req = json.loads(body.decode("utf-8"))
                wav, err = volc_tts(req.get("input", ""), req.get("voice", ""))
                if err:
                    print("  tts FAIL:", err)
                    self._send(502, json.dumps({"error": err}).encode())
                else:
                    print(f"  tts {req.get('input','')[:32]!r} -> {len(wav)}B wav")
                    self._send(200, wav, "audio/wav")
            else:
                self._send(404, b'{"error":"unknown endpoint"}')
        except Exception as e:  # keep the robot's 20s timeout from hanging us
            self._send(500, json.dumps({"error": str(e)}).encode())


def lan_ip():
    """The IP the robot should dial. A VPN/TUN (e.g. singbox) hijacks the
    connect()-trick, so prefer the default-route source; env wins over all."""
    override = os.environ.get("VOLC_PROXY_IP")
    if override:
        return override
    try:
        import subprocess
        out = subprocess.run(["ip", "route", "show", "default"],
                             capture_output=True, text=True, timeout=2).stdout
        for tok_prev, tok in zip(out.split(), out.split()[1:]):
            if tok_prev == "src":
                return tok
    except Exception:
        pass
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("223.5.5.5", 80))
        return s.getsockname()[0]
    except OSError:
        return "<this-PC-ip>"
    finally:
        s.close()


def print_config(port):
    ip = lan_ip()
    print(json.dumps({
        "chat": {"provider": "openai", "base_url": f"http://{ip}:{port}",
                 "api_key": "x", "model": "doubao-seed-1-6-250615",
                 "max_tokens": 512,
                 "system_prompt": "你是 NanoSoul，一只桌面陪伴小机器人。回答要简短、温暖。"},
        "stt": {"base_url": f"http://{ip}:{port}", "api_key": "x", "model": "bigmodel"},
        "tts": {"base_url": f"http://{ip}:{port}", "api_key": "x",
                "model": "tts", "voice": "cancan"},
    }, ensure_ascii=False, indent=2))


def check(text):
    print("1) TTS:", repr(text))
    wav, err = volc_tts(text, "cancan")
    if err:
        sys.exit("   TTS FAIL: " + err)
    path = "/tmp/volc_check.wav"
    with open(path, "wb") as f:
        f.write(wav)
    print(f"   OK {len(wav)}B -> {path} (可播放验听)")
    print("2) ASR: feeding that wav back")
    got, err = volc_asr(wav)
    if err:
        sys.exit("   ASR FAIL: " + err)
    print("   OK ->", repr(got))
    print("3) chat: ping Ark")
    st, out = volc_chat(json.dumps({
        "model": os.environ.get("VOLC_ARK_MODEL", "doubao-seed-1-6-250615"),
        "max_tokens": 32,
        "messages": [{"role": "user", "content": "回复两个字：在的"}],
    }).encode(), None)
    if st != 200:
        sys.exit(f"   chat FAIL {st}: {out[:300]!r}")
    j = json.loads(out)
    print("   OK ->", repr(j["choices"][0]["message"]["content"]))
    print("\n全链路凭证有效 ✅")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--port", type=int, default=8600)
    ap.add_argument("--check", metavar="TEXT", help="PC-local TTS->ASR->chat credential check")
    ap.add_argument("--print-config", action="store_true", help="emit SD config.json block")
    args = ap.parse_args()

    if args.print_config:
        print_config(args.port)
        return
    if args.check:
        check(args.check)
        return

    srv = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    ip = lan_ip()
    print(f"volc proxy on http://{ip}:{args.port}  "
          f"(ark_key={'set' if ARK_KEY else 'MISSING'}, "
          f"speech={'set' if APP_ID and TOKEN else 'MISSING'})")
    print(f"SD config -> base_url http://{ip}:{args.port} for chat/stt/tts "
          f"(--print-config for the full block)")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
