#include "system_monitor.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "cstring"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "math.h"
// #include "queue.h"

namespace system_monitor {

namespace {

static const char TAG[] = "SYSTEM_MONITOR";
[[maybe_unused]]
void watchdog_task(void *arg)
{
    SystemMonitor &monitor = SystemMonitor::getInstance();
    esp_task_wdt_add(nullptr);
    ESP_LOGI(TAG, "Starting watchdog task.");
    // while(mini)

    while (true) {
        char task_name[CONFIG_FREERTOS_MAX_TASK_NAME_LEN];

        if (monitor.twdt_queue && xQueueReceive(monitor.twdt_queue, task_name, 1000 / portTICK_PERIOD_MS) == pdTRUE) {
            ESP_LOGE(TAG, ".........Processing TWDT Timeout for Task: %s", task_name);
            auto it = monitor.task_registry_.find(task_name);

            if (it != monitor.task_registry_.end()) {
                TaskStatus_t task_list[20]; // Adjust size based on expected number of tasks
                UBaseType_t count = uxTaskGetSystemState(task_list, 20, nullptr);
                TaskStatus_t *status = nullptr;

                for (UBaseType_t i = 0; i < count; ++i) {
                    if (task_list[i].xHandle == it->second.handle) {
                        status = &task_list[i];
                        break;
                    }
                }

                if (status != nullptr) {
                    const char *state_str;
                    switch (status->eCurrentState) {
                        case eRunning:
                            state_str = "Running";
                            break;
                        case eReady:
                            state_str = "Ready";
                            break;
                        case eBlocked:
                            state_str = "Blocked";
                            break;
                        case eSuspended:
                            state_str = "Suspended";
                            break;
                        case eDeleted:
                            state_str = "Deleted";
                            break;
                        default:
                            state_str = "Unknown";
                            break;
                    }

                    TickType_t elapsed = (xTaskGetTickCount() - it->second.last_twdt_reset) * portTICK_PERIOD_MS;

                    ESP_LOGE(TAG, "Task %s (Handle: %p) failed to reset TWDT. Elapsed: %" PRIu32 "ms, State: %s, Stack Free: %" PRIu32 " bytes, Priority:%d", task_name, it->second.handle, elapsed,
                             state_str, status->usStackHighWaterMark * sizeof(StackType_t), status->uxCurrentPriority);

                    // Take configured action
                    switch (monitor.twdt_action_) {
                        case TwdtAction::LOG_ONLY:
                            vTaskSuspend(it->second.handle);
                            esp_task_wdt_delete(it->second.handle);
                            ESP_LOGE(TAG, "Suspended task %s due to TWDT timeout", task_name);
                            // Already logged
                            break;
                        case TwdtAction::SUSPEND:
                            vTaskSuspend(it->second.handle);
                            ESP_LOGE(TAG, "Suspended task %s due to TWDT timeout", task_name);
                            break;
                        case TwdtAction::RECREATE:
                            ESP_LOGE(TAG, "Recreating task %s due to TWDT timeout", task_name);
                            monitor.recreate_task(task_name);
                            break;
                    }
                } else {
                    ESP_LOGW(TAG, "Task handle not found in system state for task: %s", task_name);
                }
            } else {
                ESP_LOGW(TAG, "Task %s not found in registry", task_name);
            }
        }

        monitor.update_twdt_reset_time(nullptr);
        esp_task_wdt_reset();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void monitor_task(void *arg)
{
    SystemMonitor &monitor = SystemMonitor::getInstance();
    esp_task_wdt_add(nullptr);
    ESP_LOGI(TAG, "Starting monitor task. vApplicationStackOverflowHook registered.");

    // Pre-allocate all buffers outside the loop to prevent heap fragmentation
    // char *task_list_buffer = static_cast<char *>(pvPortMalloc(1024));
    // if (!task_list_buffer)
    // {
    //     ESP_LOGE(TAG, "Failed to allocate task list buffer");
    //     vTaskDelete(nullptr);
    //     return;
    // }

    // Pre-allocate max possible tasks to avoid frequent reallocation
    // const UBaseType_t MAX_TASKS = 50;
    // TaskStatus_t *task_status_array =
    //     static_cast<TaskStatus_t *>(pvPortMalloc(MAX_TASKS * sizeof(TaskStatus_t)));
    // if (!task_status_array)
    // {
    //     ESP_LOGE(TAG, "Failed to allocate task status array");
    //     vPortFree(task_list_buffer);
    //     vTaskDelete(nullptr);
    //     return;
    // }

    // Pre-allocate health list
    // std::vector<TaskHealth> task_health_list;
    // task_health_list.reserve(MAX_TASKS);

    // Track runtime stats
    // vTaskGetRunTimeStats(task_list_buffer);
    // TickType_t last_monitor_time = xTaskGetTickCount();
    // std::vector<TickType_t> last_run_times(uxTaskGetNumberOfTasks(), 0);
    // UBaseType_t previous_task_count = uxTaskGetNumberOfTasks();

    while (true) {
        // Reset the watchdog early in the loop to prevent timeout
        esp_task_wdt_reset();
        monitor.update_twdt_reset_time(nullptr);

        // System resource monitoring
        uint32_t free_heap = esp_get_free_heap_size();
        uint32_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        uint32_t min_free_heap = esp_get_minimum_free_heap_size();
        // TickType_t tick_count = xTaskGetTickCount();
        // bool interrupts_enabled = false;
        // = !portCHECK_IF_IN_ISR();
        // BaseType_t scheduler_state = xTaskGetSchedulerState();
        // std::string scheduler_state_str = (scheduler_state == taskSCHEDULER_RUNNING) ? "Running"
        //                                   : (scheduler_state == taskSCHEDULER_SUSPENDED)
        //                                       ? "Suspended"
        //                                       : "Not Started";

        ESP_LOGI(TAG, "System Resources - Free Heap: %" PRIu32 " bytes, Min Free Heap: %" PRIu32 " bytes, Free PSRAM: %" PRIu32 " bytes", free_heap, min_free_heap, free_psram);

        // ESP_LOGI(TAG, "System Status - Uptime: %" PRIu32 " ticks, Interrupts: %s, Scheduler: %s",
        //          tick_count, interrupts_enabled ? "Enabled" : "Disabled",
        //          scheduler_state_str.c_str());

        // Get current task count and resize if needed
        // UBaseType_t task_count = uxTaskGetNumberOfTasks();
        // ESP_LOGI(TAG, "Total Tasks Running: %d", static_cast<int>(task_count));

        // // Only resize vectors if needed
        // if (task_count > previous_task_count)
        // {
        //     if (task_count > MAX_TASKS)
        //     {
        //         ESP_LOGW(TAG, "Task count exceeds pre-allocated buffer, limiting to %d tasks",
        //                  static_cast<int>(MAX_TASKS));
        //         task_count = MAX_TASKS;
        //     }
        //     last_run_times.resize(task_count, 0);
        //     previous_task_count = task_count;
        // }

        // // Clear the health list and reuse it
        // task_health_list.clear();

        // // Get task stats
        // UBaseType_t tasks_reported = uxTaskGetSystemState(task_status_array, task_count,
        // nullptr); task_health_list.resize(tasks_reported);

        // // Calculate total runtime for CPU usage percentage
        // uint32_t total_run_time = 0;
        // for (UBaseType_t i = 0; i < tasks_reported; i++)
        // {
        //     total_run_time += task_status_array[i].ulRunTimeCounter;
        // }

        // // Track if IDLE1 task is starved (potential TWDT risk)
        // bool idle1_starved = false;
        // int idle1_index = -1;

        // Process each task
        // for (UBaseType_t i = 0; i < tasks_reported; i++)
        // {
        //     TaskHealth &health = task_health_list[i];
        //     health.name = task_status_array[i].pcTaskName;
        //     health.is_healthy = true;
        //     health.health_reason = "OK";
        //     health.stack_free = task_status_array[i].usStackHighWaterMark * sizeof(StackType_t);

        //     // Calculate stack usage information
        //     auto it = monitor.task_registry_.find(health.name);
        //     if (it != monitor.task_registry_.end())
        //     {
        //         health.stack_size = it->second.stack_size;
        //         health.stack_usage =
        //             health.stack_size > 0
        //                 ? (static_cast<float>(health.stack_size - health.stack_free) /
        //                    health.stack_size) *
        //                       100.0f
        //                 : 0.0f;

        //         // Track previous stack free to detect memory issues
        //         uint32_t previous_free_stack = it->second.previous_free_stack;
        //         if (previous_free_stack > 0)
        //         {
        //             float stack_change = 0.0f;
        //             if (previous_free_stack > 0)
        //             {
        //                 stack_change =
        //                     (static_cast<float>(health.stack_free - previous_free_stack) *
        //                     100.0f) / previous_free_stack;
        //                 // Only update if change is significant but not extreme
        //                 if (fabs(stack_change) < 1000.0f)
        //                 {
        //                     it->second.stack_Increament = stack_change;
        //                 }
        //             }
        //         }
        //         it->second.previous_free_stack = health.stack_free;

        //         // Only warn about stack issues if free stack is low and changing rapidly
        //         if (fabs(it->second.stack_Increament) > 50.0f && health.stack_free <= 2000)
        //         {
        //             ESP_LOGW(TAG, "Stack Increment of %s task is :: %f", health.name.c_str(),
        //                      it->second.stack_Increament);
        //         }
        //     }
        //     else
        //     {
        //         health.stack_size = 0;
        //         health.stack_usage = 0.0f;
        //     }

        //     // Calculate CPU usage
        //     health.cpu_usage =
        //         total_run_time > 0
        //             ? (static_cast<float>(task_status_array[i].ulRunTimeCounter) /
        //             total_run_time) *
        //                   100.0f
        //             : 0.0f;

        //     // Other task metrics
        //     health.blocked_time_ms = 0;
        //     health.last_yield_ms = 0;
        //     health.twdt_risk = false;
        //     health.core_id = xTaskGetCoreID(task_status_array[i].xHandle);
        //     if (health.core_id == tskNO_AFFINITY) health.core_id = -1;
        //     health.base_priority = task_status_array[i].uxBasePriority;
        //     health.task_number = task_status_array[i].xTaskNumber;
        //     health.runtime_counter = task_status_array[i].ulRunTimeCounter;
        //     health.stack_base = task_status_array[i].pxStackBase;
        //     health.twdt_subscribed = esp_task_wdt_status(task_status_array[i].xHandle) == ESP_OK;

        //     // Check if this task has previously overflowed its stack
        //     health.stack_overflow =
        //         std::find(monitor.get_stack_overflow_tasks().begin(),
        //                   monitor.get_stack_overflow_tasks().end(),
        //                   health.name) != monitor.get_stack_overflow_tasks().end();

        //     // Health checks
        //     if (health.stack_overflow)
        //     {
        //         health.is_healthy = false;
        //         health.health_reason = "Stack Overflow";
        //         health.twdt_risk = true;
        //     }

        //     if (health.is_healthy && health.stack_free < config::MIN_STACK_THRESHOLD)
        //     {
        //         health.is_healthy = false;
        //         health.health_reason =
        //             "Low Stack (" + std::to_string(health.stack_free) + " bytes)";
        //         health.twdt_risk = true;
        //     }

        //     // Check CPU usage, but exclude IDLE tasks which are expected to have varying usage
        //     if (health.is_healthy && health.name != "IDLE0" && health.name != "IDLE1" &&
        //         health.cpu_usage > config::CPU_USAGE_THRESHOLD)
        //     {
        //         health.is_healthy = false;
        //         health.health_reason =
        //             "High CPU (" + std::to_string(static_cast<int>(health.cpu_usage)) + "%)";
        //         health.twdt_risk = true;
        //     }

        //     // Check for tasks blocked too long
        //     if (health.is_healthy && task_status_array[i].eCurrentState == eBlocked)
        //     {
        //         health.blocked_time_ms =
        //             (xTaskGetTickCount() - last_monitor_time) * portTICK_PERIOD_MS;
        //         if (health.blocked_time_ms > config::BLOCKED_TIMEOUT_MS)
        //         {
        //             health.is_healthy = false;
        //             health.health_reason =
        //                 "Blocked Too Long (" + std::to_string(health.blocked_time_ms) + "ms)";
        //         }
        //     }

        //     // Check if task is not yielding (not making progress)
        //     if (health.is_healthy && health.name != "IDLE0" && health.name != "IDLE1" &&
        //         (task_status_array[i].eCurrentState == eRunning ||
        //          task_status_array[i].eCurrentState == eReady) &&
        //         i < last_run_times.size())
        //     {  // Safety check for index bounds

        //         if (task_status_array[i].ulRunTimeCounter == last_run_times[i])
        //         {
        //             health.last_yield_ms =
        //                 (xTaskGetTickCount() - last_monitor_time) * portTICK_PERIOD_MS;
        //             if (health.last_yield_ms > config::YIELD_TIMEOUT_MS)
        //             {
        //                 health.is_healthy = false;
        //                 health.health_reason =
        //                     "Not Yielding (" + std::to_string(health.last_yield_ms) + "ms)";
        //                 health.twdt_risk = true;
        //             }
        //         }
        //     }

        //     // Check for IDLE1 starvation which indicates potential TWDT issues
        //     if (health.is_healthy && health.name == "IDLE1" &&
        //         health.cpu_usage < config::IDLE_STARVATION_THRESHOLD)
        //     {
        //         idle1_starved = true;
        //         idle1_index = i;
        //         health.is_healthy = false;
        //         health.health_reason = "Starved (TWDT Risk)";
        //     }

        //     // Check task TWDT subscription status
        //     if (health.is_healthy && !health.twdt_subscribed &&
        //         std::find(SystemMonitor::system_tasks_.begin(),
        //         SystemMonitor::system_tasks_.end(),
        //                   health.name) == SystemMonitor::system_tasks_.end())
        //     {
        //         health.is_healthy = false;
        //         health.health_reason = "Not Subscribed to TWDT";
        //         health.twdt_risk = true;
        //     }

        //     // Check if task is at risk of TWDT timeout
        //     if (health.is_healthy && health.twdt_subscribed)
        //     {
        //         auto it = monitor.task_registry_.find(health.name);
        //         if (it != monitor.task_registry_.end() &&
        //             it->second.handle == task_status_array[i].xHandle)
        //         {
        //             TickType_t last_reset = it->second.last_twdt_reset;
        //             TickType_t elapsed = (xTaskGetTickCount() - last_reset) * portTICK_PERIOD_MS;
        //             if (elapsed > config::TWDT_WARNING_THRESHOLD_MS)
        //             {
        //                 health.is_healthy = false;
        //                 health.health_reason =
        //                     "TWDT Timeout Risk (" + std::to_string(elapsed) + "ms)";
        //                 health.twdt_risk = true;

        //                 // Log TWDT timeout risk
        //                 const char *state_str;
        //                 switch (task_status_array[i].eCurrentState)
        //                 {
        //                     case eRunning:
        //                         state_str = "Running";
        //                         break;
        //                     case eReady:
        //                         state_str = "Ready";
        //                         break;
        //                     case eBlocked:
        //                         state_str = "Blocked";
        //                         break;
        //                     case eSuspended:
        //                         state_str = "Suspended";
        //                         break;
        //                     case eDeleted:
        //                         state_str = "Deleted";
        //                         break;
        //                     default:
        //                         state_str = "Unknown";
        //                         break;
        //                 }
        //                 ESP_LOGW(TAG,
        //                          "Task %s (Handle: %p) at risk of TWDT timeout. Elapsed: %"
        //                          PRIu32 "ms, State: %s, Stack Free: %" PRIu32 " bytes",
        //                          health.name.c_str(), task_status_array[i].xHandle, elapsed,
        //                          state_str, health.stack_free);

        //                 // Use log-only approach by default to avoid disrupting system
        //                 if (monitor.twdt_action_ != TwdtAction::LOG_ONLY)
        //                 {
        //                     ESP_LOGW(TAG, "TWDT action set to %d but using LOG_ONLY for safety",
        //                              static_cast<int>(monitor.twdt_action_));
        //                 }
        //             }
        //         }
        //     }

        //     // Store runtime counter for next iteration
        //     if (i < last_run_times.size())
        //     {
        //         last_run_times[i] = task_status_array[i].ulRunTimeCounter;
        //     }
        // }

        // Handle IDLE1 starvation - identify tasks that might be causing it
        // if (idle1_starved)
        // {
        //     for (UBaseType_t i = 0; i < tasks_reported; i++)
        //     {
        //         TaskHealth &health = task_health_list[i];
        //         if (health.name != "IDLE0" && health.name != "IDLE1" &&
        //             (health.core_id == 1 || health.core_id == -1) &&
        //             task_status_array[i].uxCurrentPriority > 0 && health.cpu_usage > 10.0f)
        //         {
        //             health.is_healthy = false;
        //             health.health_reason = "Causing TWDT (Starving IDLE1)";
        //             health.twdt_risk = true;
        //         }
        //     }
        // }

        // Process TWDT timeout tasks in a safer way
        // if (!monitor.twdt_timeout_tasks_.empty())
        // {
        //     ESP_LOGW(TAG, "Processing %zu tasks that failed TWDT",
        //              monitor.twdt_timeout_tasks_.size());
        //     for (const auto &task_name : monitor.twdt_timeout_tasks_)
        //     {
        //         ESP_LOGW(TAG, "TWDT Timeout Detected for Task: %s", task_name.c_str());

        //         // Only log the issue instead of taking more disruptive actions
        //         ESP_LOGW(TAG, "Task %s failed to reset watchdog. Logging only for stability.",
        //                  task_name.c_str());
        //     }
        //     monitor.twdt_timeout_tasks_.clear();
        // }

        // Take remedial actions for unhealthy tasks
        // for (UBaseType_t i = 0; i < tasks_reported; i++)
        // {
        //     TaskHealth &health = task_health_list[i];
        //     if (!health.is_healthy)
        //     {
        //         ESP_LOGW(TAG, "Unhealthy Task: %s - Reason: %s%s", health.name.c_str(),
        //                  health.health_reason.c_str(), health.twdt_risk ? " (TWDT Risk)" : "");

        //         // For IPC tasks, just log a warning
        //         if (health.name == "ipc0" || health.name == "ipc1")
        //         {
        //             ESP_LOGW(TAG,
        //                      "Task %s has critically low stack free (%" PRIu32
        //                      " bytes). Increase CONFIG_ESP_IPC_TASK_STACK_SIZE to 2048.",
        //                      health.name.c_str(), health.stack_free);
        //             continue;  // Skip further actions for system IPC tasks
        //         }

        //         // For stack overflow, don't try to suspend/resume
        //         if (health.stack_overflow)
        //         {
        //             ESP_LOGW(TAG, "Task %s has stack overflow, recommend system restart",
        //                      health.name.c_str());
        //             continue;
        //         }

        //         // For TWDT risks, be more cautious
        //         if (health.twdt_risk && task_status_array[i].uxCurrentPriority > 1)
        //         {
        //             // Only log this instead of changing priority
        //             ESP_LOGW(TAG, "Task %s has TWDT risk at priority %d", health.name.c_str(),
        //                      task_status_array[i].uxCurrentPriority);
        //         }
        //     }
        // }

        // ESP_LOGI(TAG, "Task Status:");
        // ESP_LOGI(
        //     TAG,
        //     "┌────────────────────┬────────────┬──────────┬──────────┬────────────┬────────────┬───"
        //     "─────────┬────────────┬──────────┬────────────┬────────────┬────────────────────┐");
        // ESP_LOGI(TAG,
        //          "│ %-18s │ %-10s │ %-8s │ %-4s │ %-5s │ %-10s │ %-10s │ %-10s │ %-8s│ %-10s"
        //          "│%-10s │ %-18s │",
        //          "Task Name", "H", "S", "PR", "BPIR", " Stack Free ", " Stack Size",
        //          " Stack Use % ", " Core ", " CPU % ", " Runtime ", "TWDT Sub |Health ");
        // ESP_LOGI(TAG,
        //          "├────────────────────┼────────────┼──────────┼──────────┼────────────┼───────────"
        //          "─┼────────────┼────────────┼──────────┼────────────┼────────────┼─────────"
        //          "───────────┤");
        // for (UBaseType_t i = 0; i < tasks_reported; i++)
        // {
        //     const char *state_str;
        //     switch (task_status_array[i].eCurrentState)
        //     {
        //         case eRunning:
        //             state_str = "Running";
        //             break;
        //         case eReady:
        //             state_str = "Ready";
        //             break;
        //         case eBlocked:
        //             state_str = "Blocked";
        //             break;
        //         case eSuspended:
        //             state_str = "Suspend";
        //             break;
        //         case eDeleted:
        //             state_str = "Deleted";
        //             break;
        //         default:
        //             state_str = "Unknown";
        //             break;
        //     }
        //     const char *core_str = (task_health_list[i].core_id == -1)
        //                                ? "Any"
        //                                : (task_health_list[i].core_id == 0 ? "Core0" : "Core1");

        //     std::stringstream row;
        //     row << "│ " << std::left << std::setw(18) << task_status_array[i].pcTaskName << " │ "
        //         << std::setw(10) << std::hex << std::showbase
        //         << reinterpret_cast<uintptr_t>(task_status_array[i].xHandle) << " │ " <<
        //         std::left
        //         << std::setw(8) << state_str << " │ " << std::right << std::setw(4) << std::dec
        //         << task_status_array[i].uxCurrentPriority << " │ " << std::setw(5)
        //         << task_health_list[i].base_priority << " │ " << std::setw(5)
        //         << task_health_list[i].stack_free << " │ " << std::setw(10)
        //         << task_health_list[i].stack_size << " │ " << std::setw(10) << std::fixed
        //         << std::setprecision(1) << task_health_list[i].stack_usage << " │ " <<
        //         std::setw(8)
        //         << core_str << " │ " << std::setw(10) << std::fixed << std::setprecision(1)
        //         << task_health_list[i].cpu_usage << " │ " << std::setw(10)
        //         << task_health_list[i].runtime_counter << " │ " << std::setw(18)
        //         << (task_health_list[i].twdt_subscribed
        //                 ? (task_health_list[i].is_healthy
        //                        ? "Yes | Healthy"
        //                        : ("Yes | " + task_health_list[i].health_reason))
        //                 : "No | N/A")
        //         << " │";

        //     ESP_LOGI(TAG, "%s", row.str().c_str());
        // }

        // ESP_LOGI(TAG,

        //          "└────────────────────┴────────────┴──────────┴──────────┴────────────"
        //          "┴────────────┴───"
        //          "─────────┴────────────┴──────────┴────────────┴────────────┴─────────"
        //          "───────────┘");

        // Output raw task list for debugging
        // vTaskList(task_list_buffer);
        // ESP_LOGI(TAG, "Raw Task List:\n%s", task_list_buffer);
        // Delay before next monitoring cycle
        // last_monitor_time = xTaskGetTickCount();

        UBaseType_t task_count = uxTaskGetNumberOfTasks();
        ESP_LOGI(TAG, "Total tasks: %u", task_count);
        TaskStatus_t *task_status_array = new TaskStatus_t[task_count];
        // uint32_t total_run_time;
        UBaseType_t tasks_reported = uxTaskGetSystemState(task_status_array, task_count, NULL);
        for (UBaseType_t i = 0; i < tasks_reported; i++) {
            TaskStatus_t &task_status = task_status_array[i];
            ESP_LOGI(TAG, "Task: %s, State: %d, Priority: %d, Stack Free: %lu bytes", task_status.pcTaskName, task_status.eCurrentState, task_status.uxCurrentPriority,
                     task_status.usStackHighWaterMark * sizeof(StackType_t));
        }
        // Process tasks and populate metrics
        delete[] task_status_array;
        vTaskDelay(config::MONITOR_INTERVAL_MS / portTICK_PERIOD_MS);
    }

    // Free resources (this code won't be reached in normal operation)
    // vPortFree(task_status_array);
    // vPortFree(task_list_buffer);
    vTaskDelete(nullptr);
}
// // TWDT ISR user handler
extern "C" void esp_task_wdt_isr_user_handler(void)
{
    SystemMonitor::getInstance().handle_twdt_timeout();
}

extern "C" void vApplicationIdleHook(void)
{
    // Minimal implementation
}

extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    // system_monitor::SystemMonitor &monitor = system_monitor::SystemMonitor::getInstance();
    // uint32_t stack_free = uxTaskGetStackHighWaterMark(xTask) * sizeof(StackType_t);
    // TaskStatus_t task_status;
    // uxTaskGetSystemState(&task_status, 1, nullptr);
    // // ESP_LOGE(TAG,
    // //          "Stack overflow detected: Task: %s, Handle: %p, Stack Base: %p, Stack Free: %"
    // //          PRIu32 " bytes", pcTaskName, xTask, task_status.pxStackBase, stack_free);
    // monitor.add_stack_overflow_task(pcTaskName);
    // vTaskSuspend(xTask);
    // monitor.recreate_task(pcTaskName);
}

} // anonymous namespace

bool SystemMonitor::init(uint32_t stack_size, UBaseType_t priority, BaseType_t core_id)
{
    registry_mutex_ = xSemaphoreCreateMutex();
    assert(registry_mutex_ != nullptr);
    twdt_queue = xQueueCreate(config::TWDT_QUEUE_SIZE, CONFIG_FREERTOS_MAX_TASK_NAME_LEN);
    if (!twdt_queue) {
        ESP_LOGE(TAG, "Failed to create TWDT timeout queue");
        return false;
    }
    BaseType_t result = xTaskCreatePinnedToCore(monitor_task, "Monitor", stack_size, nullptr, (configMAX_PRIORITIES - 5), &monitor_handle_, core_id);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create monitor task");
        return false;
    }
    // configMAX_PRIORITIES;
    // BaseType_t result2 = xTaskCreatePinnedToCore(watchdog_task, "Watchdog", stack_size, nullptr,
    //                                              priority, &watchdog_handle_, core_id);
    // if (result2 != pdPASS)
    // {
    //     ESP_LOGE(TAG, "Failed to create watchdog task");
    // }

