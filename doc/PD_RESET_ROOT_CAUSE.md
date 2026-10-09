# PD 取电反复复位：根因与修复记录

> 现象：插 PD 充电器供电时，板子每隔一两秒复位一次；改用外部电源供电不复位。
> 结论：**受电端回复 Request 超时**，源端执行错误恢复并撤掉 VBUS，本板 3V3 由 VBUS 变换而来，跟着掉电。

---

## 0. 现象与现场

| 项 | 内容 |
|---|---|
| 触发条件 | 只在使用 PD 充电器（支持 5/9/15/20V 四档）供电时出现 |
| 不触发条件 | 外部电源供电（CC 上没有 PD 源端，不产生 PD 报文往来） |
| 复位周期 | 约一到两秒，与 PD 协商动作同步 |
| 复位标志 | `reset: POR` 与 `reset: SOFT` |
| 串口表现 | 日志每次都在同一位置被截断，截断点紧跟在“我们发出 Request 并收到 GoodCRC”之后 |

---

## 1. 定位过程

按“先看现象、再取证据”的顺序推进，每一步的结论都写清了依据。

### 1.1 第一轮：怀疑源端等不到 Request

最初固件在收到 Source_Capabilities（源端能力报文）后并不会请求档位，要等按键。源端等不到 Request，会判定超时。于是改成收到能力报文后自动请求第一档（5V）。

结果：仍然复位，但复位点后移到了我们发出 Request 之后。

### 1.2 第二轮：核对 Request 报文内容

把请求数据对象（RDO）与官方 WCH Sink 例程逐字节比对：

| 项 | 数值 |
|---|---|
| 实际发出的报文 | `42 00 2C B1 04 13` |
| RDO | `0x1304B12C`（位置 1，即 5V 档，工作电流与最大电流均 3A，bit25 与 bit24 置位） |
| 官方例程同档位结果 | 完全一致 |

结论：报文内容没有问题，排除编码错误。

### 1.3 第三轮：与旧工程逐条对齐

逐条对照旧工程 `code/CH32X035G8U/Code/UserBsp/bsp_usb_pd.c`（官方 USBPD_SNK 例程的移植，现场验证不复位），发现并修正：

| 差异 | 旧工程做法 | 原固件做法 | 处理 |
|---|---|---|---|
| 中断里 `IF_TX_END` | 立刻清 `CC_LVE`，放掉 CC 低压驱动 | 只在 `BspPdRxMode()` 里清，最坏要等一个任务周期 | 已改为在中断里清 |
| 服务节拍 | 1ms | 5ms | `DEV_PD_SERVICE_MS` 改为 1 |
| 内部 CC 下拉 | `CC_CMP_66 \| CC_PD` | 曾被误改为只留阈值 | 已还原 |
| 已连接时的 CC 重复检测 | 已连接时不做 CC 检测 | 每秒重配一次比较器 | 保持 1 秒一次，不再加密 |

结果：仍然复位。说明上述差异都不是主因（但都是应当保留的修正）。

### 1.4 决定性实验：判断“掉电”还是“固件复位”

SRAM 在复位（含看门狗复位、异常复位）中不会被清零，只在真正掉电时才丢失。于是在 SRAM 里放一块复位现场标记：

| 位置 | 内容 |
|---|---|
| `0x20004700` | 魔数、MCAUSE、MEPC、MTVAL、NMI 标记（在 `.bss` 之上、堆顶 `0x20004800` 之下） |
| `Code/user_config.h` | `BOOT_TRACE_*` 宏定义 |
| `User/main.c` | 上电读标记并打印 `trace:` 一行，随后重新登记 |
| `User/ch32x035_it.c` | 异常时先把现场写进标记再复位，避免异常上下文里的 `printf` 卡死导致取不到现场 |

同时加了一条 50ms 心跳，打印 VBUS、VOUT、IBUS 的 ADC 原始码值（VBUS 5V 空载约 780 到 784）。

实测输出：

```text
trace: SRAM lost -> last reset was power-off
...
1s INFO  dev_sensor.c:91: hb 1674 781 4 4
1s INFO  user_pd.c:38: PD phy start
...
1s INFO  dev_pd.c:360: PD request pdo1 3000mA rdo=1304B12C tx=42 00 2C B1 04 13
1s INFO  bsp_pdsink.c:427: PD phy GoodCRC try1 hdr=61 01      <- 之后整板掉电
```

得到三条硬结论：

1. **SRAM 丢失 → 是物理掉电**，不是固件异常，也不是看门狗复位（否则 SRAM 内容会保留）。
2. **掉电前 VBUS 仍是约 5.0V**（码值 781 到 784），掉电是瞬时的，不是逐渐塌陷。
3. **掉电时刻固定出现在“我们的 Request 被源端确认之后几毫秒”**。

既然是源端主动撤电，问题就落在协议时序上，而不是板上电源设计。

---

## 2. 根因

USB-PD 规定：受电端收到 Source_Capabilities 后，必须在 `tSenderResponse`（发送方响应超时，24 到 30ms）内回复 Request。超时视为协议错误，源端会执行错误恢复（Hard Reset），期间 VBUS 会被撤掉。

原固件的处理顺序是：

```text
收到能力报文
  -> 打印 "PD rx state1 type1 len22 hdr=A1 41"      约 5ms
  -> 打印 "PD support: yes, 4 pdo, pps 0"           约 4ms
  -> 打印 4 行档位表                                 约 19ms
  -> 下一个任务节拍才发 Request
  -> 发送前还打印一行 request 日志                    约 6ms
```

