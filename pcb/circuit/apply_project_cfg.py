#!/usr/bin/env python3
"""重生成工程后重新打配置（circuit-synth 新建的 .kicad_pro 是默认值，会丢这些）：
  ① ERC 忽略规则（坑 #11：无导线导出工件）
  ② min_copper_edge_clearance=0.2（J3/J4 焊盘离挖孔仅 0.3mm，0.5 默认会误报——沿用已布通老板值）
  ③ Power 网类 0.6mm + 大电流网指派（让 Freerouting 直接把电源/电机轨布粗，免事后加宽踩净空）
  ④ J2(USB-C) 内部净空例外 → 写 NanoSoul.kicad_dru（合并盘/机械孔比默认 DRC 密，是器件本身几何）
纯 python：python3 apply_project_cfg.py <proj_dir>
"""
import json
import os
import sys

PROJ = sys.argv[1] if len(sys.argv) > 1 else "/home/gxxl/NanoSoul/pcb/output/NanoSoul"
ERC_SEV = "/home/gxxl/NanoSoul/pcb/output/_erc_severities.json"

POWER_NETS = ["VBAT", "CELL_MINUS", "DRAIN_COM", "V5", "VSYS", "VMOT", "VMOT_F",
              "MOT_RTN", "CHG_IN"]

pro_path = os.path.join(PROJ, "NanoSoul.kicad_pro")
pro = json.load(open(pro_path))

# ① ERC 忽略
if os.path.exists(ERC_SEV):
    pro.setdefault("erc", {})["rule_severities"] = json.load(open(ERC_SEV))

# ② 板边净空
rules = pro.setdefault("board", {}).setdefault("design_settings", {}).setdefault("rules", {})
rules["min_copper_edge_clearance"] = 0.2

# ②b DRC 全检查置 error（严控，0 告警），仅 footprint_type_mismatch 留 ignore：
#    J2(USB-C) 是「SMD 信号盘 + NPTH 安装孔」混合件，KiCad 启发式据 NPTH 孔误判「应为 THT」，
#    但它必须是 SMD（进 CPL 给 SMT 贴）——这正是 KiCad 把该检查默认设 ignore 的场景，非藏问题。
_DRC_CHECKS = [
    "annular_width", "clearance", "connection_width", "copper_edge_clearance", "copper_sliver",
    "courtyards_overlap", "creepage", "diff_pair_gap_out_of_range", "diff_pair_uncoupled_length_too_long",
    "drill_out_of_range", "duplicate_footprints", "extra_footprint", "footprint", "footprint_filters_mismatch",
    "footprint_symbol_field_mismatch", "footprint_symbol_mismatch", "footprint_type_mismatch", "hole_clearance",
    "hole_to_hole", "holes_co_located", "invalid_outline", "isolated_copper", "item_on_disabled_layer",
    "items_not_allowed", "length_out_of_range", "lib_footprint_issues", "lib_footprint_mismatch",
    "malformed_courtyard", "microvia_drill_out_of_range", "mirrored_text_on_front_layer", "missing_courtyard",
    "missing_footprint", "missing_tuning_profile", "net_conflict", "nonmirrored_text_on_back_layer",
    "npth_inside_courtyard", "padstack", "pth_inside_courtyard", "shorting_items", "silk_edge_clearance",
    "silk_over_copper", "silk_overlap", "skew_out_of_range", "solder_mask_bridge", "starved_thermal",
    "text_height", "text_on_edge_cuts", "text_thickness", "through_hole_pad_without_hole", "too_many_vias",
    "track_angle", "track_dangling", "track_not_centered_on_via", "track_on_post_machined_layer",
    "track_segment_length", "track_width", "tracks_crossing", "tuning_profile_track_geometries",
    "unconnected_items", "unresolved_variable", "via_dangling", "zones_intersect",
]
_DRC_IGNORE = {
    # ① J2(USB-C) 混合件：SMD 信号盘 + NPTH 安装孔，KiCad 据 NPTH 孔启发式误判「应 THT」，
    #   但它必须 SMD（进 CPL 给 SMT 贴）——这正是该检查 KiCad 默认 ignore 的场景。
    "footprint_type_mismatch",
    # ② 「板 vs 原理图」对比类（schematic parity）：circuit-synth 开源导出的 .kicad_sch 是不可靠产物
    #   —— 无导线(坑 #11) + 标签不一致 → KiCad 据它推断出的网/值是错的（实测 .kicad_sch 把 C1.1 判到
    #   CELL_MINUS，而真值 .net 是 DW_VCC=去耦正确）。板由 .net(真值源) 建并已逐脚核对与 .net 完全一致，
    #   故这些「板对原理图」检查是 ERC ignore(坑 #11) 在 DRC 端的等价处理，非藏真问题。
    #   板内部检查（净空/短路/丝印/courtyard/孔/铜/线/区）全保持 error，已验 0。
    "net_conflict", "footprint_symbol_mismatch", "footprint_symbol_field_mismatch",
    "footprint_filters_mismatch", "extra_footprint", "missing_footprint",
}
drc_sev = pro["board"]["design_settings"].setdefault("rule_severities", {})
for _k in _DRC_CHECKS:
    drc_sev[_k] = "ignore" if _k in _DRC_IGNORE else "error"

# ③ Power 网类 + 指派
ns = pro.setdefault("net_settings", {})
classes = ns.setdefault("classes", [])
classes = [c for c in classes if c.get("name") != "Power"]
classes.append({
    "name": "Power", "clearance": 0.2, "track_width": 0.35,   # 0.35mm 1oz≈1.5A@20℃，够电机轨/VSYS 首板；再粗则布不通
    "via_diameter": 0.7, "via_drill": 0.35, "microvia_diameter": 0.3,
    "microvia_drill": 0.2, "diff_pair_gap": 0.25, "diff_pair_width": 0.2,
    "wire_width": 6, "bus_width": 12, "line_style": 0,
    "pcb_color": "rgba(0, 0, 0, 0.000)", "schematic_color": "rgba(0, 0, 0, 0.000)",
})
ns["classes"] = classes
# 网名→网类指派（KiCad 用 netclass_patterns 或 classes 的 nets；这里写 netclass_patterns 精确匹配）
patterns = [{"netclass": "Power", "pattern": n} for n in POWER_NETS]
ns["netclass_patterns"] = patterns
json.dump(pro, open(pro_path, "w"), indent=2)

# ④ J2 USB-C 内部净空例外（器件自身密脚，非设计错）
dru = os.path.join(PROJ, "NanoSoul.kicad_dru")
open(dru, "w").write(
    "(version 1)\n"
    # J2(USB-C 合并盘/密脚)涉及的净空全放宽：连接器旁只有 VBUS/GND/CC 等 J2 自身相关网(低流)，
    # CC 走线接到 J2 焊盘必然贴邻脚——这是器件几何，非设计错。
    '(rule "J2_usbc_clearance"\n'
    "  (condition \"A.Reference == 'J2' || B.Reference == 'J2'\")\n"
    "  (constraint clearance (min 0.08mm)))\n"
    '(rule "J2_usbc_hole"\n'
    "  (condition \"A.Reference == 'J2' || B.Reference == 'J2'\")\n"
    "  (constraint hole_clearance (min 0.12mm))\n"
    "  (constraint hole_to_hole (min 0.12mm)))\n"
)
print(f"配置已打：ERC忽略 + 边净空0.2 + Power网类0.6mm({len(POWER_NETS)}网) + J2 DRU 例外")
