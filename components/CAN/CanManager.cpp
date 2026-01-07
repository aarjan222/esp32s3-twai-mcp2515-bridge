#include "CanManager.hpp"

#include <cstring>
#include <iostream>

#include "esp_log.h"
#include "system_monitor.hpp"

namespace CAN {
static const char *TAG = "CANManager";

CanManager::CanManager() : running(false)
{
}

CanManager::~CanManager()
{
    stopReceiving();
}

esp_err_t CanManager::transmit(const twai_message_t &message)
{
    esp_err_t ret = twai_transmit(&message, pdMS_TO_TICKS(30)); // 1-second timeout
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to transmit CAN message: %s", esp_err_to_name(ret));
    } else {
#if CONFIG_CAN_MANAGER_VERBOSE_LOGGING
        ESP_LOGI(TAG, "Transmitting the can message with id: %X", message.identifier);
#endif
    }
    return ret;
}

esp_err_t CanManager::transmit(const uint32_t arbitration_id, const uint8_t *data, const uint8_t size, int extd, int rtr, int ss, int self, int dlc_non_comp)
{
    if (data == nullptr || size > 8) {
        ESP_LOGE(TAG, "Invalid data or size");
        return ESP_ERR_INVALID_ARG;
    }

    twai_message_t canSendMessage = {};
    canSendMessage.identifier = arbitration_id;
    canSendMessage.data_length_code = size;
    canSendMessage.extd = extd;
    canSendMessage.rtr = rtr;
    canSendMessage.ss = ss;
    canSendMessage.self = self;
    canSendMessage.dlc_non_comp = dlc_non_comp;

    if (size > 0) {
        std::memcpy(canSendMessage.data, data, size);
    }
    return transmit(canSendMessage); // calls the other class member method
}

void CanManager::setMessageCallback(MessageCallback callback)
{
    messageCallback = callback;
}

CanManager::MessageCallback CanManager::getMessageCallback()
{
    return this->messageCallback;
}

void CanManager::initCanManager(FreeRTOSWrapper::TaskGroup &initTaskGroup)
{
    if (receive_task_handle) {
        ESP_LOGW(TAG, "Can Start Receiving: Already running");
        return;
    }
    if (!messageCallback) {
        ESP_LOGE(TAG, "Invalid messageCallback pointer");
        return;
    }
    this->taskGroup = &initTaskGroup;
    try {
        initTaskGroup.addTask(
            "CANReceive", RECEIVE_CAN_MESSAGES_TASK_STACK_SIZE, RECIEVE_CAN_MESSAGES_TASK_PRIORITY, [this]() { CanManager::receiveTaskHandler(this); }, pdMS_TO_TICKS(10000), 1, CAN_HANDLER_EVENT_BIT);
        receive_task_handle = initTaskGroup.getTaskHandle("CANReceive");
    } catch (const FreeRTOSWrapper::FreeRTOSException &e) {
        ESP_LOGE(TAG, "Failed with task group:: %s", e.what());
    }
}
void CanManager::startReceiving()
{
    if (receive_task_handle) {
        ESP_LOGW(TAG, "Can Start Receiving: Already running");
        return;
    }
    if (!messageCallback) {
        ESP_LOGE(TAG, "Invalid messageCallback pointer");
        return;
    }

    // std::function<void()> taskFunction = [this]() { receiveTaskHandler(this); };
    // receiveTask = new FreeRTOSWrapper::Task("CanReceive",                         // Task name
    //                                         RECEIVE_CAN_MESSAGES_TASK_STACK_SIZE, // Stack size (bytes)
    //                                         RECIEVE_CAN_MESSAGES_TASK_PRIORITY,   // Priority
    //                                         taskFunction,                         // Task function with captured parameter
    //                                         true,                                 // Watchdog disabled (set true to enable)
    //                                         RECEIVE_CAN_MESSAGES_TASK_CORE,       // No core pinning
    //                                         FreeRTOSWrapper::TaskType::DYNAMIC,   // Dynamic allocation
    //                                         0                                     // No liveness bit
    // );
    // receive_task_handle = receiveTask->getHandle();
    // system_monitor::SystemMonitor::getInstance().register_task("CanReceive", RECEIVE_CAN_MESSAGES_TASK_STACK_SIZE, RECIEVE_CAN_MESSAGES_TASK_PRIORITY, RECEIVE_CAN_MESSAGES_TASK_CORE,
    //                                                            CanManager::receiveTaskHandler, receive_task_handle, true);

    xTaskCreate(receiveTaskHandler, "CAN_RECEIVE_MESSAGE_TASK", RECEIVE_CAN_MESSAGES_TASK_STACK_SIZE, this, RECIEVE_CAN_MESSAGES_TASK_PRIORITY, &receive_task_handle);
    ESP_LOGI(TAG, "Started Receiving CAN Message");
}

void CanManager::stopReceiving()
{
    if (!receive_task_handle) {
        ESP_LOGW(TAG, "Can Stop Receiving: Already stopped");
        return; // Already stopped
    }
    vTaskDelete(receive_task_handle);
    receive_task_handle = nullptr;
    ESP_LOGI(TAG, "Stopped Receiving CAN Message");
}
void CanManager::receiveTaskHandler(void *pvParameters)
{
    CanManager *instance = static_cast<CanManager *>(pvParameters);
    // CanManager::MessageCallback msg_callback = *(CanManager::MessageCallback *)pvParameters;
    // FreeRTOSWrapper::TaskGroup *group = instance->getTaskGroup();
    // const char *taskName = pcTaskGetName(NULL); // Get current task name

    twai_message_t message;
    esp_err_t ret = ESP_OK;
    while (true) {
        ret = twai_receive(&message, pdMS_TO_TICKS(4000));
        if (ret == ESP_OK) {
            if (instance->messageCallback) {
                instance->messageCallback(message);
                ESP_LOGD(TAG, "Received CAN message with ID: 0x%08lX", message.identifier);
            }
        } else if (ret != ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "Error receiving CAN message: %s (%d)", esp_err_to_name(ret), ret);
        }
    }
    vTaskDelete(NULL);
}

} // namespace CAN
