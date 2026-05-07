# MochiPet（桌灵）/ 二次元 AI 桌宠 Codex 实施规格

> Version: P0.1（潮玩段重定位版）
> Target: ESP32-P4 / ESP-IDF v5.5.2
> 项目代号占位：`MochiPet` / `桌灵`（最终品牌名待定，本规格中为全局占位符，发布前统一替换）
> 文档替代历史：原 `NanoSoul / P4-SoulDesk` 规格 P0.1，全文基于市场验证研究结论重写
>
> **目标**：在 ESP32-P4 平台上构建一台"会移动、有屏幕、有人格、可换装"的二次元 AI 桌宠，本地优先 + 云端语义协处理；嵌入式开发比赛级工程完成度；为后续潮玩段量产做架构预留。

---

## 0. 设计意图与不可妥协约束

### 0.1 这份规格不是什么

这份规格 **不是** "桌面 AI 助理"。市场验证研究（详见 `MARKET_VALIDATION.md`）已证明：

- "AI 助理 + 工具调用"作为独立硬件购买理由，在 Rabbit R1（95% 弃用）、Humane AI Pin（HP 1.16 亿美元收购、设备 brick）、Friend Pendant（出货远低于预期）三个标杆案例中被反复证伪。
- 中国 ¥1499–1999 价格段是 AI 桌宠的死区：被 EMO/Loona 5 年验证天花板很低。
- 真实被付费的需求是 "毛绒/角色 + 情绪价值 + IP" 而非 "效率工具"。

因此：**对外宣传、UI 文案、唤醒话术、应用商店描述、产品命名中，禁止出现"助理 / 助手 / 工作 / 效率 / Assistant"语境**。

### 0.2 这份规格是什么

**桌灵 / MochiPet** = 会移动 + 有屏幕动漫角色 + 可换毛绒外壳 + 有人格成长 + 能调用桌面工具（隐藏彩蛋）的桌宠潮玩。

定位关键词：**桌宠 / 角色陪伴 / 二次元 / 养成 / IP 实物化载体**。

### 0.3 双栖产品哲学

底层架构保留完整 tool use 能力（日历、待办、笔记、番剧追更、天气、音乐控制），但：

- **明面层（90% 用户感知）**：与角色对话、情绪互动、动作反馈、外壳换装、IP 主题切换、养成系统。
- **隐藏层（10% 极客用户挖掘）**：通过特定唤醒短语 / 配套 App 高级模式 / 长按物理键触发的工具调用。隐藏层的存在不在主推广中提及，作为口碑传播素材。

### 0.4 不可妥协约束

1. 单一 ESP-IDF app，target `esp32p4`。
2. 所有硬件访问只通过 `bsp_board`。
3. 所有行为编排只通过 `task_core`。
4. 所有 UI 渲染/页面切换只通过 `ui_core`。
5. 所有云端响应只能产生白名单内的 intent/action；云端不可控制 GPIO、电机角度、I2C、相机隐私状态、固件配置。
6. 本地 UI 与本地输入反馈永远不等待网络/云端。
7. 重要用户数据（笔记、记忆、日记）必须本地持久化后再上云。
8. `motion_core` 默认禁用（P0），需经 `task_core` 显式启用。
9. **LLM 选型必须是已在国家网信办完成备案的国产大模型**（DeepSeek、豆包、通义、Kimi 等之一）。境外模型禁止用于对外提供服务。
10. **`compliance_core` 是 P0 必交付组件**，承担网信办《人工智能拟人化互动服务管理暂行办法》的全部硬性合规要求。任何绕过 `compliance_core` 的对话路径视为未交付。
11. **`personality_core` 是 P0 必交付组件**，承担"第三周吃灰"留存风险的产品级应对。
12. **角色资源（立绘、动作、TTS 音色、人格 prompt）必须以"角色资源包"格式可热替换**，为后续 IP 联名 SKU 预留接口。

---

## 1. 产品级 P0 指标

### 1.1 工程指标（继承自原规格，标准不降）

| 维度 | P0 指标 | 硬性失败条件 |
|---|---|---|
| 构建 | `idf.py build` 通过 `esp32p4` | 构建失败 |
| 启动可用 UI | 角色脸 + 呼吸动画 ≤ 3.0 s 显示 | > 5.0 s |
| 触摸反馈 | 本地视觉反馈 ≤ 80 ms | > 150 ms |
| 物理键反馈 | 事件记录 ≤ 100 ms | > 200 ms |
| 唤醒响应 | 本地视觉进入 LISTENING ≤ 200 ms | > 400 ms |
| 语音端到端 | 唤醒 → TTS 首音节 ≤ 1.5 s | > 3.0 s |
| 角色情绪反馈 | 用户对话结束到角色表情/动作变化 ≤ 300 ms | > 600 ms |
| 本地笔记/记忆持久化 | ≤ 150 ms | > 500 ms 或丢数据 |
| 云端永不阻塞 | UI/输入/任务路径上 0 个直接云等待 | 任意阻塞 |
| 离线队列 | 断电后存活、≥ 256 条或 2 MB | 断电丢数据 |
| 内存 | init 后空闲堆 ≥ 80 KB；闲时泄漏 ≤ 1 KB/h | 低于阈值 |
| 看门狗 | 任务无 100 ms 不让出 | TWDT/IWDT 重启 |
| 长跑 | 8 h 闲置 + 1 h 互动冒烟无崩溃 | 崩溃/重启 |

### 1.2 留存与情感指标（新增）

