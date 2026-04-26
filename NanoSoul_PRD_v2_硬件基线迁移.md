# NanoSoul PRD v2.1：从 testP4 迁移硬件基线到 NanoSoul

> 面向执行者：Codex / 工程实现代理  
> 目标仓库：NanoSoul  
> 来源仓库/工程：testP4  
> 当前状态：testP4 已验证屏幕、触摸、BH1750 光传感器、喇叭、SD 卡、相机、Wi-Fi 可同时使用且无资源冲突  
> 本 PRD 目标：把 testP4 中已验证的 ESP32-P4 硬件基线迁入 NanoSoul，形成正式产品工程主线  
> 明确不做：运动控制闭环、自动回充、dock、Agent 完整云端规划、最终 UI 视觉重设计

---

## 1. 背景与决策

NanoSoul 当前应从“架构空壳 / mock 框架”进入“真实硬件基线”阶段。testP4 已经完成关键板级资源验证：

- 屏幕与触摸可用
- BH1750 光传感器可用
- 喇叭 / 音频输出可用
- SD 卡读写可用
- 相机可用
- Wi-Fi / ESP32-C6 网络链路可用
- 上述模块可同时运行，未出现明显引脚、总线或外设冲突

因此，应把 testP4 中已验证的 BSP、driver、sdkconfig、初始化顺序迁移进 NanoSoul。

核心决策：

```text
不要把 testP4 整个覆盖到 NanoSoul。
testP4 继续作为硬件实验场。
NanoSoul 作为正式产品主线。
本次只迁移已验证硬件基线，不新增产品功能。
```

---

## 2. 本次 PR 目标

建议分支名：

```text
feat/p4-hw-baseline-from-testP4
```

建议 PR 标题：

```text
feat: migrate verified ESP32-P4 hardware baseline from testP4
```

目标：

1. NanoSoul 可在 ESP32-P4 真板上启动。
2. NanoSoul 复现 testP4 已验证硬件能力。
3. NanoSoul 形成正式 BSP / driver / module status 基线。
4. 非关键外设失败时系统不崩溃，而是在诊断页显示 `ABSENT` / `ERROR` / `DISABLED`。
5. 为后续 UI、task、sense、vision、speech、net 开发提供真实硬件基础。

---

## 3. 当前范围

### 3.1 本次必须迁移

| 类别 | 模块 | 状态目标 |
|---|---|---|
| 显示 | LCD / MIPI DSI / ST7701S 相关驱动 | 可显示最小诊断 UI |
| 触摸 | FT6x36 或 testP4 当前触摸驱动 | 可读点位 / 可产生触摸事件 |
| UI 基础 | LVGL port | 可运行最小页面 |
| 光照 | BH1750 | 可读 lux 或显示错误状态 |
| 音频 | ES8311 / I2S / speaker 播放链路 | 可播放测试音 |
| 存储 | SD / FATFS / 文件读写 | 可 mount / list / read-write |
| 相机 | camera / esp_video / MIPI-CSI | 可初始化 / 抓帧 / 进入诊断页 |
| 网络 | ESP32-C6 hosted Wi-Fi | 可初始化 / 显示连接状态 |
| 板级 | pin map / I2C / 电源相关初始化 | 形成正式硬件基线 |
| 配置 | sdkconfig.defaults / idf_component.yml | 与上述硬件能力匹配 |
| 诊断 | diag page / module status | 显示各模块状态 |

### 3.2 本次明确不做

```text
不做运动闭环
不做三全向轮底盘控制
不做自动回充
不做 dock_return / charging / 桌角重上电
不做最终产品 UI 重设计
不做完整 Agent 云端规划
不做本地 LLM / 本地 RAG
不让 task_core 直接操作硬件
不让 ui_core 直接读底层 driver
```

### 3.3 保留但默认关闭

