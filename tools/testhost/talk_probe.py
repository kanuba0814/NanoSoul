#!/usr/bin/env python3
"""
talk_probe.py — 在 PC 上镜像固件的火山 talk 链路（chat + TTS），用真实 SD 配置
逐环验证/调参，调通后再把配置落到开发板。凭证从 config.json 读，不进仓库。

请求构造与固件一字不差（llm.c / tts_volc）：
  chat: POST {base}/chat/completions（base 含 /api/ 时；否则 {base}/api/v3/chat/completions）
        Authorization: Bearer <chat.api_key>
  tts : POST {base|openspeech}/api/v3/plan/tts/unidirectional
        X-Api-Key + X-Api-Resource-Id，body req_params{text,speaker,audio_params{pcm,16000}}
        响应 = 无分隔连续 JSON，扫 "data":"<base64>" 拼 PCM（与固件同算法）

用法:
  python3 talk_probe.py chat  [--text 你好]
  python3 talk_probe.py tts   [--text 你好呀] [--voice zh_female_vv_uranus_bigtts] [--resource seed-tts-2.0]
  python3 talk_probe.py all
  python3 talk_probe.py check     # 无麦自检：TTS 合成音频回灌 ASR，验证识别链
  python3 talk_probe.py talk      # 用本机麦克风聊天（回车开录/回车说完，Ctrl+C 退出）
                                  #   录音提示处输入 v 可随时换音色、q 退出
  python3 talk_probe.py voices    # 列出内置音色预设（全属 seed-tts-2.0）
  （--voice 可传预设名/序号，也可直接传任意火山 voice_id；talk 不带 --voice 时进入前先选）
  （--config 指配置文件，默认 ../../sdcard_template/nanosoul/config.json）
TTS 成功会写 /tmp/talk_probe_tts.wav，可直接播放验听。
talk/asr 需要 pip 包 websockets 与 PipeWire 的 pw-record/pw-play（Fedora 自带）。
"""
import argparse
import base64
import gzip as gzmod
import json
import os
import struct
import subprocess
import sys
import uuid
import wave
from urllib import request as urlreq
from urllib.error import HTTPError, URLError

DEF_CONFIG = os.path.join(os.path.dirname(__file__), "..", "..",
                          "sdcard_template", "nanosoul", "config.json")
TIMEOUT = 30

# 内置音色预设：(类别, 中文名, voice_id)。全部属 seed-tts-2.0（uranus）资源，
# 与当前 config 的 resource_id 一致，换 voice 即可、不用改别的。
# 完整目录（数百个）见 https://www.volcengine.com/docs/6561/1257544；
# 任意 voice_id 都能用 --voice 直接传，不必在此登记。
PRESET_VOICES = [
    ("温柔", "Vivi（默认·多语种/方言）", "zh_female_vv_uranus_bigtts"),
    ("温柔", "温柔妈妈",   "zh_female_wenroumama_uranus_bigtts"),
    ("温柔", "知性灿灿",   "zh_female_cancan_uranus_bigtts"),
    ("温柔", "清新女声",   "zh_female_qingxinnvsheng_uranus_bigtts"),
    ("温柔", "贴心女声",   "zh_female_tiexinnvsheng_uranus_bigtts"),
    ("活泼", "萌丫头",     "zh_female_mengyatou_uranus_bigtts"),
    ("活泼", "撒娇学妹",   "zh_female_sajiaoxuemei_uranus_bigtts"),
    ("活泼", "甜美小源",   "zh_female_tianmeixiaoyuan_uranus_bigtts"),
    ("活泼", "爽快思思",   "zh_female_shuangkuaisisi_uranus_bigtts"),
    ("活泼", "邻家女孩",   "zh_female_linjianvhai_uranus_bigtts"),
    ("活泼", "开朗姐姐",   "zh_female_kailangjiejie_uranus_bigtts"),
    ("御姐", "高冷御姐",   "zh_female_gaolengyujie_uranus_bigtts"),
    ("御姐", "魅力女友",   "zh_female_meilinvyou_uranus_bigtts"),
    ("男声", "云舟",       "zh_male_m191_uranus_bigtts"),
    ("男声", "小天",       "zh_male_taocheng_uranus_bigtts"),
    ("男声", "少年梓辛",   "zh_male_shaonianzixin_uranus_bigtts"),
    ("男声", "开朗弟弟",   "zh_male_kailangdidi_uranus_bigtts"),
    ("男声", "温暖阿虎",   "zh_male_wennuanahu_uranus_bigtts"),
    ("趣味", "奶气萌娃",   "zh_male_naiqimengwa_uranus_bigtts"),
    ("趣味", "佩奇猪",     "zh_female_peiqi_uranus_bigtts"),
    ("趣味", "猴哥",       "zh_male_sunwukong_uranus_bigtts"),
    ("趣味", "熊二",       "zh_male_xionger_uranus_bigtts"),
]