    // register_task("Monitor", stack_size, configMAX_PRIORITIES - 5, core_id, monitor_task,
    //               monitor_handle_, true);
    // register_task("Watchdog", stack_size, priority, core_id, watchdog_task, watchdog_handle_,
    // true);
    return true;
}

void SystemMonitor::register_task(std::string_view name, uint32_t stack_size, UBaseType_t priority, BaseType_t core_id, TaskFunction_t task_function, TaskHandle_t handle, bool subscribe_to_twdt)
{
    task_registry_[std::string(name)] = {std::string(name), stack_size, priority, core_id, task_function, handle, xTaskGetTickCount()};
    if (subscribe_to_twdt && handle) {
        esp_err_t ret = esp_task_wdt_add(handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Task %s subscribed to TWDT", name.data());
        } else {
            ESP_LOGE(TAG, "Failed to subscribe task %s to TWDT: %d", name.data(), ret);
        }
    }
    ESP_LOGI(TAG, "Registered task: %s, Stack: %" PRIu32 ", Priority:%d, Core: %d, TWDT: %s", name.data(), stack_size, priority, core_id, subscribe_to_twdt ? "Subscribed" : "Not Subscribed");
}

void SystemMonitor::recreate_task(std::string_view task_name)
{
    std::string name(task_name);
    if (std::find(system_tasks_.begin(), system_tasks_.end(), name) != system_tasks_.end()) {
        ESP_LOGW(TAG, "Cannot recreate system task: %s", name.c_str());
        return;
    }

    auto it = task_registry_.find(name);
    if (it == task_registry_.end()) {
        ESP_LOGE(TAG, "Task %s not found in registry", name.c_str());
        return;
    }

    TaskMetadata &metadata = it->second;
    uint32_t &recreation_count = task_recreation_counts_[name];

    if (metadata.handle) {
        ESP_LOGE(TAG, "Deleting task %s and recreating with larger stack", name.c_str());
        esp_task_wdt_delete(metadata.handle);
        vTaskDelete(metadata.handle);
        metadata.handle = nullptr;

        stack_overflow_tasks_.erase(std::remove(stack_overflow_tasks_.begin(), stack_overflow_tasks_.end(), name), stack_overflow_tasks_.end());
        uint32_t new_stack_size;
        if (recreation_count < config::MAX_RECREATIONS) {
            new_stack_size = metadata.stack_size * 3 / 2;
        } else {
            new_stack_size = metadata.stack_size;
        }
        xTaskCreatePinnedToCore(metadata.task_function, name.c_str(), new_stack_size, nullptr, metadata.priority, &metadata.handle, metadata.core_id);

        recreation_count++;
        ESP_LOGI(TAG, "Recreated task %s with %ld bytes stack (Attempt %" PRIu32 "/%" PRIu32 ")", name.c_str(), new_stack_size, recreation_count, config::MAX_RECREATIONS);

        // Re-subscribe to TWDT if previously subscribed
        if (esp_task_wdt_status(metadata.handle) != ESP_OK) {
            esp_err_t ret = esp_task_wdt_add(metadata.handle);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Recreated task %s re-subscribed to TWDT", name.c_str());
            } else {
                ESP_LOGE(TAG, "Failed to re-subscribe task %s to TWDT: %d", name.c_str(), ret);
            }
        }

        metadata.stack_size = new_stack_size;
        metadata.last_twdt_reset = xTaskGetTickCount();
    } else if (recreation_count >= config::MAX_RECREATIONS) {
        ESP_LOGE(TAG, "Max recreations reached for task %s. Suspending...", name.c_str());
        vTaskSuspend(metadata.handle);
    }
}

