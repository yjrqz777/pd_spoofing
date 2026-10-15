/**
 * @file dev_pd.h
 * @brief   USB-PD sink 协议层（Device）：报文编解码、能力报文解析、协商状态机
 *******************************************************************************
 * @note    本模块属于 Device：
 *            - 认识报文和 PDO，但不认识"产品想要几伏"，请求第几档由上层给；
 *            - 向下只调 bsp_pdsink.h 的收发接口，不碰 USBPD 寄存器、不碰 CC 引脚；
 *            - 器件上下文 static 在 dev_pd.c 内，上层拿不到句柄。
 *******************************************************************************
 
 * @author 段志炫
 * @date 2026-10-15
 * @version 0.1.1
 * @par 修改记录
 * 2026-10-15 按项目注释规范补齐文件和接口说明。*/

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

/**
 * @brief 初始化 USB-PD 协议上下文
 * @param[in] 无 无输入参数。
 * @return 无返回值。
 * @note 调用前确认参数有效，并在模块规定的上下文中调用。
 */
void DevPdInit(void);

/**
 * @brief 推进 USB-PD 协商状态机
 * @param[in] 无 无输入参数。
 * @return 无返回值。
 * @note 由上层按 DEV_PD_SERVICE_MS 周期调用，以及时处理收发和超时。
 */
void DevPdService(void);

/* ---- 协商动作 ---- */

/**
 * @brief 请求对端提供指定 PDO 档位
 * @param[in] u8PdoIndex PDO 在对端能力报文中的序号，范围为 1 至已解析档位数。
 * @return 操作状态，具体成功、忙碌或错误值由调用接口定义。
 * @note 请求为异步操作；调用返回仅表示请求已受理，不表示电源协商完成。
 */
eStatusDef DevPdRequestPdo(uint8_t u8PdoIndex);

/**
 * @brief 复位协议上下文并重新开始协商
 * @param[in] 无 无输入参数。
 * @return 无返回值。
 * @note 调用前确认参数有效，并在模块规定的上下文中调用。
 */
void DevPdRestart(void);

/* ---- 查询 ---- */

/**
 * @brief 读取当前协商状态
 * @param[in] 无 无输入参数。
 * @return 函数执行结果或查询值，取值含义见函数说明。
 * @note 调用前确认参数有效，并在模块规定的上下文中调用。
 */
eDevPdStateDef DevPdGetState(void);

/**
 * @brief 读取已解析的固定 PDO 数量
 * @param[in] 无 无输入参数。
 * @return 函数执行结果或查询值，取值含义见函数说明。
 * @note 调用前确认参数有效，并在模块规定的上下文中调用。
 */
uint8_t DevPdGetPdoCount(void);

/**
 * @brief 查询对端是否支持 PPS
 * @param[in] 无 无输入参数。
 * @return 函数执行结果或查询值，取值含义见函数说明。
 * @note 调用前确认参数有效，并在模块规定的上下文中调用。
 */
uint8_t DevPdHasPps(void);

/**
 * @brief 读取指定索引的 PDO 档位
 * @param[in] u8Index PDO 档位数组下标，单位：项，从 0 开始。
 * @return 函数执行结果或查询值，取值含义见函数说明。
 * @note u8Index 是档位数组下标，从 0 开始，不是能力报文中的 PDO 序号。
 */
const tDevPdPdoDef *DevPdGetPdo(uint8_t u8Index);

/**
 * @brief 读取当前生效 PDO 在报文中的序号
 * @param[in] 无 无输入参数。
 * @return 函数执行结果或查询值，取值含义见函数说明。
 * @note 未完成协商时返回 0xFF。
 */
uint8_t DevPdGetActiveIndex(void);

/**
 * @brief 读取当前协商电压
 * @param[in] 无 无输入参数。
 * @return 当前任务或查询结果，单位和取值含义见函数说明。
 * @note 未完成协商时返回 0。
 */
uint16_t DevPdGetActiveMv(void);

/**
 * @brief 读取 CC 连接状态
 * @param[in] 无 无输入参数。
 * @return 函数执行结果或查询值，取值含义见函数说明。
 * @note 返回值 1 表示已连接，0 表示未连接。
 */
uint8_t DevPdIsConnected(void);

#ifdef __cplusplus
}
#endif

#endif /* __DEV_PD_H__ */