```text
motion_core 默认 DISABLED
VL6180X 三路 ToF 可以保留状态字段，但未接入时显示 ABSENT
Agent / planner 只保留接口，不接云端完整逻辑
```

---

## 4. 产品与工程边界

### 4.1 testP4 的角色

testP4 是硬件实验工程，用于验证：

```text
新外设
新 pin
新驱动
新 sdkconfig
硬件冲突
```

后续新增硬件应先在 testP4 验证，再按组件边界迁入 NanoSoul。

### 4.2 NanoSoul 的角色

NanoSoul 是正式产品工程，用于沉淀：

```text
组件化架构
正式 BSP
正式 UI / task / sense / vision / speech / net
模块状态管理
回归测试
参赛演示主线
```

NanoSoul 不应成为临时试验代码集合。

---

## 5. 目标目录结构

本次迁移后，NanoSoul 至少应形成以下结构：

```text
main/
  app_main.c
  app_startup.c
  app_startup.h

components/
  app_core/
  bsp_board/
    include/
      bsp_board.h
      bsp_board_types.h
      bsp_board_pins.h
    src/
      bsp_board.c
      bsp_i2c.c
      bsp_i2c_ext.c
      bsp_display_st7701.c
      bsp_touch_ft6x36.c
  ui_core/
    include/
      ui_core.h
      ui_lvgl_port.h
    src/
      ui_core.c
      ui_lvgl_port.c
      pages/
        diag_home.c
        diag_touch.c
        diag_sensors.c
        diag_camera.c
        diag_wifi.c
        diag_audio.c
        diag_storage.c
  sense_core/
    include/
      sense_core.h
      sense_types.h
    src/
      sense_core.c
      drivers/
        bh1750/
          bh1750.c
          bh1750.h
        vl6180x/
          vl6180x.c
          vl6180x.h
  audio_core/
    include/
      audio_core.h
    src/
      audio_core.c
      audio_player.c
  storage_core/
    include/
      storage_core.h
    src/
      storage_core.c
      sd_storage.c
  vision_core/
    include/
      vision_core.h
    src/
      vision_core.c
      drivers/
        camera/
          camera_driver.c
          camera_driver.h
  net_core/
    include/
      net_core.h
    src/
      net_core.c
      app_wifi.c
  diag_core/
    include/
      diag_core.h
    src/
      diag_core.c
  motion_core/
    include/
      motion_core.h
    src/
      motion_core.c
```

说明：

- `bsp_board` 可以知道具体硬件型号和 GPIO。
- `ui_core` 只负责显示和交互页面，不直接读底层硬件。
- `sense_core` 统一管理 BH1750、VL6180X 等传感器。
- `motion_core` 只保留默认禁用状态，不实现底盘控制。
- `diag_core` 汇总模块状态给 UI。

---

## 6. 迁移映射

| testP4 内容 | NanoSoul 目标位置 | 备注 |
|---|---|---|
| `board_pins.h` | `components/bsp_board/include/bsp_board_pins.h` | 正式 pin freeze |
| 板载 I2C 初始化 | `components/bsp_board/src/bsp_i2c.c` | 屏幕触摸 / codec 等板载设备 |
| 外部 I2C 初始化 | `components/bsp_board/src/bsp_i2c_ext.c` | BH1750 / VL6180X |
| ST7701S 显示驱动 | `components/bsp_board/src/bsp_display_st7701.c` | 以 testP4 实测为准 |
| FT6x36 触摸驱动 | `components/bsp_board/src/bsp_touch_ft6x36.c` | 以 testP4 实测为准 |
| LVGL port | `components/ui_core/src/ui_lvgl_port.c` | 不放在 main |
| 临时测试 UI | `components/ui_core/src/pages/diag_*.c` | 作为诊断页，不作为最终产品 UI |
| BH1750 driver | `components/sense_core/src/drivers/bh1750/` | 统一由 sense_core 包装 |
| audio player | `components/audio_core/src/` | 喇叭测试与提示音接口 |
| SD storage | `components/storage_core/src/` | mount / list / read / write |
| camera driver | `components/vision_core/src/drivers/camera/` | 先作为 camera bring-up |
| Wi-Fi app | `components/net_core/src/` | 网络状态由 net_core 输出 |
| sdkconfig.defaults | NanoSoul 根目录 | 合并，不直接覆盖 |
| idf_component.yml | NanoSoul main 或根组件 | 合并依赖 |
| testP4 main 初始化流程 | `main/app_startup.c` | 参考流程，禁止大 main 化 |

