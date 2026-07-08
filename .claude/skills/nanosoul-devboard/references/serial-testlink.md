# 串口拓扑与 testlink 时序

2026-07-08 实测整理。核心教训：**先认口，再调试**——两个 USB-C 在宿主机上是两个不同设备，
插错口的所有「协议不通」都是空耗。

## 双口拓扑（真值）

| | UART 口（CH343） | 原生口（P4 USJ） |
|---|---|---|
| lsusb | `1a86:55d3` QinHeng "USB Single Serial" | `303a:1001` Espressif "USB JTAG/serial debug unit" |
| 物理连接 | CH343 桥 → P4 **UART0** | P4 片上 USB-Serial-JTAG 外设 |
| 日志 | ✅ 控制台（`ESP_CONSOLE_UART_NUM=0`） | ❌（`ESP_CONSOLE_SECONDARY_NONE` 特意让给 testlink） |
| NDJSON testlink | ❌ 只出日志，命令没人收 | ✅ TEST 模式下的协议口（docs/13） |
| esptool 烧录 | ✅（自动复位电路） | ✅（USJ 也能烧） |
| 打开即复位 | ✅ 必复位（见下） | 一般不复位 |

认口命令：`udevadm info -q property -n /dev/ttyACMx | grep ID_VENDOR_ID`。
只插了 CH343 口时，`ns_probe.py` 的 serial 传输**必然**收不到帧——去插原生口，或走 `--ws`。

## CH343 口「一开就复位」的机制与利用

Linux 打开 tty 时内核对 CDC-ACM 默认 assert DTR，CH343 的 esptool 式自动复位电路
（DTR/RTS→EN/BOOT）随即复位芯片——**pyserial 预设 dtr=False/rts=False 也拦不住**内核那一下。

后果与利用：
- 任何「连上去看看」都等于冷启动——命令别在开口后立刻发（见下时序坑）。
- 反过来抓全量启动日志极其方便：`scripts/serial_capture.py /dev/ttyACM0 25 > boot.log`
  （esptool 式 DTR/RTS 硬复位 + 定秒采集，本技能自带）。

## testlink 命令时序坑

TEST 模式启动序列（UART0 日志时间戳，实测）：

```
~1.8s  testmode: entering TEST mode
~5.1s  audio: ES8311 full-duplex（audio/voice 起来）
~5.6s  tl_serial: serial NDJSON link up   ← 此前发的 NDJSON 输入直接丢弃
~5.7s  testmode: TEST mode up: motor_test + serial link + ...
~9s    netlink: online: <ip>（WiFi 慢半拍）
```

- `ns_probe.py selftest` 若在板子刚复位后立刻发命令 → **无 ack、0 items、不报错**。
  等日志出现 `TEST mode up` 再发，或干脆 sleep 8s。
- probe 对非 `{` 开头的行打 `log|` 前缀转发 stderr——看到一堆 `log|` 说明你连的是
  **日志口**（CH343），协议帧一个都不会来。
- TEST 模式与跳线无关的情况：日志 `nanosoul: ... kconfig=TEST strap=0 -> TEST`，
  kconfig 可强制 TEST；判定以这行日志为准，别去摸 IO48。

## 传输选择

- **原生 USJ 口插着** → `python tools/testhost/ns_probe.py <cmd> --port /dev/ttyACM<USJ那个>`。
- **只有 CH343 口** → 日志单向可读；协议走 WS：
  `ns_probe.py <cmd> --ws <板子IP> --token <companion.token>`（IP 看日志 `netlink: online:`；
  token 在 SD 卡 config.json 里，**由持卡人提供，不要去翻 SD**）。
- WS 传输有背靠背丢帧问题，probe 里已做 0.3s/帧节流，别绕过它连发。

## 相关红线（CLAUDE.md 板外纪律）

烧录/monitor 谁有板谁本地跑；调试代理可以读串口、跑 ns_probe 自检，但 flash 由持板人执行。

## 2026-07-08 追加：esp_hosted SDIO 三连坑（语音云链排障实录）

1. **`CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM=y` 是大坑**：SDIO 传输缓冲放 PSRAM 后，
   与 DSI 帧缓存/XIP/大缓冲抢带宽，DMA/cache 一致性出错——症状五花八门且全是假象：
   协处理器版本读成 `0.0.0`、`sdio_get_len_from_slave: Len from slave[1638(=0x666)]`、
   `sdio_write_task Failed to send data` → `Unrecoverable host sdio state` → esp_hosted
   **整板重启**（屏幕表现为忽然全色然后慢慢褪黑）。轻流量（DHCP/mDNS）侥幸能跑，
   TLS 一波满 MTU 入站立刻崩。**解法**：mempool 留内部 RAM，OOM 用
   `ESP_HOSTED_SDIO_TX_Q_SIZE/RX_Q_SIZE 20→10` 解决。
2. **板载 C6 slave 固件其实是配套的 2.12.9**，跑 streaming 模式；host 改
   `SDIO_OPTIMIZATION_RX_NONE`（packet 模式）会在 transport 初始化直接
   `assert("SDIO mode mismatch")`。别乱切模式；版本 0.0.0 的读数在 mempool 修好前不可信。
   如确需给 C6 刷固件：官方路径是 `host_performs_slave_ota` 示例（Partition 模式零网络
   依赖），slave 工程在 esp_hosted 组件 `slave/` 下自包含、可直接 build esp32c6。
3. **出站 WSS 需要 `CONFIG_WS_BUFFER_SIZE=4096`**：火山 openspeech 握手响应头
   （一串 x-tt-*）超过 tcp_transport 默认 1024 → `transport_ws: Header size exceeded
   buffer size`。此值只能 Kconfig 改，esp_websocket_client 的 buffer_size 管不到它。

## seed-asr WebSocket 协议实战备注（volc_asr.c）

- 服务器对每个音频帧回一个**流式中间响应**（`result.text` 逐步填充），最终响应带
  `is_last_package`（flags&0x02）；只认最终响应，中间响应只累计文本。正常结束是
  ws close `code=1000 reason=finish last sequence`。
- miniz 的 **tdefl 压缩器在 P4 上会诡异地返回 NULL**（PC 同码同参正常；312KB 状态结构
  malloc 本身成功，失败在压缩内部，未深究）。**解法**：gzip 封装用 RFC1951 stored
  （不压缩）块 + mz_crc32，任何 inflate 都必须接受，PCM/短 JSON 本来也压不动。
- 识别效果实测：说完 ~0.5s 出中文结果；健康波形参考 rms≈2000+（削顶/字节序错会在
  volc_asr 的 pcm 统计行现形）。
- chat 403 "Request not allowed" = key 类型/模型权限问题：chat 要普通按量付费 Ark key，
  Agent Plan 专属 key 只对 plan 端点（TTS/ASR）有效。