def voice_name(vid):
    """voice_id → 中文名（找不到就回显 id）。"""
    for _cat, name, pid in PRESET_VOICES:
        if pid == vid:
            return name
    return vid


def resolve_voice(sel):
    """把 --voice 的取值解析成 voice_id：接受预设序号(1..N)、中文名、或原始 voice_id。"""
    if not sel:
        return None
    sel = sel.strip()
    if sel.isdigit():
        i = int(sel) - 1
        if 0 <= i < len(PRESET_VOICES):
            return PRESET_VOICES[i][2]
        raise SystemExit(f"音色序号越界：{sel}（有效 1..{len(PRESET_VOICES)}）")
    for _cat, name, pid in PRESET_VOICES:
        if sel == name or sel == pid:
            return pid
    return sel  # 未登记的原始 voice_id，直接透传给火山


def print_voices(current=None):
    print("内置音色预设（seed-tts-2.0，任意 voice_id 亦可用 --voice 直接传）：")
    last = None
    for i, (cat, name, pid) in enumerate(PRESET_VOICES, 1):
        head = cat if cat != last else "　"
        last = cat
        mark = " ←当前" if pid == current else ""
        print(f"  {head}  {i:>2}. {name:<14} {pid}{mark}")


def pick_voice(current):
    """交互式选音色：回车保持当前，序号/名字/原始 id 都收。返回选定 voice_id。"""
    print_voices(current)
    sel = input(f"选音色（回车保持「{voice_name(current)}」）: ").strip()
    if not sel:
        return current
    try:
        return resolve_voice(sel) or current
    except SystemExit as e:
        print(f"  {e}，保持当前")
        return current


def load_cfg(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def post_json(url, payload, headers):
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    req = urlreq.Request(url, data=data, method="POST")
    req.add_header("Content-Type", "application/json")
    for k, v in headers.items():
        req.add_header(k, v)
    try:
        with urlreq.urlopen(req, timeout=TIMEOUT) as r:
            return r.status, r.read()
    except HTTPError as e:
        return e.code, e.read()
    except URLError as e:
        return 0, str(e).encode()


def chat(cfg, text):
    c = cfg["chat"]
    base = c.get("base_url") or "https://ark.cn-beijing.volces.com"
    path = "/chat/completions" if "/api/" in base else "/api/v3/chat/completions"
    url = base + path
    body = {
        "model": c["model"],
        "max_tokens": c.get("max_tokens", 512),
        "messages": ([{"role": "system", "content": c["system_prompt"]}]
                     if c.get("system_prompt") else []) +
                    [{"role": "user", "content": text}],
    }
    print(f"chat -> {url}  model={c['model']}")
    st, resp = post_json(url, body, {"Authorization": "Bearer " + c["api_key"]})
    if st != 200:
        print(f"  FAIL http {st}: {resp[:300].decode('utf-8', 'replace')}")
        return None
    reply = json.loads(resp)["choices"][0]["message"]["content"]
    print(f"  OK: {reply!r}")
    return reply


def wav_wrap(pcm, rate):
    n = len(pcm)
    return (b"RIFF" + struct.pack("<I", 36 + n) + b"WAVEfmt " +
            struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16) +
            b"data" + struct.pack("<I", n) + pcm)


