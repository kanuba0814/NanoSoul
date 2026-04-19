# BUILD_AND_FLASH.md

## 1. 目标

本文档冻结本仓库的标准构建、烧录和监视流程。

主线规则：

- 只支持 `ESP-IDF`
- 只支持 target `esp32p4`
- 只描述当前仓库已验证的标准命令

## 2. 已验证工具链

当前仓库按以下本地工具链验证通过：

- ESP-IDF 路径：`~/.espressif/v5.5.2/esp-idf`
- target：`esp32p4`

说明：

- 顶层 `CMakeLists.txt` 已按官方 `examples/get-started` 形态整理
- 本仓库已经成功执行过 `idf.py set-target esp32p4` 和 `idf.py build`

## 3. 环境初始化

```sh
. ~/.espressif/v5.5.2/esp-idf/export.sh
```

如果 `export.sh` 报 Python 环境缺失，先补齐该版本 ESP-IDF 的 Python env，再继续。

## 4. 首次配置

首次 clone 或清理后，执行：

```sh
idf.py set-target esp32p4
```

规则：

- 这个仓库不允许切到其他芯片 target
- 不要把 `linux` 预览 target 当作主线固件 target

## 5. 标准构建

```sh
idf.py build
```

输出结果位于：

- `build/`
- 固件镜像与分区表都由 ESP-IDF 标准流程生成

## 6. 标准烧录与日志

```sh
idf.py -p /dev/ttyUSB0 flash
idf.py -p /dev/ttyUSB0 monitor
```

或一次完成：

```sh
idf.py -p /dev/ttyUSB0 flash monitor
```

端口名按实际系统替换，不要硬编码进文档或代码。

## 7. 手动进入下载模式

优先使用开发板现成的 `BOOT` / `RST` 按键进入下载模式。

芯片级事实：

- `GPIO35` 在复位时拉低可进入 ROM serial bootloader
- `GPIO36` 也必须为高，`GPIO35=0` 且 `GPIO36=0` 是无效组合

对本项目的规则：

- 下载模式属于板级 / 调试路径，不属于业务输入
- 不要把应用逻辑绑定到 `BOOT` 键
- 不要让业务模块依赖 `GPIO35` ~ `GPIO38`

## 8. 清理流程

常规增量构建：

```sh
idf.py build
```

彻底清理：

```sh
idf.py fullclean
```

说明：

- 只有在构建缓存被污染、target 配置不一致或组件依赖异常时再使用 `fullclean`
- 不要用手工删除 `sdkconfig` 或 destructive git 操作代替正常清理

## 9. Host 测试说明

ESP-IDF 的 host apps 仍然是实验性能力，不是主线 firmware target。

在本仓库中：

- host tests 只用于逻辑 / mock / 自动化
- main firmware 仍然固定构建为 `esp32p4`
- `hal_mock` 是 host 测试的适配层，不是板级替身

## 10. 常见硬规则

- 禁止把本仓库改成 Arduino / PlatformIO 构建入口
- 禁止改 `sdkconfig.defaults`、`partitions.csv` 而不经过组长
- 禁止把板载 `BOOT` 键当作业务输入
- 禁止擅自加第二 firmware app

## 11. 参考依据

- ESP-IDF About / Build System / esptool Boot Mode Selection
- 本仓库已验证的本地工具链路径 `~/.espressif/v5.5.2/esp-idf`
