#ifndef SPI_HELPER_H
#define SPI_HELPER_H

#define PIN_NUM_MISO 13
#define PIN_NUM_MOSI 11
#define PIN_NUM_CLK  12
#define PIN_NUM_CS   10  

// for converting CAN type
#define CCS 1

extern uint32_t ccs_can_id;
extern char msgString[128];

// for CCS
extern uint32_t rxId;
extern uint8_t len;
extern uint8_t rxBuf[8];

extern uint8_t spiToggle;

extern spi_device_handle_t spi;
extern esp_err_t ret;

spi_device_handle_t spi_init(void);
void add_device_one();
void remove_device();

uint32_t covert_can_id(uint32_t can_id, uint8_t can_type);

#endif