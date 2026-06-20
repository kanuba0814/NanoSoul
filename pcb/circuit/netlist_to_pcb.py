#!/usr/bin/env python3
"""用 KiCad pcbnew 从 circuit-synth 网表(.net) 生成 .kicad_pcb：
载入真实封装、按功能区粗放置在 Ø108mm 圆板内、连网、加圆形 Edge.Cuts。
开源版 circuit-synth 不含 PCB 生成，故本步用 KiCad 自带 pcbnew 完成。

必须用 flatpak 的 python 跑（带 pcbnew）：
  flatpak run --command=python3 org.kicad.KiCad netlist_to_pcb.py <NanoSoul.net> <out.kicad_pcb>

放置为「粗布局」：中部两排 1x20 母排 = 开发板占位（行距 22mm 占位，待实板 STEP 校准），
其余器件环形排布在圆内。大电流/关键网走线人工在 KiCad 完成。
"""
import os
import re
import sys

import pcbnew

NANOSOUL_PRETTY = "/home/gxxl/NanoSoul/pcb/libs/nanosoul.pretty"
STOCK = "/app/extensions/Library/footprints"   # flatpak sandbox 内
BOARD_DIA = 108.0
CX, CY = 150.0, 100.0          # 板心
# ESP32-P4-WIFI6 真实尺寸（Waveshare 官方尺寸图）：板宽 21.00mm、孔距长 71.05mm、
# 含 C6 模组总长 ~89mm、厚 1.61mm、排针 2.54mm；两排针行距 ~17.8mm（Pico 式）。
DEV_HALF_W = 8.9               # 两排母排半行距 = 17.8mm（匹配板宽 21mm 的边沿排针）
CUTOUT_W, CUTOUT_L = 15.5, 59.0    # 中部挖孔（让开发板底面元件/FPC/microSD 穿过；实测值）
KEEPOUT_X, KEEPOUT_Y = 11.0, 30.0  # 放置让位区半宽/半高（避开挖孔 + 母排；短件可置板下两侧）


NANOSOUL_SYM = "/home/gxxl/NanoSoul/pcb/libs/nanosoul.kicad_sym"
STOCK_SYM = "/app/extensions/Library/symbols"
SYM_LIBS = ["Device", "Connector_Generic", "Switch", "power"]   # + nanosoul


def libdir(lib):
    return NANOSOUL_PRETTY if lib == "nanosoul" else f"{STOCK}/{lib}.pretty"


def write_lib_tables(out_dir, comps):
    """写项目 sym-lib-table / fp-lib-table，使工程独立可开、ERC 干净。"""
    fp_libs = sorted({lib for lib, _ in comps.values() if lib != "nanosoul"})
    with open(os.path.join(out_dir, "sym-lib-table"), "w") as f:
        f.write("(sym_lib_table\n  (version 7)\n")
        for lib in SYM_LIBS:
            f.write(f'  (lib (name "{lib}")(type "KiCad")(uri "{STOCK_SYM}/{lib}.kicad_sym")(options "")(descr ""))\n')
        f.write(f'  (lib (name "nanosoul")(type "KiCad")(uri "{NANOSOUL_SYM}")(options "")(descr ""))\n)\n')
    with open(os.path.join(out_dir, "fp-lib-table"), "w") as f:
        f.write("(fp_lib_table\n  (version 7)\n")
        for lib in fp_libs:
            f.write(f'  (lib (name "{lib}")(type "KiCad")(uri "{STOCK}/{lib}.pretty")(options "")(descr ""))\n')
        f.write(f'  (lib (name "nanosoul")(type "KiCad")(uri "{NANOSOUL_PRETTY}")(options "")(descr ""))\n)\n')


def parse_net(path):
    txt = open(path, encoding="utf-8").read()
    comps = {}   # ref -> (lib, fpname)
    for ref, fp in re.findall(r'\(comp\s+\(ref\s+"([^"]+)"\).*?\(footprint\s+"([^"]+)"', txt, re.S):
        lib, name = fp.split(":", 1)
        comps[ref] = (lib, name)
    nets = []    # (name, [(ref, pad), ...])
    for block in txt.split("(net (code")[1:]:
        m = re.search(r'\(name\s+"([^"]*)"', block)
        if not m:
            continue
        name = m.group(1)
        nodes = re.findall(r'\(node\s+\(ref\s+"([^"]+)"\)\s+\(pin\s+"([^"]+)"\)', block)
        nets.append((name, nodes))
    return comps, nets