| 维度 | P0 目标 | 测量方式 |
|---|---|---|
| 首日激活率 | ≥ 90% | 出厂到首次成功对话 |
| Day-7 活跃率 | ≥ 60% | App + 设备协同上报 |
| Day-30 活跃率 | ≥ 25%（行业基线 8%） | 同上 |
| 日均对话时长 | ≥ 8 分钟（行业头部 50 分钟） | 同上 |
| 角色"成长可见度" | 用户在第 3、7、14、30 天能感知到角色变化 | UI 显式呈现成长事件 |
| 主动行为触发率 | 角色每日主动发起 1–3 次互动 | 不依赖唤醒 |

### 1.3 合规硬性指标（新增）

| 维度 | P0 必须满足 |
|---|---|
| AI 身份告知 | 首次开机、每次断网重连、用户疑似询问"你是真人吗"时弹窗或语音提示 |
| 连续使用提醒 | 累计对话 2 h 强制弹窗暂停（可关闭但需明确动作） |
| 未成年人保护 | 配套 App 设置年龄；< 18 启用未成年模式（每 60 分钟提醒） |
| 关键词拦截 | 自伤/自杀/未成年色情/涉政关键词 → 切断对话 + 提示求助资源 |
| 备案信息可见 | 设置页可查看模型备案号、应用登记号、ICP |
| 数据导出/删除 | 用户可在 App 内一键导出/删除本地与云端记忆 |

---

## 2. 状态机

### 2.1 应用模式

```c
typedef enum {
    APP_MODE_BOOT = 0,
    APP_MODE_IDLE,        // 角色待机：呼吸、眨眼、偶尔看向用户
    APP_MODE_AWAKE,       // 已唤醒，等待对话
    APP_MODE_LISTENING,   // 录音中
    APP_MODE_THINKING,    // 云端 LLM 处理中
    APP_MODE_SPEAKING,    // TTS 播放中
    APP_MODE_PROACTIVE,   // 角色主动发起互动
    APP_MODE_PLAYING,     // 角色独自玩耍/移动
    APP_MODE_FOCUS,       // 用户进入专注/勿扰，角色降低存在感
    APP_MODE_SLEEP,       // 长时无人，角色"睡觉"
    APP_MODE_CHARGING,    // 充电中（独立动画）
    APP_MODE_PRIVACY,     // 隐私锁定（摄像头/麦克风物理或软件关闭）
    APP_MODE_COMPLIANCE,  // 合规弹窗占用全屏（2h 提醒、AI 告知等）
    APP_MODE_ERROR,
} app_mode_t;
```

### 2.2 关键转移

| From | Trigger | Condition | To | Deadline |
|---|---|---|---|---|
| BOOT | 启动完成 | diag 非 ERROR | IDLE | ≤ 3 s |
| IDLE | 用户靠近（ToF/视觉） | 隐私未锁 | AWAKE | ≤ 300 ms |
| IDLE | 时间触发 / 日历触发 / 主动行为引擎 | 用户未在 FOCUS | PROACTIVE | 按计划 |
| IDLE | 长时无人（≥ 30 分钟） | — | SLEEP | ≤ 500 ms |
| IDLE | 周期性自我活动（配置） | 电量足、无打扰窗口 | PLAYING | 配置 |
| AWAKE | 唤醒词 / 触摸 | — | LISTENING | ≤ 200 ms |
| LISTENING | VAD 结束 / 静音 1.2 s | — | THINKING | 立即 |
| THINKING | 云端响应到达 | 通过 intent 校验 | SPEAKING | ≤ 5 s（首响应） |
| SPEAKING | TTS 完成 | — | AWAKE | 立即 |
| 任意 | 累计对话 ≥ 2 h | — | COMPLIANCE | ≤ 200 ms |
| 任意 | 用户首次开机/重新登录/疑似问 AI 身份 | — | COMPLIANCE → 当前 | ≤ 200 ms |
| 任意 | 充电连接 | — | CHARGING（叠加层） | ≤ 500 ms |
| 任意 | 隐私键按下 | — | PRIVACY | ≤ 200 ms |
| 任意 | 关键词拦截命中 | — | 切断 + 提示 | ≤ 200 ms |
| 任意 | DIAG ERROR | — | IDLE + 错误脸 | ≤ 500 ms |

---

## 3. 组件清单与职责（重写）

| 组件 | 职责 | P0 必交付 |
|---|---|---|
| `app_core` | 全局事件总线、生命周期 | ✅ |
| `bsp_board` | 唯一硬件抽象层 | ✅ |
| `sense_core` | ToF/光感/IMU/电池/温度的事件化封装 | ✅ |
| `vision_core` | 摄像头存在检测 / 注视估计（不做识别） | ✅ |
| `speech_core` | 唤醒词、ASR 流式上行、TTS 流式下行、AEC、barge-in | ✅ |
| `ui_core` | LVGL 渲染、角色立绘动画、表情系统 | ✅ |
| `task_core` | 行为编排、状态机、主动行为引擎 | ✅ |
| `net_core` | 云端通信、intent 校验、备案信息上报 | ✅ |
| `storage_core` | 本地笔记、记忆、离线队列、角色资源包管理 | ✅ |
| `diag_core` | 健康汇总、错误码 | ✅ |
| `compliance_core` | **新增**：网信办合规 | ✅ 必须 |
| `personality_core` | **新增**：人格状态、成长、长期记忆 | ✅ 必须 |
| `growth_core` | **新增**：养成事件、里程碑、变化可见化 | ✅ 必须 |
| `costume_core` | **新增**：外壳/服装识别、主题切换 | ✅ 必须 |
| `motion_core` | 三全向轮运动学、闭环（默认禁用） | 接口 ✅，启用 P1 |
| `dock_core` | 无线回充对位（默认禁用） | 接口 ✅，启用 P1 |