不得迁移：

```text
build/
managed_components/
sdkconfig.old
.claude/
临时日志文件
临时实验脚本
自动回充相关占位内容
```

---

## 7. 硬件状态模型

### 7.1 通用硬件状态

新增或统一：

```c
typedef enum {
    HW_STATUS_ABSENT = 0,
    HW_STATUS_OK,
    HW_STATUS_STALE,
    HW_STATUS_ERROR,
    HW_STATUS_DISABLED,
} hw_status_t;
```

语义：

| 状态 | 含义 |
|---|---|
| `HW_STATUS_OK` | 已初始化且可用 |
| `HW_STATUS_ABSENT` | 硬件未接入或未检测到 |
| `HW_STATUS_STALE` | 曾可用但数据过期 |
| `HW_STATUS_ERROR` | 初始化或运行错误 |
| `HW_STATUS_DISABLED` | 软件配置禁用 |

### 7.2 板级状态

新增：

```c
typedef struct {
    hw_status_t display;
    hw_status_t touch;
    hw_status_t audio;
    hw_status_t storage;
    hw_status_t camera;
    hw_status_t wifi;

    hw_status_t i2c_board;
    hw_status_t i2c_ext;

    hw_status_t bh1750;
    hw_status_t vl6180x_l;
    hw_status_t vl6180x_c;
    hw_status_t vl6180x_r;

    hw_status_t motion;
} bsp_board_status_t;
```

### 7.3 状态访问接口

`bsp_board` 提供：

```c
const bsp_board_status_t *bsp_board_get_status(void);
hw_status_t bsp_board_get_display_status(void);
hw_status_t bsp_board_get_touch_status(void);
hw_status_t bsp_board_get_audio_status(void);
hw_status_t bsp_board_get_storage_status(void);
hw_status_t bsp_board_get_camera_status(void);
hw_status_t bsp_board_get_wifi_status(void);
```

`diag_core` 从各模块收集状态，不直接访问私有 driver。

---

## 8. 初始化顺序

`main/app_main.c` 应保持极简：

```c
void app_main(void)
{
    app_startup_init();
    app_startup_run();
}
```

`app_startup_init()` 推荐顺序：

```c
esp_err_t app_startup_init(void)
{
    log_core_init();

    bsp_board_init();        // GPIO, I2C, display/touch base, board status

    storage_core_init();     // non-fatal
    audio_core_init();       // non-fatal
    sense_core_init();       // BH1750, VL6180X later; non-fatal
    net_core_init();         // C6 hosted Wi-Fi; non-fatal
    vision_core_init();      // camera; non-fatal

    ui_core_init();          // fatal only if display unavailable
    diag_core_init();        // module status page

    app_core_init();
    input_core_init();
    soul_core_init();
    task_core_init();
    speech_core_init();

#if CONFIG_NANOSOUL_ENABLE_MOTION_CORE
    motion_core_init();      // default DISABLED
#endif

    return ESP_OK;
}
```

要求：

- LCD / LVGL 失败可以视为致命，因为无法完成主展示。
- Touch / BH1750 / Audio / SD / Camera / Wi-Fi 失败不应导致整机重启。
- 非关键模块失败必须进入状态页。
- 禁止用 `ESP_ERROR_CHECK()` 包住所有非关键模块初始化。

---

## 9. 模块失败策略

