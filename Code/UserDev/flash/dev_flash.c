/**
 * @file dev_flash.c
 * @brief CH32X035 片内 Flash 主区与备份区驱动实现。
 * @author 段志炫
 * @date 2026-10-15
 * @version 0.1.1
 * @par 修改记录
 * 2026-10-15 按项目注释规范完善文件、宏、静态函数和逻辑段说明。
 */

/* ========================================================================== */
/* 依赖声明 */
/* ========================================================================== */

#include "dev_flash.h"

#include <string.h>

#include "ch32x035_flash.h"

/* ========================================================================== */
/* 内部配置 */
/* ========================================================================== */

#define DEV_FLASH_WORDS_PER_PAGE (FLASH_PAGE_SIZE / sizeof(uint32_t)) /* 单页写入缓冲区的 32 位字数，固定为 64。 */

/* ========================================================================== */
/* 内部辅助函数 */
/* ========================================================================== */

/**
 * @brief 获取存储区对应的 Flash 起始地址。
 * @param[in] region 存储区选择，取值为主区或备份区。
 * @return 存储区起始地址；region 无效时返回 0。
 * @note 地址使用 CH32X035 Flash 别名区映射。
 */
static uint32_t dev_flash_region_address(dev_flash_region_t region)
{
    switch (region)
    {
        case DEV_FLASH_REGION_MAIN:
            return DEV_FLASH_MAIN_ADDR;

        case DEV_FLASH_REGION_BACKUP:
            return DEV_FLASH_BACKUP_ADDR;

        default:
            return 0U;
    }
}

/**
 * @brief 检查存储区选择是否有效。
 * @param[in] region 存储区选择，取值为主区或备份区。
 * @return 有效返回非 0，无效返回 0。
 * @note 通过存储区地址是否为 0 判断选择值是否有效。
 */
static uint8_t dev_flash_region_valid(dev_flash_region_t region)
{
    return (uint8_t)(dev_flash_region_address(region) != 0U);
}

/* ========================================================================== */
/* 对外接口 */
/* ========================================================================== */

/**
 * @brief 从指定存储区读取数据。
 * @param[in] region 目标存储区，取值为主区或备份区。
 * @param[out] data 接收缓冲区，容量至少为 len 字节。
 * @param[in] len 读取长度，单位：字节，范围：1 至 256。
 * @return 操作结果，成功返回 DEV_FLASH_OK。
 * @note 读取通过内存映射直接访问，不执行擦除或写入。
 */
dev_flash_status_t dev_flash_read(dev_flash_region_t region,
                                  uint8_t *data,
                                  uint16_t len)
{
    uint32_t address = dev_flash_region_address(region);

    if ((address == 0U) || (data == NULL) ||
        (len == 0U) || (len > FLASH_PAGE_SIZE))
    {
        return DEV_FLASH_ERROR_PARAM;
    }

    memcpy(data, (const void *)(uintptr_t)address, len);
    return DEV_FLASH_OK;
}

/**
 * @brief 擦除指定存储区所在的整页。
 * @param[in] region 目标存储区，取值为主区或备份区。
 * @return 操作结果，成功返回 DEV_FLASH_OK。
 * @note 擦除范围为 256 字节，擦除后数据为 0xFF。
 */
dev_flash_status_t dev_flash_erase(dev_flash_region_t region)
{
    uint32_t address = dev_flash_region_address(region);

    if (address == 0U)
    {
        return DEV_FLASH_ERROR_PARAM;
    }

    if (FLASH_ROM_ERASE(address, FLASH_PAGE_SIZE) != FLASH_COMPLETE)
    {
        return DEV_FLASH_ERROR_HW;
    }

    return DEV_FLASH_OK;
}

/**
 * @brief 擦除并写入指定存储区，然后校验整页内容。
 * @param[in] region 目标存储区，取值为主区或备份区。
 * @param[in] data 待写入数据缓冲区，容量至少为 len 字节。
 * @param[in] len 写入长度，单位：字节，范围：1 至 256。
 * @return 操作结果，成功返回 DEV_FLASH_OK。
 * @note 每次调用都会擦除整页；未提供的数据字节填充为 0xFF。
 */
dev_flash_status_t dev_flash_write(dev_flash_region_t region,
                                   const uint8_t *data,
                                   uint16_t len)
{
    uint32_t address = dev_flash_region_address(region);
    uint32_t page_buffer[DEV_FLASH_WORDS_PER_PAGE];
    uint16_t i;

    if ((address == 0U) || (data == NULL) ||
        (len == 0U) || (len > FLASH_PAGE_SIZE))
    {
        return DEV_FLASH_ERROR_PARAM;
    }

    /* 先将未提供的数据字节设置为擦除态，确保每页按 256 字节对齐写入。 */
    memset(page_buffer, 0xFF, sizeof(page_buffer));
    memcpy((uint8_t *)page_buffer, data, len);

    /* 擦除目标页，满足 Flash 写入前必须先擦除的要求。 */
    if (FLASH_ROM_ERASE(address, FLASH_PAGE_SIZE) != FLASH_COMPLETE)
    {
        return DEV_FLASH_ERROR_HW;
    }

    /* 按 WCH 库要求写入整页数据。 */
    if (FLASH_ROM_WRITE(address, page_buffer, FLASH_PAGE_SIZE) != FLASH_COMPLETE)
    {
        return DEV_FLASH_ERROR_HW;
    }

    /* 逐字节比较 Flash 内容，确认整页写入结果。 */
    for (i = 0U; i < FLASH_PAGE_SIZE; ++i)
    {
        if (((const uint8_t *)(uintptr_t)address)[i] != ((const uint8_t *)page_buffer)[i])
        {
            return DEV_FLASH_ERROR_VERIFY;
        }
    }

    return DEV_FLASH_OK;
}

/**
 * @brief 将源存储区的整页数据复制到目标存储区。
 * @param[in] source 源存储区，取值为主区或备份区。
 * @param[in] destination 目标存储区，取值为主区或备份区。
 * @return 操作结果，成功返回 DEV_FLASH_OK。
 * @note 源和目标相同时返回参数错误；目标内容会被覆盖。
 */
dev_flash_status_t dev_flash_copy(dev_flash_region_t source,
                                  dev_flash_region_t destination)
{
    uint8_t page_buffer[FLASH_PAGE_SIZE];
    dev_flash_status_t status;

    if (!dev_flash_region_valid(source) ||
        !dev_flash_region_valid(destination) ||
        (source == destination))
    {
        return DEV_FLASH_ERROR_PARAM;
    }

    /* 先缓存源页数据，避免目标操作影响源数据。 */
    status = dev_flash_read(source, page_buffer, sizeof(page_buffer));
    if (status != DEV_FLASH_OK)
    {
        return status;
    }

    /* 通过统一写入接口完成目标页擦除、写入和校验。 */
    return dev_flash_write(destination, page_buffer, sizeof(page_buffer));
}
