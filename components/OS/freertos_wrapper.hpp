#pragma once

#include <esp_log.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "system_monitor.hpp"

#define PRINT_HEAP_ROW(label, cap)                                                                                              \
    ESP_LOGI(TAG, "| %-16s | %14d | %16d | %10f |", label, heap_caps_get_free_size(cap), heap_caps_get_largest_free_block(cap), \
             100 * heap_caps_get_free_size(cap) / (0.001 + heap_caps_get_total_size(cap)))

/**
 * @brief Log condensed form of heap statistics for automated processing of logs
 * @param cap_str: the string representation of capability for displaying
 * @param cap: the capability for which to get stats
 */
#define PRINT_HEAP_ROW_CONDENSED(cap_str, cap) \
    ESP_LOGI(TAG, "%s,cap:%s,total:%d,total_free:%d,largest_block_free:%d", "[Memory]", cap_str, heap_caps_get_total_size(cap), heap_caps_get_free_size(cap), heap_caps_get_largest_free_block(cap))

#if CONFIG_SPIRAM
#define PRINT_PSRAM_USAGE() PRINT_HEAP_ROW("psram", MALLOC_CAP_SPIRAM)
#else
#define PRINT_PSRAM_USAGE() ESP_LOGE(TAG, "PSRAM hasn't been set up")
#endif

#define PRINT_MEMORY_USAGE()                                                                    \
    do {                                                                                        \
        ESP_LOGI(TAG,                                                                           \
                 "\n"                                                                           \
                 "\t\t----------------------------------------------------------------------\n"            \
                 "\t\t|   Memory Type    |   Free (bytes) |   Largest Block     |  %% free |\n" \
                 "\t\t----------------------------------------------------------------------");            \
        /*PRINT_HEAP_ROW("default", MALLOC_CAP_DEFAULT);*/                                      \
        /*PRINT_HEAP_ROW("dram(8bit)", MALLOC_CAP_8BIT);*/                                      \
        /*PRINT_HEAP_ROW("iram", MALLOC_CAP_IRAM_8BIT);*/                                       \
        PRINT_HEAP_ROW("internal", MALLOC_CAP_INTERNAL);                                        \
        PRINT_PSRAM_USAGE();                                                                    \
        ESP_LOGI(TAG, "----------------------------------------------------------------------");           \
    } while (0)

namespace FreeRTOSWrapper {
static const char *TAG = "FreeRTOSWrapper";

// Exception class for FreeRTOS errors
class FreeRTOSException : public std::runtime_error {
   public:
    FreeRTOSException(const std::string &message) : std::runtime_error(message)
    {
    }
};

// Task type enum
enum class TaskType { DYNAMIC, STATIC };

// Task metadata structure
struct TaskMetadata {
    char name[32];
    uint32_t stackHighWaterMark;
    uint32_t runtime;
    TaskHandle_t handle;
    uint32_t lastExecutionTime;
    BaseType_t coreId;
    bool watchdogEnabled;
    TaskType taskType;
    uint32_t stackSize;
    UBaseType_t priority;
    EventBits_t livenessBit;
    TickType_t softwareWatchDogTimeOut;
    esp_task_wdt_user_handle_t task_wdt_handle;
    float freeStackPercentage;

    TaskMetadata()
        : stackHighWaterMark(0),
          runtime(0),
          handle(nullptr),
          lastExecutionTime(0),
          coreId(-1),
          watchdogEnabled(false),
          taskType(TaskType::DYNAMIC),
          stackSize(0),
          priority(0),
          livenessBit(0),
          softwareWatchDogTimeOut(0),
          task_wdt_handle(nullptr),
          freeStackPercentage(0.0f)
    {
        name[0] = '\0';
    }

    TaskMetadata(const char *taskName, uint32_t stack, UBaseType_t prio, BaseType_t core = -1, bool wdtEnabled = false, TaskType type = TaskType::DYNAMIC, EventBits_t bit = 0,
                 TickType_t watchDogTimeOut = 0, float freeStackPercent = 0.0f)
        : stackHighWaterMark(0),
          runtime(0),
          handle(nullptr),
          lastExecutionTime(0),
          coreId(core),
          watchdogEnabled(wdtEnabled),
          taskType(type),
          stackSize(stack),
          priority(prio),
          livenessBit(bit),
          softwareWatchDogTimeOut(watchDogTimeOut),
          task_wdt_handle(nullptr),
          freeStackPercentage(freeStackPercent)
    {
        strncpy(name, taskName, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }
};

struct TaskMetrics {
    std::string name;
    float cpuPercentage;
    uint32_t runTimeCounter;
    uint32_t freeStackBytes;
    eTaskState state;
    BaseType_t coreId;
};

// Mutex wrapper class
class Mutex {
   private:
    SemaphoreHandle_t handle;

   public:
    Mutex() : handle(xSemaphoreCreateMutex())
    {
        if (!handle) {
            throw FreeRTOSException("Failed to create mutex");
        }
        ESP_LOGI(TAG, "Mutex created successfully");
    }

    ~Mutex()
    {
        if (handle) {
            vSemaphoreDelete(handle);
        }
    }

    bool lock(TickType_t timeout)
    {
        if (!handle) {
            throw FreeRTOSException("Mutex handle is null");
        }
        return xSemaphoreTake(handle, timeout) == pdTRUE;
    }

    bool unlock()
    {
        if (!handle) {
            throw FreeRTOSException("Mutex handle is null");
        }
        return xSemaphoreGive(handle) == pdTRUE;
    }

    void deleteMutex()
    {
        if (handle) {
            vSemaphoreDelete(handle);
            handle = nullptr;
        }
    }
};

// Semaphore wrapper class
class Semaphore {
   private:
    SemaphoreHandle_t handle;

   public:
    Semaphore(uint32_t maxCount, uint32_t initialCount) : handle(xSemaphoreCreateCounting(maxCount, initialCount))
    {
        if (!handle) {
            throw FreeRTOSException("Failed to create semaphore");
        }
        ESP_LOGI(TAG, "Semaphore created successfully");
    }

    ~Semaphore()
    {
        if (handle) {
            vSemaphoreDelete(handle);
        }
    }

    bool take(TickType_t timeout)
    {
        return xSemaphoreTake(handle, timeout) == pdTRUE;
    }

    bool give()
    {
        return xSemaphoreGive(handle) == pdTRUE;
    }
};

// EventGroup wrapper class
class EventGroup {
   private:
    EventGroupHandle_t handle;

   public:
    EventGroup() : handle(xEventGroupCreate())
    {
        if (!handle) {
            ESP_LOGE(TAG, "Failed to create event group");
            throw FreeRTOSException("Failed to create event group");
        }
        ESP_LOGI(TAG, "EventGroup created successfully, handle=%p", handle);
    }

    ~EventGroup()
    {
        if (handle) {
            vEventGroupDelete(handle);
        }
    }

    EventBits_t setBits(EventBits_t bits)
    {
        if (!handle) {
            ESP_LOGE(TAG, "EventGroup::setBits: Invalid handle");
            return 0;
        }
        return xEventGroupSetBits(handle, bits);
    }

    EventBits_t waitBits(EventBits_t bits, bool clearOnExit, bool waitForAll, TickType_t timeout)
    {
        if (!handle) {
            ESP_LOGE(TAG, "EventGroup::waitBits: Invalid handle");
            return 0;
        }
        return xEventGroupWaitBits(handle, bits, clearOnExit, waitForAll, timeout);
    }

    EventBits_t clearBits(EventBits_t bits)
    {
        if (!handle) {
            ESP_LOGE(TAG, "EventGroup::clearBits: Invalid handle");
            return 0;
        }
        return xEventGroupClearBits(handle, bits);
    }

    EventBits_t getBits()
    {
        if (!handle) {
            ESP_LOGE(TAG, "EventGroup::getBits: Invalid handle");
            return 0;
        }
        return xEventGroupGetBits(handle);
    }

