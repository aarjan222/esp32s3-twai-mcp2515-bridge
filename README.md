# Bidirectional CAN Communication Bridge

ESP32-S3 based CAN bridge that enables bidirectional communication between TWAI (ESP32 internal CAN) and MCP2515 (external SPI CAN controller).

## Overview

This project demonstrates simultaneous bidirectional CAN communication using:
- **TWAI Controller**: ESP32-S3's built-in CAN peripheral
- **MCP2515**: External CAN controller via SPI interface
- **CAN Speed**: 250 kbps on both controllers

Messages sent from TWAI are received by MCP2515, and vice versa, creating a complete CAN bridge.

## Hardware Requirements

- ESP32-S3 development board
- MCP2515 CAN module with 8MHz crystal
- CAN transceivers (typically built into MCP2515 modules)
- Connecting wires

## Features

- ✅ Bidirectional CAN message forwarding
- ✅ Standard CAN ID support (11-bit)
- ✅ Extended CAN ID support (29-bit) - switchable via `#define`

## Configuration

Switch between Standard and Extended CAN IDs by commenting/uncommenting in the code:
```c
#define std_id_comm  // Use standard 11-bit IDs
// Comment out for extended 29-bit IDs
```

**Standard ID Mode:**
- TWAI sends: `0x704`
- MCP2515 sends: `0x403`

**Extended ID Mode:**
- TWAI sends: `0x0817FCFA`
- MCP2515 sends: `0x080CFAFC`

## Building and Flashing
```bash
idf.py build
idf.py -p PORT flash monitor
```

Replace `PORT` with your serial port (e.g., `/dev/ttyUSB0` or `COM3`).

## Expected Output
```
I (285) BidirectionalCAN: ====================================
I (285) BidirectionalCAN: Bidirectional CAN Communication Demo
I (285) BidirectionalCAN: ====================================
I (295) BidirectionalCAN: Initializing TWAI driver...
I (295) CANDriver: CAN baud rate set to 250 kbps
I (305) BidirectionalCAN: TWAI driver initialized successfully
I (315) CANManager: Started Receiving CAN Message
I (335) BidirectionalCAN: MCP2515 initialized successfully
I (355) BidirectionalCAN: MCP2515 set to Normal Mode successfully
I (385) BidirectionalCAN: System initialized successfully!
I (385) BidirectionalCAN: Bidirectional CAN communication active

I (475) BidirectionalCAN: TWAI Received: ID: 0x403, DLC: 8, Extended: No, RTR: No
W (575) BidirectionalCAN: Received MCP RX: ID: 0x704, DLC: 8, Extended: No, RTR: No
I (575) BidirectionalCAN: 11 22 33 44 55 66 77 88

I (1675) BidirectionalCAN: TWAI Received: ID: 0x403, DLC: 8, Extended: No, RTR: No
W (1775) BidirectionalCAN: Received MCP RX: ID: 0x704, DLC: 8, Extended: No, RTR: No
I (1775) BidirectionalCAN: 12 22 33 44 55 66 77 88
```

The first byte of the test data increments with each transmission (`0x11` → `0x12` → `0x13`...), making it easy to verify continuous operation.

## How It Works

1. **Initialization**: Both TWAI and MCP2515 controllers are configured for 250 kbps operation
2. **Transmission Loop**:
   - TWAI sends test message → MCP2515 receives it
   - MCP2515 sends test message → TWAI receives it
   - 1-second delay between cycles
3. **Reception**: Dedicated callbacks handle incoming messages from both controllers

## Troubleshooting

**No messages received:**
- Check CAN bus termination (120Ω resistors at both ends)
- Verify CANH and CANL connections
- Ensure both devices are at the same baud rate

**SPI communication errors:**
- Verify MCP2515 crystal frequency matches code (8MHz)
- Check SPI wiring (MISO, MOSI, SCK, CS)
- Confirm adequate power supply to MCP2515

## License

This project is provided as-is for educational and development purposes.