def grid_slots():
    """圆内、避开中部让位区的放置点（环形/网格）。"""
    slots = []
    step = 8.0
    r = BOARD_DIA / 2 - 6
    y = CY - r
    while y <= CY + r:
        x = CX - r
        while x <= CX + r:
            dx, dy = x - CX, y - CY
            if dx * dx + dy * dy <= (r - 2) ** 2 and not (abs(dx) < KEEPOUT_X and abs(dy) < KEEPOUT_Y):
                slots.append((round(x, 2), round(y, 2)))
            x += step
        y += step
    # 由外到内排序，先放外圈（连接器/电源更靠边）
    slots.sort(key=lambda p: -((p[0] - CX) ** 2 + (p[1] - CY) ** 2))
    return slots


def main():
    net_path, out_path = sys.argv[1], sys.argv[2]
    comps, nets = parse_net(net_path)
    board = pcbnew.NewBoard(out_path)

    # 网表 → 网络对象
    netmap = {}
    for name, _nodes in nets:
        if name not in netmap:
            ni = pcbnew.NETINFO_ITEM(board, name)
            board.Add(ni)
            netmap[name] = ni
    # ref+pad -> netname
    pad_net = {}
    for name, nodes in nets:
        for ref, pad in nodes:
            pad_net[(ref, pad)] = name

    # 识别两排 1x20 母排（compute_iface）
    headers = [r for r, (lib, nm) in comps.items() if "PinHeader_1x20" in nm]
    headers.sort()

    placed = set()
    loaded = 0
    missing = []

    def add_fp(ref, x, y, rot=0):
        nonlocal loaded
        lib, name = comps[ref]
        fp = pcbnew.FootprintLoad(libdir(lib), name)
        if fp is None:
            missing.append((ref, lib, name))
            return
        fp.SetReference(ref)
        fp.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
        if rot:
            fp.SetOrientationDegrees(rot)
        for pad in fp.Pads():
            key = (ref, pad.GetNumber())
            if key in pad_net:
                pad.SetNet(netmap[pad_net[key]])
        board.Add(fp)
        placed.add(ref)
        loaded += 1

    # 1) 两排母排放中部（竖排，pin1 在上）
    for i, ref in enumerate(headers[:2]):
        x = CX + (DEV_HALF_W if i else -DEV_HALF_W)
        add_fp(ref, x, CY - 25.4, rot=0)   # 居中 20×2.54=50.8mm 排针区

    # 2) 其余器件环形粗放
    slots = grid_slots()
    si = 0
    for ref in sorted(comps, key=lambda r: (r[0], int(re.sub(r"\D", "", r) or 0))):
        if ref in placed:
            continue
        if si >= len(slots):
            x, y = CX, CY     # 兜底（极少）
        else:
            x, y = slots[si]; si += 1
        add_fp(ref, x, y)

    # 3) 圆形 Edge.Cuts
    circ = pcbnew.PCB_SHAPE(board)
    circ.SetShape(pcbnew.SHAPE_T_CIRCLE)
    circ.SetLayer(pcbnew.Edge_Cuts)
    circ.SetCenter(pcbnew.VECTOR2I(pcbnew.FromMM(CX), pcbnew.FromMM(CY)))
    circ.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(CX + BOARD_DIA / 2), pcbnew.FromMM(CY)))
    circ.SetWidth(pcbnew.FromMM(0.15))
    board.Add(circ)

    # 3b) 中部矩形挖孔 15.5×59mm（Edge.Cuts 内框 → 让开发板底面元件穿过）
    cut = pcbnew.PCB_SHAPE(board)
    cut.SetShape(pcbnew.SHAPE_T_RECT)
    cut.SetLayer(pcbnew.Edge_Cuts)
    cut.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(CX - CUTOUT_W / 2), pcbnew.FromMM(CY - CUTOUT_L / 2)))
    cut.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(CX + CUTOUT_W / 2), pcbnew.FromMM(CY + CUTOUT_L / 2)))
    cut.SetWidth(pcbnew.FromMM(0.15))
    cut.SetFilled(False)
    board.Add(cut)

    board.BuildListOfNets()
    pcbnew.SaveBoard(out_path, board)
    write_lib_tables(os.path.dirname(os.path.abspath(out_path)), comps)
    print(f"✅ PCB: {loaded}/{len(comps)} 封装已放置，{len(netmap)} 网络，圆框 Ø{BOARD_DIA}mm → {out_path}")
    if missing:
        print("⚠️ 未找到封装:", missing)


if __name__ == "__main__":
    main()