    EventBits_t getBitsFromISR()
    {
        if (!handle) {
            ESP_LOGE(TAG, "EventGroup::getBitsFromISR: Invalid handle");
            return 0;
        }
        return xEventGroupGetBitsFromISR(handle);
    }

    void clearBitsFromISR(EventBits_t bits)
    {
        if (!handle) {
            ESP_LOGE(TAG, "EventGroup::clearBitsFromISR: Invalid handle");
            return;
        }
        xEventGroupClearBitsFromISR(handle, bits);
    }

    void deleteEventGroup()
    {
        if (handle) {
            vEventGroupDelete(handle);
            handle = nullptr;
        }
    }
};

// Analysis utility
class TaskAnalyzer {
   private:
    std::map<std::string, TaskMetadata> taskHistory;
    Mutex mutex;

   public:
    static TaskAnalyzer &getInstance()
    {
        static TaskAnalyzer instance;
        return instance;
    }
    void updateTaskMetadata(const std::string &name, const TaskMetadata &metadata)
    {
        if (!mutex.lock(pdMS_TO_TICKS(500))) {
            ESP_LOGE(TAG, "Failed to acquire mutex for updating task metadata");
            return;
        }
        taskHistory[name] = metadata;
        mutex.unlock();
        ESP_LOGI(TAG, "Task %s metadata updated in TaskAnalyzer", name.c_str());
    }
    void removeTaskMetadata(const std::string &name)
    {
        if (!mutex.lock(pdMS_TO_TICKS(500))) {
            ESP_LOGE(TAG, "Failed to acquire mutex for removing task metadata");
            return;
        }
        taskHistory.erase(name);
        mutex.unlock();
        ESP_LOGI(TAG, "Task %s metadata removed from TaskAnalyzer", name.c_str());
    }
    TaskMetadata getTaskHistory(const std::string &name)
    {
        if (!mutex.lock(pdMS_TO_TICKS(500))) {
            throw FreeRTOSException("Failed to acquire mutex for task history");
        }
        auto it = taskHistory.find(name);
        if (it != taskHistory.end()) {
            TaskMetadata metadata = it->second;
            mutex.unlock();
            return metadata;
        }
        mutex.unlock();
        throw FreeRTOSException("No history for task: " + name);
    }
};

// Task wrapper class
class Task {
   private:
    TaskMetadata metadata;
    bool watchdogEnabled = false;
    StaticTask_t *taskBuffer = nullptr;
    StackType_t *stackBuffer = nullptr;
    EventGroup *eventGroup = nullptr; // Optional EventGroup for liveness bit
    static constexpr TickType_t WATCHDOG_TIMEOUT = pdMS_TO_TICKS(5000);

    static void taskEntryPoint(void *params)
    {
        Task *task = static_cast<Task *>(params);
        if (!task) {
            ESP_LOGE(TAG, "taskEntryPoint: Null task pointer");
            vTaskDelete(nullptr);
            return;
        }

        try {
            if (task->watchdogEnabled) {
                esp_err_t err = esp_task_wdt_add(task->metadata.handle);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to add task %s to TWDT: %d", task->metadata.name, err);
                } else {
                    ESP_LOGI(TAG, "Task %s subscribed to TWDT", task->metadata.name);
                }
            }
            task->taskFunction();
        } catch (const std::exception &e) {
            ESP_LOGE(TAG, "Task %s failed: %s", task->metadata.name, e.what());
        }
        if (task->watchdogEnabled) {
            esp_err_t err = esp_task_wdt_delete(task->metadata.handle);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to unsubscribe task %s from TWDT: %d", task->metadata.name, err);
            } else {
                ESP_LOGI(TAG, "Task %s unsubscribed from TWDT", task->metadata.name);
            }
        }
        vTaskDelete(nullptr);
    }

   protected:
    std::function<void()> taskFunction;

   public:
    Task(const char *name, uint32_t stackSize, UBaseType_t priority, const std::function<void()> &func, TickType_t watchdogTimeOut, bool enableWatchdog = false, BaseType_t coreId = tskNO_AFFINITY,
         TaskType type = TaskType::DYNAMIC, EventBits_t livenessBit = 0, EventGroup *eventGroup = nullptr)
        : metadata(name, stackSize, priority, coreId, enableWatchdog, type, livenessBit, watchdogTimeOut),
          watchdogEnabled(enableWatchdog),
          taskBuffer(nullptr),
          stackBuffer(nullptr),
          eventGroup(eventGroup)
    {
        taskFunction = func;

        if (!name || name[0] == '\0') {
            throw FreeRTOSException("Task name cannot be empty");
        }

        if (type == TaskType::STATIC) {
            stackBuffer = new StackType_t[stackSize];
            taskBuffer = new StaticTask_t;
            if (!stackBuffer || !taskBuffer) {
                if (stackBuffer)
                    delete[] stackBuffer;
                if (taskBuffer)
                    delete taskBuffer;
                stackBuffer = nullptr;
                taskBuffer = nullptr;
                throw FreeRTOSException("Failed to allocate static buffers for task");
            }

            metadata.handle = xTaskCreateStaticPinnedToCore(taskEntryPoint, name, stackSize, this, priority, stackBuffer, taskBuffer, coreId);
        } else {
            BaseType_t result = xTaskCreatePinnedToCore(taskEntryPoint, name, stackSize, this, priority, &metadata.handle, coreId);
            if (result != pdPASS) {
                throw FreeRTOSException("Failed to create dynamic task");
            }
        }

        if (!metadata.handle) {
            delete[] stackBuffer;
            delete taskBuffer;
            throw FreeRTOSException("Failed to create task");
        }
        TaskAnalyzer::getInstance().updateTaskMetadata(name, metadata);
        ESP_LOGI(TAG, "Task %s created successfully", name);
    }

