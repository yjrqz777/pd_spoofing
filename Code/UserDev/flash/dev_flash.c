#include "dev_flash.h"

#include <string.h>

#include "ch32x035_flash.h"

#define DEV_FLASH_WORDS_PER_PAGE (FLASH_PAGE_SIZE / sizeof(uint32_t))

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

static uint8_t dev_flash_region_valid(dev_flash_region_t region)
{
    return (uint8_t)(dev_flash_region_address(region) != 0U);
}

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

    /* Unwritten bytes in this page have the erased value. */
    memset(page_buffer, 0xFF, sizeof(page_buffer));
    memcpy((uint8_t *)page_buffer, data, len);

    if (FLASH_ROM_ERASE(address, FLASH_PAGE_SIZE) != FLASH_COMPLETE)
    {
        return DEV_FLASH_ERROR_HW;
    }

    if (FLASH_ROM_WRITE(address, page_buffer, FLASH_PAGE_SIZE) != FLASH_COMPLETE)
    {
        return DEV_FLASH_ERROR_HW;
    }

    for (i = 0U; i < FLASH_PAGE_SIZE; ++i)
    {
        if (((const uint8_t *)(uintptr_t)address)[i] != ((const uint8_t *)page_buffer)[i])
        {
            return DEV_FLASH_ERROR_VERIFY;
        }
    }

    return DEV_FLASH_OK;
}

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

    status = dev_flash_read(source, page_buffer, sizeof(page_buffer));
    if (status != DEV_FLASH_OK)
    {
        return status;
    }

    return dev_flash_write(destination, page_buffer, sizeof(page_buffer));
}
