# ringbuf — 单生产者单消费者环形缓冲区

与硬件、操作系统、业务无关的通用字节环形缓冲区，属于 Components 层。

| 文件 | 内容 |
|---|---|
| [ringbuf.h](ringbuf.h) | 实例结构与全部接口 |
| [ringbuf.c](ringbuf.c) | 实现，只依赖 stdint / stddef / string |

特点：无动态内存、无阻塞、无原子指令、不关中断、不做除法。存储区由调用方提供，可以在同一个工程里创建任意多个互不干扰的实例。

---

## 1. 加入工程

工程已把 `Code` 目录加入包含路径，源文件也在 `.cproject` 的递归扫描范围内，所以只要在 IDE 里刷新一次工程（F5），`ringbuf.c` 就会自动参与编译，不需要手工添加文件。

使用时包含：

```c
#include "Components/ringbuf/ringbuf.h"
```

## 2. 最小用法

```c
#include "Components/ringbuf/ringbuf.h"

#define PD_RX_BUF_SIZE   (128u)                 /* 必须是 2 的幂 */

static uint8_t  sPdRxBuf[PD_RX_BUF_SIZE];
static tRingBuf sPdRx;

void PdRxInit(void)
{
    (void)RingBufInit(&sPdRx, sPdRxBuf, PD_RX_BUF_SIZE);   /* 初始化阶段调用一次 */
}
```

`RingBufInit` 返回 1 表示成功，返回 0 表示参数不合法（实例或存储区为空指针、长度为 0、长度不是 2 的幂、长度超过 `RINGBUF_SIZE_MAX`）。初始化之后，这个实例就可以一直用下去，中途不需要重新初始化。

## 3. 中断写、主循环读

```c
/* ===== 生产者：中断上下文，只做入队 ===== */
void PdRxByteIsr(uint8_t u8Byte)
{
    (void)RingBufPushByte(&sPdRx, u8Byte);      /* 缓冲区满就丢弃这个字节 */
}

/* ===== 消费者：主循环，只做出队 ===== */
void PdRxProcess(void)
{
    uint8_t u8Byte;

    while (RingBufPopByte(&sPdRx, &u8Byte) != 0u)
    {
        PdRxParse(u8Byte);
    }
}
```

两端都不需要关中断，也不需要临界区：

- 生产者只写 `u32Tail`，只读 `u32Head`；
- 消费者只写 `u32Head`，只读 `u32Tail`；
- 数据写入与游标发布之间有编译器屏障，消费者不会读到"游标已更新但数据还没落地"的半个字节。

反过来用也可以：主循环当生产者、中断当消费者，同样成立。

## 4. 先看后取：协议解析

`RingBufPeek` 只复制不消费，适合"先确认帧头，再决定整帧怎么处理"的场合。

```c
uint8_t au8Header[2];

if (RingBufPeek(&sPdRx, au8Header, 2u) == 2u)            /* 数据还不够 2 字节时返回实际字节数 */
{
    if ((au8Header[0] == 0xA1u) && (au8Header[1] == 0x2Bu))
    {
        (void)RingBufDrop(&sPdRx, 2u);                   /* 确认是帧头，吃掉它 */
    }
    else
    {
        (void)RingBufDrop(&sPdRx, 1u);                   /* 不是帧头，滑一个字节继续找 */
    }
}
```

## 5. 接口速查

| 接口 | 角色 | 返回值 |
|---|---|---|
| `RingBufInit(tRingBuf *, uint8_t *, uint32_t)` | 初始化 | 1 成功，0 参数不合法 |
| `RingBufReset(tRingBuf *)` | 清空数据 | 无 |
| `RingBufPush(tRingBuf *, const uint8_t *, uint32_t)` | 生产者，写入一段 | 实际写入字节数，满时 0 |
| `RingBufPushByte(tRingBuf *, uint8_t)` | 生产者，写入一个 | 1 成功，0 已满 |
| `RingBufPop(tRingBuf *, uint8_t *, uint32_t)` | 消费者，读出一段并移出 | 实际读出字节数，空时 0 |
| `RingBufPopByte(tRingBuf *, uint8_t *)` | 消费者，读出一个并移出 | 1 成功，0 为空 |
| `RingBufPeek(const tRingBuf *, uint8_t *, uint32_t)` | 消费者，只查看不移出 | 实际查看字节数，空时 0 |
| `RingBufDrop(tRingBuf *, uint32_t)` | 消费者，丢弃若干字节 | 实际丢弃字节数，空时 0 |
| `RingBufGetCount(const tRingBuf *)` | 查询 | 当前已缓存字节数 |
| `RingBufGetSpace(const tRingBuf *)` | 查询 | 当前还能写入的字节数 |
| `RingBufIsEmpty(const tRingBuf *)` | 查询 | 1 为空 |
| `RingBufIsFull(const tRingBuf *)` | 查询 | 1 为满 |

约定：

- 所有接口都接受"未初始化的实例"或空指针，此时返回 0、不做任何访问，不会跑飞。调用方可以按需检查返回值，也可以直接忽略。
- `Push` 与 `Pop` 的长度不足时按实际能力做部分传输，不做覆盖、不做等待。
- `GetCount` 与 `GetSpace` 是采样瞬间的快照。判断"一定读得到"要靠 `Pop` 的返回值，不要先查数量再取。
- 传结构体时按字节进出：先 `memcpy` 到字节数组再 `Push`，或用 `Pop` 攒够 `sizeof` 字节后 `memcpy` 回结构体。缓冲区总字节数依然必须是 2 的幂，因此能容纳的结构体个数不一定正好是你要的整数。

## 6. 使用约束

1. **只允许一个生产者、一个消费者**（SPSC，Single Producer Single Consumer）。典型是"中断写、主循环读"，或"主循环写、中断读"。不能两端都写，也不能两端都读。
2. **缓冲区长度必须是 2 的幂**（1、2、4、8 直到 `RINGBUF_SIZE_MAX`）。这是内部用按位与代替取模、从而不出现除法指令的前提，`RingBufInit` 会在初始化时校验。
3. **容量等于缓冲区长度**，存满 `u32Size` 字节才算满，不牺牲空槽。游标是 32 位无符号单调计数，靠差值判空判满，回绕由无符号运算自然处理。
4. **`RingBufReset` 不是线程安全的**，只能在两端都停用时调用，不能在生产者还在写入的同时清空。
5. **不做多生产者保护**。如果将来出现第二个写入方，需要在 `Push` 外面自己加临界区。

## 7. 常见误用

```c
/* 错的：同一个游标被两端写，队列内容会错乱 */
void UsrFastIsr(void)     { (void)RingBufPushByte(&sRb, 0x5Au); }   /* 中断 */
void UsrPollInMain(void)  { (void)RingBufPushByte(&sRb, 0x11u); }   /* 主循环 */

/* 错的：长度不是 2 的幂，Init 返回 0，后续所有操作都无效 */
static uint8_t sBad[100];
(void)RingBufInit(&sRb, sBad, 100u);        /* 改成 128 */

/* 错的：在生产者还在跑的时候清空 */
void UsrOnError(void) { RingBufReset(&sRb); }   /* 先停生产者，或改用 Drop 由消费者清 */
```
