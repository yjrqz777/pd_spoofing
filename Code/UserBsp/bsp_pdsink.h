/**
 * @file    bsp_pdsink.h
 * @brief   USB-PD 的 PHY 层（BSP）：CC 引脚、USBPD 外设、收发一帧原始报文
 *******************************************************************************
 * @note    本模块属于 BSP，只搬字节、不懂报文含义：
 *            - 中断服务函数 USBPD_IRQHandler 写在本模块的 .c 里，
 *              中断里只清标志、搬字节、置事件位；
 *            - 收到一帧不做任何协议判断，直接放进接收缓冲等上层来取；
 *            - 上层用 BspPdTakeRx() 取走整帧，用 BspPdSend() 发整帧。
 *          唯一漏进本层的协议动作是 GoodCRC：它必须在收完报文后几十微秒内
 *          发出，只能在中断里做；发送侧等对端 GoodCRC 也在本层，理由同上。
 *******************************************************************************
 */

#ifndef __BSP_PDSINK_H__
#define __BSP_PDSINK_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "user_global.h"

/** @brief 一帧 PD 报文的字节数上限（2 字节头 + 7 个数据对象 + CRC） */
#define BSP_PD_FRAME_MAX  (34u)

/** @brief BspPdSend() 里一帧最多重发几次 */
#define BSP_PD_TX_TRY     (3u)

/**
 * @brief 中断里回 GoodCRC 之前等的微秒数
 * @note  对端发完报文后要把接收窗口打开才听得见我们的应答，回太快它收不到。
 *        30 是 WCH 例程标定的值。如果对端一直重发能力报文（说明它没收到
 *        GoodCRC），可以把这里往下调试试。
 */
#define BSP_PD_ACK_DELAY_US  (30u)

/* ---- 初始化 ---- */

/** @brief 时钟、CC 引脚、USBPD 外设、中断优先级、接收模式，一次配好 */
void BspPdPhyInit(void);

/** @brief 复位 PHY 回到接收模式（断连或 SoftReset 之后调） */
void BspPdPhyReset(void);

/* ---- CC 连接检测 ---- */

/**
 * @brief  检测 CC 上有没有插上源端
 * @return 0 = 没插；1 = CC1 有；2 = CC2 有
 * @note   内部把比较器阈值临时切到 0.22V 再读模拟输入位；测完不恢复，
 *         选定通道后必须调 BspPdSelectCc() 把参数恢复成通信用的那套。
 */
uint8_t BspPdDetectCc(void);

/**
 * @brief  选定通信走哪一路 CC，并把比较器参数恢复成 SNK 通信配置
 * @param[in] u8Which 1 = CC1，2 = CC2
 */
void BspPdSelectCc(uint8_t u8Which);

/* ---- 收发 ---- */

/**
 * @brief  发一帧，并等对端的 GoodCRC
 * @param[in] pu8Data 报文内容（不含 CRC，硬件自动补）
 * @param[in] u8Len   字节数
 * @retval E_OK    对端已确认
 * @retval E_BUSY  中断那边正忙着回 GoodCRC
 * @retval E_ERROR 参数不对，或重发 BSP_PD_TX_TRY 次仍没等到确认
 * @note   最坏要阻塞约 2.3ms（3 次重发），只能在主循环上下文调，不能在中断里调。
 */
eStatusDef BspPdSend(const uint8_t *pu8Data, uint8_t u8Len);

/**
 * @brief  取走收到的整帧
 * @param[out] pu8Buf    缓冲，至少 BSP_PD_FRAME_MAX 字节
 * @param[in]  u8BufLen  缓冲能装多少字节
 * @param[out] pu8Len    实际取到多少字节
 * @retval E_OK   取到一帧
 * @retval E_BUSY 还没有新帧
 */
eStatusDef BspPdTakeRx(uint8_t *pu8Buf, uint8_t u8BufLen, uint8_t *pu8Len);

/** @brief 有没有收到新帧（1 = 有，可以取） */
uint8_t BspPdHasRx(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_PDSINK_H__ */