| 模块 | 失败策略 | UI 状态 |
|---|---|---|
| LCD / LVGL | 致命或进入最小错误日志 | `ERROR` |
| Touch | 非致命 | `ABSENT` / `ERROR` |
| BH1750 | 非致命 | `ABSENT` / `ERROR` |
| Audio | 非致命 | `ABSENT` / `ERROR` |
| SD | 非致命 | `ABSENT` / `ERROR` |
| Camera | 非致命 | `ABSENT` / `ERROR` |
| Wi-Fi | 非致命 | `DISABLED` / `ERROR` / `OK` |
| VL6180X-L/C/R | 当前可缺失 | `ABSENT` |
| Motion | 默认关闭 | `DISABLED` |

---

## 10. 组件边界要求

### 10.1 bsp_board

允许知道：

```text
GPIO
I2C port
MIPI DSI
ST7701S
FT6x36
ES8311
BH1750 总线位置
VL6180X XSHUT / 地址策略
SDMMC pin
Camera pin / MIPI-CSI config
ESP32-C6 hosted Wi-Fi 基础链路
```

### 10.2 ui_core

只能做：

```text
初始化 LVGL
显示页面
切换状态
显示模块状态
响应触摸事件
```

禁止：

```text
直接读 BH1750
直接 mount SD
直接初始化 camera
直接启动 Wi-Fi
直接操作 PWM / motor
```

### 10.3 sense_core

负责：

```text
BH1750 读数
VL6180X 三路状态与距离读取
传感器错误隔离
传感器数据时间戳
```

本次 VL6180X 可只保留框架和 ABSENT 状态，具体接入可后续完成。

### 10.4 task_core

只能消费：

```text
world_state
event bus
module status
```

禁止直接操作硬件。

### 10.5 motion_core

本次只要求：

```text
默认 DISABLED
提供 motion 状态
不实现三全向轮真实控制
不实现自动回充
不被其他模块强依赖
```

---

## 11. sdkconfig 与依赖要求

### 11.1 合并原则

从 testP4 合并到 NanoSoul：

```text
PSRAM / cache 配置
MIPI DSI / LCD 配置
LVGL 配置
Camera / esp_video 配置
Audio codec / I2S 配置
SDMMC / FATFS 配置
ESP32-C6 hosted Wi-Fi 配置
FreeRTOS task stack / tick 配置
Partition table 配置
```

禁止直接覆盖 NanoSoul 的 `sdkconfig.defaults`。必须人工合并。

### 11.2 idf_component.yml 依赖

按 testP4 实际依赖合并。候选项：

```yaml
dependencies:
  idf: ">=5.5"
  lvgl/lvgl: "9.2.2"
  espressif/esp_lcd_st7701: "^2.0.0"
  espressif/esp_codec_dev: "^1.5"
  espressif/esp_video: "^0.8"
  espressif/esp_wifi_remote: "^1.5.1"
  espressif/esp_hosted: "^2.12.6"
```

以 testP4 当前能 build 的实际版本为准。

### 11.3 Flash size

不要盲目复制 flash size。执行：

```bash
idf.py flash_id
```

根据真实结果确认：

```text
16MB 或 32MB
```

---

## 12. 诊断 UI 要求

本次必须提供一个最小诊断 UI，用于证明硬件基线迁移成功。

### 12.1 首页显示

```text
NanoSoul HW Baseline
Display: OK
Touch: OK
Audio: OK / ERROR
SD: OK / ERROR
Camera: OK / ERROR
Wi-Fi: OK / ERROR / DISABLED
BH1750: OK / ERROR / ABSENT
VL6180X-L: ABSENT
VL6180X-C: ABSENT
VL6180X-R: ABSENT
Motion: DISABLED
```

### 12.2 诊断页面

至少包含：

```text
Touch test page
Sensor page
Audio test page
SD card page
Camera page
Wi-Fi page
```

### 12.3 交互方式

允许使用：

```text
屏幕触摸
物理按键
串口命令
```

