# 原理图信息提取说明（可复现）

本目录下的 txt/tsv/png 均由 `doc/SCH_Schematic1_2026-09-14.pdf` 自动提取或渲染生成，方法记录如下，
换新版原理图后可按同样步骤重新生成。

## 1. 源文件性质（为什么能直接读）

- 该 PDF 是**矢量原理图导出**，不是扫描件：1 页 A3（1786.25 × 861.48 pt），1427 个绘图图元，**0 张内嵌位图**。
- 内嵌字体：`SimSun`（标注/中文）、`Arial`（数字）、`SimHei`（标题栏），编码 `UniGB-UCS2-H` / `WinAnsiEncoding`。
- 因此存在**可搜索文字层**：568 个文字 span，包含位号、参数、网络标号、MCU 引脚复用名、标题栏与 TODO 文字。
- 图形（连线、器件符号）本身不是数据，需要靠**渲染 + 放大读图**来确认连接关系；文字层提供的是"元件与网络清单"。

## 2. 环境与工具

| 项 | 值 |
|---|---|
| Python | `D:\App\Python\Python312\python.exe` |
| PDF 库 | **PyMuPDF (fitz)** 已安装；`pypdf` 亦可用；无 `pdftotext`/`mutool`/`gs`/ImageMagick |

## 3. 重新生成的命令

### 3.1 文字层（原文 + 坐标表）

```powershell
python -c @"
import fitz
src=r'D:\document\code\wsh\code\pd_spoofing\doc\SCH_Schematic1_2026-09-14.pdf'
out=r'D:\document\code\wsh\code\pd_spoofing\doc\schematic'
d=fitz.open(src); pg=d[0]
open(out+r'\01_textlayer_raw.txt','w',encoding='utf-8').write(pg.get_text())
lines=['x_pt\ty_pt\tsize\ttext']
for b in pg.get_text('dict')['blocks']:
    if b.get('type')!=0: continue
    for l in b['lines']:
        for s in l['spans']:
            t=s['text'].strip()
            if t: lines.append('%.1f\t%.1f\t%.1f\t%s'%(s['bbox'][0],s['bbox'][1],s['size'],t))
open(out+r'\02_textlayer_coords.tsv','w',encoding='utf-8').write('\n'.join(lines)+'\n')
"@
```

> `02_textlayer_coords.tsv` 的坐标单位是 PDF point（原点在左上角），可用来**定位任何一个标号在图上的位置**：
> 想看清某个网络接什么，就用该坐标开一个 ±80pt 的窗口放大渲染（见下）。

### 3.2 分区高倍裁图（读写器件连接用）

```powershell
python -c @"
import fitz
src=r'D:\document\code\wsh\code\pd_spoofing\doc\SCH_Schematic1_2026-09-14.pdf'
sd=r'D:\document\code\wsh\code\pd_spoofing\doc\schematic\crops'
d=fitz.open(src); pg=d[0]
for k,(x0,y0,x1,y1) in {
 '01_usb_pd_input':(80,20,470,180),
 '02_3v3_boost':(430,20,720,140),
 '03_current_sense_out':(620,250,1180,420),
 '04_adc_dividers':(60,270,420,380),
 '05_mcu':(0,520,700,860),
 '06_keys_leds_buzzer':(650,530,1000,700),
 '07_mcu_vdd_decoupling':(80,520,180,580),
 '08_dcdc_detail':(455,20,620,140),
 '09_ws2812_c23':(90,790,320,830),
}.items():
    pix=pg.get_pixmap(matrix=fitz.Matrix(900/72,900/72),clip=fitz.Rect(x0,y0,x1,y1))
    pix.save(rf'{sd}\{k}.png')
"@
```

### 3.3 整页预览

```powershell
python -c @"
import fitz
d=fitz.open(r'D:\document\code\wsh\code\pd_spoofing\doc\SCH_Schematic1_2026-09-14.pdf')
d[0].get_pixmap(matrix=fitz.Matrix(200/72,200/72)).save(r'D:\document\code\wsh\code\pd_spoofing\doc\schematic\crops\00_full_page.png')
"@
```

## 4. 提取结果的可靠度

| 信息 | 来源 | 可靠度 |
|---|---|---|
| 位号、参数值（R/C/D/Q/U 型号） | 文字层 | 高（直接取自图纸） |
| 网络名及其归属 | 文字层 | 高 |
| MCU 全部 29 脚的网络 | 文字层 + 放大读图核对 | 高（已逐脚核对引脚号与网络名相邻关系） |
| LCD / USB-C / WS2812 / U1 / Q1 Q2 的引脚接法 | 放大读图 | 高 |
| DCDC1 内部拓扑（L1、D1 的接法） | 放大读图 | **中——画法可疑，见 SCHEMATIC_DESIGN.md §8.1** |
| D1、D3~D6、DCDC1 的型号 | 图纸未标注 | 无（需补 BOM） |
| R14/R18 重叠标号 | 文字层显示两标号同位置 | **中——需与 PCB/BOM 核对** |

## 5. 目录内容

| 文件 | 生成方式 |
|---|---|
| `SCHEMATIC_DESIGN.md` | 人工整理（读图结论 + 存疑项） |
| `NETLIST.md` | 人工整理（网络↔引脚） |
| `COMPONENTS.md` | 人工整理（BOM 视角） |
| `01_textlayer_raw.txt` | 自动：`get_text()` |
| `02_textlayer_coords.tsv` | 自动：`get_text('dict')` span 坐标 |
| `crops/*.png` | 自动：`get_pixmap` 分区渲染（500~900 dpi 等效） |
