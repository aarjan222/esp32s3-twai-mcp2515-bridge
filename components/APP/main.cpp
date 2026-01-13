#include <stdio.h>
#include "string.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

extern "C"
{
#include "can.h"
#include "mcp2515.h"
}
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#include "CanManager.hpp"
#include "can.hpp"

#define PIN_NUM_MISO 39
#define PIN_NUM_MOSI 40
#define PIN_NUM_CLK 41
#define PIN_NUM_CS 38

constexpr char TAG[] = "BidirectionalCAN";

CAN::CanManager canManager;
#define std_id_comm // Uncomment for standard ID, comment for extended ID

static void printCanRxMessage(const char *source,
                              uint32_t can_id,
                              const uint8_t *data,
                              uint8_t dlc,
                              bool is_extended,
                              bool is_rtr)
{
    char id_str[9];

    if (is_extended)
    {
        // 29-bit extended ID → 8 hex digits
        snprintf(id_str, sizeof(id_str), "%08lX", can_id);
    }
    else
    {
        // 11-bit standard ID → 3 hex digits
        snprintf(id_str, sizeof(id_str), "%03lX", can_id & 0x7FF);
    }

    char payload_str[3 * 8 + 1]; // "AA BB CC DD EE FF GG HH"
    payload_str[0] = '\0';

    if (dlc > 0 && !is_rtr)
    {
        for (uint8_t i = 0; i < dlc; i++)
        {
            char byte_str[4];
            snprintf(byte_str, sizeof(byte_str), "%02X ", data[i]);
            strncat(payload_str,
                    byte_str,
                    sizeof(payload_str) - strlen(payload_str) - 1);
        }
    }

    ESP_LOGW(TAG,
             "%s Received: ID: 0x%s, DLC: %d, Extended: %s, RTR: %s, Data: %s",
             source,
             id_str,
             dlc,
             is_extended ? "Yes" : "No",
             is_rtr ? "Yes" : "No",
             (dlc > 0 && !is_rtr) ? payload_str : "N/A");
}

void handleReceivedMessage(const twai_message_t &CanMessage)
{
    ESP_LOGI(TAG, "TWAI Message received!");

    printCanRxMessage(
        "TWAI",
        CanMessage.identifier,
        CanMessage.data,
        CanMessage.data_length_code,
        CanMessage.extd,
        CanMessage.rtr);
}

// Fixed: Declare as struct, not array
CAN_FRAME_t can_frame_tx;
CAN_FRAME_t can_frame_rx;

bool SPI_Init(void)
{
    printf("Hello from SPI_Init!\n\r");
    esp_err_t ret;

    // Configuration for the SPI bus
    spi_bus_config_t bus_cfg = {};
    bus_cfg.miso_io_num = PIN_NUM_MISO;
    bus_cfg.mosi_io_num = PIN_NUM_MOSI;
    bus_cfg.sclk_io_num = PIN_NUM_CLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 0;

    // Define MCP2515 SPI device configuration
    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.mode = 0;
    dev_cfg.clock_speed_hz = 10 * 1000 * 1000; // 10 MHz
    dev_cfg.spics_io_num = PIN_NUM_CS;
    dev_cfg.queue_size = 8;

    // Initialize SPI bus
    ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);

    // Add MCP2515 SPI device to the bus
    ret = spi_bus_add_device(SPI2_HOST, &dev_cfg, &MCP2515_Object->spi);
    ESP_ERROR_CHECK(ret);

    return true;
}

