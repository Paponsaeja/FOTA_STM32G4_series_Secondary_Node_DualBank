/*
 * FLASH_LAYOUT.h
 *
 *  Created on: Feb 11, 2026
 *      Author: saerj
 */

#ifndef INC_FLASH_LAYOUT_H_
#define INC_FLASH_LAYOUT_H_

/* 1. Bootloader Section (32 KB) */
#define BL_START_ADDR         0x08000000

/* Application 1 Section (Total 240 KB) */
/* พื้นที่เนื้อโปรแกรม (238 KB) */
#define APP1_START_ADDR       0x08008000
#define APP1_MAX_SIZE         (238 * 1024)
/* Header ของ APP1 อยู่ Page สุดท้ายของ Slot 1 (2 KB) */
#define APP1_HEADER_ADDR      0x08043800

/* Application 2 Section (Total 240 KB) */
/* พื้นที่ว่างรอรับ Firmware ใหม่ (238 KB) */
#define APP2_START_ADDR       0x08044000
#define APP2_MAX_SIZE         (238 * 1024)
/* Header ของ APP2 อยู่ Page สุดท้ายของ Slot 2 (2 KB) */
#define APP2_HEADER_ADDR      0x0807F800

#endif /* INC_FLASH_LAYOUT_H_ */
