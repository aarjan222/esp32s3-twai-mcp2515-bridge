#ifndef CAN_MANAGER_HPP
#define CAN_MANAGER_HPP

#include <atomic>
#include <functional>
#include <thread>

#include "driver/twai.h"
#include "freertos_wrapper.hpp"

namespace CAN {

class CanManager {
   public:
    using MessageCallback = std::function<void(const twai_message_t &)>;

    // Constructor
    CanManager();

    // Destructor
    ~CanManager();

    // Transmit a CAN message
    esp_err_t transmit(const twai_message_t &message);
    esp_err_t transmit(const uint32_t arbitration_id, const uint8_t *data, const uint8_t size, int extd = 0, int rtr = 0, int ss = 0, int self = 0, int dlc_non_comp = 0);

    // Set a callback for received messages
    void setMessageCallback(MessageCallback callback);
    MessageCallback getMessageCallback();
    void initCanManager(FreeRTOSWrapper::TaskGroup &initTaskGroup);
    // Start receiving messages
    void startReceiving();

    // Stop receiving messages
    void stopReceiving();
    static void receiveTaskHandler(void *pvParameters);
    TaskHandle_t receive_task_handle = nullptr;
    FreeRTOSWrapper::TaskGroup *getTaskGroup()
    {
        return taskGroup;
    }

   private:
    FreeRTOSWrapper::TaskGroup *taskGroup = nullptr; // Task group for managing tasks
    std::atomic<bool> running;                       // Flag for controlling the receive thread
    MessageCallback messageCallback = NULL;          // Callback for received messages
    FreeRTOSWrapper::Task *receiveTask = nullptr;
};

constexpr uint8_t RECIEVE_CAN_MESSAGES_TASK_PRIORITY = 2;
constexpr uint8_t RECEIVE_CAN_MESSAGES_TASK_CORE = 1;
constexpr uint32_t RECEIVE_CAN_MESSAGES_TASK_STACK_SIZE = 4096;

static constexpr EventBits_t CAN_HANDLER_EVENT_BIT = 1 << 0;

} // namespace CAN

#endif // CAN_MANAGER_HPP
