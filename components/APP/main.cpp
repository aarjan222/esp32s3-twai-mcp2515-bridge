#include <stdio.h>
#include "string.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"

#include "CanManager.hpp"
#include "can.hpp"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
extern "C"
{
#include "spi-helper.h"
#include "MCP2515.h"
#include "mcp_can_dfs.h"
}

#define std_id_comm

CAN::CanManager canManager;
constexpr char TAG[] = "BidirectionalCAN";

void handleReceivedMessage(const twai_message_t &CanMessage)
{
    uint32_t canID = CanMessage.identifier;
    uint8_t data[8] = {0};
    uint8_t size = CanMessage.data_length_code;
    memcpy(data, CanMessage.data, size);

    bool is_extended = CanMessage.extd;
    bool is_rtr = CanMessage.rtr;

    ESP_LOGW(TAG, "TWAI Received: ID: 0x%lX, DLC: %d, Extended: %s, RTR: %s",
             canID, size, is_extended ? "Yes" : "No", is_rtr ? "Yes" : "No");

    if (size > 0 && !is_rtr)
    {
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, size, ESP_LOG_INFO);
    }
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
        vTaskDelay(pdMS_TO_TICKS(100)); // Small delay between operations

// ============ Send test message from MCP2515 ============
#ifdef std_id_comm
        uint8_t mcp_result = sendMsgBuf(mcp_std_id, 8, mcp_test_data);
#else
        uint8_t mcp_result = sendMsgBuf(mcp_extd_id | CAN_IS_EXTENDED, 8, mcp_test_data);
#endif
        if (mcp_result != CAN_OK)
        {
            ESP_LOGE(TAG, "MCP TX: Failed to send message. Error code: 0x%02X", mcp_result);
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Small delay between operations

        // ============ Check for MCP2515 received messages ============
        uint32_t rx_id = 0;
        uint8_t rx_len = 0;
        uint8_t rx_buf[8] = {0};

        uint8_t read_result = readMsgBuf(&rx_id, &rx_len, rx_buf);

        if (read_result == CAN_OK)
        {
            bool is_extended = (rx_id & CAN_IS_EXTENDED) != 0;
            bool is_rtr = (rx_id & CAN_IS_REMOTE_REQUEST) != 0;
            uint32_t clean_id = rx_id & CAN_EXTENDED_ID;

            ESP_LOGW(TAG, "MCP Received: ID: 0x%lX, DLC: %d, Extended: %s, RTR: %s",
                     clean_id, rx_len, is_extended ? "Yes" : "No", is_rtr ? "Yes" : "No");

            if (rx_len > 0 && !is_rtr)
            {
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx_buf, rx_len, ESP_LOG_INFO);
            }
        }
        else if (read_result != CAN_NOMSG)
        {
            ESP_LOGW(TAG, "MCP RX: Error reading message. Error code: 0x%02X", read_result);
        }

        // Update test data for next iteration (optional - creates changing pattern)
        mcp_test_data[0] = (mcp_test_data[0] + 1) & 0xFF;
        twai_test_data[0] = (twai_test_data[0] + 1) & 0xFF;

        vTaskDelay(pdMS_TO_TICKS(1000)); // Main loop delay
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

    // ============ Initialize SPI and MCP2515 ============
    ESP_LOGI(TAG, "Initializing SPI interface...");
    len = 0;
    spi_init();
    add_device_one();
    ESP_LOGI(TAG, "SPI interface initialized");

    ESP_LOGI(TAG, "Initializing MCP2515...");
    if (MCP2515_begin(MCP_ANY, CAN_250KBPS, MCP_8MHZ) != CAN_OK)
    {
        ESP_LOGE(TAG, "MCP2515 initialization failed!");
        return;
    }
    ESP_LOGI(TAG, "MCP2515 initialized successfully");

    if (setMode(MCP_NORMAL) != MCP2515_OK)
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