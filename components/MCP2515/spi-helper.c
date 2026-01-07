#include "driver/spi_master.h"
#include "spi-helper.h"
#include "driver/gpio.h"

spi_device_interface_config_t devCfg;
spi_device_handle_t spi;
esp_err_t ret;

uint32_t ccs_can_id;
char msgString[128];
uint32_t rxId;
uint8_t len;
uint8_t rxBuf[8];
uint8_t spiToggle;

inline void add_device_one()
{
    ret = spi_bus_add_device(SPI2_HOST, &devCfg, &spi);
    ESP_ERROR_CHECK(ret);
}

inline void remove_device()
{
    ret = spi_bus_remove_device(spi);
    ESP_ERROR_CHECK(ret);
}

spi_device_handle_t spi_init(void)
{
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (4 * 8)};

    //========for CCS Device SPI=========//
    devCfg.mode = 0;
    devCfg.clock_speed_hz = 40*1000*1000;
    devCfg.spics_io_num = PIN_NUM_CS;
    devCfg.queue_size = 5;

    // Initialize the HSPI bus
    ret = spi_bus_initialize(SPI2_HOST, &buscfg, 0);
    ESP_ERROR_CHECK(ret);
    if (ret == ESP_OK)
    {
        printf("\nSuccessfully Initialized HSPI!\n");
    }
    else
    {
        printf("\nInitialization Failed!\n");
    }
    return spi;
}

uint32_t covert_can_id(uint32_t can_id, uint8_t can_type)
{
    uint32_t temp_id = can_id;
    switch (can_type)
    {
    case CCS:
        temp_id = temp_id >> 4;
        temp_id = (temp_id << 4) | (0xA);
        break;
    }
    return temp_id;
}