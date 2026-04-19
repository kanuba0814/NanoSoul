# P1_MOTION_SPEC.md

## 1. 目标

本文档冻结 `motion_core` 作为 P1 占位组件的边界。

一句话定义：

> `motion_core` 在 v1.0 中只允许存在“接口与状态定义”，不允许成为 MVP 主线依赖。

## 2. 允许范围

v1.0 只允许：

- yaw 舵机控制接口定义
- 脚轮驱动接口定义
- 运动状态结构体定义
- mock / stub 占位实现

当前公共状态结构为：

```c
typedef struct {
    int yaw_degrees;
    int left_wheel_percent;
    int right_wheel_percent;
    bool enabled;
} motion_state_t;
```

## 3. 默认运行规则

- 默认可编译
- 默认不启用
- 默认不在 `app_main` 启动链中承担关键路径

规则：

- 关闭时必须存在安全 stub，不能因为 motion 未接硬件就导致主线无法 build
- `request_motion` 动作在 v1.0 默认可被忽略

## 4. 硬件边界

P4 官方 `MCPWM` 适合用于舵机与电机控制，这证明 P1 路线在芯片能力上成立。

但当前仓库约束为：

- 不冻结具体舵机型号
- 不冻结电机驱动型号
- 不冻结引脚映射到 MVP 主线

这些都必须在 P1 立项时由组长重新定义。

## 5. 明确禁止项

- auto-start from `app_main`
- hard dependency for MVP demo
- board blocking integration
- 以 motion 为理由改动主线资源优先级

## 6. 参考依据

- Espressif MCPWM: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/peripherals/mcpwm.html>
- `components/motion_core/include/motion_core_types.h`
