# components/ — 自研驱动

放本仓库自写/移植的 ESP-IDF 组件。每个组件一个子目录，含 `CMakeLists.txt` + `idf_component.yml`。

按 `docs/03` 自研清单，落在这里的核心驱动：

| 组件 | 外设 | 总线 | 状态 |
|---|---|---|---|
| `icm42688` | ICM-42688-P IMU | SPI | 待移植（官方仅 I2C 42670） |
| `ip5306` | IP5306 电量/充电 | I²C | 待写（第三方 PMIC，读寄存器） |

> 悬崖红外（ITR20001）本版已去掉（见 `docs/04` D-017），故不在此列。
> 现成有官方组件的（BH1750、PCNT 编码器、MCPWM 电机调速等）走 managed components，不在这里重写。

目前为占位，随固件推进逐个补。
