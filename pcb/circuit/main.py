"""NanoSoul 载板顶层：组合 电源/接口/电机/传感 四个子电路，
生成分层原理图(.kicad_sch) + 网表(.net)。

⚠️ 开源版 circuit-synth 不含 PCB(.kicad_pcb) 生成（licensed feature），
故本文件只出原理图+网表；圆板框 Ø108mm 的 PCB 由 netlist_to_pcb.py（KiCad pcbnew）
据网表生成。整条流水线见 pcb/scripts/generate.fish。
"""

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))

from circuit_synth import Net, circuit  # noqa: E402
from power import power  # noqa: E402
from motors import motors  # noqa: E402
from sensors import sensors  # noqa: E402
from compute_iface import compute_iface  # noqa: E402


class Nets:
    """按名取/建 Net 的注册表，子电路共享。"""

    def __init__(self):
        self._d = {}

    def __getitem__(self, name):
        if name not in self._d:
            self._d[name] = Net(name)
        return self._d[name]


@circuit(name="NanoSoul")
def nanosoul():
    n = Nets()
    _ = n["GND"]            # 先建地网
    power(n)
    compute_iface(n)
    motors(n)
    sensors(n)


if __name__ == "__main__":
    c = nanosoul()
    c.generate_kicad_project("NanoSoul", generate_pcb=False)
    print("✅ 原理图 + 网表已生成。PCB 用 netlist_to_pcb.py（pcbnew）据网表生成。")