def tts(cfg, text, voice=None, resource=None, rate=16000):
    t = cfg["tts"]
    voice = voice or t.get("voice")
    resource = resource or t.get("resource_id") or "seed-tts-2.0"
    base = t.get("base_url") or "https://openspeech.bytedance.com"
    url = base + "/api/v3/plan/tts/unidirectional"
    body = {"req_params": {"text": text, "speaker": voice,
                           "audio_params": {"format": "pcm", "sample_rate": rate}}}
    print(f"tts  -> {url}  speaker={voice} resource={resource}")
    st, resp = post_json(url, body, {
        "X-Api-Key": t["api_key"],
        "X-Api-Resource-Id": resource,
        "X-Api-Connect-Id": str(uuid.uuid4()),
    })
    if st != 200:
        print(f"  FAIL http {st}: {resp[:300].decode('utf-8', 'replace')}")
        return False
    # 与固件同算法：扫 "data":"<base64>" 拼 PCM
    pcm = bytearray()
    s = resp.decode("utf-8", "replace")
    pos = 0
    while True:
        i = s.find('"data":"', pos)
        if i < 0:
            break
        j = s.find('"', i + 8)
        if j < 0:
            break
        try:
            pcm += base64.b64decode(s[i + 8:j])
        except Exception:
            pass
        pos = j + 1
    if not pcm:
        print(f"  FAIL no audio chunks: {s[:300]}")
        return False
    out = "/tmp/talk_probe_tts.wav"
    with open(out, "wb") as f:
        f.write(wav_wrap(bytes(pcm), rate))
    print(f"  OK: {len(pcm)}B pcm = {len(pcm) / 2 / rate:.2f}s -> {out}（可播放验听）")
    return True


# ---------------- ASR: seed-asr-2.0 单流 WSS（分帧与固件 volc_asr.c 一致） ----------------

def _asr_frame(msg_type, flags, ser, comp, seq, payload):
    f = bytearray([0x11, (msg_type << 4) | flags, (ser << 4) | comp, 0x00])
    f += struct.pack(">i", seq)
    f += struct.pack(">I", len(payload))
    f += payload
    return bytes(f)


def asr(cfg, pcm, rate=16000):
    """pcm: s16le mono bytes → 识别文本（'' = 没听到话）。None = 传输/服务错误。"""
    import asyncio
    import websockets

    s = cfg["stt"]
    base = s.get("base_url") or "wss://openspeech.bytedance.com"
    base = base.replace("https://", "wss://")
    url = base + "/api/v3/plan/sauc/bigmodel_nostream"
    rid = str(uuid.uuid4())
    headers = {
        "X-Api-Key": s["api_key"],
        "X-Api-Resource-Id": s.get("resource_id") or "volc.seedasr.sauc.duration",
        "X-Api-Request-Id": rid,
        "X-Api-Connect-Id": rid,
        "X-Api-Sequence": "-1",
    }
    conf = json.dumps({
        "user": {"uid": "nanosoul-pc"},
        "audio": {"format": "pcm", "codec": "raw", "rate": rate, "bits": 16, "channel": 1},
        "request": {"model_name": "bigmodel", "enable_itn": True, "enable_punc": True,
                    "enable_ddc": True, "show_utterances": False, "enable_nonstream": False},
    }, ensure_ascii=False).encode()

    async def run():
        text = ""
        async with websockets.connect(url, additional_headers=headers,
                                      max_size=10 * 1024 * 1024) as ws:
            await ws.send(_asr_frame(0x1, 0x1, 0x1, 0x1, 1, gzmod.compress(conf)))
            seq, step = 1, 6400  # 200ms/帧，同固件
            for off in range(0, max(len(pcm), 1), step):
                chunk = pcm[off:off + step]
                last = off + step >= len(pcm)
                seq += 1
                await ws.send(_asr_frame(0x2, 0x3 if last else 0x1, 0x0, 0x1,
                                         -seq if last else seq, gzmod.compress(chunk)))
            while True:
                msg = await asyncio.wait_for(ws.recv(), timeout=15)
                if not isinstance(msg, (bytes, bytearray)):
                    continue
                hdr = msg[0] & 0x0f
                mtype, flags = msg[1] >> 4, msg[1] & 0x0f
                comp = msg[2] & 0x0f
                p = hdr * 4
                if flags & 0x1:
                    p += 4
                if flags & 0x4:
                    p += 4
                if mtype == 0xf:
                    code = struct.unpack(">i", msg[p:p + 4])[0]
                    p += 4
                    n = struct.unpack(">I", msg[p:p + 4])[0]
                    body = msg[p + 4:p + 4 + n]
                    if comp == 1:
                        body = gzmod.decompress(body)
                    print(f"  ASR 服务错误 code={code}: {body[:200].decode('utf-8', 'replace')}")
                    return None
                n = struct.unpack(">I", msg[p:p + 4])[0]
                body = msg[p + 4:p + 4 + n]
                if comp == 1 and body:
                    body = gzmod.decompress(body)
                if body:
                    r = json.loads(body).get("result") or {}
                    if r.get("text"):
                        text = r["text"]
                if flags & 0x2:   # is_last_package
                    return text
    import asyncio
    try:
        return asyncio.run(run())
    except Exception as e:
        print(f"  ASR FAIL: {e}")
        return None