void SystemMonitor::handle_twdt_timeout()
{
    // Flag tasks that triggered TWDT timeout
    for (const auto &[name, metadata] : task_registry_) {
        if (std::find(system_tasks_.begin(), system_tasks_.end(), name) != system_tasks_.end()) {
            continue; // Skip system tasks
        }

        if (esp_task_wdt_status(metadata.handle) == ESP_OK) {
            TickType_t elapsed = (xTaskGetTickCount() - metadata.last_twdt_reset) * portTICK_PERIOD_MS;
            if (elapsed >= 20 * 1000) {
                char task_name[CONFIG_FREERTOS_MAX_TASK_NAME_LEN];
                strncpy(task_name, name.c_str(), CONFIG_FREERTOS_MAX_TASK_NAME_LEN - 1);
                task_name[CONFIG_FREERTOS_MAX_TASK_NAME_LEN - 1] = '\0';
                if (!twdt_queue && xQueueSendFromISR(twdt_queue, task_name, nullptr) != pdTRUE) {
                    // Cannot log in ISR; queue full, handled by monitor_task
                }
            }
        }
    }
}

void SystemMonitor::update_twdt_reset_time(TaskHandle_t handle)
{
    if (registry_mutex_ && xSemaphoreTake(registry_mutex_, pdMS_TO_TICKS(500)) == pdTRUE) {
        const char *current_task_name = nullptr;
        if (handle == nullptr) {
            current_task_name = pcTaskGetName(nullptr);
            if (current_task_name == nullptr) {
                xSemaphoreGive(registry_mutex_);
                return;
            }
        }

        for (auto &[name, metadata] : task_registry_) {
            if (metadata.handle == handle || (handle == nullptr && name == current_task_name)) {
                metadata.last_twdt_reset = xTaskGetTickCount();
                break;
            }
        }

        xSemaphoreGive(registry_mutex_);
    }
}

} // namespace system_monitor
