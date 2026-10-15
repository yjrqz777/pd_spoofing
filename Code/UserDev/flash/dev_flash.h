/**
 * @file dev_flash.h
 * @brief CH32X035 片内 Flash 主区与备份区接口。
 * @author 段志炫
 * @date 2026-10-15
 * @version 0.1.1
 * @par 修改记录
 * 2026-10-15 按项目注释规范完善文件、宏、类型和函数说明。
 */

#ifndef _DEV_FLASH_H_
#define _DEV_FLASH_H_

/* ========================================================================== */
/* 依赖声明 */
/* ========================================================================== */

#include <stdint.h>

/* ========================================================================== */
/* Flash 地址与容量定义 */
/* ========================================================================== */

#define FLASH_BASE_ADDR       (0x08000000UL) /**< CH32X035 Flash 别名区基地址，链接地址 0x00000000 映射同一片物理 Flash。 */
#define FLASH_PAGE_SIZE       (256UL) /**< Flash 单页容量，单位：字节，固定为 256 字节。 */
#define FLASH_PAGE_ADDR(n)    (FLASH_BASE_ADDR + ((uint32_t)(n) * FLASH_PAGE_SIZE)) /**< 根据页号计算页起始地址，n 为页号。 */

#define DEV_FLASH_MAIN_PAGE   (246U) /**< 主区页号，范围：246。 */
#define DEV_FLASH_BACKUP_PAGE (247U) /**< 备份区页号，范围：247。 */
#define DEV_FLASH_MAIN_ADDR   FLASH_PAGE_ADDR(DEV_FLASH_MAIN_PAGE) /**< 主区起始地址：0x0800F600。 */
#define DEV_FLASH_BACKUP_ADDR FLASH_PAGE_ADDR(DEV_FLASH_BACKUP_PAGE) /**< 备份区起始地址：0x0800F700。 */
#define DEV_FLASH_REGION_SIZE ((uint16_t)FLASH_PAGE_SIZE) /**< 单个存储区容量，单位：字节，范围：0 至 256。 */

/* ========================================================================== */
/* 存储区与操作状态 */
/* ========================================================================== */

/**
 * @brief Flash 存储区选择。
 */
typedef enum
{
    DEV_FLASH_REGION_MAIN = 0, /**< 主区选择值，映射第 246 页。 */
    DEV_FLASH_REGION_BACKUP   /**< 备份区选择值，映射第 247 页。 */
} dev_flash_region_t;

/**
 * @brief Flash 操作结果。
 */
typedef enum
{
    DEV_FLASH_OK = 0,       /**< 操作成功。 */
    DEV_FLASH_ERROR_PARAM,  /**< 参数或存储区选择无效。 */
    DEV_FLASH_ERROR_HW,     /**< Flash 擦除或写入操作失败。 */
    DEV_FLASH_ERROR_VERIFY  /**< 写入后的整页数据校验失败。 */
} dev_flash_status_t;

/* ========================================================================== */
/* 对外接口 */
/* ========================================================================== */

/**
 * @brief 从指定存储区读取数据。
 * @param[in] region 目标存储区，取值为主区或备份区。
 * @param[out] data 接收缓冲区，容量至少为 len 字节。
 * @param[in] len 读取长度，单位：字节，范围：1 至 256。
 * @return 操作结果，成功返回 DEV_FLASH_OK。
 * @note 只读取请求长度，不会修改 Flash 内容。
 */
dev_flash_status_t dev_flash_read(dev_flash_region_t region,
                                  uint8_t *data,
                                  uint16_t len);

/**
 * @brief 擦除指定存储区所在的整页。
 * @param[in] region 目标存储区，取值为主区或备份区。
 * @return 操作结果，成功返回 DEV_FLASH_OK。
 * @note 擦除后该页 256 字节均为 0xFF。
 */
dev_flash_status_t dev_flash_erase(dev_flash_region_t region);

/**
 * @brief 擦除并写入指定存储区，然后校验整页内容。
 * @param[in] region 目标存储区，取值为主区或备份区。
 * @param[in] data 待写入数据缓冲区，容量至少为 len 字节。
 * @param[in] len 写入长度，单位：字节，范围：1 至 256。
 * @return 操作结果，成功返回 DEV_FLASH_OK。
 * @note 每次调用都会擦除整页；len 之后的字节写为 0xFF。需要掉电恢复时，应先更新备份区，再更新主区。
 */
dev_flash_status_t dev_flash_write(dev_flash_region_t region,
                                   const uint8_t *data,
                                   uint16_t len);

/**
 * @brief 将源存储区的整页数据复制到目标存储区。
 * @param[in] source 源存储区，取值为主区或备份区。
 * @param[in] destination 目标存储区，取值为主区或备份区。
 * @return 操作结果，成功返回 DEV_FLASH_OK。
 * @note 源和目标不能是同一个存储区；目标页会先擦除，再写入并校验 256 字节。
 */
dev_flash_status_t dev_flash_copy(dev_flash_region_t source,
                                  dev_flash_region_t destination);

#endif /* _DEV_FLASH_H_ */
