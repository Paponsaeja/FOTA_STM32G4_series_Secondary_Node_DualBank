/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : APP1 - OTA Receiver for APP2 Firmware
  ******************************************************************************
  * Flash Layout:
  *   Bootloader   : 0x08000000 (32 KB)
  *   APP1 Code    : 0x08008000 (238 KB)
  *   APP1 Header  : 0x08043800 (2 KB)
  *   APP2 Code    : 0x08044000 (238 KB)  <- เขียน firmware ที่นี่
  *   APP2 Header  : 0x0807F800 (2 KB)    <- เขียน metadata ที่นี่
  *
  * OTA Protocol:
  *   packet_id == 0  -> Header packet  (OTA_HeaderPacket_t)
  *   packet_id >= 1  -> Data  packet   (firmware chunk)
  *
  * Packet Frame:
  *   [0xAA][packet_id 2B][target_addr 4B][data_len 2B][data NB][crc16 2B][0x55]
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "app_header.h"
#include "stdbool.h"
#include "string.h"
#include "stdio.h"
#include "Software_timer.h"
#include <stdint.h>
#include "crc16.h"
#include "flash_driver.h"
#include "ota_protocol.h"

/* Private typedef -----------------------------------------------------------*/
/* APP1 Header ที่เก็บใน Flash ที่ 0x08043800 */
__attribute__((section(".app_header_section"))) const APP_Header_t app_header = {
    .fw_version = 0x00000001,
    .fw_size    = 0x00005DCC,
};

/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;
DMA_HandleTypeDef  hdma_usart2_rx;

/* USER CODE BEGIN PV */
SoftwareTimer ledTimer;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);