    ~Task()
    {
        if (metadata.handle) {
            if (watchdogEnabled) {
                esp_err_t err = esp_task_wdt_delete(metadata.handle);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to unsubscribe task %s from TWDT: %d", metadata.name, err);
                } else {
                    ESP_LOGI(TAG, "Task %s unsubscribed from TWDT", metadata.name);
                }
            }
            vTaskDelete(metadata.handle);
            metadata.handle = nullptr;
            ESP_LOGI(TAG, "Task %s deleted from FreeRTOS", metadata.name);
        }
        if (metadata.taskType == TaskType::STATIC) {
            if (stackBuffer)
                delete[] stackBuffer;
            if (taskBuffer)
                delete taskBuffer;
            stackBuffer = nullptr;
            taskBuffer = nullptr;
        }
        if (metadata.livenessBit != 0 && eventGroup) {
            eventGroup->clearBits(metadata.livenessBit);
            ESP_LOGI(TAG, "Cleared liveness bit 0x%lx for task %s", metadata.livenessBit, metadata.name);
        }
        TaskAnalyzer::getInstance().removeTaskMetadata(metadata.name);
        ESP_LOGI(TAG, "Task %s destroyed", metadata.name);
    }

    void suspend()
    {
        if (metadata.handle) {
            if (metadata.livenessBit != 0 && eventGroup) {
                eventGroup->clearBits(metadata.livenessBit);
                ESP_LOGI(TAG, "Cleared liveness bit 0x%lx for task %s", metadata.livenessBit, metadata.name);
            }
            if (watchdogEnabled) {
                esp_err_t err = esp_task_wdt_delete(metadata.handle);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to unsubscribe task %s from TWDT: %d", metadata.name, err);
                } else {
                    ESP_LOGI(TAG, "Task %s unsubscribed from TWDT", metadata.name);
                }
            }
            vTaskSuspend(metadata.handle);
            ESP_LOGI(TAG, "Task %s suspended", metadata.name);
        }
    }

    void resume()
    {
        if (metadata.handle) {
            vTaskResume(metadata.handle);
            if (metadata.livenessBit != 0 && eventGroup) {
                eventGroup->setBits(metadata.livenessBit);
                ESP_LOGI(TAG, "Restored liveness bit 0x%lx for task %s", metadata.livenessBit, metadata.name);
            }
            if (watchdogEnabled) {
                esp_err_t err = esp_task_wdt_add(metadata.handle);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to re-add task %s to TWDT: %d", metadata.name, err);
                } else {
                    ESP_LOGI(TAG, "Task %s re-added to TWDT", metadata.name);
                }
            }
            ESP_LOGI(TAG, "Task %s resumed", metadata.name);
        }
    }

    void resetWatchdog()
    {
        if (watchdogEnabled && metadata.handle) {
            esp_err_t err = esp_task_wdt_reset();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to reset TWDT for task %s: %d", metadata.name, err);
            } else {
                ESP_LOGI(TAG, "TWDT reset for task %s", metadata.name);
            }
        }
    }

    void resizeStack(uint32_t newStackSize)
    {
        if (!metadata.handle) {
            throw FreeRTOSException("Task handle is null");
        }

        // Save properties
        std::function<void()> func = taskFunction;
        UBaseType_t priority = metadata.priority;
        BaseType_t coreId = metadata.coreId;
        TaskType type = metadata.taskType;
        EventBits_t livenessBit = metadata.livenessBit;
        TickType_t watchdogTimeout = metadata.softwareWatchDogTimeOut;
        bool wdtEnabled = watchdogEnabled;
        std::string name = metadata.name;

        // Delete existing task
        if (watchdogEnabled) {
            esp_err_t err = esp_task_wdt_delete(metadata.handle);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to unsubscribe task %s from TWDT: %d", metadata.name, err);
            }
        }
        if (livenessBit != 0 && eventGroup) {
            eventGroup->clearBits(livenessBit);
            ESP_LOGI(TAG, "Cleared liveness bit 0x%lx for task %s", livenessBit, metadata.name);
        }
        vTaskDelete(metadata.handle);
        metadata.handle = nullptr;
        if (stackBuffer)
            delete[] stackBuffer;
        if (taskBuffer)
            delete taskBuffer;
        stackBuffer = nullptr;
        taskBuffer = nullptr;

        // Recreate task
        metadata = TaskMetadata(name.c_str(), newStackSize, priority, coreId, wdtEnabled, type, livenessBit, watchdogTimeout);
        taskFunction = func;
        watchdogEnabled = wdtEnabled;

        if (type == TaskType::STATIC) {
            stackBuffer = new StackType_t[newStackSize];
            taskBuffer = new StaticTask_t;
            if (!stackBuffer || !taskBuffer) {
                if (stackBuffer)
                    delete[] stackBuffer;
                if (taskBuffer)
                    delete taskBuffer;
                stackBuffer = nullptr;
                taskBuffer = nullptr;
                throw FreeRTOSException("Failed to allocate static buffers for task");
            }
            metadata.handle = xTaskCreateStaticPinnedToCore(taskEntryPoint, name.c_str(), newStackSize, this, priority, stackBuffer, taskBuffer, coreId);
        } else {
            BaseType_t result = xTaskCreatePinnedToCore(taskEntryPoint, name.c_str(), newStackSize, this, priority, &metadata.handle, coreId);
            if (result != pdPASS) {
                throw FreeRTOSException("Failed to create dynamic task");
            }
        }

        if (!metadata.handle) {
            if (stackBuffer)
                delete[] stackBuffer;
            if (taskBuffer)
                delete taskBuffer;
            stackBuffer = nullptr;
            taskBuffer = nullptr;
            throw FreeRTOSException("Failed to create task");
        }

        if (livenessBit != 0 && eventGroup) {
            eventGroup->setBits(livenessBit);
            ESP_LOGI(TAG, "Restored liveness bit 0x%lx for task %s", livenessBit, metadata.name);
        }
        if (wdtEnabled) {
            esp_err_t err = esp_task_wdt_add(metadata.handle);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to add task %s to TWDT: %d", metadata.name, err);
            }
        }

        TaskAnalyzer::getInstance().updateTaskMetadata(name, metadata);
        ESP_LOGI(TAG, "Task %s stack resized to %lu bytes", name.c_str(), newStackSize);
    }

    void setPriority(UBaseType_t newPriority)
    {
        if (metadata.handle) {
            vTaskPrioritySet(metadata.handle, newPriority);
            metadata.priority = newPriority;
            TaskAnalyzer::getInstance().updateTaskMetadata(metadata.name, metadata);
            ESP_LOGI(TAG, "Task %s priority set to %u", metadata.name, newPriority);
        } else {
            throw FreeRTOSException("Task handle is null");
        }
    }

    void setCore(BaseType_t newCoreId)
    {
        if (!metadata.handle) {
            throw FreeRTOSException("Task handle is null");
        }

        // Save properties
        std::function<void()> func = taskFunction;
        uint32_t stackSize = metadata.stackSize;
        UBaseType_t priority = metadata.priority;
        TaskType type = metadata.taskType;
        EventBits_t livenessBit = metadata.livenessBit;
        TickType_t watchdogTimeout = metadata.softwareWatchDogTimeOut;
        bool wdtEnabled = watchdogEnabled;
        std::string name = metadata.name;

        // Delete existing task
        if (watchdogEnabled) {
            esp_err_t err = esp_task_wdt_delete(metadata.handle);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to unsubscribe task %s from TWDT: %d", metadata.name, err);
            }
        }
        if (livenessBit != 0 && eventGroup) {
            eventGroup->clearBits(livenessBit);
            ESP_LOGI(TAG, "Cleared liveness bit 0x%lx for task %s", livenessBit, metadata.name);
        }
        vTaskDelete(metadata.handle);
        metadata.handle = nullptr;
        if (stackBuffer)
            delete[] stackBuffer;
        if (taskBuffer)
            delete taskBuffer;
        stackBuffer = nullptr;
        taskBuffer = nullptr;

        // Recreate task
        metadata = TaskMetadata(name.c_str(), stackSize, priority, newCoreId, wdtEnabled, type, livenessBit, watchdogTimeout);
        taskFunction = func;
        watchdogEnabled = wdtEnabled;

        if (type == TaskType::STATIC) {
            stackBuffer = new StackType_t[stackSize];
            taskBuffer = new StaticTask_t;
            if (!stackBuffer || !taskBuffer) {
                if (stackBuffer)
                    delete[] stackBuffer;
                if (taskBuffer)
                    delete taskBuffer;
                stackBuffer = nullptr;
                taskBuffer = nullptr;
                throw FreeRTOSException("Failed to allocate static buffers for task");
            }
            metadata.handle = xTaskCreateStaticPinnedToCore(taskEntryPoint, name.c_str(), stackSize, this, priority, stackBuffer, taskBuffer, newCoreId);
        } else {
            BaseType_t result = xTaskCreatePinnedToCore(taskEntryPoint, name.c_str(), stackSize, this, priority, &metadata.handle, newCoreId);
            if (result != pdPASS) {
                throw FreeRTOSException("Failed to create dynamic task");
            }
        }

        if (!metadata.handle) {
            if (stackBuffer)
                delete[] stackBuffer;
            if (taskBuffer)
                delete taskBuffer;
            stackBuffer = nullptr;
            taskBuffer = nullptr;
            throw FreeRTOSException("Failed to create task");
        }

        if (livenessBit != 0 && eventGroup) {
            eventGroup->setBits(livenessBit);
            ESP_LOGI(TAG, "Restored liveness bit 0x%lx for task %s", livenessBit, metadata.name);
        }
        if (wdtEnabled) {
            esp_err_t err = esp_task_wdt_add(metadata.handle);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to add task %s to TWDT: %d", metadata.name, err);
            }
        }

        TaskAnalyzer::getInstance().updateTaskMetadata(name, metadata);
        ESP_LOGI(TAG, "Task %s core changed to %d", name.c_str(), newCoreId);
    }

    const std::function<void()> &getTaskFunction() const
    {
        return taskFunction;
    }

    const TaskMetadata &getMetadata()
    {
        if (metadata.handle) {
            metadata.stackHighWaterMark = uxTaskGetStackHighWaterMark(metadata.handle);
            metadata.runtime = xTaskGetTickCount() - metadata.lastExecutionTime;
            metadata.lastExecutionTime = xTaskGetTickCount();
            metadata.freeStackPercentage = (static_cast<float>(metadata.stackHighWaterMark) / static_cast<float>(metadata.stackSize)) * 100.0f;
        }
        return metadata;
    }

    TaskHandle_t getHandle()
    {
        return metadata.handle;
    }
};