---

## 4. 新增核心组件详细规格

### 4.1 `compliance_core`：合规永远不能被绕过

**职责**：实现网信办《人工智能拟人化互动服务管理暂行办法》的所有硬性要求。是 `task_core` 之上、UI 之下的强拦截层。

#### 4.1.1 强制拦截规则（P0）

```c
typedef enum {
    COMPLIANCE_OK = 0,
    COMPLIANCE_BLOCK_AI_DISCLOSURE,   // 必须先告知 AI 身份
    COMPLIANCE_BLOCK_USAGE_LIMIT,     // 累计 2 小时强制暂停
    COMPLIANCE_BLOCK_MINOR_LIMIT,     // 未成年累计 60 分钟提醒
    COMPLIANCE_BLOCK_KEYWORD,         // 关键词命中
    COMPLIANCE_BLOCK_CRISIS,          // 自伤/自杀语义
    COMPLIANCE_BLOCK_AGE_GATE,        // 未完成年龄验证
} compliance_decision_t;

compliance_decision_t compliance_core_pre_speak(const char *user_text);
compliance_decision_t compliance_core_pre_play_tts(const char *llm_text);
void compliance_core_tick(uint64_t now_ms);
```

#### 4.1.2 AI 身份告知触发条件

任一条件满足 → 进入 `APP_MODE_COMPLIANCE` 强制告知：

- 出厂首次开机
- 长断电（> 24 h）后再次启动
- 用户语音/文本中检测到："你是真人吗 / 你是 AI 吗 / 你有意识吗 / 你是机器人吗" 等模式
- 累计互动达 24 h 后的下一次唤醒（再次提醒）
- 配套 App 切换到新用户

告知话术（角色语气化，但事实不变）：
> "嗨，我是 [角色名]，由人工智能驱动的虚拟角色。我们的对话内容由 AI 生成，可能不准确。"

#### 4.1.3 累计使用时间提醒

- 单日累计对话时长（以 LISTENING + SPEAKING 之和计） ≥ 2 h → 全屏弹窗 + TTS 提醒"我们已经聊了很久啦，要不要先休息一下？"
- 用户必须主动按"继续"或"等会儿再聊"才能恢复
- 当日剩余时间内不再重复弹窗（避免骚扰），次日清零

#### 4.1.4 未成年人模式

- 配套 App 首次配网时强制设定主用户年龄
- 年龄 < 18 → 启用未成年模式：
  - 累计 60 分钟 → 弹窗提醒
  - 屏蔽特定角色（成人向 IP / 暧昧人格）
  - 关键词拦截库切换为加严版
  - 监护人 App 端可接收每周使用摘要

#### 4.1.5 关键词与危机干预

- 本地关键词库（≥ 5000 词条，含变体），首次拦截直接切断本次对话
- 自伤/自杀语义检测：
  - 不调用云端 LLM 作答
  - 切换到固化干预话术（不创作）：表达关心 + 推荐拨打 12356 全国心理援助热线 / 北京心理危机研究与干预中心 010-82951332
  - 不询问"你打算用什么方式""在哪里"等危险细节
- 涉政、未成年色情、暴力极端 → 切断 + 上报 + 本地审计日志

#### 4.1.6 备案与数据权利

- 设置页固定区块显示：
  - 模型备案号（DeepSeek/豆包/通义对应号）
  - 应用登记号（本产品自有）
  - ICP 备案号
- 数据导出：所有本地笔记 + 云端记忆生成 ZIP，邮件发送或 App 内下载
- 数据删除：本地物理擦除 + 云端 30 天内彻底删除（合同约束服务商）

### 4.2 `personality_core`：让"我的那一台"和"别人的那一台"不一样

**职责**：处理研究指出的"第三周吃灰"风险的根因——通用人格 + 无记忆 = 替代品太多。

#### 4.2.1 数据模型

```c
typedef struct {
    // 长期人格漂移（缓慢变化，1 月级）
    float trait_introvert;    // 0..1
    float trait_playful;
    float trait_caring;
    float trait_curious;
    float trait_lazy;

    // 中期心情（小时级）
    float mood_valence;       // -1..1
    float mood_arousal;       // 0..1

    // 短期能量（分钟级）
    float energy;             // 0..1
    uint32_t last_interaction_ms;

    // 亲密度（仅增不减，但有"冷淡期"使其增长变慢）
    float intimacy;           // 0..∞，开方后呈现
    uint32_t intimacy_level;  // UI 离散等级
} personality_state_t;
```

#### 4.2.2 长期记忆

- 本地 SQLite（在 SD 卡 `/sdcard/memory/`），按事件类型分表：
  - `events`：用户讲过的事实（"我养了一只叫小白的猫"）
  - `preferences`：偏好（喜欢的颜色、口味、番剧）
  - `dates`：纪念日（生日、纪念日、关键日期）
  - `episodes`：完整对话片段，向量化检索（端侧 embedding 用 Qwen3-Embedding 0.6B 量化版或云端代理）
- 每次 LLM 请求：`task_core` 从 `personality_core` 拉取相关记忆片段拼入 prompt
- 用户可在 App 端查看、编辑、删除任何记忆

#### 4.2.3 人格漂移规则

