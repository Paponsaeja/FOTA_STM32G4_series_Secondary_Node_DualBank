/*
 * flash_driver.h
 *
 *  Created on: Mar 05, 2026
 *      Author: saerj
 */

#ifndef INC_FLASH_DRIVER_H_
#define INC_FLASH_DRIVER_H_

#include "main.h"

HAL_StatusTypeDef OTA_EraseFlashRegion(uint32_t start_addr, uint32_t size_bytes);
HAL_StatusTypeDef Internal_Flash_Write(uint32_t address, uint8_t *data, uint16_t len);

#endif /* INC_FLASH_DRIVER_H_ */
