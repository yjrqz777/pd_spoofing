/**
 * @file    ringbuf.c
 * @brief   单生产者单消费者环形缓冲区实现。
 *
 * @note    发布顺序：生产者先写数据、再更新 u32Tail；消费者先读 u32Tail、再读数据。
 *          两次访问之间插入编译器屏障，防止优化把数据读写挪到游标更新之后，
 *          单核 MCU 上因此不需要关中断、也不需要原子指令。
 */

#include "ringbuf.h"
#include <stddef.h>
#include <string.h>

/* 编译器屏障：阻止内存访问被跨过该点重排。 */
#if defined(__GNUC__) || defined(__clang__)
#define RINGBUF_BARRIER()   __asm__ __volatile__ ("" ::: "memory")
#else
#define RINGBUF_BARRIER()   do { } while (0)
#endif

/**
 * @brief  检查实例是否已经初始化。
 * @return 1 表示可用，0 表示实例为空或还没初始化。
 * @note   未初始化的实例一律当成不可用，函数返回"没做任何事"，而不是去访问空指针。
 */
static uint8_t RingBufIsReady(const tRingBuf *ptBuf)
{
    if ((ptBuf == NULL) || (ptBuf->pu8Buffer == NULL) || (ptBuf->u32Size == 0u))
    {
        return 0u;
    }

    return 1u;
}

uint8_t RingBufInit(tRingBuf *ptBuf, uint8_t *pu8Buffer, uint32_t u32Size)
{
    uint32_t u32Mask;

    if ((ptBuf == NULL) || (pu8Buffer == NULL) || (u32Size == 0u) ||
        (u32Size > RINGBUF_SIZE_MAX))
    {
        return 0u;
    }

    u32Mask = u32Size - 1u;
    if ((u32Size & u32Mask) != 0u)
    {
        return 0u;                                  /* 不是 2 的幂：拒绝，避免留下取模的慢路径 */
    }

    ptBuf->pu8Buffer = pu8Buffer;
    ptBuf->u32Size = u32Size;
    ptBuf->u32Mask = u32Mask;
    ptBuf->u32Head = 0u;
    ptBuf->u32Tail = 0u;

    return 1u;
}

void RingBufReset(tRingBuf *ptBuf)
{
    if (ptBuf == NULL)
    {
        return;
    }

    ptBuf->u32Head = 0u;
    ptBuf->u32Tail = 0u;
}

uint32_t RingBufPush(tRingBuf *ptBuf, const uint8_t *pu8Data, uint32_t u32Len)
{
    uint32_t u32Count;
    uint32_t u32Space;
    uint32_t u32Write;
    uint32_t u32Index;
    uint32_t u32First;

    if ((RingBufIsReady(ptBuf) == 0u) || (pu8Data == NULL) || (u32Len == 0u))
    {
        return 0u;
    }

    u32Count = ptBuf->u32Tail - ptBuf->u32Head;     /* 已缓存字节数，无符号差值自带回绕 */
    u32Space = ptBuf->u32Size - u32Count;
    u32Write = (u32Len < u32Space) ? u32Len : u32Space;
    if (u32Write == 0u)
    {
        return 0u;                                  /* 已满：不覆盖尚未读走的旧数据 */
    }

    u32Index = ptBuf->u32Tail & ptBuf->u32Mask;
    u32First = ptBuf->u32Size - u32Index;           /* 到存储区末尾的连续长度 */
    if (u32First > u32Write)
    {
        u32First = u32Write;
    }

    memcpy(&ptBuf->pu8Buffer[u32Index], pu8Data, u32First);
    if (u32Write > u32First)
    {
        memcpy(&ptBuf->pu8Buffer[0], &pu8Data[u32First], u32Write - u32First);
    }

    RINGBUF_BARRIER();                              /* 数据先落地，再发布写游标 */
    ptBuf->u32Tail += u32Write;

    return u32Write;
}

uint32_t RingBufPop(tRingBuf *ptBuf, uint8_t *pu8Data, uint32_t u32Len)
{
    uint32_t u32Count;
    uint32_t u32Read;
    uint32_t u32Index;
    uint32_t u32First;

    if ((RingBufIsReady(ptBuf) == 0u) || (pu8Data == NULL) || (u32Len == 0u))
    {
        return 0u;
    }

    u32Count = ptBuf->u32Tail - ptBuf->u32Head;
    RINGBUF_BARRIER();                              /* 先读到写游标，再去读它发布的数据 */
    u32Read = (u32Len < u32Count) ? u32Len : u32Count;
    if (u32Read == 0u)
    {
        return 0u;
    }

    u32Index = ptBuf->u32Head & ptBuf->u32Mask;
    u32First = ptBuf->u32Size - u32Index;
    if (u32First > u32Read)
    {
        u32First = u32Read;
    }

    memcpy(pu8Data, &ptBuf->pu8Buffer[u32Index], u32First);
    if (u32Read > u32First)
    {
        memcpy(&pu8Data[u32First], &ptBuf->pu8Buffer[0], u32Read - u32First);
    }

    RINGBUF_BARRIER();                              /* 数据先取走，再发布读游标 */
    ptBuf->u32Head += u32Read;

    return u32Read;
}