# ---------------- 本机麦克风对话 ----------------

def read_wav_pcm(path):
    with wave.open(path, "rb") as w:
        assert w.getsampwidth() == 2 and w.getnchannels() == 1, "需要 s16 mono"
        return w.readframes(w.getnframes()), w.getframerate()


def record_push_to_talk(rate=16000):
    path = "/tmp/talk_probe_mic.wav"
    p = subprocess.Popen(["pw-record", "--rate", str(rate), "--channels", "1",
                          "--format", "s16", path],
                         stderr=subprocess.DEVNULL)
    input("   （说话中）说完按回车…")
    p.terminate()
    p.wait()
    pcm, r = read_wav_pcm(path)
    print(f"   录到 {len(pcm) // 2 / r:.1f}s")
    return pcm, r


def play_wav(path):
    subprocess.run(["pw-play", path], check=False)


def talk_loop(cfg, voice=None):
    print("=== NanoSoul PC 对话（同一份 SD 配置，链路与固件一致；Ctrl+C 退出）===")
    voice = voice or cfg["tts"].get("voice")
    print(f"当前音色：{voice_name(voice)}  ({voice})")
    history = []
    c = cfg["chat"]
    base = c.get("base_url") or "https://ark.cn-beijing.volces.com"
    path = "/chat/completions" if "/api/" in base else "/api/v3/chat/completions"
    while True:
        act = input(f"🎤 回车说话（v=换音色，当前「{voice_name(voice)}」／q=退出）…").strip().lower()
        if act in ("q", "quit", "exit"):
            print("再见！")
            return
        if act in ("v", "voice"):
            voice = pick_voice(voice)
            print(f"→ 已切到：{voice_name(voice)}  ({voice})")
            continue
        pcm, rate = record_push_to_talk()
        text = asr(cfg, pcm, rate)
        if text is None:
            continue
        if not text:
            print("   （没听到内容）")
            continue
        print(f"你: {text}")
        history.append({"role": "user", "content": text})
        body = {
            "model": c["model"], "max_tokens": c.get("max_tokens", 512),
            "messages": ([{"role": "system", "content": c["system_prompt"]}]
                         if c.get("system_prompt") else []) + history[-10:],
        }
        st, resp = post_json(base + path, body, {"Authorization": "Bearer " + c["api_key"]})
        if st != 200:
            print(f"   chat FAIL {st}: {resp[:200].decode('utf-8', 'replace')}")
            history.pop()
            continue
        reply = json.loads(resp)["choices"][0]["message"]["content"]
        history.append({"role": "assistant", "content": reply})
        print(f"AI: {reply}")
        if tts(cfg, reply, voice):
            play_wav("/tmp/talk_probe_tts.wav")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("cmd", choices=["chat", "tts", "all", "check", "talk", "voices"])
    ap.add_argument("--config", default=DEF_CONFIG)
    ap.add_argument("--text", default=None)
    ap.add_argument("--voice", default=None,
                    help="预设序号/中文名，或任意火山 voice_id")
    ap.add_argument("--resource", default=None)
    args = ap.parse_args()

    if args.cmd == "voices":
        print_voices()
        sys.exit(0)

    cfg = load_cfg(args.config)
    voice = resolve_voice(args.voice)  # 预设序号/名 → voice_id；原始 id 透传

    ok = True
    if args.cmd in ("chat", "all"):
        ok = chat(cfg, args.text or "回复两个字：在的") is not None and ok
    if args.cmd in ("tts", "all"):
        reply = args.text or "你好呀，我是 NanoSoul。"
        ok = tts(cfg, reply, voice, args.resource) and ok
    if args.cmd == "check":
        ok = tts(cfg, args.text or "你好呀，我是小机器人。", voice, args.resource)
        if ok:
            pcm, rate = read_wav_pcm("/tmp/talk_probe_tts.wav")
            got = asr(cfg, pcm, rate)
            print(f"  ASR 回灌结果: {got!r}")
            ok = bool(got)
    if args.cmd == "talk":
        try:
            talk_loop(cfg, voice)
        except (KeyboardInterrupt, EOFError):
            print("\n再见！")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
