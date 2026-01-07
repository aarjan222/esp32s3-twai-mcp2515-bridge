#ifndef _MCP2515_H_
#define _MCP2515_H_
#define MAX_CHAR_IN_MESSAGE 8

extern uint8_t m_nExtFlg;
extern uint32_t m_nID;                      // CAN ID
extern uint8_t m_nDlc;                      // Data Length Code
extern uint8_t m_nDta[MAX_CHAR_IN_MESSAGE]; // Data array
extern uint8_t m_nRtr;                      // Remote request flag
extern uint8_t m_nfilhit;

uint8_t MCP2515_begin(uint8_t idmodeset, uint8_t speedset, uint8_t clockset);
uint8_t readMsgBuf(uint32_t *id, uint8_t *len, uint8_t buf[]);
uint8_t readMsg();
uint8_t MCP2515_init(const uint8_t canIDMode, const uint8_t canSpeed, const uint8_t canClock);
uint8_t mcp2515_configRate(const uint8_t canSpeed, const uint8_t canClock);
uint8_t mcp2515_setCANCTRL_Mode(const uint8_t newmode);
void mcp2515_initCANBuffers(void);
void mcp2515_write_mf(const uint8_t mcp_addr, const uint8_t ext, const uint32_t id);
uint8_t mcp2515_requestNewMode(const uint8_t newmode);
uint8_t mcp2515_readStatus(void);
void mcp2515_setRegister(const uint8_t address, const uint8_t value);
void mcp2515_setRegisterS(const uint8_t address, uint8_t values[], const uint8_t n);
void mcp2515_modifyRegister(const uint8_t address, const uint8_t mask, const uint8_t _data);
uint8_t mcp2515_readRegister(const uint8_t address);
void mcp2515_readRegisterS(const uint8_t address, uint8_t values[], const uint8_t n);
void mcp2515_read_id(const uint8_t mcp_addr, uint8_t *ext, uint32_t *id);
void mcp2515_reset(void);
uint8_t setMode(const uint8_t opMode);

void mcp2515_write_id(const uint8_t mcp_addr, const uint8_t ext, const uint32_t id);
void mcp2515_write_canMsg(const uint8_t buffer_sidh_addr);
uint8_t mcp2515_getNextFreeTXBuf(uint8_t *txbuf_n);
uint8_t setMsg(uint32_t id, uint8_t rtr, uint8_t ext, uint8_t len, uint8_t *pData);
uint8_t sendMsg();
uint8_t sendMsgBuf(uint32_t id, uint8_t len, uint8_t *buf);

#endif