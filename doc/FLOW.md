# 屏幕显示流程

## 0. 三条路径的关系

| 路径 | 入口 | 每行内容来源 | 什么时候走 |
|---|---|---|---|
| 带内容的 DMA | `DevSt7789vDrawText()` → `DevSt7789vBlitRectStart()` | 每行调一次回调 | 画文字、图标等任意图案 |
| 纯色的 DMA | `DevSt7789vFillRectStart()` / `DevSt7789vFillScreenStart()` | 整块同一个颜色 | 清屏、卡片底色、色块演示 |
| 阻塞逐像素 | 同上两个入口 | 同上 | 只有 `ST7789V_USE_DMA` 设成 0 时才编进来 |

前两条共用同一套推进器（`tFill` + `DevSt7789vService()`），差别只在 `Service()` 里一个分支：`pfnRow` 为 0 就整行填同一个颜色，不为 0 就调回调取这一行的像素。

`DevSt7789vDrawPoint` / `DrawLine` / `DrawRectangle` / `DrawCircle` / `FillRect` 这几个旧接口不分宏，永远走阻塞路径。

---

## 1. 文字显示（带内容的 DMA）

```mermaid
flowchart TD
    A["App：UserDisplayTask<br/>每 10ms 一拍（DISPLAY_TASK_MS）"] --> B["App：DevWs2812Fill 清灯缓冲"]
    B --> C["App：sDisModeTable[tSysData.eState].pfDisplay()"]
    C --> D["App：DisplayOff()"]
    D --> E["Dev：DevSt7789vDrawText<br/>(x=8, baselineY=40, ptFont=gtFontInter24,<br/>fg=0xFFFF, bg=0x0000, pcText=12.08)"]
    E --> F["Dev：遍历字符串<br/>FontFindGlyph 逐个查字形<br/>按 advance 推进笔位置<br/>累计墨迹包围盒 minX/minY/maxX/maxY"]
    F --> G{"有任何墨迹，<br/>且 minX、minY 都大于等于 0 吗"}
    G -- "否（空串或全越界）" --> H["返回 1：这一拍不画"]
    G -- 是 --> I["Dev：保存文本状态<br/>ptTextFont / pcTextStr / u16TextFg / u16TextBg<br/>i16TextPenX / i16TextBaseY / i16TextX0 / i16TextY0"]
    I --> J["Dev：DevSt7789vBlitRectStart<br/>(minX, minY, W, H, TextRowFn)"]
    J --> K["Dev：FillBegin(x, y, w, h)"]
    K --> L{"宽度为 0、高度为 0、<br/>x+w 超出屏宽、y+h 超出屏高"}
    L -- 是 --> H
    L -- 否 --> M{"tFill.u16Left 不为 0<br/>或 BspSpiDmaIsIdle 为 0"}
    M -- "是（上一块没发完）" --> H
    M -- 否 --> N["Dev：St7789vSetAddress<br/>发 0x2a / 0x2b / 0x2c 与坐标（阻塞单字节）<br/>再 BspSpiDc(DATA)"]
    N --> O["Dev：tFill = 宽度 W、pfnRow=TextRowFn、<br/>u16Row=0、u16Left=H<br/>返回 0：窗口设好了，像素一个都还没发"]
    O -. "交出去，异步" .-> P
    P["主循环：while 1 里 PT_TASK_REG(5, DevSt7789vTask)<br/>→ DevSt7789vService()，每趟主循环都跑"] --> Q["BSP：BspSpiDmaService()<br/>u8SpiDmaIdle 不为 0 直接返回<br/>否则查 TC 标志、清标志、等 BSY 清零、关通道、置空闲"]
    Q --> R{"u16Left 不为 0，且 DMA 空闲"}
    R -- 否 --> S["return：这趟什么都不做"]
    R -- 是 --> T["Dev：tFill.pfnRow(u16Row, u16W, aLine)<br/>也就是 TextRowFn"]
    T --> U["Dev/Components：TextRowFn 整行先铺 u16TextBg<br/>再逐字符判断墨迹是否落在本行<br/>FontRenderRow 解 2bpp 打包并混合成 RGB565<br/>盖到 aLine 里该字形的起始列"]
    U --> V["Dev：aLine 就地字节对调<br/>aLine[i] = U16_SWAP_BYTES(aLine[i])"]
    V --> W["BSP：BspSpiSendDmaStart(aLine, u16W*2)<br/>关通道、清标志、装计数与源地址<br/>开 SPI1 TX 请求、开通道"]
    W --> X["硬件：DMA1_Ch3 逐个字节写 SPI1-&gt;DATAR<br/>SCK / MOSI 出线"]
    X --> Y["ST7789V 按窗口自动递增写进 GRAM"]
    Y --> Z["Dev：u16Row++、u16Left--"]
    Z --> Q
    Y --> AA["面板扫描到那一行时才显示出来"]
```

关键点：`DrawText` 那一趟只登记、设窗口，**不发像素**。真正搬数据是之后 `u16Left` 行、每行一趟主循环。

---

## 2. 纯色矩形（之前的 DMA 方式）