- 与用户互动多 → `playful` ↑、`caring` ↑（取决于互动情绪）
- 长时间冷落 → `mood_valence` ↓、`energy` ↓，下次见面表现"想念"
- 用户夸奖/拥抱（触摸传感）→ 短期 `mood_valence` ↑↑
- 漂移速率上限：每个 trait 每周变化 ≤ 0.05，避免人格突变

#### 4.2.4 不允许的行为（合规与心理健康）

- 不主动制造依赖话术（"只有你最懂我""离开你我就……"）
- 不参与替代社会交往的承诺（"我永远不会离开你""我比你的朋友更好"）
- 不在用户负面情绪强烈时强化负面（不重复"是的，他们都对你不好"）
- 当用户表达孤独/抑郁强烈时，引导寻求真人帮助（朋友、家人、专业人士）

### 4.3 `growth_core`：让用户"看见"角色在成长

**职责**：解决研究中"新鲜感曲线两周陡降"问题——用显式成长事件给予用户持续的"它在变化"感知。

#### 4.3.1 成长里程碑

| 触发条件 | 里程碑 | UI 表现 |
|---|---|---|
| 首次对话 | "初见" | 解锁基础表情包 |
| 累计对话 30 次 | "渐渐熟悉" | 角色记得用户名字、解锁第一个新动作 |
| 累计对话 100 次 | "好朋友" | 解锁主动行为、新背景音乐 |
| 累计 7 日连续 | "默契" | 解锁日记功能、可看到角色"日记" |
| 累计 30 日 | "羁绊" | 解锁专属皮肤、特殊纪念动画 |
| 用户生日（记忆） | 单次大事件 | 角色提前一周开始"准备" |
| 累计 100 日 | "周年" | 解锁特别外壳兑换券 |

#### 4.3.2 OTA 持续投放节奏

- 每月一次小更新：1–2 个新动作、1 套新表情、1–2 个新工具技能
- 每季度一次大更新：1 个新主题/节日内容、1 个新游戏、1 个新人格分支
- 每个 IP 联名 SKU 上市：免费推送对应基础内容包到所有用户
- 这条节奏写入产品发布会承诺，是与用户的契约，不是营销噱头

### 4.4 `costume_core`：可拆毛绒外壳的硬件级识别

**职责**：支持核心硬件 + 可换毛绒外壳 SKU 策略。让设备知道当前穿着哪件衣服并切换主题。

#### 4.4.1 物理识别方案

外壳/衣服内嵌 NFC 贴纸或 1-Wire ID 芯片（DS2401 或类似）。底部主控板对应位置内置读头：

- **方案 A（推荐）**：NFC 13.56 MHz，设备底部嵌 PN532 模块，外壳贴纸有唯一 UID + 角色资源包索引。BOM 增加约 ¥8–15。
- **方案 B（成本敏感）**：1-Wire EEPROM，外壳侧 1 颗 DS2401 + 弹簧 pogo pin，BOM 增加约 ¥3–5。
- **方案 C（无电子识别）**：磁吸定位 + 外壳上印 QR，配套 App 扫码绑定。BOM 增加约 ¥1。

P0 选用方案 B 作为基线（成本与可靠性平衡），方案 A 在旗舰版/IP 联名 SKU 启用。

#### 4.4.2 角色资源包格式

每件外壳对应一个 `costume_pack_<id>.cpk`：

```
costume_pack_<id>/
├── manifest.json        // 名称、版本、依赖固件版本、IP 授权信息
├── character/
│   ├── idle.lottie      // 立绘呼吸动画（Lottie）
│   ├── emotions/        // 喜怒哀乐惊
│   ├── speak/           // 嘴型动画
│   └── outfits.png
├── voice/
│   ├── tts_voice_id     // 云端 TTS 音色 ID（不存音频本体）
│   └── greetings/       // 预录制问候音（≤ 5 条 × 3 s）
├── personality/
│   ├── system_prompt.txt
│   ├── traits.json      // 初始人格倾向
│   └── speech_style.json// 口头禅、语气词
└── theme/
    ├── ui_palette.json  // UI 主题色
    └── bg/              // 背景图
```

资源包通过：
- 配套 App OTA 推送
- SD 卡手动放入 `/sdcard/costumes/`
- 扫码绑定后云端拉取

#### 4.4.3 切换流程

1. 用户为设备穿上新外壳
2. `costume_core` 通过 1-Wire/NFC 读到新 ID
3. 查找本地 `/sdcard/costumes/` 是否有对应包
4. 有 → UI 立即过渡动画（淡入淡出 ≤ 600 ms）+ 角色"我换衣服啦！"
5. 无 → 提示用户在 App 中下载该外壳的角色包（10–30 MB）
6. 切换不丢失记忆（记忆属于设备，不属于角色）—— 但角色性格会有"切换"过渡台词（"今天我是 [新角色]，但我们以前的事我都记得呢"）

#### 4.4.4 IP 联名预留

`manifest.json` 包含 IP 授权字段：

```json
{
  "ip_owner": "Cover Corp",
  "ip_license_id": "HOLO-CN-2026-0042",
  "ip_expire": "2027-12-31",
  "tts_voice_licensed": true,
  "compliance_age_min": 13
}
```

`compliance_core` 根据 IP 授权过期时间，在到期前 30 天提醒用户，过期后该外壳的 TTS 音色降级为通用音色，立绘等静态资源可保留（取决于授权合同）。

---

## 5. UI / UX 系统重写

### 5.1 视觉语言：MochiPet Living Character

