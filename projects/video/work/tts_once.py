#!/usr/bin/env python3
import argparse
import base64
import json
import struct
import uuid
from urllib import request as urlreq
from urllib.error import HTTPError, URLError


def wav_wrap(pcm, rate):
    n = len(pcm)
    return (
        b"RIFF"
        + struct.pack("<I", 36 + n)
        + b"WAVEfmt "
        + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
        + b"data"
        + struct.pack("<I", n)
        + pcm
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", required=True)
    ap.add_argument("--text", required=True)
    ap.add_argument("--voice", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--rate", type=int, default=16000)
    args = ap.parse_args()

    with open(args.config, encoding="utf-8") as f:
        cfg = json.load(f)
    tts = cfg["tts"]
    resource = tts.get("resource_id") or "seed-tts-2.0"
    base = tts.get("base_url") or "https://openspeech.bytedance.com"
    url = base + "/api/v3/plan/tts/unidirectional"
    body = {
        "req_params": {
            "text": args.text,
            "speaker": args.voice,
            "audio_params": {"format": "pcm", "sample_rate": args.rate},
        }
    }
    req = urlreq.Request(url, data=json.dumps(body, ensure_ascii=False).encode("utf-8"), method="POST")
    req.add_header("Content-Type", "application/json")
    req.add_header("X-Api-Key", tts["api_key"])
    req.add_header("X-Api-Resource-Id", resource)
    req.add_header("X-Api-Connect-Id", str(uuid.uuid4()))
    try:
        with urlreq.urlopen(req, timeout=30) as resp:
            raw = resp.read()
    except HTTPError as e:
        raise SystemExit(f"TTS HTTP {e.code}: {e.read()[:240].decode('utf-8', 'replace')}")
    except URLError as e:
        raise SystemExit(f"TTS URL error: {e}")

    s = raw.decode("utf-8", "replace")
    pcm = bytearray()
    pos = 0
    while True:
        i = s.find('"data":"', pos)
        if i < 0:
            break
        j = s.find('"', i + 8)
        if j < 0:
            break
        try:
            pcm += base64.b64decode(s[i + 8 : j])
        except Exception:
            pass
        pos = j + 1
    if not pcm:
        raise SystemExit(f"TTS returned no audio chunks: {s[:240]}")
    with open(args.out, "wb") as f:
        f.write(wav_wrap(bytes(pcm), args.rate))
    print(f"{args.out}: {len(pcm) / 2 / args.rate:.2f}s")


if __name__ == "__main__":
    main()