/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(void)
{
    HAL_Init();

    /* USER CODE BEGIN Init */
    SCB->VTOR = APP1_START_ADDR;
    __enable_irq();
    /* USER CODE END Init */

    SystemClock_Config();

    MX_GPIO_Init();
    MX_DMA_Init();
    MX_USART2_UART_Init();

    /* USER CODE BEGIN 2 */
    Timer_Init(&ledTimer, 500);
    Timer_Start(&ledTimer);

    Timer_Init(&otaTimeoutTimer, OTA_TIMEOUT_MS);
    Timer_Start(&otaTimeoutTimer);

    ota_snapshot_packet_id = 0;
    ota_timeout_retry      = 0;

    /* เปิดรับ packet แรก */
    HAL_UARTEx_ReceiveToIdle_DMA(&huart2, ota_rx_buffer, OTA_MAX_PACKET_SIZE);
    /* USER CODE END 2 */

    while (1)
    {
        /* ------------------------------------------------------------------
         * LED blink — แสดงว่าระบบยังทำงาน
         * ------------------------------------------------------------------ */
        if (Timer_Expired(&ledTimer)) {
            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
            Timer_Start(&ledTimer);
        }

        /*
         * OTA Timeout Check
         * ทำงานเฉพาะช่วงที่รับ data packet อยู่ (หลัง header ถึงก่อน complete)
         */
        if (ota_header_received && ota_state != OTA_COMPLETE && ota_state != OTA_WAIT_UPDATE)
        {
            if (Timer_Expired(&otaTimeoutTimer))
            {
                if (expected_packet_id == ota_snapshot_packet_id)
                {
                    /* ไม่มี progress — packet_id ยังไม่เปลี่ยน */
                    ota_timeout_retry++;

                    int len = snprintf(msg_buffer, MSG_BUF_SIZE,
                        "NACK:TIMEOUT(pkt=%d,retry=%d/%d)\r\n",
                        expected_packet_id,
                        ota_timeout_retry,
                        OTA_MAX_RETRY);
                    HAL_UART_Transmit(&huart2, (uint8_t*)msg_buffer, len, 200);

                    if (ota_timeout_retry >= OTA_MAX_RETRY)
                    {
                        /* หมด retry → abort OTA ทั้งหมด */
                        HAL_UART_Transmit(&huart2, (uint8_t*)"NACK:OTA_ABORT(timeout max retry)\r\n", 35, 300);
                        OTA_ResetSession();
                        Timer_Start(&otaTimeoutTimer);  /* restart timer สำหรับ session ใหม่ */
                        continue;                        /* ข้ามไปรอบถัดไปของ while */
                    }
                }
                else
                {
                    /* มี progress → reset retry counter */
                    ota_timeout_retry = 0;
                }

                /* อัปเดต snapshot และ restart timer ทุกครั้งที่หมด */
                ota_snapshot_packet_id = expected_packet_id;
                Timer_Start(&otaTimeoutTimer);
            }
        }

        /* ==================================================================
         * OTA State Machine
         * ================================================================ */
        switch (ota_state)
        {
            /* --------------------------------------------------------------
             * IDLE: รอ flag จาก DMA callback
             * ------------------------------------------------------------ */
            case OTA_IDLE:
                if (ota_packet_ready) {
                    ota_packet_ready = false;
                    ota_state = OTA_PARSE;
                }
                break;

            /* --------------------------------------------------------------
             * PARSE: แยก field จาก raw buffer
             * ------------------------------------------------------------ */
            case OTA_PARSE:
                if (OTA_ParsePacket(ota_rx_buffer, ota_rx_size, &parsed_pkt)) {
                    ota_state = OTA_VERIFY;
                } else {
                    ota_last_error = OTA_ERR_PARSE;
                    ota_state = OTA_ERROR;
                }
                break;

            /* --------------------------------------------------------------
             * VERIFY: ตรวจ CRC, ลำดับ, address range
             * ------------------------------------------------------------ */
            case OTA_VERIFY:
            {
                if (!OTA_VerifyPacket(ota_rx_buffer, ota_rx_size, &parsed_pkt)) {
                    ota_last_error = OTA_ERR_CRC;
                    ota_state = OTA_ERROR;
                    break;
                }

                if (parsed_pkt.packet_id == 0) {
                    if (!OTA_ProcessHeader(&parsed_pkt)) {
                        ota_last_error = OTA_ERR_PROCESS_HDR;
                        ota_state = OTA_ERROR;
                        break;
                    }
                    ota_state = OTA_ACK;
                    break;
                }

                if (!ota_header_received) {
                    ota_last_error = OTA_ERR_NO_HEADER;
                    ota_state = OTA_ERROR;
                    break;
                }
                if (parsed_pkt.packet_id < expected_packet_id) {
                    /* duplicate — ACK ซ้ำได้เลย */
                    ota_state = OTA_ACK;
                    break;
                } else if (parsed_pkt.packet_id > expected_packet_id) {
                    ota_last_error = OTA_ERR_SEQ;
                    ota_state = OTA_ERROR;
                    break;
                }

                uint32_t write_end = parsed_pkt.target_addr + parsed_pkt.data_len;
                if (parsed_pkt.target_addr < APP2_START_ADDR || write_end > APP2_HEADER_ADDR) {
                    ota_last_error = OTA_ERR_ADDR;
                    ota_state = OTA_ERROR;
                    break;
                }

                ota_state = OTA_WRITE;
                break;
            }

            /* --------------------------------------------------------------
             * WRITE: เขียน firmware chunk ลง APP2 flash
             * ------------------------------------------------------------ */
            case OTA_WRITE:
                if (parsed_pkt.packet_id == 1) {
                    /* ลบ APP2 firmware region ทั้งหมดก่อน data packet แรก */
                    if (OTA_EraseFlashRegion(APP2_START_ADDR, APP2_MAX_SIZE) != HAL_OK) {
                        ota_last_error = OTA_ERR_FLASH_ERASE;
                        ota_state = OTA_ERROR;
                        break;
                    }
                }
                if (Internal_Flash_Write(parsed_pkt.target_addr,
                                         parsed_pkt.data_ptr,
                                         parsed_pkt.data_len) == HAL_OK)
                {
                    current_package_count++;
                    expected_packet_id++;
                    ota_state = OTA_ACK;
                } else {
                    ota_last_error = OTA_ERR_FLASH;
                    ota_state = OTA_ERROR;
                }
                break;

            /* --------------------------------------------------------------
             * ACK: ส่งผลสำเร็จ แล้วเช็คว่าครบหรือยัง
             * ------------------------------------------------------------ */
            case OTA_ACK:
            {
                int len = snprintf(msg_buffer, MSG_BUF_SIZE,
                    "ACK: packet %d is completed\r\n", parsed_pkt.packet_id);
                HAL_UART_Transmit(&huart2, (uint8_t*)msg_buffer, len, 100);

                ota_snapshot_packet_id = expected_packet_id;
                ota_timeout_retry      = 0;
                ota_nack_retry = 0;
                Timer_Start(&otaTimeoutTimer);

                if (parsed_pkt.packet_id >= 1 &&
                    current_package_count >= total_expected_packages)
                {
                    ota_state = OTA_COMPLETE;
                } else {
                    ota_state = OTA_IDLE;
                }
                break;
            }

            /* --------------------------------------------------------------
             * ERROR: ส่ง NACK แล้วรีเซ็ตกลับ IDLE
             * ------------------------------------------------------------ */
            case OTA_ERROR:
            {
                int len = 0;
                switch (ota_last_error)
                {
                    case OTA_ERR_PARSE:
                        len = snprintf(msg_buffer, MSG_BUF_SIZE,
                            "NACK:PARSE(size=%d) DATA LOSE \r\n", ota_rx_size);
                        break;
                    case OTA_ERR_CRC:
                        len = snprintf(msg_buffer, MSG_BUF_SIZE,
                            "NACK:CRC(pkt=%d)\r\n", parsed_pkt.packet_id);
                        break;
                    case OTA_ERR_NO_HEADER:
                        len = snprintf(msg_buffer, MSG_BUF_SIZE,
                            "NACK:NO_HEADER\r\n");
                        break;
                    case OTA_ERR_SEQ:
                        len = snprintf(msg_buffer, MSG_BUF_SIZE,
                            "NACK:SEQ(got=%d,expect=%d)\r\n",
                            parsed_pkt.packet_id, expected_packet_id);
                        break;
                    case OTA_ERR_ADDR:
                        len = snprintf(msg_buffer, MSG_BUF_SIZE,
                            "NACK:ADDR(0x%08lX,len=%d)\r\n",
                            parsed_pkt.target_addr, parsed_pkt.data_len);
                        break;
                    case OTA_ERR_FLASH:
                        len = snprintf(msg_buffer, MSG_BUF_SIZE,
                            "NACK:FLASH(addr=0x%08lX)\r\n",
                            parsed_pkt.target_addr);
                        break;
                    case OTA_ERR_HDR_WRITE:
                        len = snprintf(msg_buffer, MSG_BUF_SIZE,
                            "NACK:HDR_WRITE\r\n");
                        break;
                    case OTA_ERR_PROCESS_HDR:
                        len = snprintf(msg_buffer, MSG_BUF_SIZE,
                            "NACK:PROCESS_HDR\r\n");
                        break;
                    case OTA_ERR_FLASH_ERASE:
                        len = snprintf(msg_buffer, MSG_BUF_SIZE,
                            "NACK:FLASH_ERASE_ERROR\r\n");
                        break;
                    default:
                        len = snprintf(msg_buffer, MSG_BUF_SIZE,
                            "NACK:UNKNOWN\r\n");
                        break;
                }

                if (len > 0) {
                    HAL_UART_Transmit(&huart2, (uint8_t*)msg_buffer, len, 200);
                }

                ota_snapshot_packet_id = expected_packet_id;
                ota_nack_retry++;
                    if (ota_nack_retry >= OTA_MAX_NACK) {
                        HAL_UART_Transmit(&huart2,
                            (uint8_t*)"NACK:OTA_ABORT(too many nack)\r\n", 31, 300);
                        OTA_ResetSession();
                        ota_nack_retry = 0;
                    }
                Timer_Start(&otaTimeoutTimer);

                ota_last_error = OTA_ERR_NONE;
                ota_state = OTA_IDLE;
                break;
            }

            /* --------------------------------------------------------------
             * COMPLETE: เขียน APP2 Header แล้วรอ command
             * ------------------------------------------------------------ */
            case OTA_COMPLETE:
            {
                if (OTA_WriteAppHeader()) {
                    HAL_UART_Transmit(&huart2,
                        (uint8_t*)"OTA_COMPLETE_WAIT_FOR_COMMAND\r\n", 31, 500);
                    ota_state = OTA_WAIT_UPDATE;
                } else {
                    ota_last_error = OTA_ERR_HDR_WRITE;
                    ota_state = OTA_ERROR;
                }
                break;
            }

            /* --------------------------------------------------------------
             * WAIT_UPDATE: รอ command 0xFFFF/0x01 จาก PC แล้ว reboot
             * ------------------------------------------------------------ */
            case OTA_WAIT_UPDATE:
            {
                if (ota_packet_ready)
                {
                    ota_packet_ready = false;
                    if (OTA_ParsePacket(ota_rx_buffer, ota_rx_size, &parsed_pkt))
                    {
                        if (parsed_pkt.packet_id == 0xFFFF &&
                            parsed_pkt.data_len  == 1 &&
                            parsed_pkt.data_ptr[0] == OTA_CMD_UPDATE)
                        {
                            HAL_UART_Transmit(&huart2,
                                (uint8_t*)"REBOOTING...\r\n", 14, 200);
                            HAL_Delay(200);
                            NVIC_SystemReset();
                        } else {
                            HAL_UART_Transmit(&huart2,
                                (uint8_t*)"Unknown_command\r\n", 17, 200);
                        }
                    }
                }
                break;
            }

        } /* end switch */
    } /* end while */
}

/* ===========================================================================
 * USER CODE BEGIN 4 — Function Implementations
 * ========================================================================= */
/* USER CODE END 4 */

/* ===========================================================================
 * HAL / Peripheral Init (CubeMX generated)
 * ========================================================================= */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM            = RCC_PLLM_DIV4;
    RCC_OscInitStruct.PLL.PLLN            = 85;
    RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ            = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR            = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) Error_Handler();
}

static void MX_USART2_UART_Init(void)
{
    huart2.Instance                    = USART2;
    huart2.Init.BaudRate               = 115200;
    huart2.Init.WordLength             = UART_WORDLENGTH_8B;
    huart2.Init.StopBits               = UART_STOPBITS_1;
    huart2.Init.Parity                 = UART_PARITY_NONE;
    huart2.Init.Mode                   = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling           = UART_OVERSAMPLING_16;
    huart2.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    huart2.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
    huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart2) != HAL_OK)                                             Error_Handler();
    if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) Error_Handler();
    if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK) Error_Handler();
    if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK)                               Error_Handler();
}

static void MX_DMA_Init(void)
{
    __HAL_RCC_DMAMUX1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin  = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin   = GPIO_PIN_5;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file; (void)line;
}
#endif
