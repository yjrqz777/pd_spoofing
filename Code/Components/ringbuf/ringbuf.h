/**
 * @file    ringbuf.h
 * @brief   单生产者单消费者环形缓冲区（SPSC，Single Producer Single Consumer）接口。
 *
 * @note    本模块属于 Components：不依赖板级外设、操作系统或动态内存，只用 C 标准库的
 *          内存拷贝。存储区由调用方提供，可同时创建多个实例。
 *
 *          三条使用约束，违反任意一条都会读写错乱：
 *          1. 只允许一个生产者、一个消费者。典型是"中断里写入、主循环里读出"，
 *             或"主循环写入、中断里读出"；不能两端都写或两端都读。
 *          2. 生产者只调用 Push / PushByte，消费者只调用 Pop / PopByte / Peek / Drop。
 *             两端各写自己的游标、只读对方的游标，因此入队出队都不需要关中断。
 *          3. 缓冲区长度必须是 2 的幂，内部用按位与代替取模，不做除法。
 *
 *          容量等于缓冲区长度：存满 u32Size 字节才算满，不牺牲空槽。
 *          游标单调递增，靠无符号差值处理回绕，因此不会出现"写满一圈后又变成空"的误判。
 *
 *          最小用法：
 *          @code
 *          static uint8_t  sBuf[64];       // 长度必须是 2 的幂
 *          static tRingBuf sRb;
 *
 *          RingBufInit(&sRb, sBuf, sizeof(sBuf));   // 初始化阶段调用一次
 *
 *          // 生产者（中断里）：
 *          (void)RingBufPush(&sRb, pu8Data, u32Len);
 *
 *          // 消费者（主循环里）：
 *          uint8_t u8Byte;
 *          while (RingBufPopByte(&sRb, &u8Byte) != 0u) { 处理 u8Byte; }
 *          @endcode
 */

#ifndef __RINGBUF_H__
#define __RINGBUF_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief 缓冲区长度上限。
 *
 * @note  游标是无符号 32 位单调计数，差值判空判满要求真实数据量不超过 2 的 31 次方，
 *        超过这个长度就失去分辨空与满的能力。
 */
#define RINGBUF_SIZE_MAX    (0x80000000u)

/** @brief 环形缓冲区实例。存储区与实例都由调用方持有。 */
typedef struct
{
    uint8_t  *pu8Buffer;        /**< 调用方提供的存储区，字节数等于 u32Size */
    uint32_t  u32Size;          /**< 存储区字节数，必须是 2 的幂 */
    uint32_t  u32Mask;          /**< u32Size - 1，把单调游标折成存储区下标 */
    volatile uint32_t u32Head;  /**< 读游标：只有消费者写，生产者只读 */
    volatile uint32_t u32Tail;  /**< 写游标：只有生产者写，消费者只读 */
} tRingBuf;

/**
 * @brief  初始化环形缓冲区。
 * @param[out] ptBuf     缓冲区实例。
 * @param[in]  pu8Buffer 调用方提供的存储区，生命周期必须长于实例。
 * @param[in]  u32Size   存储区字节数，必须是 2 的幂，且不超过 RINGBUF_SIZE_MAX。
 * @return 1 表示成功，0 表示参数无效。
 * @note   可在任意上下文调用，但必须保证调用时两端都没有在用这个实例。
 */
uint8_t RingBufInit(tRingBuf *ptBuf, uint8_t *pu8Buffer, uint32_t u32Size);

/**
 * @brief 清空缓冲区，丢弃其中所有数据。
 * @note  不是线程安全的：只能在两端都停用时调用，不能在生产者写入的同时调用。
 */
void RingBufReset(tRingBuf *ptBuf);