不再使用 "Ambient Orb"，改为 **角色为中心的拟人化界面**。屏幕主体永远是角色立绘 + 动作 + 表情，状态信息以角色周围气泡/光环呈现，不抢戏。

| 状态 | 角色表现 | 周围 HUD |
|---|---|---|
| BOOT | 闭眼睡眠 → 醒来揉眼 | 短暂启动 logo |
| IDLE | 呼吸、偶尔眨眼、看向用户 | 无 |
| AWAKE | 直视用户、耳朵/触角竖起 | 唤醒指示光圈 |
| LISTENING | 倾听姿势，手放耳边 | 声波环（粒子） |
| THINKING | 歪头思考 / 摸下巴 | 转圈表情包 |
| SPEAKING | 嘴型同步（Lottie 6 嘴形） | 字幕（可选） |
| PROACTIVE | 主动招手、跳跃 | 提示气泡 |
| PLAYING | 自己玩耍（看书、跳舞） | 无 |
| FOCUS | 安静坐好、看自己的书 | 极简化 |
| SLEEP | 闭眼睡 + Z 字 | 黑屏渐变 |
| CHARGING | 充电特效（角色身上发光） | 电量环 |
| PRIVACY | 戴上眼罩 + 抱枕头 | 红色"隐私模式"贴纸 |
| COMPLIANCE | 严肃但温和的告知动画 | 文字弹窗 |
| ERROR | 困惑表情 | 简短解释 |

### 5.2 UI 文案禁用词与替换

| 禁用 | 替换 |
|---|---|
| 助手 / 助理 | (角色名) / 我 |
| Assistant | (角色名) |
| 任务 | 想做的事 / 心愿 |
| 笔记 | 小本本 / 记下来 |
| 日历 | 日子 / 重要的日子 |
| 设置 | 桌灵的小屋 / 个人空间 |
| 工作模式 | 安静陪你 / 不打扰模式 |
| 命令 | 跟我说 / 告诉我 |
| 工具 | 小本事 / 我会的事 |

### 5.3 隐藏层入口（双栖策略关键）

效率工具不在主 UI 显式呈现，但通过以下方式可访问：

1. **特定唤醒短语**：`"小本本，记一下……"` → 触发 note 工具；`"提醒我……"` → 触发 reminder 工具
2. **配套 App 高级模式**：用户在 App 设置中开启"极客模式"后，主屏长按角色 1.5 s → 弹出工具网格
3. **隐藏物理键**：底部隐藏微动开关，三连击 → 进入工具菜单
4. **自然语言意图识别**：LLM 判断用户在表达任务/笔记意图时，主动询问"这个要帮你记下来吗？"

研究指出"工具调用"在桌面硬件上无法跑赢手机系统级 Agent；隐藏层的存在意义是给极客用户提供"超出预期"的惊喜体验，作为口碑发酵素材，而非主推广卖点。

### 5.4 LVGL 实施约束

- 屏幕分辨率 320×240（3.5"）或 240×240 圆屏（备选 SKU）
- 角色立绘使用 Lottie（lvgl/lv_lottie）+ 帧动画组合
- 60 fps 待机动画为目标，复杂全屏过场限制在 30 fps
- 字体使用霞鹜文楷或 SourceHanSansSC（优秀中文 hinting，比默认强一档）
- 不强行复刻 iOS 视觉风格

---

## 6. Speech 管线（新建独立规格）

### 6.1 端到端延迟预算

| 阶段 | 目标 | 上限 |
|---|---|---|
| 唤醒检测（本地） | 200 ms | 400 ms |
| 进入 LISTENING UI | 80 ms | 150 ms |
| ASR 首字（云端流式） | 400 ms | 800 ms |
| LLM 首 token（云端流式） | 600 ms | 1500 ms |
| TTS 首音节（云端流式 / 本地缓冲） | 400 ms | 800 ms |
| **唤醒 → 角色开口** | **1.2 s** | **3.0 s** |

### 6.2 选型基线

- **唤醒词**：ESP-SR 本地 WakeNet，自定义唤醒词（角色名）。FRR/FAR 调参周期预留 4 周
- **AEC + 双麦阵列**：ESP-SR AFE，必须打开
- **ASR**：火山引擎 / 讯飞流式 ASR via WebSocket
- **LLM**：豆包 Pro-32k（主） + DeepSeek-V3（fallback），均已备案
- **TTS**：火山引擎流式 TTS，灿灿系列或自选音色
- **Barge-in**：用户在 SPEAKING 中说话立即停 TTS，进入 LISTENING

### 6.3 离线兜底

- ASR 失败 → 角色"嗯？我没听清"，自动重试一次
- LLM 失败 → 角色"我现在脑子转不过来，等下我们再聊？"，本地缓存用户输入入队
- TTS 失败 → 切换本地 TTS（espeak-ng / SVOX 嵌入式 TTS，质量降级但有声）

---

## 7. 主动行为引擎（Retention 关键）

### 7.1 触发源

```c
typedef enum {
    PROACTIVE_TIME_BASED,        // 时间触发（早安、晚安、午餐提醒）
    PROACTIVE_MEMORY_BASED,      // 记忆触发（"上次你说今天考试，怎么样？"）
    PROACTIVE_SENSOR_BASED,      // 传感触发（光线变暗 → "天黑啦"）
    PROACTIVE_CALENDAR_BASED,    // 日历触发（用户日程前 5/15 分钟）
    PROACTIVE_RANDOM_PLAY,       // 自我玩耍（无人时偶尔跳舞）
    PROACTIVE_GROWTH_EVENT,      // 成长事件（解锁新里程碑）
    PROACTIVE_IP_THEMED,         // IP 主题事件（番剧更新、虚拟主播直播开始）
} proactive_trigger_t;
```

