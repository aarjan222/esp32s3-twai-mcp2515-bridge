/* Created by Abhay Dutta, 27th Feb, 2023 */

/* ESP32 library for MCP2515 CAN interface via SPI */

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_log.h"
#include "MCP2515.h"
#include "mcp_can_dfs.h"
#include "spi-helper.h"
#include <stdio.h>

uint8_t m_nExtFlg;
uint32_t m_nID;
uint8_t m_nDlc;
uint8_t m_nDta[MAX_CHAR_IN_MESSAGE];
uint8_t m_nRtr;
uint8_t m_nfilhit;

uint8_t mcpMode;

uint8_t setMode(const uint8_t opMode)
{
    mcpMode = opMode;
    return mcp2515_setCANCTRL_Mode(mcpMode);
}

void mcp2515_reset(void)
{
    esp_err_t ret;
    spi_transaction_t tM = {
        .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA,
        .length = 8,
        .tx_data[0] = MCP_RESET};
    ret = spi_device_transmit(spi, &tM);
    if (ret != ESP_OK)
    {
        printf("SPI_Device_Transmit Failed!\n");
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
}

uint8_t mcp2515_readRegister(const uint8_t address)
{
    esp_err_t ret;
    spi_transaction_t tM = {
        .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA,
        .length = 24,
        .tx_data[0] = MCP_READ,
        .tx_data[1] = address,
        .tx_data[2] = 0x00};
    ret = spi_device_transmit(spi, &tM);
    if (ret != ESP_OK)
    {
        printf("SPI_Device_Transmit Failed!\n");
    }
    return tM.rx_data[2];
}

void mcp2515_readRegisterS(const uint8_t address, uint8_t values[], const uint8_t n)
{
    esp_err_t ret;
    uint8_t rx_data[n + 2];
    uint8_t tx_data[n + 2];

    tx_data[0] = MCP_READ;
    tx_data[1] = address;

    spi_transaction_t tM = {
        .length = ((2 + ((size_t)n)) * 8),
        .rx_buffer = rx_data,
        .tx_buffer = tx_data};

    ret = spi_device_transmit(spi, &tM);
    if (ret != ESP_OK)
    {
        printf("SPI_Device_Transmit Failed!\n");
    }

    for (uint8_t i = 0; i < n; i++)
    {
        values[i] = rx_data[i + 2];
    }
}

void mcp2515_modifyRegister(const uint8_t address, const uint8_t mask, const uint8_t data)
{
    esp_err_t ret;
    spi_transaction_t tM = {
        .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA,
        .length = 32,
        .tx_data[0] = MCP_BITMOD,
        .tx_data[1] = address,
        .tx_data[2] = mask,
        .tx_data[3] = data};
    ret = spi_device_transmit(spi, &tM);
    if (ret != ESP_OK)
    {
        printf("SPI_Device_Transmit Failed!\n");
    }
}

void mcp2515_setRegister(const uint8_t address, const uint8_t value)
{
    esp_err_t ret;
    spi_transaction_t tM = {
        .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA,
        .length = 24,
        .tx_data[0] = MCP_WRITE,
        .tx_data[1] = address,
        .tx_data[2] = value};
    ret = spi_device_transmit(spi, &tM);
    if (ret != ESP_OK)
    {
        printf("SPI_Device_Transmit Failed!\n");
    }
}

void mcp2515_setRegisterS(const uint8_t address, uint8_t values[], const uint8_t n)
{
    esp_err_t ret;
    uint8_t data[n + 2];
    data[0] = MCP_WRITE;
    data[1] = address;

    for (uint8_t i = 0; i < n; i++)
    {
        data[i + 2] = values[i];
    }

    spi_transaction_t tM = {
        .length = ((2 + ((size_t)n)) * 8),
        .tx_buffer = data};
    ret = spi_device_transmit(spi, &tM);
    if (ret != ESP_OK)
    {
        printf("SPI_Device_Transmit Failed!\n");
    }
}

uint8_t mcp2515_readStatus(void)
{
    esp_err_t ret;
    spi_transaction_t tM = {
        .flags = SPI_TRANS_USE_RXDATA | SPI_TRANS_USE_TXDATA,
        .length = 16,
        .tx_data[0] = MCP_READ_STATUS,
        .tx_data[1] = 0x00};
    ret = spi_device_transmit(spi, &tM);
    if (ret != ESP_OK)
    {
        printf("SPI_Device_Transmit Failed!\n");
    }
    // printf("spi data transmitted\n");
    return tM.rx_data[1];
}

void mcp2515_read_id(const uint8_t mcp_addr, uint8_t *ext, uint32_t *id)
{
    uint8_t tbufdata[5];
    *ext = 0;
    *id = 0;

    mcp2515_readRegisterS(mcp_addr, tbufdata, 5);
    *id = (tbufdata[MCP_SIDH] << 3) + (tbufdata[MCP_SIDL] >> 5);
    if ((tbufdata[MCP_SIDL] & MCP_TXB_EXIDE_M) == MCP_TXB_EXIDE_M)
    {
        /* extended id */
        *id = (*id << 2) + (tbufdata[MCP_SIDL] & 0x03);
        *id = (*id << 8) + tbufdata[MCP_EID8];
        *id = (*id << 8) + tbufdata[MCP_EID0];
        *ext = 1;
    }
}

uint8_t mcp2515_requestNewMode(const uint8_t newmode)
{
    TickType_t ticks_start = xTaskGetTickCount();

    // Spam new mode request and wait for the operation  to complete
    while (1)
    {
        // Request new mode
        // This is inside the loop as sometimes requesting the new mode once doesn't work (usually when attempting to sleep)
        mcp2515_modifyRegister(MCP_CANCTRL, MODE_MASK, newmode);
        uint8_t statReg = mcp2515_readRegister(MCP_CANSTAT);
        if ((statReg & MODE_MASK) == newmode) // We're now in the new mode      FF & 0xE0 should be equal to 128 (0x80)
        {
            return MCP2515_OK;
        }
        else if ((xTaskGetTickCount() - ticks_start) > 200) // Wait no more than 200ms for the operation to complete
        {
            return MCP2515_FAIL;
        }
    }
}

void mcp2515_write_mf(const uint8_t mcp_addr, const uint8_t ext, const uint32_t id)
{
    uint16_t canid;
    uint8_t tbufdata[4];
    canid = (uint16_t)(id & 0x0FFFF);

    if (ext == 1)
    {
        tbufdata[MCP_EID0] = (uint8_t)(canid & 0xFF);
        tbufdata[MCP_EID8] = (uint8_t)(canid >> 8);
        canid = (uint16_t)(id >> 16);
        tbufdata[MCP_SIDL] = (uint8_t)(canid & 0x03);
        tbufdata[MCP_SIDL] += (uint8_t)((canid & 0x1C) << 3);
        tbufdata[MCP_SIDL] |= MCP_TXB_EXIDE_M;
        tbufdata[MCP_SIDH] = (uint8_t)(canid >> 5);
    }
    else
    {
        tbufdata[MCP_EID0] = (uint8_t)(canid & 0xFF);
        tbufdata[MCP_EID8] = (uint8_t)(canid >> 8);
        canid = (uint16_t)(id >> 16);
        tbufdata[MCP_SIDL] = (uint8_t)((canid & 0x07) << 5);
        tbufdata[MCP_SIDH] = (uint8_t)(canid >> 3);
    }
    mcp2515_setRegisterS(mcp_addr, tbufdata, 4);
}

void mcp2515_initCANBuffers(void)
{
    uint8_t i, a1, a2, a3;

    uint8_t std = 0;
    uint8_t ext = 1;
    uint32_t ulMask = 0x00, ulFilt = 0x00;

    mcp2515_write_mf(MCP_RXM0SIDH, ext, ulMask); /*Set both masks to 0           */
    mcp2515_write_mf(MCP_RXM1SIDH, ext, ulMask); /*Mask register ignores ext bit */

    /* Set all filters to 0         */
    mcp2515_write_mf(MCP_RXF0SIDH, ext, ulFilt); /* RXB0: extended               */
    mcp2515_write_mf(MCP_RXF1SIDH, std, ulFilt); /* RXB1: standard               */
    mcp2515_write_mf(MCP_RXF2SIDH, ext, ulFilt); /* RXB2: extended               */
    mcp2515_write_mf(MCP_RXF3SIDH, std, ulFilt); /* RXB3: standard               */
    mcp2515_write_mf(MCP_RXF4SIDH, ext, ulFilt);
    mcp2515_write_mf(MCP_RXF5SIDH, std, ulFilt);

    /* Clear, deactivate the three  */
    /* transmit buffers             */
    /* TXBnCTRL -> TXBnD7           */
    a1 = MCP_TXB0CTRL;
    a2 = MCP_TXB1CTRL;
    a3 = MCP_TXB2CTRL;
    for (i = 0; i < 14; i++)
    { /* in-buffer loop               */
        mcp2515_setRegister(a1, 0);
        mcp2515_setRegister(a2, 0);
        mcp2515_setRegister(a3, 0);
        a1++;
        a2++;
        a3++;
    }
    mcp2515_setRegister(MCP_RXB0CTRL, 0);
    mcp2515_setRegister(MCP_RXB1CTRL, 0);
}

uint8_t mcp2515_setCANCTRL_Mode(const uint8_t newmode)
{
    // If the chip is asleep and we want to change mode then a manual wake needs to be done
    // This is done by setting the wake up interrupt flag
    // This undocumented trick was found at https://github.com/mkleemann/can/blob/master/can_sleep_mcp2515.c
    if ((mcp2515_readRegister(MCP_CANSTAT) & MODE_MASK) == MCP_SLEEP && newmode != MCP_SLEEP)
    {
        // Make sure wake interrupt is enabled
        uint8_t wakeIntEnabled = (mcp2515_readRegister(MCP_CANINTE) & MCP_WAKIF);
        if (!wakeIntEnabled)
        {
            mcp2515_modifyRegister(MCP_CANINTE, MCP_WAKIF, MCP_WAKIF);
        }
        // Set wake flag (this does the actual waking up)
        mcp2515_modifyRegister(MCP_CANINTF, MCP_WAKIF, MCP_WAKIF);
        // Wait for the chip to exit SLEEP and enter LISTENONLY mode.
        // If the chip is not connected to a CAN bus (or the bus has no other powered nodes) it will sometimes trigger the wake interrupt as soon
        // as it's put to sleep, but it will stay in SLEEP mode instead of automatically switching to LISTENONLY mode.
        // In this situation the mode needs to be manually set to LISTENONLY.
        if (mcp2515_requestNewMode(MCP_LISTENONLY) != MCP2515_OK)
        {
            return MCP2515_FAIL;
        }

        // Turn wake interrupt back off if it was originally off
        if (!wakeIntEnabled)
        {
            mcp2515_modifyRegister(MCP_CANINTE, MCP_WAKIF, 0);
        }
    }
    // Clear wake flag
    mcp2515_modifyRegister(MCP_CANINTF, MCP_WAKIF, 0);

    uint8_t ret = mcp2515_requestNewMode(newmode);
    return ret;
}

uint8_t mcp2515_configRate(const uint8_t canSpeed, const uint8_t canClock)
{
    uint8_t set, cfg1, cfg2, cfg3;
    set = 1;
    switch (canClock & MCP_CLOCK_SELECT)
    {
    case (MCP_8MHZ):
        switch (canSpeed)
        {
        case (CAN_5KBPS): //   5KBPS
            cfg1 = MCP_8MHz_5kBPS_CFG1;
            cfg2 = MCP_8MHz_5kBPS_CFG2;
            cfg3 = MCP_8MHz_5kBPS_CFG3;
            break;

        case (CAN_10KBPS): //  10KBPS
            cfg1 = MCP_8MHz_10kBPS_CFG1;
            cfg2 = MCP_8MHz_10kBPS_CFG2;
            cfg3 = MCP_8MHz_10kBPS_CFG3;
            break;

        case (CAN_20KBPS): //  20KBPS
            cfg1 = MCP_8MHz_20kBPS_CFG1;
            cfg2 = MCP_8MHz_20kBPS_CFG2;
            cfg3 = MCP_8MHz_20kBPS_CFG3;
            break;

        case (CAN_31K25BPS): //  31.25KBPS
            cfg1 = MCP_8MHz_31k25BPS_CFG1;
            cfg2 = MCP_8MHz_31k25BPS_CFG2;
            cfg3 = MCP_8MHz_31k25BPS_CFG3;
            break;

        case (CAN_33K3BPS): //  33.33KBPS
            cfg1 = MCP_8MHz_33k3BPS_CFG1;
            cfg2 = MCP_8MHz_33k3BPS_CFG2;
            cfg3 = MCP_8MHz_33k3BPS_CFG3;
            break;

        case (CAN_40KBPS): //  40Kbps
            cfg1 = MCP_8MHz_40kBPS_CFG1;
            cfg2 = MCP_8MHz_40kBPS_CFG2;
            cfg3 = MCP_8MHz_40kBPS_CFG3;
            break;

        case (CAN_50KBPS): //  50Kbps
            cfg1 = MCP_8MHz_50kBPS_CFG1;
            cfg2 = MCP_8MHz_50kBPS_CFG2;
            cfg3 = MCP_8MHz_50kBPS_CFG3;
            break;

        case (CAN_80KBPS): //  80Kbps
            cfg1 = MCP_8MHz_80kBPS_CFG1;
            cfg2 = MCP_8MHz_80kBPS_CFG2;
            cfg3 = MCP_8MHz_80kBPS_CFG3;
            break;

        case (CAN_100KBPS): // 100Kbps
            cfg1 = MCP_8MHz_100kBPS_CFG1;
            cfg2 = MCP_8MHz_100kBPS_CFG2;
            cfg3 = MCP_8MHz_100kBPS_CFG3;
            break;

        case (CAN_125KBPS): // 125Kbps
            cfg1 = MCP_8MHz_125kBPS_CFG1;
            cfg2 = MCP_8MHz_125kBPS_CFG2;
            cfg3 = MCP_8MHz_125kBPS_CFG3;
            break;

        case (CAN_200KBPS): // 200Kbps
            cfg1 = MCP_8MHz_200kBPS_CFG1;
            cfg2 = MCP_8MHz_200kBPS_CFG2;
            cfg3 = MCP_8MHz_200kBPS_CFG3;
            break;

        case (CAN_250KBPS): // 250Kbps
            cfg1 = MCP_8MHz_250kBPS_CFG1;
            cfg2 = MCP_8MHz_250kBPS_CFG2;
            cfg3 = MCP_8MHz_250kBPS_CFG3;
            break;

        case (CAN_500KBPS): // 500Kbps
            cfg1 = MCP_8MHz_500kBPS_CFG1;
            cfg2 = MCP_8MHz_500kBPS_CFG2;
            cfg3 = MCP_8MHz_500kBPS_CFG3;
            break;

        case (CAN_1000KBPS): //   1Mbps
            cfg1 = MCP_8MHz_1000kBPS_CFG1;
            cfg2 = MCP_8MHz_1000kBPS_CFG2;
            cfg3 = MCP_8MHz_1000kBPS_CFG3;
            break;

        default:
            set = 0;
            return MCP2515_FAIL;
            break;
        }
        break;

    case (MCP_16MHZ):
        switch (canSpeed)
        {
        case (CAN_5KBPS): //   5Kbps
            cfg1 = MCP_16MHz_5kBPS_CFG1;
            cfg2 = MCP_16MHz_5kBPS_CFG2;
            cfg3 = MCP_16MHz_5kBPS_CFG3;
            break;

        case (CAN_10KBPS): //  10Kbps
            cfg1 = MCP_16MHz_10kBPS_CFG1;
            cfg2 = MCP_16MHz_10kBPS_CFG2;
            cfg3 = MCP_16MHz_10kBPS_CFG3;
            break;

        case (CAN_20KBPS): //  20Kbps
            cfg1 = MCP_16MHz_20kBPS_CFG1;
            cfg2 = MCP_16MHz_20kBPS_CFG2;
            cfg3 = MCP_16MHz_20kBPS_CFG3;
            break;

        case (CAN_33K3BPS): //  20Kbps
            cfg1 = MCP_16MHz_33k3BPS_CFG1;
            cfg2 = MCP_16MHz_33k3BPS_CFG2;
            cfg3 = MCP_16MHz_33k3BPS_CFG3;
            break;

        case (CAN_40KBPS): //  40Kbps
            cfg1 = MCP_16MHz_40kBPS_CFG1;
            cfg2 = MCP_16MHz_40kBPS_CFG2;
            cfg3 = MCP_16MHz_40kBPS_CFG3;
            break;

        case (CAN_50KBPS): //  50Kbps
            cfg2 = MCP_16MHz_50kBPS_CFG2;
            cfg3 = MCP_16MHz_50kBPS_CFG3;
            break;

        case (CAN_80KBPS): //  80Kbps
            cfg1 = MCP_16MHz_80kBPS_CFG1;
            cfg2 = MCP_16MHz_80kBPS_CFG2;
            cfg3 = MCP_16MHz_80kBPS_CFG3;
            break;

        case (CAN_100KBPS): // 100Kbps
            cfg1 = MCP_16MHz_100kBPS_CFG1;
            cfg2 = MCP_16MHz_100kBPS_CFG2;
            cfg3 = MCP_16MHz_100kBPS_CFG3;
            break;

        case (CAN_125KBPS): // 125Kbps
            cfg1 = MCP_16MHz_125kBPS_CFG1;
            cfg2 = MCP_16MHz_125kBPS_CFG2;
            cfg3 = MCP_16MHz_125kBPS_CFG3;
            break;

        case (CAN_200KBPS): // 200Kbps
            cfg1 = MCP_16MHz_200kBPS_CFG1;
            cfg2 = MCP_16MHz_200kBPS_CFG2;
            cfg3 = MCP_16MHz_200kBPS_CFG3;
            break;

        case (CAN_250KBPS): // 250Kbps
            cfg1 = MCP_16MHz_250kBPS_CFG1;
            cfg2 = MCP_16MHz_250kBPS_CFG2;
            cfg3 = MCP_16MHz_250kBPS_CFG3;
            break;

        case (CAN_500KBPS): // 500Kbps
            cfg1 = MCP_16MHz_500kBPS_CFG1;
            cfg2 = MCP_16MHz_500kBPS_CFG2;
            cfg3 = MCP_16MHz_500kBPS_CFG3;
            break;

        case (CAN_1000KBPS): //   1Mbps
            cfg1 = MCP_16MHz_1000kBPS_CFG1;
            cfg2 = MCP_16MHz_1000kBPS_CFG2;
            cfg3 = MCP_16MHz_1000kBPS_CFG3;
            break;

        default:
            set = 0;
            return MCP2515_FAIL;
            break;
        }
        break;

    case (MCP_20MHZ):
        switch (canSpeed)
        {
        case (CAN_40KBPS): //  40Kbps
            cfg1 = MCP_20MHz_40kBPS_CFG1;
            cfg2 = MCP_20MHz_40kBPS_CFG2;
            cfg3 = MCP_20MHz_40kBPS_CFG3;
            break;

        case (CAN_50KBPS): //  50Kbps
            cfg1 = MCP_20MHz_50kBPS_CFG1;
            cfg2 = MCP_20MHz_50kBPS_CFG2;
            cfg3 = MCP_20MHz_50kBPS_CFG3;
            break;

        case (CAN_80KBPS): //  80Kbps
            cfg1 = MCP_20MHz_80kBPS_CFG1;
            cfg2 = MCP_20MHz_80kBPS_CFG2;
            cfg3 = MCP_20MHz_80kBPS_CFG3;
            break;

        case (CAN_100KBPS): // 100Kbps
            cfg1 = MCP_20MHz_100kBPS_CFG1;
            cfg2 = MCP_20MHz_100kBPS_CFG2;
            cfg3 = MCP_20MHz_100kBPS_CFG3;
            break;

        case (CAN_125KBPS): // 125Kbps
            cfg1 = MCP_20MHz_125kBPS_CFG1;
            cfg2 = MCP_20MHz_125kBPS_CFG2;
            cfg3 = MCP_20MHz_125kBPS_CFG3;
            break;

        case (CAN_200KBPS): // 200Kbps
            cfg1 = MCP_20MHz_200kBPS_CFG1;
            cfg2 = MCP_20MHz_200kBPS_CFG2;
            cfg3 = MCP_20MHz_200kBPS_CFG3;
            break;

        case (CAN_250KBPS): // 250Kbps
            cfg1 = MCP_20MHz_250kBPS_CFG1;
            cfg2 = MCP_20MHz_250kBPS_CFG2;
            cfg3 = MCP_20MHz_250kBPS_CFG3;
            break;

        case (CAN_500KBPS): // 500Kbps
            cfg1 = MCP_20MHz_500kBPS_CFG1;
            cfg2 = MCP_20MHz_500kBPS_CFG2;
            cfg3 = MCP_20MHz_500kBPS_CFG3;
            break;

        case (CAN_1000KBPS): //   1Mbps
            cfg1 = MCP_20MHz_1000kBPS_CFG1;
            cfg2 = MCP_20MHz_1000kBPS_CFG2;
            cfg3 = MCP_20MHz_1000kBPS_CFG3;
            break;

        default:
            set = 0;
            return MCP2515_FAIL;
            break;
        }
        break;

    default:
        set = 0;
        return MCP2515_FAIL;
        break;
    }

    if (canClock & MCP_CLKOUT_ENABLE)
    {
        cfg3 &= (~SOF_ENABLE);
    }

    if (set)
    {
        mcp2515_setRegister(MCP_CNF1, cfg1);
        mcp2515_setRegister(MCP_CNF2, cfg2);
        mcp2515_setRegister(MCP_CNF3, cfg3);
        return MCP2515_OK;
    }

    return MCP2515_FAIL;
}

uint8_t MCP2515_init(const uint8_t canIDMode, const uint8_t canSpeed, const uint8_t canClock)
{
    uint8_t res;
    printf("preparing to reset!\n");
    mcp2515_reset();
    mcpMode = MCP_LOOPBACK;

    res = mcp2515_setCANCTRL_Mode(MODE_CONFIG);
    if (res > 0)
    {
#if DEBUG_MODE
        printf("Entering Configuration Mode Failure...\n");
#endif
        return res;
    }
#if DEBUG_MODE
    printf("Entering Configuration Mode Successful!\n");
#endif

    // Set Baudrate
    if (mcp2515_configRate(canSpeed, canClock))
    {
#if DEBUG_MODE
        printf("Setting Baudrate Failure...\n");
#endif
        return res;
    }
#if DEBUG_MODE
    printf("Setting Baudrate Successful!\n");
#endif

    if (res == MCP2515_OK)
    {
        /* init canbuffers */
        mcp2515_initCANBuffers();
        /* interrupt mode */
        mcp2515_setRegister(MCP_CANINTE, MCP_RX0IF | MCP_RX1IF);
        // Sets BF pins as GPO
        mcp2515_setRegister(MCP_BFPCTRL, MCP_BxBFS_MASK | MCP_BxBFE_MASK);
        // Sets RTS pins as GPI
        mcp2515_setRegister(MCP_TXRTSCTRL, 0x00);

        switch (canIDMode)
        {
        case (MCP_ANY):
            mcp2515_modifyRegister(MCP_RXB0CTRL, MCP_RXB_RX_MASK | MCP_RXB_BUKT_MASK, MCP_RXB_RX_ANY | MCP_RXB_BUKT_MASK);
            mcp2515_modifyRegister(MCP_RXB1CTRL, MCP_RXB_RX_MASK, MCP_RXB_RX_ANY);
            break;
            // The followingn two functions of the MCP2515 do not work, there is a bug in the silicon.
        case (MCP_STD):
            mcp2515_modifyRegister(MCP_RXB0CTRL,
                                   MCP_RXB_RX_MASK | MCP_RXB_BUKT_MASK,
                                   MCP_RXB_RX_STD | MCP_RXB_BUKT_MASK);
            mcp2515_modifyRegister(MCP_RXB1CTRL, MCP_RXB_RX_MASK,
                                   MCP_RXB_RX_STD);
            break;

        case (MCP_EXT):
            mcp2515_modifyRegister(MCP_RXB0CTRL,
                                   MCP_RXB_RX_MASK | MCP_RXB_BUKT_MASK,
                                   MCP_RXB_RX_EXT | MCP_RXB_BUKT_MASK);
            mcp2515_modifyRegister(MCP_RXB1CTRL, MCP_RXB_RX_MASK,
                                   MCP_RXB_RX_EXT);
            break;

        case (MCP_STDEXT):
            mcp2515_modifyRegister(MCP_RXB0CTRL, MCP_RXB_RX_MASK | MCP_RXB_BUKT_MASK, MCP_RXB_RX_STDEXT | MCP_RXB_BUKT_MASK);
            mcp2515_modifyRegister(MCP_RXB1CTRL, MCP_RXB_RX_MASK, MCP_RXB_RX_STDEXT);
            break;

        default:
#if DEBUG_MODE
            printf("Setting ID Mode Failure...");
#endif
            return MCP2515_FAIL;
            break;
        }

        res = mcp2515_setCANCTRL_Mode(mcpMode);
        if (res)
        {
#if DEBUG_MODE
            printf("Returning to Previous Mode Failure...");
#endif
            return res;
        }
    }
    return res;
}

#if 1

void mcp2515_write_id(const uint8_t mcp_addr, const uint8_t ext, const uint32_t id)
{
    uint16_t canid;
    uint8_t tbufdata[4];

    canid = (uint16_t)(id & 0x0FFFF);

    if (ext == 1)
    {
        tbufdata[MCP_EID0] = (uint8_t)(canid & 0xFF);
        tbufdata[MCP_EID8] = (uint8_t)(canid >> 8);
        canid = (uint16_t)(id >> 16);
        tbufdata[MCP_SIDL] = (uint8_t)(canid & 0x03);
        tbufdata[MCP_SIDL] += (uint8_t)((canid & 0x1C) << 3);
        tbufdata[MCP_SIDL] |= MCP_TXB_EXIDE_M;
        tbufdata[MCP_SIDH] = (uint8_t)(canid >> 5);
    }
    else
    {
        tbufdata[MCP_SIDH] = (uint8_t)(canid >> 3);
        tbufdata[MCP_SIDL] = (uint8_t)((canid & 0x07) << 5);
        tbufdata[MCP_EID0] = 0;
        tbufdata[MCP_EID8] = 0;
    }

    mcp2515_setRegisterS(mcp_addr, tbufdata, 4);
}

void mcp2515_write_canMsg(const uint8_t buffer_sidh_addr)
{
    uint8_t mcp_addr;
    mcp_addr = buffer_sidh_addr;
    mcp2515_setRegisterS(mcp_addr + 5, m_nDta, m_nDlc); /* write data bytes             */

    if (m_nRtr == 1) /* if RTR set bit in byte       */
    {
        m_nDlc |= MCP_RTR_MASK;
    }

    mcp2515_setRegister((mcp_addr + 4), m_nDlc);  /* write the RTR and DLC        */
    mcp2515_write_id(mcp_addr, m_nExtFlg, m_nID); /* write CAN id                 */
}

uint8_t mcp2515_getNextFreeTXBuf(uint8_t *txbuf_n) /* get Next free txbuf          */
{
    uint8_t res, i, ctrlval;
    uint8_t ctrlregs[MCP_N_TXBUFFERS] = {MCP_TXB0CTRL, MCP_TXB1CTRL, MCP_TXB2CTRL};

    res = MCP_ALLTXBUSY;
    *txbuf_n = 0x00;

    /*   check all 3 TX-Buffers    */
    for (i = 0; i < MCP_N_TXBUFFERS; i++)
    {
        ctrlval = mcp2515_readRegister(ctrlregs[i]);
        if ((ctrlval & MCP_TXB_TXREQ_M) == 0)
        {
            *txbuf_n = ctrlregs[i] + 1; /* return SIDH-address of Buffer*/
            res = MCP2515_OK;
            return res; /* ! function exit              */
        }
    }
    return res;
}

uint8_t setMsg(uint32_t id, uint8_t rtr, uint8_t ext, uint8_t len, uint8_t *pData)
{
    int i = 0;
    m_nID = id;
    m_nRtr = rtr;
    m_nExtFlg = ext;
    m_nDlc = len;
    for (i = 0; i < MAX_CHAR_IN_MESSAGE; i++)
    {
        m_nDta[i] = *(pData + i);
    }

    return MCP2515_OK;
}

uint8_t sendMsg()
{
    uint8_t res, res1, txbuf_n;
    uint32_t uiTimeOut, temp;

    temp = xTaskGetTickCount();
    // 24 * 4 microseconds typical //
    do
    {
        res = mcp2515_getNextFreeTXBuf(&txbuf_n); /* info = addr.                 */
        uiTimeOut = xTaskGetTickCount() - temp;
    } while (res == MCP_ALLTXBUSY && (uiTimeOut < TIMEOUTVALUE));

    if (uiTimeOut >= TIMEOUTVALUE)
    {
        return CAN_GETTXBFTIMEOUT; /* get tx buff time out         */
    }
    uiTimeOut = 0;
    mcp2515_write_canMsg(txbuf_n);
    mcp2515_modifyRegister(txbuf_n - 1, MCP_TXB_TXREQ_M, MCP_TXB_TXREQ_M);

    temp = xTaskGetTickCount();
    do
    {
        res1 = mcp2515_readRegister(txbuf_n - 1); /* read send buff ctrl reg  */
        res1 = res1 & 0x08;
        uiTimeOut = xTaskGetTickCount() - temp;
    } while (res1 && (uiTimeOut < TIMEOUTVALUE));

    if (uiTimeOut >= TIMEOUTVALUE) /* send msg timeout             */
    {
        return CAN_SENDMSGTIMEOUT;
    }

    return CAN_OK;
}

uint8_t sendMsgBuf(uint32_t id, uint8_t len, uint8_t *buf)
{
    uint8_t ext = 0, rtr = 0;
    uint8_t res;

    if ((id & 0x80000000) == 0x80000000)
    {
        ext = 1;
    }

    if ((id & 0x40000000) == 0x40000000)
    {
        rtr = 1;
    }

    setMsg(id, rtr, ext, len, buf);
    res = sendMsg();

    return res;
}

#endif

void mcp2515_read_canMsg(const uint8_t buffer_sidh_addr) /* read can msg                 */
{
    uint8_t mcp_addr, ctrl;
    mcp_addr = buffer_sidh_addr;
    mcp2515_read_id(mcp_addr, &m_nExtFlg, &m_nID);

    ctrl = mcp2515_readRegister(mcp_addr - 1);
    m_nDlc = mcp2515_readRegister(mcp_addr + 4);

    if (ctrl & 0x08)
        m_nRtr = 1;
    else
        m_nRtr = 0;

    m_nDlc &= MCP_DLC_MASK;
    mcp2515_readRegisterS(mcp_addr + 5, &(m_nDta[0]), m_nDlc);
}

uint8_t readMsg()
{
    uint8_t stat, res;
    stat = mcp2515_readStatus();

    if (stat & MCP_STAT_RX0IF) /* Msg in Buffer 0              */
    {
        mcp2515_read_canMsg(MCP_RXBUF_0);
        mcp2515_modifyRegister(MCP_CANINTF, MCP_RX0IF, 0);
        res = CAN_OK;
    }
    else if (stat & MCP_STAT_RX1IF) /* Msg in Buffer 1              */
    {
        mcp2515_read_canMsg(MCP_RXBUF_1);
        mcp2515_modifyRegister(MCP_CANINTF, MCP_RX1IF, 0);
        res = CAN_OK;
    }
    else
    {
        printf("inside can nomsg\n");
        res = CAN_NOMSG;
    }
    return res;
}

uint8_t readMsgBuf(uint32_t *id, uint8_t *len, uint8_t buf[])
{
    if (readMsg() == CAN_NOMSG)
    {
        printf("can no msg\r");
        return CAN_NOMSG;
    }

    if (m_nExtFlg)
    {
        m_nID |= 0x80000000;
    }

    if (m_nRtr)
    {
        m_nID |= 0x40000000;
    }


    *id = m_nID;
    *len = m_nDlc;

    for (int i = 0; i < m_nDlc; i++)
    {
        buf[i] = m_nDta[i];
    }
    return CAN_OK;
}

uint8_t MCP2515_begin(uint8_t idmodeset, uint8_t speedset, uint8_t clockset)
{
    uint8_t res;
    // initialize spi bus here for esp
    res = MCP2515_init(idmodeset, speedset, clockset);
    if (res == MCP2515_OK)
    {
        return CAN_OK;
    }
    else
    {
        return CAN_FAILINIT;
    }
}