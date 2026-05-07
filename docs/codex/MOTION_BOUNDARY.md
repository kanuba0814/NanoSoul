# MOTION_BOUNDARY.md

## 1. 目标

本文档冻结 NanoSoul PRD v2.0 的运动边界。运动系统是 Captain-owned 子系统，其他模块只能通过抽象状态和请求入口协作。

## 2. 产品抽象

当前产品抽象是三 120° 全向轮底盘。公共合约不暴露具体执行器、PWM、速度环、里程估计或底层控制参数。

## 3. 公共状态

```c
typedef enum {
    MOTION_STATE_DISABLED = 0,
    MOTION_STATE_LOCKED,
    MOTION_STATE_READY,
    MOTION_STATE_MOVING,
    MOTION_STATE_FAULT,
} motion_state_t;
```

状态语义：

- `DISABLED`：默认状态；未启用硬件路径
- `LOCKED`：硬件可用但策略禁止移动
- `READY`：可接受请求
- `MOVING`：正在执行已接受请求
- `FAULT`：运动子系统自检或执行失败

## 4. 公共请求

```c
typedef enum {
    MOTION_REQUEST_NONE = 0,
    MOTION_REQUEST_LOCK,
    MOTION_REQUEST_UNLOCK,
    MOTION_REQUEST_STOP,
    MOTION_REQUEST_MOVE_RELATIVE,
} motion_request_type_t;

typedef struct {
    motion_request_type_t type;
    int direction_degrees;
    int distance_mm;
    uint32_t duration_ms;
} motion_request_t;
```

唯一请求入口：

```c
esp_err_t motion_core_request(const motion_request_t *request);
```

## 5. Phase 0 行为

- `motion_core_init()` 将状态置为 `MOTION_STATE_DISABLED`
- `motion_core_get_state()` 返回当前抽象状态
- `motion_core_request()` 在 disabled 状态下返回失败
- 请求失败不得影响 UI、speech、sense、vision、task 启动

## 6. Policy Gate

`request_motion` action 必须经过：

1. `task_core` 规则命中
2. policy/motion gate
3. `motion_core_request()`

UI、Agent、speech 不得直接调用运动内部实现。状态页只展示 `Motion Locked / Motion Ready / Moving / Motion Fault`。

## 7. 禁止项

- 在 UI 中展示底层控制参数
- 在任务规则中写底层运动算法
- 让 presence 直接驱动运动
- 让运动失败阻断主线启动
- 普通模块直接访问运动硬件资源