当前不要求最终产品级 UI 动效。

---

## 13. 迁移执行步骤

Codex 应按以下顺序执行。每一步必须 build，避免一次性大搬迁。

### Step 0：准备

```bash
git checkout -b feat/p4-hw-baseline-from-testP4
```

确认 testP4 已经有当前可用提交，建议打 tag：

```bash
git tag hw-baseline-screen-touch-audio-sd-camera-wifi-ok
```

### Step 1：迁移 pin map

- 从 testP4 拿出 `board_pins.h`
- 放入 `components/bsp_board/include/bsp_board_pins.h`
- 更新 include 路径
- 不改变当前已验证 GPIO

验收：

```bash
idf.py build
```

### Step 2：迁移 I2C

- 迁移板载 I2C
- 迁移外部 I2C
- 提供 `bsp_i2c_init()` / `bsp_i2c_ext_init()`

验收：

```text
build 通过
启动日志显示 I2C init OK 或明确错误
```

### Step 3：迁移 LCD / Touch / LVGL

- 迁移 ST7701S 显示初始化
- 迁移 FT6x36 触摸初始化
- 迁移 LVGL port
- 显示一个最小页面

验收：

```text
真板显示 NanoSoul HW Baseline 页面
触摸有日志输出或页面反馈
```

### Step 4：迁移 SD

- 迁移 SDMMC / FATFS 初始化
- 提供 `storage_core_init()`
- 提供 list / read / write 测试接口

验收：

```text
状态页显示 SD: OK
串口日志可列出文件或创建测试文件
```

### Step 5：迁移 Audio

- 迁移 codec / I2S / speaker 播放链路
- 提供 `audio_core_play_test_tone()` 或等价接口

验收：

```text
状态页显示 Audio: OK
可播放测试音
```

### Step 6：迁移 BH1750

- 迁移 BH1750 driver
- 放入 `sense_core`
- 输出 lux 值与状态

验收：

```text
状态页显示 BH1750: OK
Sensor page 显示 lux 数值
```

### Step 7：迁移 Camera

- 迁移 camera / esp_video 初始化
- 提供最小抓帧或 camera diag

验收：

```text
状态页显示 Camera: OK
日志显示 frame captured 或 camera ready
```

### Step 8：迁移 Wi-Fi

- 迁移 ESP32-C6 hosted Wi-Fi 初始化
- 提供网络状态

验收：

```text
状态页显示 Wi-Fi 状态
本地功能不依赖 Wi-Fi 成功
```

### Step 9：统一 diag_core

- 汇总所有模块状态
- UI 页面读取 diag_core，而不是直接访问底层 driver

验收：

```text
状态页完整显示全部模块状态
任一非关键模块断开时不崩溃
```

### Step 10：清理临时代码

删除或隔离：

```text
testP4 风格大 main
临时硬编码菜单
重复 driver
重复 pin 定义
自动回充相关占位
```

---

## 14. 验收标准

### 14.1 build 验收

```bash
idf.py set-target esp32p4
idf.py build
```

必须通过。

### 14.2 flash / monitor 验收

```bash
idf.py flash monitor
```

必须看到：

```text
NanoSoul boot
bsp_board init
display OK
touch OK or ERROR
storage OK or ERROR
audio OK or ERROR
sense OK or ERROR
camera OK or ERROR
wifi OK or ERROR
ui_core ready
diag_core ready
```

### 14.3 真板功能验收

必须验证：

| 项 | 通过标准 |
|---|---|
| LCD | 显示诊断首页 |
| Touch | 触摸有响应或日志 |
| BH1750 | 有 lux 值或明确错误状态 |
| Speaker | 可播放测试音 |
| SD | 可 mount / list / write |
| Camera | 可初始化 / 抓帧 |
| Wi-Fi | 可初始化 / 显示状态 |
| VL6180X | 当前未接时显示 ABSENT |
| Motion | 显示 DISABLED |

