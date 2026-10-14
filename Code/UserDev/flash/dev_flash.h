/**
 * @file dev_flash.h
 * @brief CH32X035 片内 Flash 主区和备份区接口。
 * @details
 * 使用第 246 页作为主区、第 247 页作为备份区，每页 256 字节。
 * 这两页必须从应用程序链接地址范围中预留。
 */

#ifndef _DEV_FLASH_H_
#define _DEV_FLASH_H_

#include <stdint.h>

/**
 * @def FLASH_BASE_ADDR
 * @brief CH32X035 Flash 别名区基地址。
 * @details 链接脚本从 0x00000000 映射同一片物理 Flash。
 */
#define FLASH_BASE_ADDR       (0x08000000UL)

/**
 * @def FLASH_PAGE_SIZE
 * @brief Flash 页大小，单位为字节。
 */
#define FLASH_PAGE_SIZE       (256UL)

/**
 * @def FLASH_PAGE_ADDR(n)
 * @brief 根据页号计算 Flash 页起始地址。
 * @param n 页号。
 */
#define FLASH_PAGE_ADDR(n)    (FLASH_BASE_ADDR + ((uint32_t)(n) * FLASH_PAGE_SIZE))

/** @brief 主区所在页号。 */
#define DEV_FLASH_MAIN_PAGE   (246U)
/** @brief 备份区所在页号。 */
#define DEV_FLASH_BACKUP_PAGE (247U)
/** @brief 主区起始地址。 */
#define DEV_FLASH_MAIN_ADDR   FLASH_PAGE_ADDR(DEV_FLASH_MAIN_PAGE)
/** @brief 备份区起始地址。 */
#define DEV_FLASH_BACKUP_ADDR FLASH_PAGE_ADDR(DEV_FLASH_BACKUP_PAGE)
/** @brief 每个存储区的容量，单位为字节。 */
#define DEV_FLASH_REGION_SIZE ((uint16_t)FLASH_PAGE_SIZE)

/**
 * @brief Flash 存储区选择。
 */
typedef enum
{
    DEV_FLASH_REGION_MAIN = 0, /**< 主区，第 246 页。 */
    DEV_FLASH_REGION_BACKUP   /**< 备份区，第 247 页。 */
} dev_flash_region_t;

/**
 * @brief Flash 操作结果。
 */
typedef enum
{
    DEV_FLASH_OK = 0,       /**< 操作成功。 */
    DEV_FLASH_ERROR_PARAM,  /**< 参数无效或存储区选择无效。 */
    DEV_FLASH_ERROR_HW,     /**< Flash 擦除或写入操作失败。 */
    DEV_FLASH_ERROR_VERIFY  /**< 写入后数据校验失败。 */
} dev_flash_status_t;

/**
 * @brief 从指定存储区读取数据。
 * @param[in] region 目标存储区。
 * @param[out] data 接收数据的缓冲区。
 * @param[in] len 读取长度，范围为 1 至 DEV_FLASH_REGION_SIZE 字节。
 * @return 操作结果，成功返回 DEV_FLASH_OK。
 */
dev_flash_status_t dev_flash_read(dev_flash_region_t region,
                                  uint8_t *data,
                                  uint16_t len);

/**
 * @brief 擦除指定存储区所在的整页。
 * @param[in] region 目标存储区。
 * @return 操作结果，成功返回 DEV_FLASH_OK。
 * @note 擦除后该页所有字节均为 0xFF。
 */
dev_flash_status_t dev_flash_erase(dev_flash_region_t region);

/**
 * @brief 擦除并写入指定存储区，然后校验整页内容。
 * @param[in] region 目标存储区。
 * @param[in] data 待写入数据。
 * @param[in] len 写入长度，范围为 1 至 DEV_FLASH_REGION_SIZE 字节。
 * @return 操作结果，成功返回 DEV_FLASH_OK。
 * @warning 每次调用都会擦除整页；len 之后的字节将保持为 0xFF。
 * @note 需要支持掉电恢复时，可先写入备份区，再更新主区。
 */
dev_flash_status_t dev_flash_write(dev_flash_region_t region,
                                   const uint8_t *data,
                                   uint16_t len);

/**
 * @brief 将一个存储区的整页数据复制到另一个存储区。
 * @param[in] source 源存储区。
 * @param[in] destination 目标存储区。
 * @return 操作结果，成功返回 DEV_FLASH_OK。
 * @note 目标页会先被擦除，再写入并校验 256 字节。
 */
dev_flash_status_t dev_flash_copy(dev_flash_region_t source,
                                  dev_flash_region_t destination);

#endif /* _DEV_FLASH_H_ */
