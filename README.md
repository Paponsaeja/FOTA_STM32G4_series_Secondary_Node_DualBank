# APP1\_RECEIVE — STM32G4 OTA Firmware Receiver

APP1 ทำหน้าที่รับ Firmware ใหม่ (APP2) จาก PC ผ่าน UART+DMA
ตรวจสอบความถูกต้องด้วย CRC-16/IBM เขียนลง Internal Flash
แล้วสั่ง reboot ให้ Bootloader กระโดดไปรัน APP2 โดยอัตโนมัติ

---

## สารบัญ

1. [Hardware & เครื่องมือ](#1-hardware--เครื่องมือ)
2. [Flash Memory Layout](#2-flash-memory-layout)
3. [โครงสร้างโปรเจค](#3-โครงสร้างโปรเจค)
4. [สถาปัตยกรรม Library](#4-สถาปัตยกรรม-library)
5. [OTA Protocol Specification](#5-ota-protocol-specification)
6. [OTA State Machine](#6-ota-state-machine)
7. [CRC-16/IBM](#7-crc-16ibm)
8. [Error Codes & NACK Messages](#8-error-codes--nack-messages)
9. [Python OTA Sender (PC Side)](#9-python-ota-sender-pc-side)
10. [ขั้นตอน OTA แบบ End-to-End](#10-ขั้นตอน-ota-แบบ-end-to-end)

---

## 1. Hardware & เครื่องมือ

| รายการ | รายละเอียด |
|--------|------------|
| MCU | **STM32G491RE** |
| IDE | STM32CubeIDE |
| System Clock | HSI → PLL → **170 MHz** |
| UART | **USART2** — 115200 baud, 8N1, No Flow Control |
| DMA | **DMA1 Channel 1** (USART2 RX, Idle-line detection) |
| LED | **PA5** — blink 500 ms แสดงว่าระบบทำงาน |
| Button/EXTI | **PC13** — Rising edge interrupt (reserved) |

---

## 2. Flash Memory Layout

```
Flash Base: 0x08000000                              Total: 512 KB
┌─────────────────────────────────────────────────────────────────┐
│  0x08000000 ──  32 KB  ──  Bootloader                           │
├─────────────────────────────────────────────────────────────────┤
│  0x08008000 ── 238 KB  ──  APP1 Code   ◄─── โปรแกรมนี้          │
│  0x08043800 ──   2 KB  ──  APP1 Header (fw_version, fw_size)    │
├─────────────────────────────────────────────────────────────────┤
│  0x08044000 ── 238 KB  ──  APP2 Code   ◄─── OTA เขียน Firmware  │
│  0x0807F800 ──   2 KB  ──  APP2 Header ◄─── OTA เขียน Metadata  │
└─────────────────────────────────────────────────────────────────┘
```

| Symbol | Address | ขนาด |
|--------|---------|------|
| `APP1_START_ADDR` | `0x08008000` | — |
| `APP1_MAX_SIZE` | — | 238 KB |
| `APP1_HEADER_ADDR` | `0x08043800` | 2 KB |
| `APP2_START_ADDR` | `0x08044000` | — |
| `APP2_MAX_SIZE` | — | 238 KB |
| `APP2_HEADER_ADDR` | `0x0807F800` | 2 KB |

> Flash page size = **2 KB** บน STM32G4
> Flash write ต้องทำทีละ **64-bit (8 bytes)** ตาม datasheet

---

## 3. โครงสร้างโปรเจค

```
APP1_RECEIVE/
│
├── Core/
│   ├── Inc/
│   │   ├── main.h                HAL includes + Error_Handler prototype
│   │   ├── FLASH_LAYOUT.h        Address map ของ Flash ทั้งหมด
│   │   ├── app_header.h          struct APP_Header_t
│   │   ├── crc16.h               prototype Calculate_CRC16()
│   │   ├── flash_driver.h        prototype OTA_EraseFlashRegion(), Internal_Flash_Write()
│   │   ├── ota_protocol.h        OTA types / defines / extern globals / prototypes
│   │   ├── Software_timer.h      struct SoftwareTimer + prototypes
│   │   ├── stm32g4xx_hal_conf.h  HAL config (CubeMX generated)
│   │   └── stm32g4xx_it.h        IRQ handler prototypes (CubeMX generated)
│   │
│   └── Src/
│       ├── main.c                main() + OTA state machine loop + peripheral init
│       ├── crc16.c               CRC-16/IBM implementation
│       ├── flash_driver.c        Internal flash erase & write
│       ├── ota_protocol.c        OTA parse / verify / session + UART DMA callback
│       ├── Software_timer.c      Software timer (uwTick based)
│       ├── stm32g4xx_hal_msp.c   HAL MSP (CubeMX generated)
│       ├── stm32g4xx_it.c        IRQ handlers (CubeMX generated)
│       └── system_stm32g4xx.c    System init (CubeMX generated)
│
├── ota_sender.py                 Python script สำหรับส่ง Firmware จาก PC
├── APP1_RECEIVE.ioc              STM32CubeMX configuration
└── README.md                     เอกสารนี้
```

---

## 4. สถาปัตยกรรม Library

แยก logic ออกเป็น 3 library ตามหน้าที่ — `main.c` เหลือแค่ state machine loop และ peripheral init

```
main.c
 ├── #include "crc16.h"         ← CRC utility (pure function, ไม่มี side-effect)
 ├── #include "flash_driver.h"  ← Flash HAL operations (Erase / Write)
 └── #include "ota_protocol.h"  ← OTA engine (types, globals, parse, verify, session)
```

### 4.1 `crc16` — CRC Utility

```
crc16.h / crc16.c
└── Calculate_CRC16(uint8_t *data, uint16_t length) → uint16_t
```

Pure function ไม่มี dependency กับ global ใด ๆ — นำ reuse ที่อื่นได้ทันที

---

### 4.2 `flash_driver` — Flash Operations

```
flash_driver.h / flash_driver.c
├── OTA_EraseFlashRegion(start_addr, size_bytes) → HAL_StatusTypeDef
│     └── คำนวณ first_page, num_pages → HAL_FLASHEx_Erase()
└── Internal_Flash_Write(address, data, len) → HAL_StatusTypeDef
      └── เขียนทีละ 64-bit (double word) → HAL_FLASH_Program()
```

ไม่มี global variables — รับ address/data ผ่าน parameter เท่านั้น

---

### 4.3 `ota_protocol` — OTA Protocol Engine

```
ota_protocol.h / ota_protocol.c
│
├── Types / Enums
│   ├── OTA_State_t          (IDLE → PARSE → VERIFY → WRITE → ACK → ERROR → COMPLETE → WAIT_UPDATE)
│   ├── OTA_Error_t          (OTA_ERR_NONE .. OTA_ERR_FLASH_ERASE)
│   ├── OTA_ParsedPacket_t   (packet_id, target_addr, data_len, *data_ptr, crc)
│   └── OTA_HeaderPacket_t   (fw_version, fw_size, total_packets) — packed
│
├── Global Variables (defined ใน .c / extern ใน .h)
│   ├── ota_rx_buffer[]      DMA receive buffer (300 bytes)
│   ├── ota_rx_size          ขนาดที่รับได้จริง
│   ├── ota_packet_ready     flag ให้ main loop ทราบว่ามี packet ใหม่
│   ├── ota_state            state ปัจจุบัน
│   ├── parsed_pkt           packet ที่ parse แล้ว
│   ├── ota_last_error       error ล่าสุด
│   ├── expected_packet_id   packet_id ถัดไปที่รอ
│   ├── current_package_count จำนวน data packet ที่เขียนแล้ว
│   ├── total_expected_packages จาก header packet
│   ├── ota_fw_version / ota_fw_size metadata จาก header
│   ├── ota_header_received  flag
│   ├── ota_snapshot_packet_id สำหรับตรวจ progress ของ timeout
│   ├── ota_timeout_retry / ota_nack_retry นับ retry
│   ├── msg_buffer[]         sprintf buffer สำหรับ UART response
│   └── otaTimeoutTimer      software timer สำหรับ OTA timeout
│
└── Functions
    ├── OTA_ResetSession()           reset ตัวแปรทั้งหมดกลับ default
    ├── OTA_ParsePacket(buf,size,pkt) แยก field จาก raw buffer (zero-copy)
    ├── OTA_VerifyPacket(buf,size,pkt) ตรวจ CRC-16 ทั้ง packet body
    ├── OTA_ProcessHeader(pkt)        ประมวล header packet → ตั้งค่า session
    ├── OTA_WriteAppHeader()          เขียน APP_Header_t ลง APP2_HEADER_ADDR
    └── HAL_UARTEx_RxEventCallback()  DMA/UART IRQ callback → set ota_packet_ready
```

---

## 5. OTA Protocol Specification

### 5.1 Frame Format

```
 Byte:  0      1  2    3  4  5  6    7  8     9 .. 9+N-1    9+N  10+N   11+N
       ┌────┬─────────┬─────────────────┬─────────┬───────────┬────────┬────┐
       │0xAA│packet_id│  target_addr    │data_len │   data    │ crc16  │0x55│
       │ 1B │  2B LE  │     4B LE       │  2B LE  │   N B     │ 2B LE  │ 1B │
       └────┴─────────┴─────────────────┴─────────┴───────────┴────────┴────┘
             ◄──────────── CRC16 ครอบคลุมส่วนนี้ทั้งหมด (8+N bytes) ──────────►
```

- **HEAD** `0xAA` — ไม่นับใน CRC
- **TAIL** `0x55` — ไม่นับใน CRC
- **CRC** — ไม่นับตัวเอง
- Total frame = `12 + N` bytes
- `OTA_MAX_PACKET_SIZE = 300` → data สูงสุด **288 bytes** ต่อ packet

### 5.2 Packet Types

| `packet_id` | ประเภท | `data` content |
|-------------|--------|----------------|
| `0x0000` | **Header Packet** | `OTA_HeaderPacket_t` (10 bytes) |
| `0x0001 – 0xFFFE` | **Data Packet** | Firmware chunk (ไม่เกิน 256 bytes) |
| `0xFFFF` | **Command Packet** | `0x01` = UPDATE → trigger reboot |

### 5.3 Header Packet Data (`OTA_HeaderPacket_t`)

```c
typedef struct __attribute__((packed)) {
    uint32_t fw_version;     // version ของ firmware ใหม่
    uint32_t fw_size;        // ขนาด binary ทั้งหมด (bytes)
    uint16_t total_packets;  // จำนวน data packet 1..N
} OTA_HeaderPacket_t;        // รวม 10 bytes
```

### 5.4 UART Responses (STM32 → PC)

| ข้อความ | เมื่อไหร่ |
|---------|----------|
| `HDR_OK:V{ver},SZ{size},PKT{n}\r\n` | รับ Header packet OK |
| `ACK: packet {id} is completed\r\n` | เขียน Flash packet นั้น OK |
| `NACK:{reason}\r\n` | เกิดข้อผิดพลาด |
| `NACK:OTA_ABORT(...)\r\n` | abort ทั้ง session |
| `OTA_COMPLETE_WAIT_FOR_COMMAND\r\n` | รับ firmware ครบ รอ reboot command |
| `REBOOTING...\r\n` | กำลัง NVIC_SystemReset() |
| `Unknown_command\r\n` | command packet ไม่ถูกต้อง |

---

## 6. OTA State Machine

```
                         ┌──────────────────┐
                         │    OTA_IDLE      │◄──── OTA_ResetSession()
                         └────────┬─────────┘      (abort / session ใหม่)
                                  │ ota_packet_ready = true
                                  ▼
                         ┌──────────────────┐
                         │    OTA_PARSE     │
                         └────────┬─────────┘
                          OK      │      FAIL → ota_last_error = OTA_ERR_PARSE
                                  ▼
                         ┌──────────────────┐
                         │   OTA_VERIFY     │ ← ตรวจ CRC / sequence / address range
                         └────────┬─────────┘
                          OK      │      FAIL → ota_last_error = OTA_ERR_CRC / SEQ / ADDR / ...
                                  │
                    ┌─────────────┴─────────────┐
              packet_id == 0             packet_id >= 1
                    │                           │
                    ▼                           ▼
            ProcessHeader()             OTA_WRITE
            OK / FAIL              Flash Erase (packet 1 only)
                    │              Flash Write
                    │              OK / FAIL → OTA_ERR_FLASH_ERASE / FLASH
                    └──────────┬────────────────┘
                               ▼
                         ┌──────────────────┐
                         │     OTA_ACK      │ ← ส่ง ACK กลับ PC
                         └────────┬─────────┘
                    ┌─────────────┴──────────────┐
              ครบทุก packet                 ยังไม่ครบ
                    │                           │
                    ▼                           ▼
             OTA_COMPLETE                   OTA_IDLE
         WriteAppHeader()
             OK / FAIL
                    │
                    ▼
         ┌──────────────────────┐
         │   OTA_WAIT_UPDATE    │ ← รอ packet 0xFFFF / data[0]=0x01
         └──────────┬───────────┘
                    │
                    ▼
            NVIC_SystemReset()
```

```
                    ▼
            ┌──────────────┐
            │  OTA_ERROR   │ ── ส่ง NACK ── ota_nack_retry++
            └──────┬───────┘
          nack < 5 │   nack >= 5
                   │        └──► OTA_ABORT → OTA_ResetSession() → OTA_IDLE
                   ▼
               OTA_IDLE
```

### Timeout Logic

| พารามิเตอร์ | ค่า | คำอธิบาย |
|------------|-----|----------|
| `OTA_TIMEOUT_MS` | 15,000 ms | ระยะเวลา timeout ต่อรอบ |
| `OTA_MAX_RETRY` | 3 | timeout ซ้ำกี่ครั้งก่อน abort |
| `OTA_MAX_NACK` | 5 | NACK ซ้ำกี่ครั้งก่อน abort |

> Timeout ตรวจเฉพาะช่วง `ota_header_received == true` ก่อนถึง `OTA_COMPLETE`
> ใช้ `ota_snapshot_packet_id` เปรียบเทียบกับ `expected_packet_id`
> ถ้าค่าเท่ากัน = ไม่มี progress → นับ retry / ถ้าต่างกัน = มี progress → reset retry

---

## 7. CRC-16/IBM

**Polynomial:** `0xA001` (bit-reversed ของ `0x8005`)
**Initial value:** `0xFFFF`

```c
// crc16.c
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
```

**ขอบเขต CRC ใน OTA frame:**

```
[AA] [id_L id_H] [addr_0 addr_1 addr_2 addr_3] [len_L len_H] [data...N] [crc_L crc_H] [55]
      ◄───────────────── Calculate_CRC16(&buf[1], 2+4+2+N) ─────────────►
```

```c
// ota_protocol.c — OTA_VerifyPacket()
uint16_t crc_len  = 2 + 4 + 2 + pkt->data_len;   // id + addr + len + data
uint16_t calc_crc = Calculate_CRC16(&buf[1], crc_len);
return (calc_crc == pkt->crc);
```

---

## 8. Error Codes & NACK Messages

| Error Code | NACK ที่ส่งกลับ PC | สาเหตุ |
|------------|--------------------|--------|
| `OTA_ERR_PARSE` | `NACK:PARSE(size=N) DATA LOSE` | Frame ผิด format / สั้นเกิน / missing head\|tail |
| `OTA_ERR_CRC` | `NACK:CRC(pkt=N)` | CRC-16 ไม่ตรง |
| `OTA_ERR_NO_HEADER` | `NACK:NO_HEADER` | ได้รับ data packet ก่อนที่จะมี header |
| `OTA_ERR_SEQ` | `NACK:SEQ(got=N,expect=M)` | packet_id ไม่เรียงตามลำดับ |
| `OTA_ERR_ADDR` | `NACK:ADDR(0xXXXX,len=N)` | target_addr อยู่นอก APP2 region |
| `OTA_ERR_FLASH` | `NACK:FLASH(addr=0xXXXX)` | HAL_FLASH_Program() ล้มเหลว |
| `OTA_ERR_HDR_WRITE` | `NACK:HDR_WRITE` | เขียน APP2 Header ล้มเหลว |
| `OTA_ERR_PROCESS_HDR` | `NACK:PROCESS_HDR` | Header data ผิดพลาด (size/version/count) |
| `OTA_ERR_FLASH_ERASE` | `NACK:FLASH_ERASE_ERROR` | HAL_FLASHEx_Erase() ล้มเหลว |
| *(timeout)* | `NACK:TIMEOUT(pkt=N,retry=R/3)` | ไม่มี packet ใหม่ภายใน 15 วินาที |
| *(abort)* | `NACK:OTA_ABORT(timeout max retry)` | timeout เกิน OTA_MAX_RETRY → reset |
| *(abort)* | `NACK:OTA_ABORT(too many nack)` | NACK เกิน OTA_MAX_NACK → reset |

---

## 9. Python OTA Sender (PC Side)

### ติดตั้ง

```bash
pip install pyserial
```

### วิธีใช้

```bash
# รูปแบบพื้นฐาน
python ota_sender.py -p <PORT> -f <HEX_FILE>

# ตัวอย่าง Windows
python ota_sender.py -p COM3 -f APP2_RECEIVE.hex

# ตัวอย่าง Linux / macOS
python ota_sender.py -p /dev/ttyUSB0 -f APP2_RECEIVE.hex

# กำหนด firmware version, chunk size
python ota_sender.py -p COM3 -f APP2_RECEIVE.hex -v 2 -c 128
```

### Arguments

| Flag | Default | คำอธิบาย |
|------|---------|----------|
| `-p / --port` | *(required)* | Serial port (COM3, /dev/ttyUSB0) |
| `-f / --file` | *(required)* | Intel HEX firmware file |
| `-v / --version` | `1` | Firmware version number |
| `-c / --chunk` | `256` | Bytes per data packet (8 – 256) |
| `-b / --baud` | `115200` | Baud rate |

### Intel HEX Parser

รองรับ record types:

| Record Type | คำอธิบาย |
|-------------|----------|
| `0x00` Data | เก็บ binary data + absolute address |
| `0x01` EOF | หยุด parse |
| `0x04` Extended Linear Address | กำหนด upper 16-bit ของ address |
| `0x05` Start Linear Address | entry point (ไม่ใช้ ข้ามไป) |

ช่องว่างระหว่าง record (gaps) จะถูกเติม `0xFF` อัตโนมัติ

### ตัวอย่าง Output

```
============================================================
  OTA Firmware Sender - STM32G4 APP2
============================================================

[1] Parsing Intel HEX: APP2_RECEIVE.hex
  Start address : 0x08044000
  End address   : 0x08047CBC
  Firmware size : 15548 bytes (15.2 KB)
  Chunk size    : 256 bytes
  Total packets : 61
  FW version    : 1

[2] Opening serial port: COM3 @ 115200 baud

[3] Sending HEADER packet (id=0)
    fw_version=1, fw_size=15548, total_packets=61
  HDR_OK:V1,SZ15548,PKT61
  Header -> ACK OK

[4] Sending 61 DATA packets...
  [########################################] 100.0%  61/61  8.2 pkt/s  ETA 0s

  All 61 packets sent successfully!
  Time: 7.4s  (2.1 KB/s)

[5] Waiting for OTA_COMPLETE...
  Received: OTA_COMPLETE_WAIT_FOR_COMMAND

[6] Sending UPDATE command (id=0xFFFF, cmd=0x01)
  Received: REBOOTING...

============================================================
  OTA Complete! Device should be rebooting now.
============================================================
```

---

## 10. ขั้นตอน OTA แบบ End-to-End

```
PC (ota_sender.py)                       STM32 APP1 (Receiver)
─────────────────────────────────────────────────────────────────────
 parse_intel_hex()
 build_header_packet()
                     ── Header (id=0) ──►  OTA_ProcessHeader()
                                           ตรวจ fw_size, total_packets
                     ◄── HDR_OK:... ───    ส่งกลับ info
                     ◄── ACK: pkt 0 ───    → OTA_IDLE

 build_packet(id=1, addr=0x08044000)
                     ── Data (id=1)  ──►  OTA_ParsePacket()
                                          OTA_VerifyPacket()   ← CRC check
                                          OTA_EraseFlashRegion() ← erase ครั้งเดียว
                                          Internal_Flash_Write()
                     ◄── ACK: pkt 1 ───    → OTA_IDLE

 build_packet(id=2, addr=0x08044100)
                     ── Data (id=2)  ──►  Internal_Flash_Write()
                     ◄── ACK: pkt 2 ───    → OTA_IDLE

 ...                        ...                    ...

 build_packet(id=N, last chunk)
                     ── Data (id=N)  ──►  Internal_Flash_Write()
                     ◄── ACK: pkt N ───    current_count == total → OTA_COMPLETE
                                           OTA_WriteAppHeader()
                     ◄── OTA_COMPLETE ──   → OTA_WAIT_UPDATE

 build_update_command()
                     ── CMD (0xFFFF) ──►  NVIC_SystemReset()
                     ◄── REBOOTING... ─

                                       [Bootloader ตรวจ APP2 Header]
                                       [Jump to 0x08044000 → APP2]
─────────────────────────────────────────────────────────────────────
```

---

*Author: saerj — STM32G4 OTA Firmware Update System*
