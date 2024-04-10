# Documentation

## Overview

This driver provides a set of APIs to control the [ThingMagic M6e Nano](https://www.jadaktech.com/products/rfid/thingmagic-rfid/embedded-rfid-readers/m6e-nano/) RFID reader. The driver is implemented as a Zephyr device driver and provides a set of APIs to control the reader.

### Command

The following example shows a command send by the host to the reader. While this is abstracted by the driver, it is important to understand the structure of the command.

```text
FF  0A  97 | 03  E8 | 00  00  00  00  00  03  00  EE | 58  9D

FF 0A 97 = Header, LEN, Opcode
03 E8    = Timeout in ms
08       = Data
58 9D    = CRC
```

### Response

The following example shows a command send by the reader back to the host.

```text

```

## Usage

The driver provides a set of APIs to control the reader. The following sections describe the APIs provided by the driver.

### Initialization

The driver is initialized by creating a device struct pointing at the `thingmagic_m6enano` driver. The device struct is then used to control the reader.

```c
const struct device *dev = DEVICE_DT_GET_ONE(thingmagic_m6enano);
```