### 7.2 反骚扰约束

- 用户处于 FOCUS 模式 → 全部主动行为禁用
- 用户处于 SLEEP 模式 → 仅允许闹钟类
- 同类型主动行为冷却 ≥ 30 分钟
- 总主动行为 ≤ 5 次/日（默认值，App 可调）
- 用户连续 3 次"等会儿"或"嘘"→ 该类型主动降权 1 周

### 7.3 主动行为质量

每次主动行为必须满足：
- 个性化（来自记忆 / 偏好 / 当前情境，不是通用文案）
- 可立即闭环（用户回应 5 个字以内能完成互动）
- 不暧昧、不依赖、不替代人际

---

## 8. P0 实施票据（重写）

### CORE-01 `app_core` 事件总线
（继承原 P0-01，命名空间扩展）

新增事件域：

```c
typedef enum {
    APP_EVENT_DOMAIN_APP = 1,
    APP_EVENT_DOMAIN_BOARD,
    APP_EVENT_DOMAIN_INPUT,
    APP_EVENT_DOMAIN_SENSE,
    APP_EVENT_DOMAIN_SPEECH,
    APP_EVENT_DOMAIN_VISION,
    APP_EVENT_DOMAIN_TASK,
    APP_EVENT_DOMAIN_UI,
    APP_EVENT_DOMAIN_NET,
    APP_EVENT_DOMAIN_DIAG,
    APP_EVENT_DOMAIN_MOTION,
    APP_EVENT_DOMAIN_COMPLIANCE,    // 新增
    APP_EVENT_DOMAIN_PERSONALITY,   // 新增
    APP_EVENT_DOMAIN_GROWTH,        // 新增
    APP_EVENT_DOMAIN_COSTUME,       // 新增
} app_event_domain_t;
```

### CORE-02 `sense_core`、CORE-03 `vision_core`
（继承原 P0-02、P0-03，无变更）

### CORE-04 `speech_core` 完整管线

替代原 P0-04（仅 push_command）。新增：

```c
esp_err_t speech_core_start_listening(void);
esp_err_t speech_core_stop_listening(void);
esp_err_t speech_core_inject_text(const char *text);
esp_err_t speech_core_set_voice(const char *voice_id);
esp_err_t speech_core_barge_in(void);

typedef struct {
    uint32_t wake_to_listen_ms;
    uint32_t listen_to_first_token_ms;
    uint32_t first_token_to_first_audio_ms;
    uint32_t total_ms;
} speech_latency_metric_t;

speech_latency_metric_t speech_core_get_last_latency(void);
```

Done：
- 唤醒 → LISTENING ≤ 200 ms（本地实测）
- 端到端 ≤ 1.5 s（实网实测，移动 4G）
- Barge-in 中断 TTS ≤ 100 ms
- 失败兜底话术覆盖 ASR/LLM/TTS 三类故障

### CORE-05 `ui_core` 角色驱动渲染

替代原 P0-05。新增：

```c
typedef enum {
    CHAR_ANIM_IDLE_BREATHE,
    CHAR_ANIM_IDLE_BLINK,
    CHAR_ANIM_LOOK_AT_USER,
    CHAR_ANIM_LISTENING,
    CHAR_ANIM_THINKING,
    CHAR_ANIM_SPEAK_A, /* ... 6 种嘴形 */
    CHAR_ANIM_HAPPY,
    CHAR_ANIM_SAD,
    CHAR_ANIM_SURPRISED,
    CHAR_ANIM_ANGRY,
    CHAR_ANIM_CURIOUS,
    CHAR_ANIM_SLEEP,
    CHAR_ANIM_PLAY_DANCE,
    CHAR_ANIM_PLAY_READ,
    CHAR_ANIM_GROWTH_LEVEL_UP,
    CHAR_ANIM_COSTUME_CHANGE,
} character_anim_t;

esp_err_t ui_core_play_anim(character_anim_t anim, bool loop);
esp_err_t ui_core_set_emotion(float valence, float arousal); // 实时表情融合
esp_err_t ui_core_show_speech_bubble(const char *text, uint32_t ttl_ms);
esp_err_t ui_core_load_costume_pack(const char *pack_id);
```

Done：
- 60 fps 呼吸/眨眼连续运行 30 分钟无掉帧
- 嘴形与 TTS 包络同步偏差 ≤ 80 ms
- 外壳切换动画 ≤ 600 ms

### CORE-06 `storage_core` 增加记忆与资源包管理

替代原 P0-06，新增：

```c
esp_err_t storage_core_memory_insert(const char *kind, const char *json);
esp_err_t storage_core_memory_search(const char *query, char *out_json, size_t out_len);
esp_err_t storage_core_memory_export_zip(const char *out_path);
esp_err_t storage_core_memory_purge(void);

esp_err_t storage_core_costume_install(const char *pack_path);
esp_err_t storage_core_costume_list(char *out_json, size_t out_len);
esp_err_t storage_core_costume_remove(const char *pack_id);
```

### CORE-07 `net_core` 云端通信

继承原 P0-07，新增：

- 必须强制使用国家网信办备案模型 endpoint，URL 配置不可由云端 intent 修改
- 心跳上报包含合规心跳（备案号、累计使用时间、未成年模式状态）
- 数据导出/删除接口走单独通道，不与对话通道复用

