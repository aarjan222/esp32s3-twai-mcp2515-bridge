#include "can.hpp"

#include "esp_log.h"

namespace CAN
{

    static const char *TAG = "CANDriver";

    Driver::Driver()
    {
    }

    esp_err_t Driver::init(uint16_t baud_rate)
    {
        generalConfig = TWAI_GENERAL_CONFIG_DEFAULT(static_cast<gpio_num_t>(CONFIG_CAN_TX_GPIO_NUM), static_cast<gpio_num_t>(CONFIG_CAN_RX_GPIO_NUM), TWAI_MODE_NORMAL);
        generalConfig.mode = TWAI_MODE_NORMAL;
        generalConfig.tx_io = static_cast<gpio_num_t>(CONFIG_CAN_TX_GPIO_NUM);
        generalConfig.rx_io = static_cast<gpio_num_t>(CONFIG_CAN_RX_GPIO_NUM);
        generalConfig.clkout_io = TWAI_IO_UNUSED;
        generalConfig.bus_off_io = TWAI_IO_UNUSED;
        generalConfig.tx_queue_len = CONFIG_CAN_TX_QUEUE_SIZE;
        generalConfig.rx_queue_len = CONFIG_CAN_RX_QUEUE_SIZE;
        generalConfig.alerts_enabled = TWAI_ALERT_ALL;
        generalConfig.clkout_divider = 0;

        // Select timing configuration based on CONFIG_CAN_BAUD_RATE
        switch (baud_rate)
        {
        case 1000:
            timingConfig = TWAI_TIMING_CONFIG_1MBITS();
            ESP_LOGI(TAG, "CAN baud rate set to 1 Mbps");
            break;
        case 800:
            timingConfig = TWAI_TIMING_CONFIG_800KBITS();
            ESP_LOGI(TAG, "CAN baud rate set to 800 kbps");
            break;
        case 500:
            timingConfig = TWAI_TIMING_CONFIG_500KBITS();
            ESP_LOGI(TAG, "CAN baud rate set to 500 kbps");
            break;
        case 250:
            timingConfig = TWAI_TIMING_CONFIG_250KBITS();
            ESP_LOGI(TAG, "CAN baud rate set to 250 kbps");
            break;
        case 125:
            timingConfig = TWAI_TIMING_CONFIG_125KBITS();
            ESP_LOGI(TAG, "CAN baud rate set to 125 kbps");
            break;
        case 100:
            timingConfig = TWAI_TIMING_CONFIG_100KBITS();
            ESP_LOGI(TAG, "CAN baud rate set to 100 kbps");
            break;
        case 50:
            timingConfig = TWAI_TIMING_CONFIG_50KBITS();
            ESP_LOGI(TAG, "CAN baud rate set to 50 kbps");
            break;
        case 25:
            timingConfig = TWAI_TIMING_CONFIG_25KBITS();
            ESP_LOGI(TAG, "CAN baud rate set to 25 kbps");
            break;
        case 20:
            timingConfig = TWAI_TIMING_CONFIG_20KBITS();
            ESP_LOGI(TAG, "CAN baud rate set to 20 kbps");
            break;
        case 16:
            timingConfig = TWAI_TIMING_CONFIG_16KBITS();
            ESP_LOGI(TAG, "CAN baud rate set to 16 kbps");
            break;
        case 12:
            timingConfig = TWAI_TIMING_CONFIG_12_5KBITS();
            ESP_LOGI(TAG, "CAN baud rate set to 12.5 kbps");
            break;
        case 10:
            timingConfig = TWAI_TIMING_CONFIG_10KBITS();
            ESP_LOGI(TAG, "CAN baud rate set to 10 kbps");
            break;
        default:
            ESP_LOGE(TAG, "Unsupported CAN baud rate: %d kbps", CONFIG_CAN_BAUD_RATE);
            ESP_LOGE(TAG, "Supported baud rates: 10, 12.5, 16, 20, 25, 50, 100, 125, 250, 500, 800, 1000 kbps");
            ESP_LOGE(TAG, "Set menuconfig -> CAN Module Configuration -> CAN baud Rate to a supported value");
            timingConfig = TWAI_TIMING_CONFIG_250KBITS(); // Fallback to 250 kbps
            break;
        }

        filterConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();
#if CONFIG_CAN_FILTER_ENABLE == 1
        filterConfig.acceptance_code = CONFIG_FILTER_ID & 0x1FFFFFFF, filterConfig.acceptance_mask = CONFIG_MASK_ID & 0x1FFFFFFF, filterConfig.single_filter = CONFIG_SINGLE_FILTER;
#endif

        esp_err_t ret = twai_driver_install(&generalConfig, &timingConfig, &filterConfig);
        if (ret == ESP_OK)
        {
            // ESP_LOGI(TAG, "CAN driver initialized successfully");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize CAN driver: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    esp_err_t Driver::start()
    {
        esp_err_t ret = twai_start();
        if (ret == ESP_OK)
        {
            // ESP_LOGI(TAG, "CAN driver started successfully");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to start CAN driver: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    esp_err_t Driver::enableAlerts(uint32_t alerts)
    {
        esp_err_t ret = twai_reconfigure_alerts(alerts, NULL);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "CAN alerts reconfigured successfully");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to reconfigure CAN alerts: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    esp_err_t Driver::stop()
    {
        esp_err_t ret = twai_stop();
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "CAN driver stopped successfully");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to stop CAN driver: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    esp_err_t Driver::deinit()
    {
        esp_err_t ret = twai_driver_uninstall();
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "CAN driver uninstalled successfully");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to uninstall CAN driver: %s", esp_err_to_name(ret));
        }
        return ret;
    }

} // namespace CAN