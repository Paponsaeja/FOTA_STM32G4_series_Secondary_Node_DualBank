/*
 * ota_protocol.h
 *
 *  Created on: Mar 05, 2026
 *      Author: saerj
 */

#ifndef INC_OTA_PROTOCOL_H_
#define INC_OTA_PROTOCOL_H_

#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include "Software_timer.h"

/* ------------------------------------------------------------------
 * Defines
 * ------------------------------------------------------------------ */
#define OTA_MAX_PACKET_SIZE   300       /* max frame size */
#define MSG_BUF_SIZE           64       /* sprintf buffer */
#define OTA_CMD_UPDATE        0x01      /* command UPDATE FIRMWARE */
#define OTA_TIMEOUT_MS        15000     /* timeout per round */
#define OTA_MAX_RETRY         3         /* retry max before abort */
#define OTA_MAX_NACK          5         /* nack max */

/* ------------------------------------------------------------------
 * OTA State Machine
 * ------------------------------------------------------------------ */
typedef enum {
    OTA_IDLE,
    OTA_PARSE,
    OTA_VERIFY,
    OTA_WRITE,
    OTA_ACK,
    OTA_ERROR,
    OTA_COMPLETE,
    OTA_WAIT_UPDATE
} OTA_State_t;

typedef enum {
    OTA_ERR_NONE = 0,
    OTA_ERR_PARSE,
    OTA_ERR_CRC,
    OTA_ERR_NO_HEADER,
    OTA_ERR_SEQ,
    OTA_ERR_ADDR,
    OTA_ERR_FLASH,
    OTA_ERR_HDR_WRITE,
    OTA_ERR_PROCESS_HDR,
    OTA_ERR_FLASH_ERASE
} OTA_Error_t;

/* ------------------------------------------------------------------
 * Parsed packet (zero-copy: data_ptr -> ota_rx_buffer)
 * ------------------------------------------------------------------ */
typedef struct {
    uint16_t  packet_id;
    uint32_t  target_addr;
    uint16_t  data_len;
    uint8_t  *data_ptr;
    uint16_t  crc;
} OTA_ParsedPacket_t;

/* ------------------------------------------------------------------
 * Header Packet (packet_id == 0)
 * ------------------------------------------------------------------ */
typedef struct __attribute__((packed)) {
    uint32_t fw_version;
    uint32_t fw_size;
    uint16_t total_packets;
} OTA_HeaderPacket_t;

/* ------------------------------------------------------------------
 * Extern global variables
 * ------------------------------------------------------------------ */
extern uint8_t           ota_rx_buffer[OTA_MAX_PACKET_SIZE];
extern volatile uint16_t ota_rx_size;
extern volatile bool     ota_packet_ready;

extern OTA_State_t        ota_state;
extern OTA_ParsedPacket_t parsed_pkt;
extern OTA_Error_t        ota_last_error;

extern uint16_t expected_packet_id;
extern uint16_t current_package_count;
extern uint16_t total_expected_packages;
extern uint32_t ota_fw_version;
extern uint32_t ota_fw_size;
extern bool     ota_header_received;

extern uint16_t ota_snapshot_packet_id;
extern uint8_t  ota_timeout_retry;
extern uint8_t  ota_nack_retry;

extern char msg_buffer[MSG_BUF_SIZE];

extern SoftwareTimer otaTimeoutTimer;

/* ------------------------------------------------------------------
 * Function prototypes
 * ------------------------------------------------------------------ */
void OTA_ResetSession(void);
bool OTA_ParsePacket(uint8_t *buf, uint16_t size, OTA_ParsedPacket_t *pkt);
bool OTA_VerifyPacket(uint8_t *buf, uint16_t size, OTA_ParsedPacket_t *pkt);
bool OTA_ProcessHeader(OTA_ParsedPacket_t *pkt);
bool OTA_WriteAppHeader(void);

#endif /* INC_OTA_PROTOCOL_H_ */
