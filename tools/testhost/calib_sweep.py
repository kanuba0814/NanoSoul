#!/usr/bin/env python3
"""
calib_sweep — 每轮 duty→RPM 特性扫描，拟合前馈 kS/kV（docs/14 校准 A2）。

逐轮双向发 motor_test 阶梯（默认 ±15..100% duty，每档 1.5s），从 sense 流取
稳态电机轴 rpm（后半窗中位数），线性回归 rpm = a·duty + b：
    kv = 1/a   （duty / 电机轴rpm）
    ks = -b/a  （静摩擦死区 duty）
输出每轮双向表格 + 建议的 config.json motion.calib 片段 + 原始数据 JSON。

⚠️ 跑之前把机器人【架起来、三轮悬空】；建议电池供电（XL6009 轨才是真实 VM）。

用法（WS 为主——电机占用 IO24/25 后 USJ 串口不可用）：
    python3 calib_sweep.py --ws 192.168.1.xx --token <companion.token>
    python3 calib_sweep.py --ws 192.168.1.xx --m 0          # 只扫 M0
    python3 calib_sweep.py --port /dev/ttyACM1               # 串口（USJ 可用时）
"""

import argparse
import json
import statistics
import sys
import time

from ns_probe import SerialChan, WsChan  # noqa: E402  同目录复用传输层

LEVELS_PCT = [15, 20, 25, 30, 40, 50, 60, 75, 90, 100]
SETTLE_S = 0.7     # 每档丢弃前 0.7s（电机+固件 rpm EMA 的建立时间）
HOLD_MS = 1500     # 每档 motor_test 时长
GAP_S = 0.4        # 档间停轮间隔


def collect_rpm(lk, m, duty, ms):
    """发一档 motor_test，回收稳态窗内 sense 帧的 enc[m].rpm 中位数。"""
    seen = lk.mark()
    cid = lk.send(type="motor_test", m=m, duty=duty, ms=ms)
    ak = lk.ack(cid)
    if not ak or not ak.get("ok"):
        return None, (ak or {}).get("err", "no ack")
    t0 = time.time()
    samples = []
    while time.time() - t0 < ms / 1000.0 - 0.05:
        f = lk.wait_from(seen, lambda f: f.get("type") == "sense", timeout=0.5)
        if f is None:
            continue
        with lk.lock:
            seen = len(lk.frames)
        if time.time() - t0 >= SETTLE_S:
            enc = f.get("enc") or []
            if len(enc) == 3 and isinstance(enc[m], list) and len(enc[m]) >= 2:
                samples.append(float(enc[m][1]))
    if not samples:
        return None, "no sense samples (sense_rate on?)"
    return statistics.median(samples), None


def fit_line(pts):
    """最小二乘 rpm = a·duty + b。pts = [(duty, rpm)]，剔除 |rpm|<30 的死区档。"""
    live = [(d, r) for d, r in pts if abs(r) > 30.0]
    if len(live) < 3:
        return None
    n = len(live)
    sx = sum(d for d, _ in live); sy = sum(r for _, r in live)
    sxx = sum(d * d for d, _ in live); sxy = sum(d * r for d, r in live)
    den = n * sxx - sx * sx
    if abs(den) < 1e-9:
        return None
    a = (n * sxy - sx * sy) / den
    b = (sy - a * sx) / n
    return a, b


def sweep_wheel(lk, m, levels_pct, hold_ms):
    print(f"\n== M{m} ==")
    result = {"m": m, "points": []}
    for sign in (+1, -1):
        tag = "FWD" if sign > 0 else "REV"
        for pct in levels_pct:
            duty = sign * round(1023 * pct / 100)
            rpm, err = collect_rpm(lk, m, duty, hold_ms)
            if rpm is None:
                print(f"  {tag} {pct:>3}% duty={duty:+5}  !! {err}")
            else:
                warn = "  ⚠️不转" if pct >= 30 and abs(rpm) < 30 else ""
                print(f"  {tag} {pct:>3}% duty={duty:+5}  rpm={rpm:+8.0f}{warn}")
                result["points"].append([duty, rpm])
            time.sleep(GAP_S)
    # 双向分别拟合，再合并成单套 ks/kv（不对称度打印出来供判断）
    fits = {}
    for tag, sel in (("fwd", lambda d: d > 0), ("rev", lambda d: d < 0)):
        f = fit_line([(d, r) for d, r in result["points"] if sel(d)])
        if f:
            a, b = f
            fits[tag] = {"kv": 1.0 / a, "ks": -b / a, "rpm_at_full": a * 1023 + b}
    result["fits"] = fits
    if "fwd" in fits and "rev" in fits:
        kv = (abs(fits["fwd"]["kv"]) + abs(fits["rev"]["kv"])) / 2
        ks = (abs(fits["fwd"]["ks"]) + abs(fits["rev"]["ks"])) / 2
        asym = abs(abs(fits["fwd"]["kv"]) - abs(fits["rev"]["kv"])) / kv * 100
        result["kv"], result["ks"] = kv, ks
        print(f"  -> kv={kv:.5f} duty/rpm  ks={ks:.0f} duty  "
              f"双向不对称 {asym:.1f}%  满duty≈{abs(fits['fwd']['rpm_at_full']):.0f}rpm")
    else:
        print("  -> 拟合失败（有效点不足；检查接线/编码器）")
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--ws", help="companion WS ip（主用通道）")
    ap.add_argument("--token", default="", help="companion.token")
    ap.add_argument("--port", help="serial port（USJ 可用时）")
    ap.add_argument("--m", type=int, choices=[0, 1, 2], help="只扫单轮")
    ap.add_argument("--hold", type=int, default=HOLD_MS, help="每档时长 ms")
    ap.add_argument("--out", default="calib_sweep_result.json", help="原始数据输出")
    ap.add_argument("--yes", action="store_true", help="跳过安全确认")
    args = ap.parse_args()

    if not args.yes:
        input("确认机器人已【架起、三轮悬空】且电池供电？回车继续（Ctrl-C 取消）")

    if args.ws:
        lk = WsChan(args.ws, args.token)
    elif args.port:
        lk = SerialChan(args.port)
    else:
        sys.exit("需要 --ws <ip> 或 --port <dev>")

    lk.ack(lk.send(type="sense_rate", hz=20))
    wheels = [args.m] if args.m is not None else [0, 1, 2]
    results = []
    try:
        for m in wheels:
            results.append(sweep_wheel(lk, m, LEVELS_PCT, args.hold))
    finally:
        lk.send(type="motor_stop")
        lk.send(type="sense_rate", hz=0)

    with open(args.out, "w") as f:
        json.dump(results, f, indent=1)
    print(f"\n原始数据 -> {args.out}")

    if all("kv" in r for r in results) and len(results) == 3:
        ks = [round(r["ks"], 1) for r in results]
        kv = [round(r["kv"], 5) for r in results]
        rpm_full = min(abs(r["fits"]["fwd"]["rpm_at_full"]) for r in results)
        print("\n建议 config.json motion.calib 片段（也回填代码默认值）：")
        print(json.dumps({"ks": ks, "kv": kv,
                          "rpm_max": int(rpm_full * 0.95 // 100 * 100)},
                         ensure_ascii=False))
        print("（rpm_max 取三轮满 duty 转速最小值的 95%，保证设定值可达）")


if __name__ == "__main__":
    main()
