/*
 * app_header.h
 *
 *  Created on: Feb 11, 2026
 *      Author: saerj
 */

#ifndef INC_APP_HEADER_H_
#define INC_APP_HEADER_H_

#include "FLASH_LAYOUT.h"

typedef struct __attribute__((packed)) {
    uint32_t fw_version;
    uint32_t fw_size;
} APP_Header_t;

#endif /* INC_APP_HEADER_H_ */