### 14.4 稳定性验收

要求：

```text
连续启动 5 次不随机崩溃
Wi-Fi 失败不影响 UI
SD 缺卡不影响 UI
BH1750 缺失不影响 UI
Camera 初始化失败不影响 UI
Audio 失败不影响 UI
```

---

## 15. Codex 实施约束

Codex 执行时必须遵守：

```text
一次只迁移一个子系统
每步都 build
不要重构业务功能
不要新增自动回充相关模块
不要修改运动架构实现
不要让 main.c 变成大 main
不要让 UI 直接依赖 driver
不要让 task_core 直接操作硬件
不要覆盖 sdkconfig.defaults，必须合并
不要复制 managed_components 或 build 目录
```

遇到不确定内容时，优先保留 testP4 已验证实现，不做抽象过度重写。

---

## 16. 交付物

本 PR 最终应交付：

```text
1. 可 build 的 NanoSoul 工程
2. 真实硬件可启动的 NanoSoul 固件
3. 正式 bsp_board 组件
4. 最小诊断 UI
5. 模块状态模型
6. 屏幕、触摸、BH1750、音频、SD、相机、Wi-Fi 的迁移代码
7. VL6180X-L/C/R 显示 ABSENT 的状态占位
8. motion_core 默认 DISABLED
9. docs/BRINGUP_STATUS.md
10. docs/HARDWARE_BASELINE.md
```

---

## 17. docs/BRINGUP_STATUS.md 模板

```markdown
# NanoSoul Bring-up Status

| Module | Hardware | Bus / GPIO | Status | Test Method | Notes |
|---|---|---|---|---|---|
| Display | ST7701S / current panel | MIPI DSI | OK | Boot UI | migrated from testP4 |
| Touch | FT6x36 / current touch | I2C | OK | Touch page | migrated from testP4 |
| Light | BH1750 | external I2C | OK | lux read | migrated from testP4 |
| Audio | ES8311 + speaker | I2S/I2C | OK | test tone | migrated from testP4 |
| SD | SDMMC | SD pins | OK | mount/list/write | migrated from testP4 |
| Camera | current camera | MIPI CSI | OK | init/capture | migrated from testP4 |
| Wi-Fi | ESP32-C6 hosted | board link | OK | init/connect | migrated from testP4 |
| VL6180X-L | VL6180X | external I2C | ABSENT | not connected | future |
| VL6180X-C | VL6180X | external I2C | ABSENT | not connected | future |
| VL6180X-R | VL6180X | external I2C | ABSENT | not connected | future |
| Motion | 3 omni wheels | motor pins | DISABLED | not in this PR | owner: team lead |
```

---

## 18. docs/HARDWARE_BASELINE.md 模板

```markdown
# NanoSoul ESP32-P4 Hardware Baseline

## Verified in testP4

- Display + Touch
- BH1750
- Speaker / Audio
- SD card read/write
- Camera
- Wi-Fi

These modules were verified running together without observed resource conflicts.

## Current NanoSoul Baseline

NanoSoul imports the verified hardware baseline from testP4 and exposes each subsystem through formal components.

## Out of Scope

- Motion control
- Auto charging
- Dock return
- Full Agent planning

## Rules

- testP4 remains the hardware experiment project.
- NanoSoul is the product project.
- New hardware must be validated in testP4 before being migrated.
```

---

## 19. 最终结论

本次工作不是新增功能，而是把 NanoSoul 从 mock 框架推进到真实硬件主线。

最终目标状态：

```text
NanoSoul 可以在 ESP32-P4 真板启动。
屏幕、触摸、BH1750、喇叭、SD、相机、Wi-Fi 基线可用。
VL6180X 当前未接时显示 ABSENT。
motion_core 默认 DISABLED。
自动回充完全不进入当前范围。
```

完成后，NanoSoul 才适合作为正式开发主线；testP4 继续承担后续硬件试验职责。