// TaskGroup wrapper for managing multiple tasks
class TaskGroup {
   private:
    std::map<std::string, Task *> tasks;
    Mutex mutex;
    std::string taskGroupName;
    bool groupWatchdogEnabled;
    EventBits_t nextLivenessBit;
    std::map<std::string, TickType_t> lastWatchdogResetTimes;
    static constexpr TickType_t SOFTWARE_WATCHDOG_TIMEOUT = pdMS_TO_TICKS(5000);
    std::vector<std::string> taskNames;
    uint32_t numberOfTasksInGroup;
    UBaseType_t groupPriority;
    bool enableMonitorTask;
    BaseType_t monitorCoreId;
    static constexpr float MAX_CUP_LOAD = 40.0f;
    static constexpr uint32_t TaskOverloadTime = 30000;

   public:
    EventGroup eventGroup;
    static void monitorTaskFunction(void *arg)
    {
        esp_task_wdt_add(nullptr);
        auto *self = static_cast<TaskGroup *>(arg);
        // static TickType_t previousTime = 0;
        std::map<std::string, TickType_t> taskOverLoadTime;
        std::map<std::string, std::string> suspendedTask;
        std::map<std::string, std::string> deletedTask;
        while (true) {
            std::vector<std::string> logMessages;

            bool allHealthy = true;
            EventBits_t allBits = 0;
            EventBits_t setBits = self->eventGroup.getBits();
            // TickTypes_t tick_count = xTaskGetTickCount();
            system_monitor::SystemMonitor::getInstance().update_twdt_reset_time(nullptr);
            if (!self->mutex.lock(pdMS_TO_TICKS(500))) {
                logMessages.push_back("Monitor task for group " + self->taskGroupName + " failed to acquire mutex");
                allHealthy = false;
            } else {
                self->mutex.unlock(); // release mutex here
                try {
                    auto [watchdogsOk, failedTasks] = self->checkSoftwareWatchdogs();
                    std::vector<TaskMetrics> taskGroupMetrix = self->getTaskGroupRuntimeStats(self);
                    for (const auto &metrix : taskGroupMetrix) {
                        if (metrix.name == "IDLE1") {
                            if (metrix.cpuPercentage < 20.0f) {
                                ESP_LOGE(TAG, "IDLE1 Starved (TWDT Risk): %.2f%%", metrix.cpuPercentage);
                            }
                            continue;
                        } else if (metrix.name == "IDLE0") {
                            if (metrix.cpuPercentage < 20.0f) {
                                ESP_LOGE(TAG, "IDLE0 Starved (TWDT Risk): %.2f%%", metrix.cpuPercentage);
                            }
                            continue;
                        }
                        auto it = self->tasks.find(metrix.name);
                        if (it != self->tasks.end()) {
                            const std::string &name = metrix.name;
                            Task *task = it->second;
                            if (!task) {
                                logMessages.push_back("Null task pointer for " + name);
                                allHealthy = false;
                                continue;
                            }
                            TaskMetadata metadata = task->getMetadata();
                            if (metrix.cpuPercentage > self->MAX_CUP_LOAD) {
                                ESP_LOGE(TAG, "Task %s CPU load is too high: %.2f%%", metrix.name.c_str(), metrix.cpuPercentage);
                                if (taskOverLoadTime.find(name) == taskOverLoadTime.end()) {
                                    ESP_LOGE(TAG, "Store Tick Count for task %s", metrix.name.c_str());
                                    taskOverLoadTime[name] = xTaskGetTickCount();
                                } else if ((xTaskGetTickCount() - taskOverLoadTime[name]) > pdMS_TO_TICKS(self->TaskOverloadTime)) {
                                    ESP_LOGE(TAG, "%s Task is suspend for ever........", metrix.name.c_str());
                                    self->suspendTask(metrix.name);
                                    suspendedTask[name] = "Task " + metrix.name + " suspended due to high CPU load after " +
                                                          std::to_string(static_cast<uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount() - taskOverLoadTime[name]))) + "ms";
                                    taskOverLoadTime.erase(name);
                                    allHealthy = false;
                                } else {
                                    ESP_LOGE(TAG, "%s Task is suspend for 100ms", metrix.name.c_str());
                                    self->suspendTask(metrix.name);
                                    vTaskDelay(pdMS_TO_TICKS(100));
                                    self->resumeTask(metrix.name);
                                    logMessages.push_back("error!!! Task " + metrix.name + " suspended and resumed due to high CPU load");
                                    allHealthy = false;
                                }
                            } else {
                                if (suspendedTask.find(name) != suspendedTask.end())
                                    suspendedTask.erase(name);
                            }

                            if (metadata.stackHighWaterMark < (metadata.stackSize * 0.2)) {
                                logMessages.push_back("Task " + name + ": High stack usage, only " + std::to_string(static_cast<uint32_t>(metadata.stackHighWaterMark * sizeof(StackType_t))) +
                                                      " bytes free");
                                allHealthy = false;
                                self->adjustTaskStack(self, metrix);
                            }

                            bool watchdogOk = std::find(failedTasks.begin(), failedTasks.end(), name) == failedTasks.end();
                            bool livenessOk = (metadata.livenessBit == 0) || (setBits & metadata.livenessBit);
                            allBits |= metadata.livenessBit;
                            std::string stateStr = "Unknown";
                            switch (metrix.state) {
                                case eRunning:
                                    stateStr = "Running";
                                    break;
                                case eReady:
                                    stateStr = "Ready";
                                    break;
                                case eBlocked:
                                    stateStr = "Blocked";
                                    break;
                                case eSuspended:
                                    stateStr = "Suspended";
                                    break;
                                case eDeleted:
                                    stateStr = "Deleted";
                                    break;
                                case eInvalid:
                                    stateStr = "Invalid";
                                    allHealthy = false;
                                    break;
                            }
                            std::string log;
                            log.reserve(512);
                            log = "Task " + name + " (Group " + self->taskGroupName + "): State=" + stateStr +
                                  ", Stack Size=" + std::to_string(static_cast<uint32_t>(metadata.stackSize * sizeof(StackType_t))) + " bytes" +
                                  ", StackHighWater=" + std::to_string(static_cast<uint32_t>(metrix.freeStackBytes)) + " bytes" +
                                  ", Runtime=" + std::to_string(static_cast<uint32_t>(metrix.runTimeCounter)) + ", Free Stack =" + std::to_string(static_cast<uint32_t>(metadata.freeStackPercentage)) +
                                  "%" + ", CPU Usage =" + std::to_string(static_cast<uint32_t>(metrix.cpuPercentage)) + "%" + ", Priority=" + std::to_string(static_cast<uint32_t>(metadata.priority)) +
                                  ", Core=" + std::to_string(static_cast<int32_t>(metadata.coreId)) + ", Watchdog=" + (watchdogOk ? "OK" : "Timeout") + ", LivenessBit=0x" +
                                  std::to_string(static_cast<uint32_t>(metadata.livenessBit)) + " (" + (livenessOk ? "Set" : "Not Set") + ")";
                            logMessages.push_back(std::move(log));

                            if (!watchdogOk || !livenessOk) {
                                allHealthy = false;
                            }
                        }
                    }

                    bool livenessOk = (allBits == 0) || ((setBits & allBits) == allBits);
                    if (!livenessOk) {
                        auto failedTask = self->checkFailedBitTask(setBits);
                        for (const auto &failedTaskName : failedTask) {
                            ESP_LOGW(TAG, "%s Task Unable to set the eventBits....", failedTaskName.c_str());
                        }
                        std::string log;
                        log.reserve(128);
                        log = "Group " + self->taskGroupName + " liveness check failed: Expected bits=0x" + std::to_string(static_cast<uint32_t>(allBits)) + ", Got bits=0x" +
                              std::to_string(static_cast<uint32_t>(setBits));
                        logMessages.push_back(std::move(log));
                        allHealthy = false;
                    }

                    if (!watchdogsOk) {
                        std::string failedList;
                        failedList.reserve(64);
                        for (const auto &task : failedTasks) {
                            failedList += task + ", ";
                        }
                        if (!failedList.empty()) {
                            failedList.resize(failedList.size() - 2);
                        }
                        std::string log;
                        log.reserve(128);
                        log = "Group " + self->taskGroupName + " watchdog check failed for tasks: " + failedList;
                        logMessages.push_back(std::move(log));
                        allHealthy = false;
                    }

                    if (allHealthy) {
                        logMessages.push_back("Group " + self->taskGroupName + ": All tasks healthy");
                        self->eventGroup.clearBits(allBits);
                    }
                } catch (const FreeRTOSException &e) {
                    logMessages.push_back("Monitor task for group " + self->taskGroupName + " error: " + e.what());
                    allHealthy = false;
                } catch (...) {
                    logMessages.push_back("Monitor task for group " + self->taskGroupName + " unknown error");
                    allHealthy = false;
                }
                self->mutex.unlock();
            }
            for (const auto &msg : suspendedTask) {
                ESP_LOGE(TAG, "%s", msg.second.c_str());
            }

