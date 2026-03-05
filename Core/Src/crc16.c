/*
 * crc16.c
 *
 *  Created on: Mar 05, 2026
 *      Author: saerj
 */

#include "crc16.h"

/* ---------------------------------------------------------------------------
 * Calculate_CRC16  (CRC-16/IBM)
 * ------------------------------------------------------------------------- */
uint16_t Calculate_CRC16(uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
        }
    }
    return crc;
}
