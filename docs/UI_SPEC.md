# UI_SPEC.md

## 1. 目标

本文档冻结 MVP 的 UI 页面集合、页面跳转、状态展示和输入边界。

主规则：

- UI 只通过 `ui_core` 对外暴露
- 其他模块不得直接操作 LVGL 对象
- UI 是呈现层，不是行为决策中心

## 2. 冻结页面集合

当前页面枚举冻结为：

```c
typedef enum {
    UI_PAGE_HOME = 0,
    UI_PAGE_QUICK_MENU,
    UI_PAGE_SETTINGS,
    UI_PAGE_STATUS,
} ui_page_t;
```

页面职责：

- `UI_PAGE_HOME`：表情主页 / 默认主屏
- `UI_PAGE_QUICK_MENU`：快捷入口
- `UI_PAGE_SETTINGS`：设置页
- `UI_PAGE_STATUS`：系统状态页

不允许新增：

- 多层应用桌面
- 聊天窗口体系
- 复杂多标签页框架

## 3. 页面跳转边界

推荐主路径冻结为：

```text
HOME <-> QUICK_MENU -> SETTINGS
HOME <-> QUICK_MENU -> STATUS
```

规则：

- 默认落点始终是 `HOME`
- `SETTINGS` 和 `STATUS` 是功能页，不是常驻主页
- 页面切换只允许通过 `ui_core_show_page(ui_page_t page)`

## 4. 输入边界

- 日常交互主入口：`TOUCH_DISC`
- `SYS`：明确动作与安全兜底
- 屏幕触摸：只在 `SETTINGS` / `STATUS` 启用

禁止：

- 把屏幕触摸做成日常主入口
- 让其他模块绕过 `ui_core` 直接绑触摸回调

## 5. 状态条冻结内容

状态条只允许显示轻量系统状态：

- 当前模式
- Wi-Fi 状态
- 音频 profile（正常 / 静音 / 夜间）
- TF / storage 状态
- 健康状态（`OK / WARN / ERROR`）

规则：

- 不显示复杂图表
- 不显示调试级原始传感器值

## 6. 主页表情冻结映射

表情资源目录已冻结为：

- `assets/faces/idle/`
- `assets/faces/listen/`
- `assets/faces/speak/`
- `assets/faces/sleep/`
- `assets/faces/alert/`

页面层语义映射冻结为：

- `APP_MODE_IDLE` -> `idle`
- `APP_MODE_AWAKE` -> `listen`
- `APP_MODE_FOCUS` -> `speak`
- `APP_MODE_SLEEP` -> `sleep`
- `DIAG_HEALTH_WARN/ERROR` 或提醒强调 -> `alert`

## 7. 快捷菜单冻结范围

`UI_PAGE_QUICK_MENU` 只允许出现轻量入口：

- 音量 profile
- 夜间模式 / 免打扰
- 网络状态入口
- 设置页入口
- 状态页入口

禁止：

- 复杂子菜单树
- 多页面 app launcher

## 8. 设置页冻结范围

`UI_PAGE_SETTINGS` 只允许处理：

- 网络配置入口
- soul preset 选择
- 音量 / 夜间 profile
- 基础设备设置

规则：

- 设置页是有限表单，不是通用配置平台
- WebUI 与屏幕设置页只做配置，不做业务主脑

## 9. 状态页冻结范围

`UI_PAGE_STATUS` 只允许展示：

- Wi-Fi / BLE 配网状态
- 存储状态
- 传感器简要状态
- 视觉 / 语音可用性状态
- 最近健康状态

禁止：

- 实时视频流
- 复杂图像分析可视化
- 开发者调试台

## 10. 视觉风格冻结

- 主题：单色主题
- 视觉重点：表情 + 少量高对比状态信息
- 动效：只允许轻量切页与表情过渡

明确禁止：

- 彩色复杂主题系统
- 大量自定义 widget 框架移植
- 页面级重动画堆叠

## 11. 与其他模块的边界

- `ui_core` 可以消费 `app_core` / `task_core` / `diag_core` / `net_core` 的稳定状态
- `ui_core` 不得直接调用 `task_core`
- `ui_core` 不得直接访问 `bsp_board`
- `ui_core` 不得拥有业务规则

## 12. 参考依据

- `components/ui_core/include/ui_core.h`
- `components/ui_core/include/ui_core_types.h`
- `docs/INPUT_SPEC.md`
- `docs/MODULE_CONTRACTS.md`