void MCP_RW_Task(void *arg)
{
    ESP_LOGI(TAG, "Starting bidirectional CAN communication task");

    // Test data for MCP2515 transmission
    uint8_t mcp_test_data[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22};
#ifdef std_id_comm
    uint32_t mcp_std_id = 0x403;
#else
    uint32_t mcp_extd_id = 0X080CFAFC;
#endif

    // Test data for TWAI transmission
    uint8_t twai_test_data[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
#ifdef std_id_comm
    uint32_t twai_std_id = 0x704;
#else
    uint32_t twai_extd_id = 0x0817FCFA;
#endif
    ESP_LOGI(TAG, "Starting CAN communication loop...");

    while (1)
    {
// ============ Send test message from TWAI ============
#ifdef std_id_comm
        esp_err_t ret = canManager.transmit(twai_std_id, twai_test_data, 8, 0, 0, 0, 0, 0);
#else
        esp_err_t ret = canManager.transmit(twai_extd_id, twai_test_data, 8, 1, 0, 0, 0, 0);
#endif
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "TWAI TX: Failed to transmit. Error: %s", esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(100));

// ============ Send test message from MCP2515 ============
#ifdef std_id_comm
        can_frame_tx->can_id = mcp_std_id;
#else
        can_frame_tx->can_id = mcp_extd_id | CAN_EFF_FLAG;
#endif
        can_frame_tx->can_dlc = 8;
        memcpy(can_frame_tx->data, mcp_test_data, 8);

        ERROR_t tx_result = MCP2515_sendMessageAfterCtrlCheck(can_frame_tx);
        if (tx_result == ERROR_ALLTXBUSY)
        {
            ESP_LOGW(TAG, "All TX buffers busy.");
        }

        // Check for received messages
        if (MCP2515_checkReceive())
        {
            ERROR_t rx_result = MCP2515_readMessageAfterStatCheck(can_frame_rx);
            if (rx_result == ERROR_OK)
            {
                ESP_LOGI(TAG, "MCP 2515 Message received!");
                bool is_extended = (can_frame_rx->can_id & CAN_EFF_FLAG) != 0;
                bool is_rtr = (can_frame_rx->can_id & CAN_RTR_FLAG) != 0;

                uint32_t id = can_frame_rx->can_id &
                              (is_extended ? CAN_EFF_MASK : CAN_SFF_MASK);

                printCanRxMessage(
                    "MCP2515",
                    id,
                    can_frame_rx->data,
                    can_frame_rx->can_dlc,
                    is_extended,
                    is_rtr);
            }
            else
            {
                ESP_LOGE(TAG, "Failed to read message. Error: %d", rx_result);
            }
        }

        // Check for errors
        if (MCP2515_checkError())
        {
            uint8_t error_flags = MCP2515_getErrorFlags();
            ESP_LOGW(TAG, "CAN Error detected. Flags: 0x%02X", error_flags);

            if (error_flags & (EFLG_RX0OVR | EFLG_RX1OVR))
            {
                ESP_LOGW(TAG, "RX buffer overflow detected, clearing...");
                MCP2515_clearRXnOVR();
            }
        }

        // Update test data for next iteration (optional - creates changing pattern)
        mcp_test_data[0] = (mcp_test_data[0] + 1) & 0xFF;
        twai_test_data[0] = (twai_test_data[0] + 1) & 0xFF;

        vTaskDelay(1000); // Main loop delay
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "Bidirectional CAN Communication Demo");
    ESP_LOGI(TAG, "====================================");

    // ============ Initialize TWAI (ESP32 CAN) ============
    ESP_LOGI(TAG, "Initializing TWAI driver...");
    CAN::Driver canDriver; // TX and RX pins
    if (canDriver.init(250) != ESP_OK)
    {
        ESP_LOGE(TAG, "TWAI driver initialization failed!");
    }
    ESP_LOGI(TAG, "TWAI driver initialized successfully");

    if (canDriver.start() != ESP_OK)
    {
        ESP_LOGE(TAG, "TWAI driver start failed!");
    }
    ESP_LOGI(TAG, "TWAI driver started successfully");

    canManager.setMessageCallback(handleReceivedMessage);
    canManager.startReceiving();
    ESP_LOGI(TAG, "TWAI receiver started");

    ESP_LOGI(TAG, "Initializing MCP2515...");
    if (MCP2515_init() != ERROR_OK)
    {
        ESP_LOGE(TAG, "MCP2515 initialization failed.");
        return;
    }
    ESP_LOGI(TAG, "MCP2515 initialized successfully");

    ESP_LOGI(TAG, "Initializing SPI interface...");
    if (SPI_Init())
    {
        ESP_LOGI(TAG, "SPI interface initialized");
    }
    else
    {
        ESP_LOGE(TAG, "SPI initialization failed.");
        return;
    }

    if (MCP2515_reset() != ERROR_OK)
    {
        ESP_LOGE(TAG, "MCP2515 reset failed.");
        return;
    }
    ESP_LOGI(TAG, "MCP2515 reset success.");

    if (MCP2515_setBitrate(CAN_250KBPS, MCP_8MHZ) != ERROR_OK)
    {
        ESP_LOGE(TAG, "MCP2515 setBitrate failed.");
        return;
    }
    ESP_LOGI(TAG, "MCP2515 setBitrate success.");

    if (MCP2515_setNormalMode() != ERROR_OK)
    {
        ESP_LOGE(TAG, "Failed to set MCP2515 to Normal Mode!");
        return;
    }
    ESP_LOGI(TAG, "MCP2515 set to Normal Mode successfully");

    // ============ Start bidirectional communication task ============
    ESP_LOGI(TAG, "Creating bidirectional CAN communication task...");
    BaseType_t task_result = xTaskCreate(
        MCP_RW_Task,
        "BidirCANTask",
        4096,
        NULL,
        3,
        NULL);

    if (task_result != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create communication task!");
        return;
    }

    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "System initialized successfully!");
    ESP_LOGI(TAG, "Bidirectional CAN communication active");
    ESP_LOGI(TAG, "====================================\n\n");
}