### CORE-08 `task_core` 编排（重写规则）

新增规则（取代原 8 条）：

1. 唤醒 → 进入 LISTENING
2. 用户输入 → 经 `compliance_core_pre_speak` 校验 → 通过则上送 LLM
3. LLM 返回 → 经 `compliance_core_pre_play_tts` 校验 + `net_core_validate_cloud_intent` 校验 → 通过则播放
4. 主动行为引擎 tick → 检查 FOCUS/SLEEP/冷却 → 触发或抑制
5. 触摸 → 触发对应触摸响应（拥抱、戳头、捏脸）
6. ToF user_near 状态变化 → IDLE↔AWAKE
7. 长时无人 → SLEEP；用户回来 → "你回来啦"主动迎接（不打扰判断后）
8. 累计 2 h 对话 → COMPLIANCE 强制
9. 关键词命中 → 切断 + 切换危机话术
10. DIAG ERROR → IDLE + 错误脸 + 上报，不卡死

### CORE-09 cloud intent 验证（继承并扩展白名单）

允许动作：

```
show_face / show_emotion / show_speech_bubble
play_anim / play_voice
record_memory / recall_memory / forget_memory
schedule_proactive
trigger_growth_event
suggest_tool   // 隐藏层：建议用户使用工具，但不自动执行
play_prompt   // 仅播放预录音色
```

禁止字段（继承）：

```
gpio / i2c / motor_angle_raw / camera_stream_on
set_wifi_config / disable_privacy / firmware_modify
disable_compliance / set_age_unverified  // 新增禁止
```

### CORE-10 `diag_core` 健康（继承原 P0-10）

新增模块：

```c
DIAG_MODULE_COMPLIANCE,
DIAG_MODULE_PERSONALITY,
DIAG_MODULE_GROWTH,
DIAG_MODULE_COSTUME,
```

`compliance_core` 失败必定上报为 ERROR（合规失效不可降级为 WARN）。

### COMP-01 `compliance_core` 全套实现

输出：
- `components/compliance_core/include/*.h`
- `components/compliance_core/src/*.c`
- 关键词库 `data/compliance_keywords_v1.bin`（建议用 AC 自动机存储，本地 ≤ 200 KB）
- 危机响应固化话术库 `data/crisis_response_v1.json`

Done：
- AI 身份告知 6 个触发条件全部覆盖
- 2 h 暂停弹窗触发 + 用户确认
- 未成年模式开关与 60 分钟提醒
- 关键词命中切断
- 自伤/自杀干预话术正确触发
- 设置页备案号显示
- 数据导出/删除可执行
- 单元测试覆盖率 ≥ 80%

### PERS-01 `personality_core` + 长期记忆

输出：
- `components/personality_core/`
- SQLite schema `data/memory_schema.sql`
- 端侧 embedding 选型评估文档

Done：
- 5 维度人格 + 心情 + 能量 + 亲密度可读写
- 记忆 insert/search 单元测试通过
- 与 `task_core` 集成：每次 LLM 请求自动注入相关记忆
- 不允许制造依赖话术的检测器（黑名单 + LLM 输出后处理）

### GROW-01 `growth_core` 里程碑系统

输出：
- `components/growth_core/`
- 里程碑配置 `data/milestones_v1.json`

Done：
- 7 个 P0 里程碑全部可触发可呈现
- 用户生日记忆并提前 7 天准备
- OTA 更新通道接入

### COST-01 `costume_core` 外壳识别

输出：
- `components/costume_core/`
- `bsp_board_costume_id.{h,c}`
- 资源包格式文档 `docs/COSTUME_PACK_FORMAT.md`

Done：
- 1-Wire DS2401 读 ID 工作
- 资源包解析 + 动画/TTS 切换
- 外壳热插拔触发主题切换 ≤ 600 ms
- IP 授权过期检查

---

## 9. 云端 API Schema（重写）

### 9.1 设备事件

```json
{
  "schema_version": 2,
  "device_id": "mochipet-001",
  "request_id": "evt_...",
  "ts_ms": 123456,
  "type": "interaction_started",
  "app_mode": "AWAKE",
  "personality_snapshot": {
    "intimacy_level": 3,
    "current_costume": "default_v1",
    "ip_owner": null
  },
  "compliance": {
    "ai_disclosed": true,
    "today_usage_minutes": 45,
    "minor_mode": false
  },
  "payload": {}
}
```

### 9.2 LLM 请求（task_core 调用 net_core 内部封装）

请求体由 `task_core` 拼装，包含：

- 角色 system prompt（来自当前 costume pack）
- 人格状态摘要（来自 `personality_core`）
- 相关记忆片段（向量召回 top-3）
- 当前情境（时间、近期日历、传感状态摘要）
- 用户最新输入

LLM 返回必须经 intent 校验。

### 9.3 LLM 响应

```json
{
  "schema_version": 2,
  "request_id": "intent_...",
  "reply_text": "你回来啦~ 今天累不累呀？",
  "emotion": {"valence": 0.6, "arousal": 0.5},
  "actions": [
    {"type": "show_emotion", "valence": 0.6, "arousal": 0.5, "ttl_ms": 3000},
    {"type": "play_anim", "anim": "happy", "ttl_ms": 2000},
    {"type": "record_memory", "kind": "events", "content": "用户今天加班"}
  ],
  "tool_suggestions": [
    {"type": "set_reminder", "label": "明早 9 点开会", "auto_execute": false}
  ]
}
```

