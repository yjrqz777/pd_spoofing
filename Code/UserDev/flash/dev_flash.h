#ifndef _DEV_FLASH_H_
#define _DEV_FLASH_H_

#include <stdint.h>

/*
 * CH32X035 FLASH is visible at 0x08000000 in the alias region.
 * The linker maps the same physical FLASH from 0x00000000.
 * Pages 246 and 247 are reserved for persistent data.
 */
#define FLASH_BASE_ADDR       (0x08000000UL)
#define FLASH_PAGE_SIZE       (256UL)
#define FLASH_PAGE_ADDR(n)    (FLASH_BASE_ADDR + ((uint32_t)(n) * FLASH_PAGE_SIZE))

#define DEV_FLASH_MAIN_PAGE   (246U)
#define DEV_FLASH_BACKUP_PAGE (247U)
#define DEV_FLASH_MAIN_ADDR   FLASH_PAGE_ADDR(DEV_FLASH_MAIN_PAGE)
#define DEV_FLASH_BACKUP_ADDR FLASH_PAGE_ADDR(DEV_FLASH_BACKUP_PAGE)

#define DEV_FLASH_REGION_SIZE ((uint16_t)FLASH_PAGE_SIZE)

typedef enum
{
    DEV_FLASH_REGION_MAIN = 0,
    DEV_FLASH_REGION_BACKUP
} dev_flash_region_t;

typedef enum
{
    DEV_FLASH_OK = 0,
    DEV_FLASH_ERROR_PARAM,
    DEV_FLASH_ERROR_HW,
    DEV_FLASH_ERROR_VERIFY
} dev_flash_status_t;

/*
 * Read up to one page from the selected region.
 * len must be in the range [1, FLASH_PAGE_SIZE].
 */
dev_flash_status_t dev_flash_read(dev_flash_region_t region,
                                  uint8_t *data,
                                  uint16_t len);

/*
 * Erase the selected page, program the supplied bytes, and verify them.
 * Every write erases the whole 256-byte page first. Bytes not supplied are
 * left as 0xFF. Keep the backup page current before replacing the main page
 * when the stored data must survive an interrupted update.
 */
dev_flash_status_t dev_flash_write(dev_flash_region_t region,
                                   const uint8_t *data,
                                   uint16_t len);

/* Erase the selected 256-byte page. */
dev_flash_status_t dev_flash_erase(dev_flash_region_t region);

/* Copy the complete 256-byte contents of one page to the other page. */
dev_flash_status_t dev_flash_copy(dev_flash_region_t source,
                                  dev_flash_region_t destination);

#endif /* _DEV_FLASH_H_ */