            for (const auto &msg : logMessages) {
                if (msg.find("error") != std::string::npos || msg.find("failed") != std::string::npos || msg.find("Timeout") != std::string::npos ||
                    msg.find("High stack usage") != std::string::npos || msg.find("Invalid") != std::string::npos) {
                    ESP_LOGW(TAG, "%s", msg.c_str());
                } else {
                    ESP_LOGI(TAG, "%s", msg.c_str());
                }
            }
            if (esp_task_wdt_status(xTaskGetCurrentTaskHandle()) == ESP_OK) {
                esp_task_wdt_reset();
            }
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }

    TaskGroup(std::string groupName, bool enableMonitorTask = false, UBaseType_t groupPriority = 1, BaseType_t monitorCoreId = 1)
        : taskGroupName(groupName),
          groupWatchdogEnabled(false),
          nextLivenessBit(1),
          numberOfTasksInGroup(0),
          groupPriority(groupPriority),
          enableMonitorTask(enableMonitorTask),
          monitorCoreId(monitorCoreId)
    {
        ESP_LOGI(TAG, "TaskGroup %s created, MonitorTask=%s, GroupPriority=%u, MonitorCore=%d", groupName.c_str(), enableMonitorTask ? "Enabled" : "Disabled", groupPriority, monitorCoreId);

        if (enableMonitorTask) {
            std::string monitorName = "Monitor_" + groupName;
            tasks[monitorName] = new Task(
                monitorName.c_str(), 6144, groupPriority + 1, [this]() { TaskGroup::monitorTaskFunction(this); }, pdMS_TO_TICKS(40000), false, monitorCoreId, TaskType::DYNAMIC, 0, &eventGroup);
            taskNames.push_back(monitorName);
            numberOfTasksInGroup++;
            system_monitor::SystemMonitor::getInstance().register_task(monitorName, 6144, groupPriority + 1, monitorCoreId, TaskGroup::monitorTaskFunction, tasks[monitorName]->getHandle(), true);
        }
    }

    ~TaskGroup()
    {
        if (!mutex.lock(pdMS_TO_TICKS(500))) {
            ESP_LOGE(TAG, "Failed to acquire mutex for TaskGroup destruction");
            return;
        }

        if (enableMonitorTask) {
            std::string monitorName = "Monitor_" + taskGroupName;
            if (esp_task_wdt_status(tasks[monitorName]->getHandle()) == ESP_OK) {
                esp_task_wdt_delete(tasks[monitorName]->getHandle());
            }
        }
        for (auto &pair : tasks) {
            vTaskDelay(pdMS_TO_TICKS(200));
            delete pair.second;
            pair.second = nullptr;
        }
        tasks.clear();
        taskNames.clear();
        lastWatchdogResetTimes.clear();
        numberOfTasksInGroup = 0;
        mutex.unlock();
        ESP_LOGI(TAG, "TaskGroup %s destroyed", taskGroupName.c_str());
    }

    void enableGroupWatchdog()
    {
        groupWatchdogEnabled = true;
        ESP_LOGI(TAG, "Group TWDT enabled for TaskGroup %s", taskGroupName.c_str());
    }

    void deleteTask(const std::string &name)
    {
        if (!mutex.lock(pdMS_TO_TICKS(500))) {
            ESP_LOGE(TAG, "Failed to acquire mutex for deleting task %s", name.c_str());
            throw FreeRTOSException("Failed to acquire mutex for task deletion");
        }

        try {
            auto it = tasks.find(name);
            if (it == tasks.end() || !it->second) {
                mutex.unlock();
                ESP_LOGW(TAG, "Task %s not found for deletion", name.c_str());
                throw FreeRTOSException("Task not found: " + name);
            }

            Task *task = it->second;

            // Remove from taskNames and update count
            auto nameIt = std::find(taskNames.begin(), taskNames.end(), name);
            if (nameIt != taskNames.end()) {
                taskNames.erase(nameIt);
                numberOfTasksInGroup--;
            }

            // Remove from lastWatchdogResetTimes
            lastWatchdogResetTimes.erase(name);

            // Delete task (Task destructor handles TWDT, liveness bit, and TaskAnalyzer)
            delete task;
            tasks.erase(it);

            mutex.unlock();
            ESP_LOGI(TAG, "Task %s permanently deleted from TaskGroup %s", name.c_str(), taskGroupName.c_str());
        } catch (...) {
            mutex.unlock();
            ESP_LOGE(TAG, "Error deleting task %s", name.c_str());
            throw FreeRTOSException("Failed to delete task: " + name);
        }
    }

    void suspendTask(const std::string &name)
    {
        if (!mutex.lock(pdMS_TO_TICKS(500))) {
            throw FreeRTOSException("Failed to acquire mutex for task suspension");
        }

        try {
            auto it = tasks.find(name);
            if (it == tasks.end() || !it->second) {
                mutex.unlock();
                throw FreeRTOSException("Task not found: " + name);
            }

            Task *task = it->second;
            task->suspend();

            // Exclude from software watchdog
            lastWatchdogResetTimes.erase(name);
            ESP_LOGI(TAG, "Task %s excluded from software watchdog checks", name.c_str());

            mutex.unlock();
        } catch (...) {
            mutex.unlock();
            throw FreeRTOSException("Failed to suspend task: " + name);
        }
    }

    void resumeTask(const std::string &name)
    {
        if (!mutex.lock(pdMS_TO_TICKS(500))) {
            throw FreeRTOSException("Failed to acquire mutex for task resumption");
        }

        try {
            auto it = tasks.find(name);
            if (it == tasks.end() || !it->second) {
                mutex.unlock();
                throw FreeRTOSException("Task not found: " + name);
            }

            Task *task = it->second;
            task->resume();

            // Re-enable software watchdog
            if (groupWatchdogEnabled) {
                lastWatchdogResetTimes[name] = xTaskGetTickCount();
                ESP_LOGI(TAG, "Task %s re-enabled for software watchdog with timeout %ld ticks", name.c_str(), task->getMetadata().softwareWatchDogTimeOut);
            }

            mutex.unlock();
        } catch (...) {
            mutex.unlock();
            throw FreeRTOSException("Failed to resume task: " + name);
        }
    }

    void resizeTaskStack(const std::string &name, uint32_t newStackSize)
    {
        if (!mutex.lock(pdMS_TO_TICKS(500))) {
            throw FreeRTOSException("Failed to acquire mutex for task stack resize");
        }

        try {
            auto it = tasks.find(name);
            if (it == tasks.end() || !it->second) {
                mutex.unlock();
                throw FreeRTOSException("Task not found: " + name);
            }

            Task *task = it->second;
            lastWatchdogResetTimes.erase(name); // Remove temporarily
            task->resizeStack(newStackSize);

            if (groupWatchdogEnabled) {
                lastWatchdogResetTimes[name] = xTaskGetTickCount();
                ESP_LOGI(TAG, "Task %s re-added to software watchdog after stack resize", name.c_str());
            }

            mutex.unlock();
        } catch (...) {
            mutex.unlock();
            throw FreeRTOSException("Failed to resize stack for task: " + name);
        }
    }

    void setTaskPriority(const std::string &name, UBaseType_t newPriority)
    {
        if (newPriority > groupPriority) {
            throw FreeRTOSException("New priority (" + std::to_string(newPriority) + ") exceeds group priority (" + std::to_string(groupPriority) + ")");
        }

        if (!mutex.lock(pdMS_TO_TICKS(500))) {
            throw FreeRTOSException("Failed to acquire mutex for task priority change");
        }

        try {
            auto it = tasks.find(name);
            if (it == tasks.end() || !it->second) {
                mutex.unlock();
                throw FreeRTOSException("Task not found: " + name);
            }

            Task *task = it->second;
            task->setPriority(newPriority);
            mutex.unlock();
        } catch (...) {
            mutex.unlock();
            throw FreeRTOSException("Failed to set priority for task: " + name);
        }
    }

    void setTaskCore(const std::string &name, BaseType_t newCoreId)
    {
        if (!mutex.lock(pdMS_TO_TICKS(500))) {
            throw FreeRTOSException("Failed to acquire mutex for task core change");
        }

        try {
            auto it = tasks.find(name);
            if (it == tasks.end() || !it->second) {
                mutex.unlock();
                throw FreeRTOSException("Task not found: " + name);
            }

            Task *task = it->second;
            lastWatchdogResetTimes.erase(name); // Remove temporarily
            task->setCore(newCoreId);

            if (groupWatchdogEnabled) {
                lastWatchdogResetTimes[name] = xTaskGetTickCount();
                ESP_LOGI(TAG, "Task %s re-added to software watchdog after core change", name.c_str());
            }

            mutex.unlock();
        } catch (...) {
            mutex.unlock();
            throw FreeRTOSException("Failed to set core for task: " + name);
        }
    }
    void adjustTaskStack(TaskGroup *self, const TaskMetrics &metric)
    {
        static const char *TAG = "TaskMonitor";
        // if (!mutex.lock(pdMS_TO_TICKS(500)))
        // {
        //     ESP_LOGI(TAG, "Failed to acquire mutex for task stack adjustment");
        //     return;
        // }

        // Get task metadata
        TaskMetadata metadata;
        try {
            metadata = TaskAnalyzer::getInstance().getTaskHistory(metric.name);
        } catch (const FreeRTOSException &e) {
            ESP_LOGE(TAG, "Failed to get metadata for %s: %s", metric.name.c_str(), e.what());
            // mutex.unlock();
            return;
        }

        // Check free stack using uxTaskGetStackHighWaterMark
        TaskHandle_t handle = self->getTaskHandle(metric.name);
        if (!handle) {
            ESP_LOGE(TAG, "Task %s handle not found", metric.name.c_str());
            // mutex.unlock();
            return;
        }
        UBaseType_t free_stack_words = uxTaskGetStackHighWaterMark(handle);
        uint32_t free_stack_bytes = free_stack_words * sizeof(StackType_t);
        uint32_t stack_size = metadata.stackSize * sizeof(StackType_t);

        // Resize if free stack is low (<10% of allocated size) and stack < 16 KB
        const uint32_t max_stack_bytes = 16 * 1024;      // 16 KB limit
        const uint32_t min_free_stack = stack_size / 20; // 10% threshold
        const uint32_t increment_bytes = 1024;           // Add 1 KB

        if (free_stack_bytes < min_free_stack && stack_size < max_stack_bytes) {
            // Check heap availability
            if (xPortGetFreeHeapSize() < increment_bytes + 4096) {
                ESP_LOGE(TAG, "Insufficient heap (%u bytes) to resize %s", xPortGetFreeHeapSize(), metric.name.c_str());
                // mutex.unlock();
                return;
            }

            uint32_t new_stack_size = stack_size + increment_bytes;
            try {
                ESP_LOGI(TAG, "Increasing stack for %s from %lu to %lu bytes", metric.name.c_str(), stack_size, new_stack_size);
                self->resizeTaskStack(metric.name, new_stack_size / sizeof(StackType_t));
                // metadata.stackSize = new_stack_size;  // Update metadata
                // TaskAnalyzer::getInstance().addTaskMetadata(metadata);  // Sync metadata
            } catch (const FreeRTOSException &e) {
                ESP_LOGE(TAG, "Failed to resize %s stack: %s", metric.name.c_str(), e.what());
            }
        } else if (free_stack_bytes >= min_free_stack) {
            ESP_LOGI(TAG, "Task %s has sufficient stack: %lu bytes free (total %lu)", metric.name.c_str(), free_stack_bytes, metadata.stackSize);
        } else {
            ESP_LOGE(TAG, "Task %s stack too large (%lu bytes), not resizing", metric.name.c_str(), metadata.stackSize);
        }
        // mutex.unlock();
    }
    std::vector<TaskMetrics> getTaskGroupRuntimeStats(TaskGroup *group)
    {
        std::vector<TaskMetrics> metrics;
        if (!mutex.lock(pdMS_TO_TICKS(500))) {
            return metrics;
        }

        // Get number of tasks
        UBaseType_t task_count = uxTaskGetNumberOfTasks();
        TaskStatus_t *task_status_array = new TaskStatus_t[task_count];
        configRUN_TIME_COUNTER_TYPE total_run_time;
        if (task_status_array == nullptr) {
            ESP_LOGW(TAG, "Unable to allocate the memory");
            mutex.unlock();
            return metrics;
        }

        // Get system state
        UBaseType_t tasks_reported = uxTaskGetSystemState(task_status_array, task_count, &total_run_time);
        if (tasks_reported == 0) {
            delete[] task_status_array;
            ESP_LOGW(TAG, "No tasks reported by uxTaskGetSystemState");
            mutex.unlock();
            return metrics;
        }
        for (UBaseType_t i = 0; i < tasks_reported; i++) {
            if (std::string(task_status_array[i].pcTaskName) == "IDLE0" || std::string(task_status_array[i].pcTaskName) == "IDLE1")

            {
                TaskMetrics metric;
                metric.name = task_status_array[i].pcTaskName;
                metric.runTimeCounter = task_status_array[i].ulRunTimeCounter;
                metric.freeStackBytes = task_status_array[i].usStackHighWaterMark * sizeof(StackType_t);
                metric.state = task_status_array[i].eCurrentState;
                metric.coreId = task_status_array[i].xCoreID;

                // Calculate CPU percentage
                if (total_run_time > 0) {
                    metric.cpuPercentage = (task_status_array[i].ulRunTimeCounter * 100.0f) / total_run_time;
                    if (metric.cpuPercentage < 0.01f) {
                        metric.cpuPercentage = 0.0f; // Less than 0.01%
                    }
                } else {
                    metric.cpuPercentage = 0.0f;
                }

                metrics.push_back(metric);
                // ESP_LOGI(TAG, "%s: CPU=%.2f%%, RunTime=%lu, FreeStack=%lu, State=%d, Core=%d",
                //          task_status_array[i].pcTaskName, metric.cpuPercentage,
                //          metric.runTimeCounter, metric.freeStackBytes, metric.state,
                //          metric.coreId);
            }
        }
        std::vector<std::string> task_names = group->getAllTaskNames();
        for (const std::string &name : task_names) {
            // Calculate percentages and collect metrics
            for (UBaseType_t i = 0; i < tasks_reported; i++) {
                if (std::string(task_status_array[i].pcTaskName) == name) {
                    TaskMetrics metric;
                    metric.name = name;
                    metric.runTimeCounter = task_status_array[i].ulRunTimeCounter;
                    metric.freeStackBytes = task_status_array[i].usStackHighWaterMark * sizeof(StackType_t);
                    metric.state = task_status_array[i].eCurrentState;
                    metric.coreId = task_status_array[i].xCoreID;

                    // Calculate CPU percentage
                    if (total_run_time > 0) {
                        metric.cpuPercentage = (task_status_array[i].ulRunTimeCounter * 100.0f) / total_run_time;
                        if (metric.cpuPercentage < 0.01f) {
                            metric.cpuPercentage = 0.0f; // Less than 0.01%
                        }
                    } else {
                        metric.cpuPercentage = 0.0f;
                    }

                    metrics.push_back(metric);
                    // ESP_LOGI(TAG, "%s: CPU=%.2f%%, RunTime=%lu, FreeStack=%lu, State=%d,
                    // Core=%d",
                    //          name.c_str(), metric.cpuPercentage, metric.runTimeCounter,
                    //          metric.freeStackBytes, metric.state, metric.coreId);
                }
            }
        }

        delete[] task_status_array;
        mutex.unlock();
        return metrics;
    }

    std::vector<std::string> checkFailedBitTask(EventBits_t eventBits)
    {
        std::vector<std::string> failedTasks;
        const int maxRetries = 2;
        int retries = 0;
        bool mutexAcquired = false;

        while (retries < maxRetries && !mutexAcquired) {
            if (mutex.lock(pdMS_TO_TICKS(500))) {
                mutexAcquired = true;
            } else {
                retries++;
                ESP_LOGD(TAG, "checkFailedBitTask: Mutex acquisition attempt %d failed", retries);
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }

        if (!mutexAcquired) {
            ESP_LOGW(TAG, "Failed to acquire mutex for liveness check after %d retries", maxRetries);
            return {"MutexError"};
        }

        try {
            for (const auto &pair : tasks) {
                const std::string &name = pair.first;
                Task *task = pair.second;
                if (task) {
                    EventBits_t livenessBit = task->getMetadata().livenessBit;
                    if (livenessBit != 0 && !(eventBits & livenessBit)) {
                        failedTasks.push_back(name);
                    }
                }
            }
            mutex.unlock();
        } catch (const FreeRTOSException &e) {
            mutex.unlock();
            ESP_LOGE(TAG, "Error in checkFailedBitTask: %s", e.what());
            return {"Error"};
        } catch (...) {
            mutex.unlock();
            ESP_LOGE(TAG, "Unknown error in checkFailedBitTask");
            return {"UnknownError"};
        }

        return failedTasks;
    }

    std::string getTaskGroupName() const
    {
        return taskGroupName;
    }
    bool isMonitorTaskEnabled() const
    {
        return enableMonitorTask;
    }
    UBaseType_t getGroupPriority() const
    {
        return groupPriority;
    }
    BaseType_t getMonitorCoreId() const
    {
        return monitorCoreId;
    }

    void addTask(const std::string &name, uint32_t stackSize, UBaseType_t priority, const std::function<void()> &func, TickType_t watchDogTimeOut, BaseType_t coreId = tskNO_AFFINITY,
                 EventBits_t eventBits = 0, TaskType type = TaskType::DYNAMIC)
    {
        if (priority > groupPriority) {
            // throw FreeRTOSException("Task Priority");
            throw FreeRTOSException("Task priority (" + std::to_string(static_cast<uint32_t>(priority)) + ") exceeds group priority (" + std::to_string(static_cast<uint32_t>(groupPriority)) + ")");
        }

        if (!mutex.lock(pdMS_TO_TICKS(300))) {
            throw FreeRTOSException("Failed to acquire mutex for task addition");
        }

        try {
            EventBits_t livenessBit = groupWatchdogEnabled ? eventBits : 0;
            tasks[name] = new Task(name.c_str(), stackSize, priority, func, watchDogTimeOut, false, coreId, type, livenessBit, &eventGroup);
            if (groupWatchdogEnabled) {
                lastWatchdogResetTimes[name] = xTaskGetTickCount();
                ESP_LOGI(TAG, "Task %s added to software watchdog", name.c_str());
            }
            taskNames.push_back(name);
            numberOfTasksInGroup++;
            mutex.unlock();
            ESP_LOGI(TAG, "Task %s added to TaskGroup %s", name.c_str(), taskGroupName.c_str());
        } catch (...) {
            mutex.unlock();
            throw FreeRTOSException("Failed to add task: " + name);
        }
    }

    uint32_t numberOfTasks()
    {
        return numberOfTasksInGroup;
    }

    std::vector<std::string> getAllTaskNames()
    {
        return taskNames;
    }

    void signalAndResetWatchdog(const std::string name)
    {
        Task *task = nullptr;
        bool taskFound = false;

        if (!mutex.lock(pdMS_TO_TICKS(500))) {
            ESP_LOGW(TAG, "Failed to acquire mutex for task %s signalAndResetWatchdog, skipping", name.c_str());
            return;
        }
        try {
            auto it = tasks.find(name);
            if (it != tasks.end() && it->second) {
                task = it->second;
                taskFound = true;
            }
            mutex.unlock();
        } catch (const FreeRTOSException &e) {
            mutex.unlock();
            ESP_LOGE(TAG, "Error checking task %s for signalAndResetWatchdog: %s", name.c_str(), e.what());
            return;
        } catch (...) {
            mutex.unlock();
            ESP_LOGE(TAG, "Unknown error checking task %s for signalAndResetWatchdog", name.c_str());
            return;
        }

        if (!taskFound) {
            ESP_LOGE(TAG, "Task %s not found for signalAndResetWatchdog", name.c_str());
            return;
        }

        try {
            task->resetWatchdog();
            if (!mutex.lock(pdMS_TO_TICKS(500))) {
                ESP_LOGW(TAG,
                         "Failed to acquire mutex to update signalAndResetWatchdog timestamp for "
                         "task %s",
                         name.c_str());
                return;
            }
            lastWatchdogResetTimes[name] = xTaskGetTickCount();
            eventGroup.setBits(task->getMetadata().livenessBit);
            mutex.unlock();
            // ESP_LOGI(TAG, "Task %s signaled and watchdog reset", name.c_str());
        } catch (const FreeRTOSException &e) {
            ESP_LOGE(TAG, "Error resetting signalAndResetWatchdog for task %s: %s", name.c_str(), e.what());
        } catch (...) {
            ESP_LOGE(TAG, "Unknown error resetting signalAndResetWatchdog for task %s", name.c_str());
        }
    }
    TaskMetadata getTaskMetadata(const std::string &name)
    {
        if (!mutex.lock(pdMS_TO_TICKS(500))) {
            throw FreeRTOSException("Failed to acquire mutex for task metadata");
        }
        try {
            auto it = tasks.find(name);
            if (it != tasks.end() && it->second) {
                TaskMetadata metadata = it->second->getMetadata();
                mutex.unlock();
                return metadata;
            }
            mutex.unlock();
            throw FreeRTOSException("Task not found: " + name);
        } catch (...) {
            mutex.unlock();
            throw;
        }
    }
    void resetSoftwareTaskWatchdog(const std::string &name)
    {
        Task *task = nullptr;
        bool taskFound = false;

        if (!mutex.lock(pdMS_TO_TICKS(500))) {
            ESP_LOGW(TAG, "Failed to acquire mutex for task %s watchdog reset, skipping", name.c_str());
            return;
        }
        try {
            auto it = tasks.find(name);
            if (it != tasks.end() && it->second) {
                task = it->second;
                taskFound = true;
            }
            mutex.unlock();
        } catch (const FreeRTOSException &e) {
            mutex.unlock();
            ESP_LOGE(TAG, "Error checking task %s for watchdog reset: %s", name.c_str(), e.what());
            return;
        } catch (...) {
            mutex.unlock();
            ESP_LOGE(TAG, "Unknown error checking task %s for watchdog reset", name.c_str());
            return;
        }

        if (!taskFound) {
            ESP_LOGI(TAG, "Task %s not found for watchdog reset", name.c_str());
            return;
        }

        try {
            task->resetWatchdog();
            if (!mutex.lock(pdMS_TO_TICKS(500))) {
                ESP_LOGW(TAG, "Failed to acquire mutex to update watchdog timestamp for task %s", name.c_str());
                return;
            }
            lastWatchdogResetTimes[name] = xTaskGetTickCount();
            mutex.unlock();
            ESP_LOGI(TAG, "Software watchdog reset for task %s", name.c_str());
        } catch (const FreeRTOSException &e) {
            ESP_LOGE(TAG, "Error resetting watchdog for task %s: %s", name.c_str(), e.what());
        } catch (...) {
            ESP_LOGE(TAG, "Unknown error resetting watchdog for task %s", name.c_str());
        }
    }

    std::pair<bool, std::vector<std::string>> checkSoftwareWatchdogs()
    {
        std::vector<std::string> failedTasks;
        const int maxRetries = 3;
        int retries = 0;
        bool mutexAcquired = false;

        while (retries < maxRetries && !mutexAcquired) {
            if (mutex.lock(pdMS_TO_TICKS(1000))) {
                mutexAcquired = true;
            } else {
                retries++;
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }

        if (!mutexAcquired) {
            ESP_LOGW(TAG, "Failed to acquire mutex for software watchdog check after %d retries", maxRetries);
            return {false, {"MutexError"}};
        }

        TickType_t currentTime = xTaskGetTickCount();
        try {
            for (const auto &pair : tasks) {
                const std::string &name = pair.first;
                auto it = lastWatchdogResetTimes.find(name);

                if (it != lastWatchdogResetTimes.end()) {
                    TickType_t lastReset = it->second;
                    TaskMetadata metadata = pair.second->getMetadata();
                    if ((currentTime - lastReset) > metadata.softwareWatchDogTimeOut) {
                        ESP_LOGE(TAG, "Software watchdog timeout for task %s", name.c_str());
                        failedTasks.push_back(name);
                    }
                } else if (pair.second->getMetadata().watchdogEnabled) {
                    ESP_LOGE(TAG, "No watchdog reset recorded for task %s", name.c_str());
                    failedTasks.push_back(name);
                }
            }
            mutex.unlock();
        } catch (...) {
            mutex.unlock();
            ESP_LOGE(TAG, "Error in software watchdog check");
            return {false, {"UnknownError"}};
        }

        bool allAlive = failedTasks.empty();
        return {allAlive, failedTasks};
    }

    void resetTaskWatchdog(const std::string &name)
    {
        if (!mutex.lock(pdMS_TO_TICKS(500))) {
            ESP_LOGW(TAG, "Failed to acquire mutex for task %s watchdog reset, skipping", name.c_str());
            return;
        }
        try {
            auto it = tasks.find(name);
            if (it != tasks.end() && it->second) {
                it->second->resetWatchdog();
                lastWatchdogResetTimes[name] = xTaskGetTickCount();
                ESP_LOGI(TAG, "Task %s watchdog reset", name.c_str());
            } else {
                ESP_LOGW(TAG, "Task %s not found for watchdog reset", name.c_str());
            }
            mutex.unlock();
        } catch (const FreeRTOSException &e) {
            mutex.unlock();
            ESP_LOGE(TAG, "Error resetting watchdog for task %s: %s", name.c_str(), e.what());
        } catch (...) {
            mutex.unlock();
            ESP_LOGE(TAG, "Error resetting watchdog for task %s", name.c_str());
        }
    }

    TaskHandle_t getTaskHandle(const std::string &name)
    {
        if (!mutex.lock(pdMS_TO_TICKS(500))) {
            throw FreeRTOSException("Failed to acquire mutex for task handle");
        }
        try {
            auto it = tasks.find(name);
            if (it != tasks.end() && it->second) {
                TaskHandle_t handle = it->second->getHandle();
                mutex.unlock();
                return handle;
            }
            mutex.unlock();
            throw FreeRTOSException("Task not found: " + name);
        } catch (...) {
            mutex.unlock();
            throw FreeRTOSException("Failed to get task handle: " + name);
        }
    }
};

// TaskGroupManager to track all TaskGroup instances
class TaskGroupManager {
   private:
    std::map<std::string, TaskGroup *> taskGroups;
    Mutex mutex;

    TaskGroupManager() = default;

   public:
    static TaskGroupManager &getInstance()
    {
        static TaskGroupManager instance;
        return instance;
    }

    void registerTaskGroup(TaskGroup *group)
    {
        if (!group) {
            ESP_LOGE(TAG, "Attempt to register null TaskGroup");
            return;
        }
        if (!mutex.lock(pdMS_TO_TICKS(500))) {
            ESP_LOGW(TAG, "Failed to acquire mutex for TaskGroup registration");
            return;
        }
        try {
            std::string name = group->getTaskGroupName();
            taskGroups[name] = group;
            ESP_LOGI(TAG, "TaskGroup %s registered", name.c_str());
            mutex.unlock();
        } catch (...) {
            mutex.unlock();
            ESP_LOGE(TAG, "Error registering TaskGroup");
        }
    }

    void unregisterTaskGroup(const std::string &name)
    {
        if (!mutex.lock(pdMS_TO_TICKS(500))) {
            ESP_LOGW(TAG, "Failed to acquire mutex for TaskGroup unregistration");
            return;
        }
        try {
            auto it = taskGroups.find(name);
            if (it != taskGroups.end()) {
                taskGroups.erase(it);
                ESP_LOGI(TAG, "TaskGroup %s unregistered", name.c_str());
            }
            mutex.unlock();
        } catch (...) {
            mutex.unlock();
            ESP_LOGE(TAG, "Error unregistering TaskGroup %s", name.c_str());
        }
    }

    size_t getTaskGroupCount()
    {
        if (!mutex.lock(pdMS_TO_TICKS(500))) {
            ESP_LOGW(TAG, "Failed to acquire mutex for TaskGroup count");
            return 0;
        }
        size_t count = taskGroups.size();
        mutex.unlock();
        return count;
    }

    struct TaskGroupInfo {
        std::string name;
        bool monitorTaskEnabled;
        UBaseType_t groupPriority;
        BaseType_t monitorCoreId;
        uint32_t taskCount;
        std::vector<TaskMetadata> tasks;
    };

    std::vector<TaskGroupInfo> getAllTaskGroupsInfo()
    {
        std::vector<TaskGroupInfo> infoList;
        if (!mutex.lock(pdMS_TO_TICKS(1000))) {
            ESP_LOGW(TAG, "Failed to acquire mutex for TaskGroup info");
            return infoList;
        }
        try {
            for (const auto &pair : taskGroups) {
                TaskGroup *group = pair.second;
                if (!group)
                    continue;
                TaskGroupInfo info;
                info.name = group->getTaskGroupName();
                info.monitorTaskEnabled = group->isMonitorTaskEnabled();
                info.groupPriority = group->getGroupPriority();
                info.monitorCoreId = group->getMonitorCoreId();
                info.taskCount = group->numberOfTasks();
                auto taskNames = group->getAllTaskNames();
                for (const auto &taskName : taskNames) {
                    try {
                        info.tasks.push_back(group->getTaskMetadata(taskName));
                    } catch (const FreeRTOSException &e) {
                        ESP_LOGW(TAG, "Failed to get metadata for task %s in group %s: %s", taskName.c_str(), info.name.c_str(), e.what());
                    }
                }
                infoList.push_back(info);
            }
            mutex.unlock();
        } catch (...) {
            mutex.unlock();
            ESP_LOGE(TAG, "Error retrieving TaskGroup info");
        }
        return infoList;
    }
};

// Notification wrapper class
class Notification {
   private:
    TaskHandle_t taskToNotify;

   public:
    Notification(TaskHandle_t task) : taskToNotify(task)
    {
    }

    bool notify(uint32_t value)
    {
        return xTaskNotify(taskToNotify, value, eSetValueWithOverwrite) == pdTRUE;
    }

    bool wait(uint32_t &value, TickType_t timeout)
    {
        return xTaskNotifyWait(0, ULONG_MAX, &value, timeout) == pdTRUE;
    }
};

} // namespace FreeRTOSWrapper
