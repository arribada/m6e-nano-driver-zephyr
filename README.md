# M6E Nano UHF RFID Reader Driver for Zephyr

This is a Zephyr driver for the Sparkfun M6E Nano UHF RFID Reader, based on their [Arduino Library](https://github.com/sparkfun/SparkFun_Simultaneous_RFID_Tag_Reader_Library/tree/master).

## Architecture

The M6E Nano is a Serial Peripheral and is connected to the Zephyr host via UART. The driver uses the UART Polling API for sending data to the M6E Nano and the Interrupt API for receiving data.

### Commands

Commands are sent to the M6E in the following format:

```bash
HEADER,OP_CODE,DATA,SIZE,TIMEOUT,WAIT_FOR_RESPONSE
```
