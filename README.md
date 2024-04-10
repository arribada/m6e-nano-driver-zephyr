# M6E Nano UHF RFID Reader Driver for Zephyr

This is a Zephyr driver for the Sparkfun M6E Nano UHF RFID Reader, based on their [Arduino Library](https://github.com/sparkfun/SparkFun_Simultaneous_RFID_Tag_Reader_Library/tree/master).

## Architecture

The M6E Nano is a Serial Peripheral and is connected to the Zephyr host via UART. The driver uses the UART Polling API for sending data to the M6E Nano and the Interrupt API for receiving data.

### Commands

Commands are sent to the M6E in the following format:

```bash
HEADER,OP_CODE,DATA,SIZE,TIMEOUT,WAIT_FOR_RESPONSE
```

## Setup

1. `west init -m https://github.com/arribada/m6e-nano-driver-zephyr --mr development m6e-env`
2. `cd m6e-env`
3. `west update`
4. `cd m6e-nano-driver-zephyr/examples/simple`
5. To build for Pico with a Pico debugger - `west build -b rpi_pico -- -DOPENOCD=/usr/bin/openocd -DOPENOCD_DEFAULT_PATH=/usr/share/openocd/scripts -DRPI_PICO_DEBUG_ADAPTER=cmsis-dap`