/**
 * @brief  生产者接口：写入一段数据。
 * @param[in,out] ptBuf    缓冲区实例。
 * @param[in]     pu8Data  待写入数据。
 * @param[in]     u32Len   待写入字节数。
 * @return 实际写入的字节数。空间不足时只写能容纳的部分，不做覆盖，
 *         返回 0 表示一个字节也没写进去。
 * @note   只有生产者可以调用。
 */
uint32_t RingBufPush(tRingBuf *ptBuf, const uint8_t *pu8Data, uint32_t u32Len);

/**
 * @brief  消费者接口：读出一段数据并将其移出缓冲区。
 * @param[in,out] ptBuf   缓冲区实例。
 * @param[out]    pu8Data 接收数据的目标缓冲区。
 * @param[in]     u32Len  最多读出的字节数。
 * @return 实际读出的字节数，返回 0 表示缓冲区为空。
 * @note   只有消费者可以调用。
 */
uint32_t RingBufPop(tRingBuf *ptBuf, uint8_t *pu8Data, uint32_t u32Len);

/**
 * @brief  生产者接口：写入一个字节。
 * @param[in,out] ptBuf  缓冲区实例。
 * @param[in]     u8Data 待写入的字节。
 * @return 1 表示成功，0 表示缓冲区已满。
 * @note   只有生产者可以调用。
 */
uint8_t RingBufPushByte(tRingBuf *ptBuf, uint8_t u8Data);

/**
 * @brief  消费者接口：读出一个字节。
 * @param[in,out] ptBuf   缓冲区实例。
 * @param[out]    pu8Data 接收该字节的地址。
 * @return 1 表示成功，0 表示缓冲区为空。
 * @note   只有消费者可以调用。
 */
uint8_t RingBufPopByte(tRingBuf *ptBuf, uint8_t *pu8Data);

/**
 * @brief  消费者接口：查看一段数据，但不把它们移出缓冲区。
 * @param[in]  ptBuf   缓冲区实例。
 * @param[out] pu8Data 接收数据的目标缓冲区。
 * @param[in]  u32Len  最多查看的字节数。
 * @return 实际查看的字节数，返回 0 表示缓冲区为空。
 * @note   只有消费者可以调用。用于"先看帧头再决定整帧怎么取"的解析场景，
 *         查看后缓冲区内容不变，需要真正取走时再调用 Pop 或 Drop。
 */
uint32_t RingBufPeek(const tRingBuf *ptBuf, uint8_t *pu8Data, uint32_t u32Len);

/**
 * @brief  消费者接口：丢弃一段数据。
 * @param[in,out] ptBuf 缓冲区实例。
 * @param[in]     u32Len 最多丢弃的字节数。
 * @return 实际丢弃的字节数，返回 0 表示缓冲区为空。
 * @note   只有消费者可以调用。用于跳过不关心的数据，语义等于读出后扔掉，但不占用临时缓冲区。
 */
uint32_t RingBufDrop(tRingBuf *ptBuf, uint32_t u32Len);

/**
 * @brief  取当前已缓存的字节数。
 * @return 可读出的字节数，0 表示缓冲区为空。
 * @note   两端都可调用。返回值是采样瞬间的快照，多任务下会立刻变化，
 *         不要用它来做"一定读得到"的判断，直接调 Pop 看返回值。
 */
uint32_t RingBufGetCount(const tRingBuf *ptBuf);

/**
 * @brief  取当前剩余可写入的字节数。
 * @return 还能写入的字节数，0 表示缓冲区已满。
 * @note   两端都可调用，快照语义同 RingBufGetCount。
 */
uint32_t RingBufGetSpace(const tRingBuf *ptBuf);

/** @brief 判断缓冲区是否为空。1 表示空，0 表示有数据。 */
uint8_t RingBufIsEmpty(const tRingBuf *ptBuf);

/** @brief 判断缓冲区是否已满。1 表示满，0 表示还有空间。 */
uint8_t RingBufIsFull(const tRingBuf *ptBuf);

#ifdef __cplusplus
}
#endif

#endif /* __RINGBUF_H__ */
