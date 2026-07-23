#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import math
import os
import subprocess
import zipfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageOps


ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "media" / "video"
ASSETS = OUT / "assets"
SLIDES = OUT / "slides"
CLIPS = OUT / "clips"
AUDIO = OUT / "audio"
WORK = OUT / "work"
FINAL = OUT / "NanoSoul_intro_90s.mp4"
W, H, FPS = 1920, 1080, 30
VOICE = "NotoSansSC-Regular.otf"


def run(cmd):
    print("+", " ".join(str(x) for x in cmd))
    subprocess.run([str(x) for x in cmd], cwd=ROOT, check=True)


def ffprobe_duration(path):
    p = subprocess.run(
        [
            "ffprobe",
            "-v",
            "error",
            "-show_entries",
            "format=duration",
            "-of",
            "default=nw=1:nk=1",
            str(path),
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=True,
    )
    return float(p.stdout.strip())


def fc_match(family):
    p = subprocess.run(
        ["fc-match", "-f", "%{file}", family],
        text=True,
        capture_output=True,
        check=True,
    )
    return p.stdout.strip()


FONT_REG = "/usr/share/fonts/google-noto-sans-cjk-vf-fonts/NotoSansCJK-VF.ttc"
if not Path(FONT_REG).exists():
    FONT_REG = fc_match("Noto Sans CJK SC")
FONT_BOLD = FONT_REG


def font(size, bold=False):
    from PIL import ImageFont

    return ImageFont.truetype(FONT_BOLD if bold else FONT_REG, size=size)


def ensure_dirs():
    for p in [ASSETS, SLIDES, CLIPS, AUDIO, WORK]:
        p.mkdir(parents=True, exist_ok=True)


def extract_logo():
    out = ASSETS / "competition_logo.png"
    docx = ROOT / "docs" / "report.example.docx"
    if not docx.exists():
        docx = ROOT / "docs" / "report" / "NanoSoul_作品报告.docx"
    with zipfile.ZipFile(docx) as z:
        out.write_bytes(z.read("word/media/image1.png"))
    logo = Image.open(out).convert("RGBA")
    card = Image.new("RGB", (W, H), "white")
    logo.thumbnail((1200, 360), Image.Resampling.LANCZOS)
    card.paste(logo, ((W - logo.width) // 2, (H - logo.height) // 2), logo)
    card.save(ASSETS / "logo_card.png", quality=95)
    return out


def extract_report_media():
    media_dir = ASSETS / "report_docx_media"
    media_dir.mkdir(parents=True, exist_ok=True)
    if (media_dir / "image2.png").exists():
        return
    docx = ROOT / "docs" / "report" / "NanoSoul_作品报告.docx"
    if not docx.exists():
        return
    with zipfile.ZipFile(docx) as z:
        for name in z.namelist():
            if name.startswith("word/media/") and name.endswith(".png"):
                (media_dir / Path(name).name).write_bytes(z.read(name))


def fit_cover(img, size=(W, H)):
    img = ImageOps.exif_transpose(img).convert("RGB")
    sw, sh = size
    scale = max(sw / img.width, sh / img.height)
    nw, nh = int(img.width * scale), int(img.height * scale)
    img = img.resize((nw, nh), Image.Resampling.LANCZOS)
    left = (nw - sw) // 2
    top = (nh - sh) // 2
    return img.crop((left, top, left + sw, top + sh))


def fit_contain(img, box, fill=(255, 255, 255)):
    x, y, bw, bh = box
    img = ImageOps.exif_transpose(img).convert("RGB")
    scale = min(bw / img.width, bh / img.height)
    nw, nh = int(img.width * scale), int(img.height * scale)
    img = img.resize((nw, nh), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (bw, bh), fill)
    canvas.paste(img, ((bw - nw) // 2, (bh - nh) // 2))
    return canvas


def overlay_gradient(base, opacity=150):
    grad = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    px = grad.load()
    for x in range(W):
        a = int(opacity * (1 - x / W) + 40)
        for y in range(H):
            px[x, y] = (5, 18, 28, max(0, min(210, a)))
    return Image.alpha_composite(base.convert("RGBA"), grad)


def draw_wrapped(draw, xy, text, fnt, fill, width, line_gap=16):
    x, y = xy
    lines, cur = [], ""
    for ch in text:
        test = cur + ch
        if ch == "\n":
            lines.append(cur)
            cur = ""
        elif draw.textbbox((0, 0), test, font=fnt)[2] <= width:
            cur = test
        else:
            if cur:
                lines.append(cur)
            cur = ch
    if cur:
        lines.append(cur)
    for line in lines:
        draw.text((x, y), line, font=fnt, fill=fill)
        y += fnt.size + line_gap
    return y


def rounded_panel(draw, box, fill=(255, 255, 255, 225), outline=(31, 78, 121, 80)):
    draw.rounded_rectangle(box, radius=8, fill=fill, outline=outline, width=2)


def paste_logo(img, logo_path, pos=(1420, 870), max_size=(360, 120)):
    logo = Image.open(logo_path).convert("RGBA")
    logo.thumbnail(max_size, Image.Resampling.LANCZOS)
    img.alpha_composite(logo, pos)


def slide_title(logo_path):
    bg = Image.open(ASSETS / "ai_bg_title.png")
    img = overlay_gradient(bg, 180)
    d = ImageDraw.Draw(img)
    d.text((118, 150), "小王 NanoSoul", font=font(92, True), fill=(255, 255, 255, 255))
    d.text((124, 268), "端侧 AI 桌面陪伴机器人", font=font(42), fill=(215, 235, 255, 255))
    d.rounded_rectangle((124, 370, 820, 438), radius=8, fill=(31, 78, 121, 210))
    d.text((158, 382), "ESP32-P4 · 本地感知 · 云端深度对话", font=font(30, True), fill=(255, 255, 255, 255))
    d.text((126, 862), "AI 赋能设计 · 真实素材演示", font=font(30), fill=(230, 238, 246, 240))
    paste_logo(img, logo_path, (1338, 880), (430, 120))
    img.convert("RGB").save(SLIDES / "title.png", quality=95)


def slide_summary_core():
    bg = Image.open(ASSETS / "ai_bg_chip.png")
    img = overlay_gradient(bg, 160)
    d = ImageDraw.Draw(img)
    d.text((105, 98), "功能摘要", font=font(64, True), fill=(255, 255, 255, 255))
    d.text((110, 188), "一个会认人、会回应、会表达状态的桌面伙伴", font=font(34), fill=(222, 238, 255, 250))
    items = [
        ("视觉认主", "本地识别主人，陌生人进入好奇状态"),
        ("自然语音", "唤醒词“小王”，应答后 8 秒免唤醒续聊"),
        ("动画表情", "30fps 大屏表情，传达情绪与状态"),
        ("多模态感知", "摄像头、语音、触碰与姿态共同驱动决策"),
    ]
    y = 330
    for title, body in items:
        rounded_panel(d, (120, y, 930, y + 108), fill=(255, 255, 255, 218))
        d.text((155, y + 22), title, font=font(32, True), fill=(31, 78, 121, 255))
        d.text((345, y + 27), body, font=font(28), fill=(38, 52, 64, 255))
        y += 128
    img.convert("RGB").save(SLIDES / "summary_core.png", quality=95)


def slide_summary_tech():
    bg = Image.open(ASSETS / "ai_bg_chip.png")
    img = overlay_gradient(bg, 110)
    d = ImageDraw.Draw(img)
    d.text((104, 92), "技术亮点", font=font(64, True), fill=(255, 255, 255, 255))
    metrics = [("端侧运行", "断网可用"), ("表情刷新", "30 fps"), ("续聊窗口", "8 秒"), ("自主移动", "初始验证")]
    x = 120
    for label, value in metrics:
        rounded_panel(d, (x, 270, x + 360, 450), fill=(255, 255, 255, 224))
        d.text((x + 32, 302), label, font=font(30), fill=(55, 70, 84, 255))
        d.text((x + 32, 356), value, font=font(46, True), fill=(31, 78, 121, 255))
        x += 420
    d.text((126, 575), "本地人脸识别 · 决策状态机 · 火山引擎语音链 · 自研固件闭环", font=font(38, True), fill=(255, 255, 255, 255))
    draw_wrapped(
        d,
        (130, 665),
        "所有关键感知与交互状态都在芯片端完成；只有深度对话请求云端大模型，兼顾隐私、延迟与可用性。",
        font(32),
        (228, 240, 255, 245),
        1220,
        18,
    )
    img.convert("RGB").save(SLIDES / "summary_tech.png", quality=95)


def make_label(name, title, subtitle):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((80, 780, 760, 920), radius=8, fill=(7, 18, 30, 178), outline=(255, 255, 255, 70), width=2)
    d.rectangle((80, 780, 94, 920), fill=(73, 162, 210, 240))
    d.text((126, 808), title, font=font(40, True), fill=(255, 255, 255, 255))
    d.text((128, 864), subtitle, font=font(28), fill=(218, 236, 250, 245))
    img.save(SLIDES / name)


def slide_diagram(out_name, fig_name, title, body):
    img = Image.new("RGBA", (W, H), (244, 248, 252, 255))
    d = ImageDraw.Draw(img)
    d.text((98, 78), title, font=font(56, True), fill=(31, 78, 121, 255))
    draw_wrapped(d, (104, 166), body, font(30), (47, 60, 72, 255), 650, 16)
    fig_path = ROOT / "docs" / "report" / "figures" / fig_name
    if not fig_path.exists():
        fig_path = ASSETS / "report_docx_media" / fig_name
    fig = Image.open(fig_path)
    panel = fit_contain(fig, (0, 0, 1040, 760), fill=(255, 255, 255))
    img.paste(panel, (800, 150))
    d.rounded_rectangle((785, 135, 1855, 935), radius=8, outline=(31, 78, 121, 90), width=2)
    img.convert("RGB").save(SLIDES / out_name, quality=95)


def slide_full_image(out_name, image_path):
    img = fit_contain(Image.open(image_path), (0, 0, W, H), fill=(255, 255, 255))
    img.save(SLIDES / out_name, quality=95)


def slide_photo(out_name, photo, title):
    bg = fit_cover(Image.open(ROOT / "media" / photo))
    img = bg.convert("RGBA")
    shade = Image.new("RGBA", (W, H), (0, 0, 0, 70))
    img = Image.alpha_composite(img, shade)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((76, 760, 780, 910), radius=8, fill=(255, 255, 255, 218))
    d.text((118, 792), title, font=font(42, True), fill=(31, 78, 121, 255))
    d.text((120, 852), "真实样机照片 · 自动方向修正", font=font(28), fill=(55, 70, 84, 255))
    img.convert("RGB").save(SLIDES / out_name, quality=95)


def slide_outro(logo_path):
    bg = Image.open(ASSETS / "ai_bg_warmdesk.png")
    img = overlay_gradient(bg, 150)
    d = ImageDraw.Draw(img)
    d.text((116, 162), "小王 NanoSoul", font=font(84, True), fill=(255, 255, 255, 255))
    d.text((122, 286), "让陪伴，真正住进这颗芯片里", font=font(44), fill=(226, 240, 255, 255))
    d.rounded_rectangle((124, 388, 780, 464), radius=8, fill=(31, 78, 121, 205))
    d.text((158, 404), "端侧 AI · 自研固件 · 真实样机", font=font(31, True), fill=(255, 255, 255, 255))
    paste_logo(img, logo_path, (118, 840), (470, 130))
    img.convert("RGB").save(SLIDES / "outro.png", quality=95)


def render_slides():
    extract_report_media()
    logo = extract_logo()
    slide_title(logo)
    slide_summary_core()
    slide_summary_tech()
    slide_full_image("design_flow.png", ASSETS / "design_flow_updated.png")
    make_label("label_owner.png", "视觉认主 · 端侧识别", "看见主人，送上专属问候")
    make_label("label_voice.png", "唤醒词“小王” · 自然对话", "应答后 8 秒内免唤醒续聊")
    make_label("label_expression.png", "30fps 动画表情 · 即时回应", "被抱起、被轻拍，都会反馈状态")
    make_label("label_motion.png", "移动雏形 · 初始验证", "自主移动仍处早期阶段")
    slide_photo("photo_4404.png", "IMG_4404.jpeg", "前脸嵌屏与调试 HUD")
    slide_photo("photo_4409.png", "IMG_4409.jpeg", "毛绒外壳与桌面陪伴形态")
    slide_photo("photo_4418.png", "IMG_4418.jpeg", "实物交互与手持观察")
    slide_diagram(
        "diagram_system.png",
        "fig_2_1_system.png" if (ROOT / "docs" / "report" / "figures" / "fig_2_1_system.png").exists() else "image2.png",
        "系统闭环",
        "摄像头、麦克风、触碰与姿态输入，在端侧状态机中融合，驱动表情和语音交互。",
    )
    slide_diagram(
        "diagram_face.png",
        "fig_face.png" if (ROOT / "docs" / "report" / "figures" / "fig_face.png").exists() else "image3.png",
        "认主链路",
        "人脸识别链路在本地完成，主人、陌生人与等待状态进入不同交互分支。",
    )
    slide_diagram(
        "diagram_voice.png",
        "fig_voice.png" if (ROOT / "docs" / "report" / "figures" / "fig_voice.png").exists() else "image16.png",
        "语音链路",
        "唤醒、识别、TTS 与云端大模型协同，保留端侧可用性与自然对话体验。",
    )
    slide_outro(logo)


def still_clip(image, out, dur):
    run(
        [
            "ffmpeg",
            "-y",
            "-loop",
            "1",
            "-t",
            f"{dur:.3f}",
            "-i",
            image,
            "-vf",
            f"fps={FPS},format=yuv420p,fade=t=in:st=0:d=0.25,fade=t=out:st={max(0,dur-0.25):.3f}:d=0.25",
            "-map",
            "0:v",
            "-c:v",
            "libx264",
            "-preset",
            "veryfast",
            "-crf",
            "18",
            "-pix_fmt",
            "yuv420p",
            "-an",
            out,
        ]
    )


def video_clip(src, out, dur, ss=0):
    src_path = ROOT / "media" / src
    filter_complex = (
        f"[0:v]scale={W}:{H}:force_original_aspect_ratio=decrease,"
        f"pad={W}:{H}:(ow-iw)/2:(oh-ih)/2:black,fps={FPS},format=rgba,"
        f"fade=t=in:st=0:d=0.25,fade=t=out:st={max(0,dur-0.25):.3f}:d=0.25,"
        "format=yuv420p[v]"
    )
    run(
        [
            "ffmpeg",
            "-y",
            "-stream_loop",
            "2",
            "-ss",
            f"{ss:.3f}",
            "-i",
            src_path,
            "-t",
            f"{dur:.3f}",
            "-filter_complex",
            filter_complex,
            "-map",
            "[v]",
            "-c:v",
            "libx264",
            "-preset",
            "veryfast",
            "-crf",
            "18",
            "-pix_fmt",
            "yuv420p",
            "-an",
            out,
        ]
    )


def concat_clips(clips, out):
    list_file = WORK / "concat.txt"
    list_file.write_text("".join(f"file '{Path(c).resolve()}'\n" for c in clips), encoding="utf-8")
    run(["ffmpeg", "-y", "-f", "concat", "-safe", "0", "-i", list_file, "-c", "copy", out])


def build_visual_track():
    vo = [ffprobe_duration(AUDIO / f"vo_{i}.wav") for i in range(1, 8)]
    durations = {
        "logo": 2.0,
        "title": max(8.0, vo[0] + 0.8),
        "summary_a": (vo[1] + 1.0) / 2,
        "summary_b": (vo[1] + 1.0) / 2,
        "owner": 10.0,
        "voice": 11.5,
        "expr": 12.0,
        "motion": 7.5,
        "motion_proto": 3.8,
        "arch_each": 3.0,
        "design_flow": 4.0,
        "outro": 6.5,
    }
    clips = []
    specs = [
        ("00_logo.mp4", ASSETS / "logo_card.png", durations["logo"]),
        ("01_title.mp4", SLIDES / "title.png", durations["title"]),
        ("02_summary_core.mp4", SLIDES / "summary_core.png", durations["summary_a"]),
        ("03_summary_tech.mp4", SLIDES / "summary_tech.png", durations["summary_b"]),
    ]
    for name, img, dur in specs:
        path = CLIPS / name
        still_clip(img, path, dur)
        clips.append(path)
    video_clip("IMG_4414.mov", CLIPS / "04_owner.mp4", durations["owner"], 0)
    clips.append(CLIPS / "04_owner.mp4")
    video_clip("IMG_4412.mov", CLIPS / "05_voice.mp4", durations["voice"], 0)
    clips.append(CLIPS / "05_voice.mp4")
    video_clip("IMG_4417.mov", CLIPS / "06_expression.mp4", durations["expr"], 0.4)
    clips.append(CLIPS / "06_expression.mp4")
    video_clip("IMG_4415.mov", CLIPS / "07_motion.mp4", durations["motion"], 0)
    clips.append(CLIPS / "07_motion.mp4")
    video_clip("IMG_4422.mov", CLIPS / "08_motion_proto.mp4", durations["motion_proto"], 0)
    clips.append(CLIPS / "08_motion_proto.mp4")
    arch_items = [
        ("photo_4404.png", durations["arch_each"]),
        ("diagram_system.png", durations["arch_each"]),
        ("design_flow.png", durations["design_flow"]),
        ("photo_4409.png", durations["arch_each"]),
        ("diagram_face.png", durations["arch_each"]),
        ("photo_4418.png", durations["arch_each"]),
        ("diagram_voice.png", durations["arch_each"]),
    ]
    for idx, (img, dur) in enumerate(arch_items, 9):
        path = CLIPS / f"{idx:02d}_{Path(img).stem}.mp4"
        still_clip(SLIDES / img, path, dur)
        clips.append(path)
    still_clip(SLIDES / "outro.png", CLIPS / "15_outro.mp4", durations["outro"])
    clips.append(CLIPS / "15_outro.mp4")
    base = WORK / "base_with_original_audio.mp4"
    concat_clips(clips, base)
    total = ffprobe_duration(base)
    starts = []
    acc = durations["logo"]
    starts.append(acc + 0.4)
    acc += durations["title"]
    starts.append(acc + 0.4)
    acc += durations["summary_a"] + durations["summary_b"]
    starts.append(acc + 0.6)
    acc += durations["owner"]
    starts.append(acc + 0.7)
    acc += durations["voice"]
    starts.append(acc + 0.7)
    acc += durations["expr"]
    starts.append(acc + 0.4)
    acc += durations["motion"]
    acc += durations["motion_proto"]
    starts.append(acc + 0.6)
    acc += sum(dur for _, dur in arch_items)
    # Keep narration segments from overlapping when TTS returns longer audio.
    min_gap = 0.35
    for i in range(1, len(starts)):
        starts[i] = max(starts[i], starts[i - 1] + vo[i - 1] + min_gap)
    return base, total, starts, vo


def mix_final(base, total, starts):
    inputs = ["ffmpeg", "-y", "-i", base]
    for i in range(1, 8):
        inputs += ["-i", AUDIO / f"vo_{i}.wav"]
    inputs += [
        "-f",
        "lavfi",
        "-i",
        f"sine=frequency=146:sample_rate=48000:duration={total:.3f}",
        "-f",
        "lavfi",
        "-i",
        f"sine=frequency=220:sample_rate=48000:duration={total:.3f}",
        "-f",
        "lavfi",
        "-i",
        f"sine=frequency=293.66:sample_rate=48000:duration={total:.3f}",
    ]
    parts = []
    mix_labels = []
    for i, st in enumerate(starts, 1):
        delay = int(st * 1000)
        parts.append(f"[{i}:a]aresample=48000,aformat=channel_layouts=stereo,adelay={delay}|{delay},volume=1.18[v{i}]")
        mix_labels.append(f"[v{i}]")
    pad_start = 8
    for j, vol in enumerate([0.014, 0.011, 0.008]):
        idx = pad_start + j
        label = f"p{j}"
        parts.append(
            f"[{idx}:a]aformat=channel_layouts=stereo,volume={vol},"
            f"afade=t=in:st=0:d=4,afade=t=out:st={max(0,total-7):.3f}:d=7[{label}]"
        )
        mix_labels.append(f"[{label}]")
    parts.append(
        "".join(mix_labels)
        + f"amix=inputs={len(mix_labels)}:duration=longest:normalize=0,"
        + "alimiter=limit=0.95[aout]"
    )
    filter_complex = ";".join(parts)
    run(
        inputs
        + [
            "-filter_complex",
            filter_complex,
            "-map",
            "0:v:0",
            "-map",
            "[aout]",
            "-c:v",
            "libx264",
            "-preset",
            "medium",
            "-crf",
            "20",
            "-pix_fmt",
            "yuv420p",
            "-c:a",
            "aac",
            "-b:a",
            "192k",
            "-movflags",
            "+faststart",
            FINAL,
        ]
    )


def main():
    ensure_dirs()
    render_slides()
    base, total, starts, vo = build_visual_track()
    print("voice durations:", ", ".join(f"{x:.2f}s" for x in vo))
    print("voice starts:", ", ".join(f"{x:.2f}s" for x in starts))
    print("visual duration:", f"{total:.2f}s")
    mix_final(base, total, starts)
    print("final:", FINAL)


if __name__ == "__main__":
    main()