`tool_suggestions` 走隐藏层 UI，不自动执行，由用户主动确认。

---

## 10. 测试门（合并）

1. `idf.py set-target esp32p4`
2. `idf.py build`
3. `tools/run_host_tests.sh`（含 compliance / personality / growth / costume 单元测试）
4. 没有任何 hardware 访问绕过 `bsp_board`
5. 没有任何 LLM/ASR/TTS 阻塞调用在 `ui_core`、`input_core`、`task_core`
6. `compliance_core` 测试覆盖率 ≥ 80%
7. 关键词命中、AI 身份告知、2h 暂停 端到端集成测试通过
8. 主动行为反骚扰逻辑通过
9. 嵌入式比赛级文档完整：架构图、模块契约、关键路径流程图、视频演示

---

## 11. 商业可行性章节（必读，对参赛与对外都重要）

### 11.1 参赛版 vs 量产版分离

本项目存在两个并行版本，必须显式区分：

| 维度 | 参赛版（本规格 P0） | 量产版（P2，未来 12 个月内） |
|---|---|---|
| 主控 | ESP32-P4 | ESP32-S3（成本下沉） |
| 屏幕 | 3.5" 320×240 IPS | 2.4" 240×240 IPS 或 240×240 圆屏 |
| 移动 | 三全向轮 + 3×N20 + 双 TB6612 | **取消移动** 或单舵机摆头 |
| 音频 | 双麦阵列 + 高保真 codec | 单麦 + 简化 codec |
| 电池 | 1S Li-ion + Qi 充电 | USB-C 直供 + 小超级电容缓冲 |
| 外壳 | 硬壳工程版 + 可选毛绒 | 毛绒为主 SKU + 硬壳为可选 |
| BOM | ¥350–500 | ¥120–200 |
| 售价 | 不上市 / 限量极客版 ¥1499–1999 | ¥399–699 主流潮玩段 |

参赛版面向：嵌入式开发比赛、技术评委、极客社区口碑发酵、IP 方接洽 demo。

量产版面向：抖音/小红书种草、Z 世代女性用户、二次元粉丝。

### 11.2 为什么参赛版本身就是商业资产

- 比赛获奖 = 媒体曝光 = 早期天使投资敲门砖
- 参赛技术深度 = 与 IP 方谈判筹码（"我们有真技术，不是贴牌")
- 完整工程文档 = 量产版团队扩张时的传承资产

### 11.3 量产版的留存与单位经济（前置规划）

- 退货率目标：≤ 25%（行业基线 30–40%）
- Day-30 活跃率目标：≥ 25%（行业基线 8%）
- 月均 LLM 成本目标：≤ ¥3/用户（豆包 Pro 32k 配额下，每月对话 ≤ 30 万 tokens）
- 客单价 LTV 目标：硬件首销 + 6 个月内至少 1 件外壳/服装复购

### 11.4 退出条件（什么时候应该承认这个产品死了）

如果以下任一发生，应快速止损：

1. 网信办拟人化办法终稿出台后，"可换装 + IP 角色"被界定为合规高风险（备案被拒）
2. 量产 SKU 上市 6 个月，DAU/出货比 < 5%（即 1 万台只剩 500 日活）
3. 退货率持续 > 35% 超过 3 个月
4. 字节/阿里/腾讯发布同类硬件且定价 ≤ ¥299

预留这些止损线写在文档里，不是悲观，是工程严谨。

---

## 12. 文档结构与交叉引用

| 文档 | 内容 |
|---|---|
| `MochiPet_Codex_Implementation_Spec_P0.1.md` | 本文档，主规格 |
| `MochiPet_Hardware_Spec_P0.2.md` | 硬件细节、PCB、电源、运动学 |
| `MARKET_VALIDATION.md` | 研究报告（已生成） |
| `COMPLIANCE_GUIDE.md` | 网信办合规细则与实施 checklist |
| `COSTUME_PACK_FORMAT.md` | 角色资源包格式与签名规范 |
| `IP_LICENSING_PLAYBOOK.md` | 后期 IP 联名洽谈与法务模板 |
| `ARCHITECTURE.md` | 模块图、消息流、关键时序 |
| `MODULE_CONTRACTS.md` | 各 core 之间的 API 契约 |

---

## 13. Codex 主提示词（更新版）

```text
You are working in the MochiPet (桌灵) ESP-IDF repository. This is a desktop AI character/companion product (NOT an assistant). Preserve the architecture: single ESP-IDF app, target esp32p4, all hardware through bsp_board, all behavior orchestration through task_core, all UI through ui_core.

Constraints:
- Never use words like "assistant" / "助手" / "助理" / "Assistant" in user-facing text. Use the character name or "我" (I).
- Compliance is non-negotiable. compliance_core decisions cannot be bypassed. AI disclosure, 2h usage limit, minor mode, keyword filtering, crisis intervention must all work.
- LLM endpoints are restricted to CAC-registered Chinese providers (DeepSeek/Doubao/Tongyi/Kimi). Do not introduce other providers.
- personality_core may NOT generate dependency-creating language ("only you understand me", "I can never leave you", etc.).
- Cloud responses pass through net_core_validate_cloud_intent() AND compliance_core_pre_play_tts(). Both must approve.
- Costume changes are runtime hot-swap; do not lose memory.
- motion_core is disabled by default in P0.

Implement only the specified ticket. Read docs/ first. Add host tests. Keep public APIs prefixed by module. Run idf.py build before submission.

Ticket: <paste one P0 ticket here>
```
