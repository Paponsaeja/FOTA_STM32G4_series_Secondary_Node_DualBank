/*
 * ota_protocol.c
 *
 *  Created on: Mar 05, 2026
 *      Author: saerj
 */

#include "ota_protocol.h"
#include "app_header.h"
#include "crc16.h"
#include "flash_driver.h"
#include "Software_timer.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------
 * Global variable definitions
 * ------------------------------------------------------------------ */

/* --- DMA receive buffer --- */
uint8_t           ota_rx_buffer[OTA_MAX_PACKET_SIZE];
volatile uint16_t ota_rx_size      = 0;
volatile bool     ota_packet_ready = false;

/* --- State machine --- */
OTA_State_t        ota_state      = OTA_IDLE;
OTA_ParsedPacket_t parsed_pkt;
OTA_Error_t        ota_last_error = OTA_ERR_NONE;

/* --- OTA session counters --- */
uint16_t expected_packet_id      = 0;
uint16_t current_package_count   = 0;
uint16_t total_expected_packages = 0;
uint32_t ota_fw_version          = 0;
uint32_t ota_fw_size             = 0;
bool     ota_header_received     = false;

/* --- Timeout / Retry --- */
uint16_t ota_snapshot_packet_id = 0;
uint8_t  ota_timeout_retry      = 0;
uint8_t  ota_nack_retry          = 0;

/* --- Shared message buffer --- */
char msg_buffer[MSG_BUF_SIZE];

SoftwareTimer otaTimeoutTimer;

/* ===========================================================================
 * Function Implementations
 * ========================================================================= */

/* ---------------------------------------------------------------------------
 * OTA_ResetSession
 * ------------------------------------------------------------------------- */
void OTA_ResetSession(void)
{
    ota_header_received      = false;
    expected_packet_id       = 0;
    current_package_count    = 0;
    total_expected_packages  = 0;
    ota_fw_version           = 0;
    ota_fw_size              = 0;
    ota_timeout_retry        = 0;
    ota_snapshot_packet_id   = 0;
    ota_nack_retry           = 0;
    ota_last_error           = OTA_ERR_NONE;
    ota_state                = OTA_IDLE;
}

/* ---------------------------------------------------------------------------
 * DMA / UART Callback
 * ------------------------------------------------------------------------- */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART2)
    {
    	if (Size >= 9 && ota_rx_buffer[0] == 0xAA)
    	{
    		uint16_t data_len       = (uint16_t)(ota_rx_buffer[7] | (ota_rx_buffer[8] << 8));
    		uint16_t full_frame_size = 12 + data_len;

    		if (Size == full_frame_size)
    		{
    			ota_rx_size      = Size;
    			ota_packet_ready = true;
    		}
    	}
    	extern UART_HandleTypeDef huart2;
    	HAL_UARTEx_ReceiveToIdle_DMA(&huart2, ota_rx_buffer, OTA_MAX_PACKET_SIZE);
    }
}

/* ---------------------------------------------------------------------------
 * OTA_ParsePacket
 * Frame: [0xAA][id 2B][addr 4B][len 2B][data NB][crc 2B][0x55]
 * ------------------------------------------------------------------------- */
bool OTA_ParsePacket(uint8_t *buf, uint16_t size, OTA_ParsedPacket_t *pkt)
{
    if (size < 12) return false;

    uint8_t *ptr = buf;

    if (*ptr++ != 0xAA) return false;

    pkt->packet_id   = (uint16_t)(ptr[0] | (ptr[1] << 8));  ptr += 2;
    pkt->target_addr = (uint32_t)(ptr[0] | (ptr[1] << 8) |
                       ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24));  ptr += 4;
    pkt->data_len    = (uint16_t)(ptr[0] | (ptr[1] << 8));  ptr += 2;

    uint16_t expected_size = 1 + 2 + 4 + 2 + pkt->data_len + 2 + 1;
    if (size != expected_size) return false;

    pkt->data_ptr = ptr;
    ptr += pkt->data_len;

    pkt->crc = (uint16_t)(ptr[0] | (ptr[1] << 8));  ptr += 2;

    if (*ptr != 0x55) return false;

    return true;
}

/* ---------------------------------------------------------------------------
 * OTA_VerifyPacket
 * CRC16 covers entire package body (excluding head 0xAA, CRC field, tail 0x55)
 * = [packet_id 2B][target_addr 4B][data_len 2B][data NB]
 * = buf[1] .. buf[8 + data_len]
 * ------------------------------------------------------------------------- */
bool OTA_VerifyPacket(uint8_t *buf, uint16_t size, OTA_ParsedPacket_t *pkt)
{
    uint16_t crc_len  = 2 + 4 + 2 + pkt->data_len;   /* id + addr + len + data */
    uint16_t calc_crc = Calculate_CRC16(&buf[1], crc_len);
    return (calc_crc == pkt->crc);
}

/* ---------------------------------------------------------------------------
 * OTA_ProcessHeader
 * ------------------------------------------------------------------------- */
bool OTA_ProcessHeader(OTA_ParsedPacket_t *pkt)
{
    if (pkt->data_len != sizeof(OTA_HeaderPacket_t)) return false;

    OTA_HeaderPacket_t *hdr = (OTA_HeaderPacket_t*)pkt->data_ptr;

    if (hdr->fw_size == 0)             return false;
    if (hdr->fw_size > APP2_MAX_SIZE)  return false;
    if (hdr->total_packets == 0)       return false;

    ota_fw_version           = hdr->fw_version;
    ota_fw_size              = hdr->fw_size;
    total_expected_packages  = hdr->total_packets;
    current_package_count    = 0;
    expected_packet_id       = 1;
    ota_header_received      = true;

    ota_snapshot_packet_id   = 1;
    ota_timeout_retry        = 0;
    Timer_Start(&otaTimeoutTimer);

    extern UART_HandleTypeDef huart2;
    int len = snprintf(msg_buffer, MSG_BUF_SIZE,
        "HDR_OK:V%lu,SZ%lu,PKT%d\r\n",
        hdr->fw_version, hdr->fw_size, hdr->total_packets);
    HAL_UART_Transmit(&huart2, (uint8_t*)msg_buffer, len, 200);

    return true;
}

/* ---------------------------------------------------------------------------
 * OTA_WriteAppHeader
 * ------------------------------------------------------------------------- */
bool OTA_WriteAppHeader(void)
{
    APP_Header_t new_hdr;
    memset(&new_hdr, 0xFF, sizeof(APP_Header_t));

    new_hdr.fw_version = ota_fw_version;
    new_hdr.fw_size    = ota_fw_size;

    if (OTA_EraseFlashRegion(APP2_HEADER_ADDR, 2048) != HAL_OK)
        return false;

    return (Internal_Flash_Write(APP2_HEADER_ADDR,
                                 (uint8_t*)&new_hdr,
                                 sizeof(APP_Header_t)) == HAL_OK);
}
