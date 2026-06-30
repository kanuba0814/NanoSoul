#!/usr/bin/env python3
"""丝印清零（坑 #12）：小密板上位号几乎都叠字/越边/压铜。稳妥做法：
  - 隐藏所有元件位号（贴片靠 CPL 坐标定位，不靠丝印；连接器功能由底部图例 annotate 标）。
  - 删母座 J3/J4 压进挖孔的丝印段。
保留: annotate 的接线图例(0.8mm，落在空区)。这样 silk DRC 清零。
flatpak python3 silk_clean.py"""
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geom as G  # noqa: E402

P = "/home/gxxl/NanoSoul/pcb/output/NanoSoul/NanoSoul.kicad_pcb"


def main():
    b = pcbnew.LoadBoard(P)
    for fp in b.GetFootprints():
        fp.Reference().SetVisible(False)
        fp.Value().SetVisible(False)
        if fp.GetReference() in ("J1", "J2", "J3", "J4"):
            # 母座/贴边连接器轮廓丝印整条删（位置靠焊盘+底部图例定义；贴挖孔/板边的段会触 silk_edge_clearance）
            for it in list(fp.GraphicalItems()):
                try:
                    if it.GetLayer() == pcbnew.F_SilkS:
                        fp.Delete(it)
                except Exception:
                    pass
    pcbnew.SaveBoard(P, b)
    print("SILK_CLEAN: all designators hidden; J3/J4 cutout silk removed")


if __name__ == "__main__":
    main()