```mermaid
flowchart TD
    A["App：DisplayRun() 或 DevSt7789vInit()<br/>每 10ms 一拍"] --> B["Dev：DevSt7789vFillRectStart(x, y, w, h, color)<br/>或 DevSt7789vFillScreenStart(color)"]
    B --> C["Dev：FillBegin(x, y, w, h)"]
    C --> D{"越界，或 tFill.u16Left 不为 0，<br/>或 DMA 通道还占着"}
    D -- 是 --> E["返回 1：这一拍不画，下一拍重试"]
    D -- 否 --> F["Dev：St7789vSetAddress<br/>发 0x2a / 0x2b / 0x2c 与坐标<br/>再 BspSpiDc(DATA)"]
    F --> G["Dev：tFill = 宽度 w、u16Color=color、<br/>pfnRow=0、u16Row=0、u16Left=h<br/>返回 0"]
    G -. "交出去，异步" .-> H
    H["主循环：DevSt7789vService()，每趟都跑"] --> I["BSP：BspSpiDmaService() 给上一行收尾"]
    I --> J{"u16Left 不为 0，且 DMA 空闲"}
    J -- 否 --> K["return：这趟什么都不做"]
    J -- 是 --> L["Dev：pfnRow 为 0<br/>整行填同一个颜色：<br/>aLine[i] = U16_SWAP_BYTES(u16Color)"]
    L --> M["BSP：BspSpiSendDmaStart(aLine, w*2)"]
    M --> N["硬件：DMA1_Ch3 写 SPI1-&gt;DATAR<br/>SCK / MOSI 出线"]
    N --> O["ST7789V 按窗口自动递增写进 GRAM"]
    O --> P["Dev：u16Row++、u16Left--"]
    P --> I
    O --> Q["面板扫描到那一行时才显示出来"]
```

跟第 1 节的差别只有两处：入口是 `FillRectStart` 而不是 `BlitRectStart`，以及 `Service()` 里走的是"整行填同一个颜色"那个分支。

---

## 3. 阻塞逐像素（`ST7789V_USE_DMA` 设成 0 时）

```mermaid
flowchart TD
    A["App：DevSt7789vInit()<br/>或 DrawPoint / DrawLine / DrawRectangle / DrawCircle / FillRect"] --> B["Dev：FillBegin 只做边界检查，不查忙"]
    B --> C["Dev：St7789vSetAddress 发命令字节<br/>每个字节 St7789vSendCmd → BspSpiSendByte"]
    C --> D["BSP：BspSpiSendByte<br/>写 SPI1-&gt;DATAR，等 TXE 置位"]
    D --> E["Dev：BspSpiDc(DATA)"]
    E --> F["Dev：循环 w×h 次 St7789vSendData2Bytes(color)"]
    F --> G["BSP：先发高字节：写 DATAR、等 TXE"]
    G --> H["BSP：再发低字节：写 DATAR、等 TXE"]
    H --> I{"还有像素"}
    I -- 是 --> F
    I -- 否 --> J["函数返回：此时像素已经全部出线"]
    J --> K["整个过程占着 CPU，主循环其它任务全被挡住"]
```

同一个宏设成 1 时，这段代码根本不参与编译；`DrawPoint` 那几个旧接口是例外，它们永远走这条。

---

## 4. 设备层内部状态

| 变量 | 位置 | 含义 |
|---|---|---|
| `tFill.u16W` | `dev_st7789v.c` | 当前矩形宽度（像素） |
| `tFill.u16Color` | 同上 | 纯色填充用；`pfnRow` 非 0 时不看它 |
| `tFill.pfnRow` | 同上 | 0 = 纯色填充，非 0 = 每行内容由回调给 |
| `tFill.u16Row` | 同上 | 已发出的行数，从 0 起，回调用它 |
| `tFill.u16Left` | 同上 | 剩余行数，0 = 空闲（兼作忙标志） |
| `aLine[]` | 同上 | 480 字节行缓冲，一行像素，发送前就地转字节序 |
| `u8SpiDmaIdle` | `bsp_spi.c` | DMA 通道忙标志 |
| `u8SpiError` | 同上 | 传输错误锁存，置 1 后拒绝新传输 |
| `ptTextFont` 等 8 个 | `dev_st7789v.c` | 文本绘制的行来源状态，`TextRowFn` 每行读一次 |

`BspSpiSendDmaStart()` 依次做：关通道 → 清 TC/TE 标志 → 装计数 → 装源地址 → 开 SPI1 TX DMA 请求 → 开通道。
`BspSpiDmaService()` 依次做：查 TE（置错误锁存）或 TC（等 BSY 清零）→ 关通道 → 关 SPI1 TX 请求 → 置空闲。

DMA1 通道 3 的传输完成/错误中断没有开，收尾靠主循环轮询。启动文件里 `DMA1_Channel3_IRQHandler` 的弱符号默认处理是死循环，所以那个中断不能开。

---

## 5. 数据流

```text
字符串 → 字形表 → 2bpp 位图 → 混合成 RGB565 → 字节对调 → aLine → DMA → SPI → GRAM → 玻璃
         Components                        Dev                                BSP
```
