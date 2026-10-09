/**
 * @file    dev_pd.h
 * @brief   USB-PD sink 协议层（Device）：报文编解码、能力报文解析、协商状态机
 *******************************************************************************
 * @note    本模块属于 Device：
 *            - 认识报文和 PDO，但不认识"产品想要几伏"，请求第几档由上层给；
 *            - 向下只调 bsp_pdsink.h 的收发接口，不碰 USBPD 寄存器、不碰 CC 引脚；
 *            - 器件上下文 static 在 dev_pd.c 内，上层拿不到句柄。
 *******************************************************************************
 */

#ifndef __DEV_PD_H__
#define __DEV_PD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "user_global.h"

/** @brief DevPdService() 的调用周期（毫秒）：上层任务必须按这个节拍调
 *  @note  官方例程与旧工程都是 1ms：收到报文后的应答要尽快取走，
 *         取走前 CC 一直停在发送态、中断也是关的，周期越长这段空窗越大。 */
#define DEV_PD_SERVICE_MS  (1u)

/** @brief 一个能力报文最多 7 个数据对象 */
#define DEV_PD_PDO_MAX      (7u)

/** @brief 等能力报文的超时：1000ms（按 DEV_PD_SERVICE_MS 折算成调用次数） */
#define DEV_PD_T_SRCCAP     (1000u / DEV_PD_SERVICE_MS)

/** @brief 等 Accept / PS_RDY 的超时：500ms */
#define DEV_PD_T_ACCEPT     (500u / DEV_PD_SERVICE_MS)

/** @brief 未连接时每隔几次服务做一遍连接检测（求快） */
#define DEV_PD_DET_DIV      (5u)

/** @brief 已连接时的检测间隔：1 秒一次（只为了发现拔线）
 *  @note  检测会临时改动 CC 比较器，对正在进行的协商是干扰，
 *         官方例程在已连接时干脆不再用 CC 判断插拔。不要再加密。 */
#define DEV_PD_DET_LINK     (1000u)

/** @brief 连续检测到几次才算真插上（防抖） */
#define DEV_PD_DET_CONFIRM  (5u)

/** @brief 请求失败几次就报错 */
#define DEV_PD_REQ_TRY      (3u)

/** @brief 电源类型（USB-PD 的四种 PDO） */
typedef enum
{
    E_DEV_PD_PDO_FIXED = 0u,   /**< 固定电压档 */
    E_DEV_PD_PDO_BATTERY,      /**< 电池档 */
    E_DEV_PD_PDO_VARIABLE,     /**< 可变电压档 */
    E_DEV_PD_PDO_APDO,         /**< 可调电压档（PPS） */
    E_DEV_PD_PDO_TYPE_MAX
} eDevPdPdoTypeDef;

/** @brief 从能力报文里解析出来的一个档位 */
typedef struct
{
    uint8_t          u8PdoIndex;      /**< 在能力报文里的序号，发请求时用它 */
    eDevPdPdoTypeDef eType;           /**< 电源类型 */
    uint16_t         u16MinVoltageMv; /**< 最小电压；固定档等于标称电压 */
    uint16_t         u16MaxVoltageMv; /**< 最大电压；固定档与最小相同 */
    uint16_t         u16MaxCurrentMa; /**< 最大电流 */
} tDevPdPdoDef;

/** @brief 协商状态 */
typedef enum
{
    E_DEV_PD_IDLE = 0u,     /**< 未连接或未开始 */
    E_DEV_PD_WAIT_SRC_CAP,  /**< 已连接，等能力报文 */
    E_DEV_PD_WAIT_ACCEPT,   /**< 已发请求，等对方接受 */
    E_DEV_PD_WAIT_PS_RDY,   /**< 已接受，等电源就绪 */
    E_DEV_PD_READY,         /**< 协商完成，电压已到位 */
    E_DEV_PD_ERROR,         /**< 协商失败：超时、被拒、或收到软复位 */
    E_DEV_PD_STATE_MAX
} eDevPdStateDef;

/* ---- 初始化与推进 ---- */

/** @brief 初始化协议层（复位上下文）。BSP 初始化之后调一次 */
void DevPdInit(void);

/** @brief 周期推进：取帧、解帧、跑状态机、管超时重试。放任务里调 */
void DevPdService(void);

/* ---- 协商动作 ---- */

/**
 * @brief  请求一个档位（异步，发出去就返回）
 * @param[in] u8PdoIndex tDevPdPdoDef.u8PdoIndex 拿到的序号
 * @retval E_OK    已受理
 * @retval E_BUSY  上一件事还没完
 * @retval E_ERROR 当前状态不允许请求
 */
eStatusDef DevPdRequestPdo(uint8_t u8PdoIndex);

/** @brief 重新来一遍：复位协议层，回到等能力报文 */
void DevPdRestart(void);

/* ---- 查询 ---- */

/** @brief 当前协商状态 */
eDevPdStateDef DevPdGetState(void);

/** @brief 解析出来的档位个数（只含固定档，可调档不进换档表） */
uint8_t DevPdGetPdoCount(void);

/** @brief 对端能力报文里有没有可调档（PPS）：1 = 支持 */
uint8_t DevPdHasPps(void);

/**
 * @brief  取一个档位
 * @param[in] u8Index 档位表下标（0 到 DevPdGetPdoCount()-1），不是报文里的序号
 * @return 档位指针；越界返回 0
 */
const tDevPdPdoDef *DevPdGetPdo(uint8_t u8Index);

/** @brief 当前生效的档位在报文里的序号；没有则返回 0xFF */
uint8_t DevPdGetActiveIndex(void);

/** @brief 当前生效的电压（mV）；没协商到返回 0 */
uint16_t DevPdGetActiveMv(void);

/** @brief 连接检测：1 = CC 上有连接 */
uint8_t DevPdIsConnected(void);

#ifdef __cplusplus
}
#endif

#endif /* __DEV_PD_H__ */
