# UI_SPEC.md

## 1. 目标

本文档冻结 NanoSoul 的 UI 页面集合、页面跳转、状态展示和输入边界。

主规则：

- UI 只通过 `ui_core` 对外暴露
- 其他模块不得直接操作 LVGL 对象
- UI 是呈现层，不是行为决策中心

## 2. 页面集合

```c
typedef enum {
    UI_PAGE_HOME = 0,
    UI_PAGE_QUICK_MENU,
    UI_PAGE_SETTINGS,
    UI_PAGE_STATUS,
} ui_page_t;
```

页面职责：

- `UI_PAGE_HOME`：表情主页
- `UI_PAGE_QUICK_MENU`：快捷入口
- `UI_PAGE_SETTINGS`：有限设置页
- `UI_PAGE_STATUS`：状态页

## 3. 状态页设备列表

状态页必须覆盖：

- `LCD`
- `Touch`
- `Audio`
- `SD`
- `Wi-Fi-C6`
- `Camera`
- `BH1750`
- `VL6180X-L`
- `VL6180X-C`
- `VL6180X-R`
- `IMU`
- `Motion`

对应公共枚举：

```c
typedef enum {
    UI_STATUS_ITEM_LCD = 0,
    UI_STATUS_ITEM_TOUCH,
    UI_STATUS_ITEM_AUDIO,
    UI_STATUS_ITEM_SD,
    UI_STATUS_ITEM_WIFI_C6,
    UI_STATUS_ITEM_CAMERA,
    UI_STATUS_ITEM_BH1750,
    UI_STATUS_ITEM_VL6180X_L,
    UI_STATUS_ITEM_VL6180X_C,
    UI_STATUS_ITEM_VL6180X_R,
    UI_STATUS_ITEM_IMU,
    UI_STATUS_ITEM_MOTION,
} ui_status_item_t;
```

## 4. Motion 展示

UI 只允许展示抽象文案：

- `Motion Locked`
- `Motion Ready`
- `Moving`
- `Motion Fault`

不展示运动内部参数。

## 5. 状态条内容

状态条只显示轻量系统状态：

- 当前模式
- Wi-Fi 状态
- audio profile
- SD/storage 状态
- 健康状态

不显示复杂图表，不显示 raw register。

## 6. 输入边界

- 日常交互主入口：`TOUCH_DISC`
- `SYS`：明确动作与安全兜底
- 屏幕触摸：只在 `SETTINGS` / `STATUS` 启用

## 7. 与其他模块的边界

- `ui_core` 可以消费稳定状态
- `ui_core` 不得直接调用 `task_core` 主动调度
- `ui_core` 不得直接访问 `bsp_board`
- `ui_core` 不得拥有业务规则

