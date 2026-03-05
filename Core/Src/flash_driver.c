/*
 * flash_driver.c
 *
 *  Created on: Mar 05, 2026
 *      Author: saerj
 */

#include "flash_driver.h"

/* ---------------------------------------------------------------------------
 * OTA_EraseFlashRegion
 * ------------------------------------------------------------------------- */
HAL_StatusTypeDef OTA_EraseFlashRegion(uint32_t start_addr, uint32_t size_bytes)
{
    FLASH_EraseInitTypeDef erase_cfg;
    uint32_t page_error = 0;
    HAL_StatusTypeDef status;

    uint32_t first_page = (start_addr - 0x08000000) / 2048;
    uint32_t num_pages  = (size_bytes + 2047) / 2048;

    HAL_FLASH_Unlock();

    erase_cfg.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_cfg.Banks     = FLASH_BANK_1;
    erase_cfg.Page      = first_page;
    erase_cfg.NbPages   = num_pages;

    status = HAL_FLASHEx_Erase(&erase_cfg, &page_error);

    HAL_FLASH_Lock();
    return status;
}

/* ---------------------------------------------------------------------------
 * Internal_Flash_Write
 * ------------------------------------------------------------------------- */
HAL_StatusTypeDef Internal_Flash_Write(uint32_t address, uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef status = HAL_OK;

    HAL_FLASH_Unlock();

    for (uint16_t i = 0; i < len; i += 8) {
        uint64_t double_word = 0;
        for (int j = 0; j < 8; j++) {
            uint8_t byte_val = ((i + j) < len) ? data[i + j] : 0xFF;
            double_word |= ((uint64_t)byte_val << (j * 8));
        }
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address + i, double_word);
        if (status != HAL_OK) break;
    }

    HAL_FLASH_Lock();
    return status;
}
