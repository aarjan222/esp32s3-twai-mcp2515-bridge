#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace system_monitor {

/**
 * @brief Configuration constants for system monitoring.
 */
namespace config {
constexpr uint32_t MONITOR_INTERVAL_MS = 10000;       ///< Monitor interval in ms
constexpr uint32_t MIN_STACK_THRESHOLD = 256;         ///< Minimum stack free bytes
constexpr float CPU_USAGE_THRESHOLD = 40.0f;          ///< Max CPU usage percentage
constexpr uint32_t BLOCKED_TIMEOUT_MS = 30000;        ///< Max blocked time in ms
constexpr uint32_t YIELD_TIMEOUT_MS = 5000;           ///< Max time without yielding in ms
constexpr float IDLE_STARVATION_THRESHOLD = 1.0f;     ///< Min CPU % for idle tasks
constexpr uint32_t MIN_STACK_SIZE = 800;              ///< Minimum recommended stack size
constexpr uint32_t MAX_RECREATIONS = 3;               ///< Max task recreations
constexpr uint32_t TWDT_WARNING_THRESHOLD_MS = 8000;  ///< TWDT warning threshold (80% of 10s)
constexpr uint32_t TWDT_QUEUE_SIZE = 10;              ///< Size of TWDT timeout queue
constexpr float STACK_INCREASEMENT_THRESHOLD = 10.0f; ///< Stack usage increment threshold
} // namespace config

/**
 * @brief Structure to hold task health metrics.
 */
struct TaskHealth {
    std::string name;              ///< Task name
    bool is_healthy;               ///< Health status
    std::string health_reason;     ///< Reason for unhealthy status
    uint32_t stack_free;           ///< Free stack bytes
    uint32_t stack_size;           ///< Base stack size
    float stack_usage;             ///< Stack usage percentage
    float stack_incresement = 0;   ///< Stack usage increment percentage
    uint32_t stack_previous;       /// < Previous free stack bytes
    float cpu_usage;               ///< CPU usage percentage
    uint32_t blocked_time_ms;      ///< Time blocked in ms
    uint32_t last_yield_ms;        ///< Time since last yield in ms
    bool twdt_risk;                ///< TWDT timeout risk
    int core_id;                   ///< Pinned core ID (-1 for any)
    UBaseType_t base_priority;     ///< Base priority
    UBaseType_t task_number;       ///< Task number
    uint32_t runtime_counter;      ///< Runtime counter
    void *stack_base;              ///< Stack base address
    bool twdt_subscribed;          ///< TWDT subscription status
    bool stack_overflow;           ///< Stack overflow detected
    uint32_t estimated_stack_size; ///< Estimated stack size
};

/**
 * @brief Structure to hold task metadata for recreation.
 */
struct TaskMetadata {
    std::string name;                 ///< Task name
    uint32_t stack_size;              ///< Stack size in bytes
    UBaseType_t priority;             ///< Task priority
    BaseType_t core_id;               ///< Pinned core ID
    TaskFunction_t task_function;     ///< Task function
    TaskHandle_t handle;              ///< Task handle
    TickType_t last_twdt_reset;       ///< Last TWDT reset time
    float stack_Increament = 0;       /// < Stack usage increment percentage
    uint32_t previous_free_stack = 0; /// < Previous free stack bytes
};

/**
 * @brief Enum for TWDT timeout action types.
 */
enum class TwdtAction {
    LOG_ONLY, ///< Log the issue only
    SUSPEND,  ///< Suspend the task
    RECREATE  ///< Recreate the task
};

/**
 * @brief Singleton system monitor class for task health monitoring and dynamic recreation.
 */
class SystemMonitor {
   public:
    /**
     * @brief Get the singleton instance of SystemMonitor.
     * @return Reference to the singleton instance.
     */
    static SystemMonitor &getInstance()
    {
        static SystemMonitor instance;
        return instance;
    }

    /**
     * @brief Initialize the system monitor and start the monitor task.
     * @param stack_size Stack size for the monitor task.
     * @param priority Priority for the monitor task.
     * @param core_id Core to pin the monitor task to (-1 for any).
     * @return true if initialization succeeds, false otherwise.
     */
    bool init(uint32_t stack_size, UBaseType_t priority, BaseType_t core_id);

    /**
     * @brief Register a task for monitoring and recreation.
     * @param name Task name.
     * @param stack_size Stack size in bytes.
     * @param priority Task priority.
     * @param core_id Pinned core ID (-1 for any).
     * @param task_function Task function.
     * @param handle Task handle.
     * @param subscribe_to_twdt Whether to subscribe the task to TWDT.
     */
    void register_task(std::string_view name, uint32_t stack_size, UBaseType_t priority, BaseType_t core_id, TaskFunction_t task_function, TaskHandle_t handle, bool subscribe_to_twdt = true);

    /**
     * @brief Recreate a task after stack overflow or failure.
     * @param task_name Name of the task to recreate.
     */
    void recreate_task(std::string_view task_name);

    /**
     * @brief Get the list of tasks with stack overflows.
     * @return Reference to the stack overflow task list.
     */
    const std::vector<std::string> &get_stack_overflow_tasks() const
    {
        return stack_overflow_tasks_;
    }

    /**
     * @brief Add a task to the stack overflow list.
     * @param task_name Name of the task to add.
     */
    void add_stack_overflow_task(std::string_view task_name)
    {
        stack_overflow_tasks_.push_back(std::string(task_name));
    }

    /**
     * @brief Handle TWDT timeout events.
     */
    void handle_twdt_timeout();

    /**
     * @brief Update the last TWDT reset time for a task.
     * @param handle Task handle.
     */
    void update_twdt_reset_time(TaskHandle_t handle);

    /**
     * @brief Set the action to take for tasks that fail to reset TWDT.
     * @param action The action to take (LOG_ONLY, SUSPEND, RECREATE).
     */
    void set_twdt_action(TwdtAction action)
    {
        twdt_action_ = action;
    }
    static inline const std::vector<std::string> system_tasks_ = {"ipc0", "ipc1", "IDLE0", "IDLE1", "Tmr Svc", "Monitor", "Watchdog", "main"};
    std::vector<std::string> twdt_timeout_tasks_;       ///< Tasks with TWDT timeouts
    TwdtAction twdt_action_ = TwdtAction::LOG_ONLY;     ///< Action for TWDT timeout
    QueueHandle_t twdt_queue = nullptr;                 // TWDT queue handle
    std::map<std::string, TaskMetadata> task_registry_; ///< Task metadata registry
    SemaphoreHandle_t registry_mutex_ = nullptr;

   private:
    SystemMonitor() = default; // Private constructor for singleton
    ~SystemMonitor() = default;

    // Delete copy and move to enforce singleton
    SystemMonitor(const SystemMonitor &) = delete;
    SystemMonitor &operator=(const SystemMonitor &) = delete;
    SystemMonitor(SystemMonitor &&) = delete;
    SystemMonitor &operator=(SystemMonitor &&) = delete;

    // List of system tasks to exclude from TWDT subscription checks

    std::map<std::string, uint32_t> task_recreation_counts_; ///< Recreation counts per task
    std::vector<std::string> stack_overflow_tasks_;          ///< Tasks with stack overflows

    TaskHandle_t monitor_handle_ = nullptr;  ///< Monitor task handle
    TaskHandle_t watchdog_handle_ = nullptr; ///< Watchdog task handle
};

} // namespace system_monitor
