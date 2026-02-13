#ifndef CAN_HPP
#define CAN_HPP

#include <iostream>

#include "driver/twai.h"
#include "esp_err.h"

namespace CAN
{

#define CONFIG_CAN_TX_GPIO_NUM 47
#define CONFIG_CAN_RX_GPIO_NUM 21
#define CONFIG_CAN_BAUD_RATE 250
#define CONFIG_CAN_TX_QUEUE_SIZE 30
#define CONFIG_CAN_RX_QUEUE_SIZE 30

    class Driver
    {
    public:
        // Constructor
        Driver();

        // Initialize the CAN driver
        esp_err_t init(uint16_t baud_rate = 250);

        // Start the CAN driver
        esp_err_t start();

        // Enable specific alerts
        esp_err_t enableAlerts(uint32_t alerts);

        // Stop the CAN driver
        esp_err_t stop();

        // Uninstall the CAN driver
        esp_err_t deinit();

    private:
        twai_general_config_t generalConfig;
        twai_timing_config_t timingConfig;
        twai_filter_config_t filterConfig;
    };

} // namespace CAN

#endif // CAN_HPP
