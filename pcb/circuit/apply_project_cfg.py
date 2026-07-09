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

# ②b DRC 全 62 项检查【全置 error，零 ignore】（goal 硬要求；不靠忽略任何检查项达成 0/0）。
#    达成方式 = board 侧真修，而非 ignore：
#      · footprint_type_mismatch：J2(USB-C) attr 由 SMD 改 through_hole（靠屏蔽脚 THT 安装属实；
#        仍在 CPL 贴片点内、JLC 按 LCSC 料号定贴装）→ 该检查 error 级也过。
#      · 6 项 parity(net_conflict/footprint_symbol_mismatch/…)：fix_sch_labels/fix_sch_fields/
#        fix_board_meta/assign_nc_nets 把 .kicad_sch 按 .net(真值) 逐脚修通、板 Value/lib_id 回填、
#        NC 脚配同名 unconnected 网 → 板↔原理图完全一致，error 级 parity = 0（见 docs/05 §9.2）。
#    故 _DRC_IGNORE 为空。唯一规则例外 = J2 连接器内部 DRU（④，器件自身密脚几何，非放宽布线净空）。
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
_DRC_IGNORE = set()   # 【零 ignore】——全 62 项 error；parity/type 由 board 侧脚本真修(见上注 + docs/05 §9.2)
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
    "(version 1)\n\n"
    # 下面两条【只作用于 J2 连接器内部】(A、B 两对象都属 J2)，不放宽 J2 对板上任何走线/其他器件的间距
    # (那些仍走全局 0.2mm)。依据(KiCad 几何引擎实测 + 可造性)：
    #   · 连接器相邻脚(GND↔VBUS↔CC)铜-铜最小 0.100mm = JLCPCB 标准下限,可造、非短路。
    #   · 连接器自带 NPTH 屏蔽/安装孔到 VBUS/GND 焊盘 0.185mm = 厂商安装孔几何。
    #   这是真实元件内部几何,非布线产物;任何如实 USB-C 封装都低于 KiCad 0.2/0.25 保守默认。
    '(rule "J2_usbc_internal_clearance"\n'
    "  (condition \"A.Reference == 'J2' && B.Reference == 'J2'\")\n"
    "  (constraint clearance (min 0.09mm)))\n"
    '(rule "J2_usbc_internal_hole"\n'
    "  (condition \"A.Reference == 'J2' && B.Reference == 'J2'\")\n"
    "  (constraint hole_clearance (min 0.18mm)))\n"
)
print(f"配置已打：ERC忽略 + DRC 62项全 error(零 ignore) + 边净空0.2 + Power网类({len(POWER_NETS)}网) + J2 内部 DRU")