uint8_t RingBufPushByte(tRingBuf *ptBuf, uint8_t u8Data)
{
    if (RingBufIsReady(ptBuf) == 0u)
    {
        return 0u;
    }

    if ((ptBuf->u32Tail - ptBuf->u32Head) >= ptBuf->u32Size)
    {
        return 0u;                                  /* 已满 */
    }

    ptBuf->pu8Buffer[ptBuf->u32Tail & ptBuf->u32Mask] = u8Data;
    RINGBUF_BARRIER();
    ptBuf->u32Tail++;

    return 1u;
}

uint8_t RingBufPopByte(tRingBuf *ptBuf, uint8_t *pu8Data)
{
    uint32_t u32Index;

    if ((RingBufIsReady(ptBuf) == 0u) || (pu8Data == NULL))
    {
        return 0u;
    }

    if (ptBuf->u32Tail == ptBuf->u32Head)
    {
        return 0u;                                  /* 为空 */
    }

    u32Index = ptBuf->u32Head & ptBuf->u32Mask;
    RINGBUF_BARRIER();
    *pu8Data = ptBuf->pu8Buffer[u32Index];
    RINGBUF_BARRIER();
    ptBuf->u32Head++;

    return 1u;
}

uint32_t RingBufPeek(const tRingBuf *ptBuf, uint8_t *pu8Data, uint32_t u32Len)
{
    uint32_t u32Count;
    uint32_t u32Peek;
    uint32_t u32Index;
    uint32_t u32First;

    if ((RingBufIsReady(ptBuf) == 0u) || (pu8Data == NULL) || (u32Len == 0u))
    {
        return 0u;
    }

    u32Count = ptBuf->u32Tail - ptBuf->u32Head;
    RINGBUF_BARRIER();
    u32Peek = (u32Len < u32Count) ? u32Len : u32Count;
    if (u32Peek == 0u)
    {
        return 0u;
    }

    u32Index = ptBuf->u32Head & ptBuf->u32Mask;
    u32First = ptBuf->u32Size - u32Index;
    if (u32First > u32Peek)
    {
        u32First = u32Peek;
    }

    memcpy(pu8Data, &ptBuf->pu8Buffer[u32Index], u32First);
    if (u32Peek > u32First)
    {
        memcpy(&pu8Data[u32First], &ptBuf->pu8Buffer[0], u32Peek - u32First);
    }

    return u32Peek;                                 /* 读游标不动：数据仍在缓冲区里 */
}

uint32_t RingBufDrop(tRingBuf *ptBuf, uint32_t u32Len)
{
    uint32_t u32Count;
    uint32_t u32Drop;

    if ((RingBufIsReady(ptBuf) == 0u) || (u32Len == 0u))
    {
        return 0u;
    }

    u32Count = ptBuf->u32Tail - ptBuf->u32Head;
    RINGBUF_BARRIER();
    u32Drop = (u32Len < u32Count) ? u32Len : u32Count;

    RINGBUF_BARRIER();
    ptBuf->u32Head += u32Drop;

    return u32Drop;
}

uint32_t RingBufGetCount(const tRingBuf *ptBuf)
{
    if (RingBufIsReady(ptBuf) == 0u)
    {
        return 0u;
    }

    return ptBuf->u32Tail - ptBuf->u32Head;
}

uint32_t RingBufGetSpace(const tRingBuf *ptBuf)
{
    if (RingBufIsReady(ptBuf) == 0u)
    {
        return 0u;
    }

    return ptBuf->u32Size - (ptBuf->u32Tail - ptBuf->u32Head);
}

uint8_t RingBufIsEmpty(const tRingBuf *ptBuf)
{
    return (RingBufGetCount(ptBuf) == 0u) ? 1u : 0u;
}

uint8_t RingBufIsFull(const tRingBuf *ptBuf)
{
    if (RingBufIsReady(ptBuf) == 0u)
    {
        return 0u;
    }

    return (RingBufGetSpace(ptBuf) == 0u) ? 1u : 0u;
}