串口 115200、每行带文件名与时间戳，一行约 4 到 5ms，合计约 28ms 以上，**超过 24ms 的窗口**。

这不是电气问题，也不是报文格式问题，而是**把协议超时窗口花在了串口打印上**。源端撤掉 VBUS 后，本板 3V3 由 VBUS 经 DCDC 变换得到，储能电容只能撑不到 1ms，于是整板掉电，重新上电后又重复同一过程，表现为不断复位。

---

## 3. 为什么旧工程不复位

旧工程在同一个分支里的动作是：

```c
case DEF_TYPE_SRC_CAP:
    Delay_Ms( 5 );
    PD_Save_Adapter_SrcCap( );
    /* 打印能力表 */
    PDO_Request( PDO_INDEX_5 );      /* 就在本分支里立刻请求 */
```

它打印的内容更少（约 150 字符），加上 `Delay_Ms(5)` 总计约 18ms，**刚好卡在 24ms 之内**，所以现场看起来“旧的没问题”。

两者差别只有约 10ms，属于临界超时。任何新增的日志、多余的打印、更慢的节拍，都可能把余量吃光并触发复位。这也解释了为什么之前几次“看起来合理”的改动都无效：真正超时的是日志，不是 PD 代码本身。

---

## 4. 修复内容

| 文件 | 改动 |
|---|---|
| `Code/UserDev/pd/dev_pd.c` | Source_Capabilities 分支里**第一件事就是发 Request**：上层还没选档位时默认要第一档（5V）；所有日志移到发送之后；档位表移到收到 PS_RDY、契约成立后再打印；Request 日志改为发送后打印并附结果 |
| `Code/UserBsp/bsp_pdsink.c` | 中断 `IF_TX_END` 里立刻清 `CC_LVE`（与官方例程、旧工程一致） |
| `Code/UserDev/pd/dev_pd.h` | `DEV_PD_SERVICE_MS` 由 5ms 改为 1ms |
| `Code/UserBsp/bsp_iwdg.h` | 喂狗周期由 1000ms 改为 100ms（原来第一次喂狗要等整个初始化跑完，余量太小） |
| `Code/user_config.h`、`User/main.c`、`User/ch32x035_it.c` | 复位现场标记（保留，见第 6 节） |

修复后从收到能力报文到 Request 发出只需几毫秒，窗口余量恢复到 20ms 左右。

---

## 5. 验证

修复后串口顺序变为：

```text
PD request pdo1 3000mA rdo=1304B12C tx=42 00 2C B1 04 13 ok     <- 立刻发出
PD support: yes, 4 pdo, pps 0
PD state 3, pdo 4                                              <- 收到 Accept
  pdo 1: 5000 mV, 3000 mA                                      <- 契约成立后才打表
  ...
PD state 4, pdo 4
PD ready 5000 mV                                               <- 5V 取电导通
```

现场确认：不再复位，稳定取到 5V。

---

## 6. 保留的诊断手段与遗留项

| 项 | 说明 |
|---|---|
| 复位现场标记 | `0x20004700` 一块 SRAM 加 `trace:` 一行输出。以后再出现复位，先看这一行即可区分掉电与固件复位。确认长期稳定后可摘掉 |
| 异常处理 | `HardFault_Handler` 先写标记再打印再复位；`NMI_Handler` 先写标记再死循环（等看门狗复位） |
| `BspPdGetStat()` | 已删除（连同 `tBspPdStatDef`、ISR 里的收发计数、`BspPdReadCcLevel()`），这些只是诊断用，不参与运行时逻辑 |
| PD 失败告警 | `PD tx failed`、`PD phy TX_END timeout`、`PD phy GoodCRC timeout`、`PD no src cap` 保留，只在异常时出现 |
| 传感器日志 | 已移除逐采样与心跳打印；ADC 初始化那一行保留 |

本次移除的日志（如日后需要复查协议细节，可临时加回，但**不要加在任何带超时窗口的分支里**）：

| 已移除 | 原位置 |
|---|---|
| `PD rx state%u type%u len%u hdr=...` | 每帧一条，位于 `PdHandleMsg()` |
| `PD stat rx%u tx%u rst%u err%u \| last ...` | 每秒一条，位于 `DevPdService()` |
| `PD phy GoodCRC try%u hdr=...` | 每次发送一条，位于 `BspPdSend()` |
| `hb 毫秒 VBUS VOUT IBUS` | 每 50ms 一条，位于 `DevSensorTask()` |
| `PD request GoodCRC received` | 已并入 Request 日志的结尾结果 |

---

## 7. 经验

1. **带超时的协议分支里不要做串口打印。** 115200 下一行日志就是 4 到 5ms，PD 的响应窗口只有 24 到 30ms，三五行就吃光了。
2. **判断掉电还是固件复位，用 SRAM 标记。** 复位不清 SRAM，掉电会清；配合异常处理先写标记再复位，可以同时覆盖异常与看门狗两种情况。
3. **现象与 PD 报文同步出现时，先怀疑时序而不是硬件。** 本例中“掉电”确实是真的（SRAM 丢失），但撤电的是源端，触发原因是本地响应太慢。
4. **与现场验证过的旧代码逐条对齐时，要包括打印与节拍这类“非功能”差异。** 本次真正致命的一处，正是日志顺序。
