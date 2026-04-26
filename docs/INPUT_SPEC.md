# INPUT_SPEC.md

## 1. 目标

本文档冻结 `input_core` 的硬件输入边界、事件输出和时序状态机。

主规则：

- 输入层只负责“采样、去抖、归一化、生成高层事件”
- 输入层不负责“切模式、切页面、执行业务动作”
- 任何其他模块不得重写按键去抖或长按识别

## 2. 冻结输入源

### 2.1 `SYS`

- 形态：唯一物理键
- 引脚：`GPIO3`
- 电气约束：外接、默认上拉、低有效
- 角色：安全 / 明确 / 兜底输入

### 2.2 `TOUCH_DISC`

- 形态：唯一电容触摸键
- 引脚：`GPIO2`
- 角色：日常交互主入口

说明：

- P4 触摸外设支持硬件 FSM 和连续扫描
- 本项目只使用单 pad 触摸，不做矩阵、滑条、多点交互

### 2.3 屏幕触摸

- 形态：外接 SPI touch controller
- 入口：通过 `bsp_board` 暴露给 `input_core`
- 允许页面：仅 `UI_PAGE_SETTINGS`、`UI_PAGE_STATUS`

禁止：

- 在主页或快捷菜单里把屏幕触摸做成主交互入口
- 实现多指、缩放、复杂手势

## 3. 冻结高层事件

`input_core` 只允许输出以下事件：

```c
typedef enum {
    INPUT_SYS_SHORT = 0,
    INPUT_SYS_LONG,
    INPUT_TOUCH_TAP,
    INPUT_TOUCH_DOUBLE,
    INPUT_TOUCH_LONG,
    INPUT_SCREEN_TAP,
    INPUT_SCREEN_GESTURE_SIMPLE,
} input_core_event_id_t;
```

说明：

- 事件语义由 `input_core` 一次性判定
- 其他模块只消费这些高层事件，不再接触原始电平或 raw touch count

## 4. 时序状态机冻结值

以下时序是项目级冻结常量，不是建议值：

| 项目 | 数值 |
| --- | --- |
| 去抖时间 | `30 ms` |
| 单击判定最大按下时间 | `250 ms` |
| 双击窗口 | `350 ms` |
| 长按阈值 | `800 ms` |
| 屏幕手势最小位移 | `24 px` |

规则：

- 这些值由 `input_core` 内部实现
- 其他模块不得自己再加“二次去抖”或“业务层长按”

## 5. 各输入源行为边界

### 5.1 `SYS`

只允许产生：

- `INPUT_SYS_SHORT`
- `INPUT_SYS_LONG`

说明：

- `SYS` 不参与双击
- `SYS` 是明确动作键，不承担日常菜单导航主职责

### 5.2 `TOUCH_DISC`

只允许产生：

- `INPUT_TOUCH_TAP`
- `INPUT_TOUCH_DOUBLE`
- `INPUT_TOUCH_LONG`

说明：

- `TOUCH_DISC` 是 P0 日常交互主入口
- `TOUCH_DISC` 的误触容忍应优先通过硬件阈值和软件稳定窗口解决，不得把误触处理甩给 `ui_core`

### 5.3 屏幕触摸

只允许产生：

- `INPUT_SCREEN_TAP`
- `INPUT_SCREEN_GESTURE_SIMPLE`

`INPUT_SCREEN_GESTURE_SIMPLE` 的范围冻结为：

- 单指短滑动
- 单轴滚动

不包括：

- pinch
- multi-touch
- 长拖拽排序
- 任意自定义复杂手势

## 6. 输出路径

`input_core` 的唯一输出路径是全局事件总线。

禁止：

- 直接调用 `task_core`
- 直接调用 `ui_core_show_page()`
- 直接切换 `app_core` 模式

## 7. 与 BSP 的关系

`input_core` 只允许通过 `bsp_board` 获取：

- `SYS` GPIO 输入
- `TOUCH_DISC` 触摸通道
- 屏幕 touch controller 事件

禁止：

- 直接包含板级私有头
- 自己初始化 GPIO / touch / SPI

## 8. Mock 要求

`hal_mock` 必须支持以下伪输入：

- `SYS` 短按 / 长按
- `TOUCH_DISC` 单击 / 双击 / 长按
- 屏幕点击 / 简单手势

这保证没有真实板卡时，B/C 仍能验证状态机与行为规则。

## 9. 参考依据

- Espressif Capacitive Touch Sensor: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/cap_touch_sens.html>
- `docs/BOARD_MAPPING.md`
