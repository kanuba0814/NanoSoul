"""开发板接口子电路：两排 1×20 / 2.54mm 母排座（沿长边对插 ESP32-P4-WIFI6）。
逐脚顺序 = datasheet 丝印（见 pins.LEFT_HDR / RIGHT_HDR 与 docs/BOARD_MAPPING.md）。
透传脚（P4_VBUS/EN/RUN/SDA0/SCL0/GPIO23）只到母排（连去开发板），无其它连接属正常。"""

from circuit_synth import Component, circuit
import pins as P


def _header(n, order):
    j = Component(symbol="Connector_Generic:Conn_01x20", ref="J", footprint=P.FP_HDR_1x20)
    for i, name in enumerate(order, 1):
        j[i] += n[name]      # "GND" 也走 n["GND"]，自动并到地网
    return j


@circuit(name="Compute_Iface")
def compute_iface(n):
    _header(n, P.LEFT_HDR)     # J1 左排
    _header(n, P.RIGHT_HDR)    # J2 右排
