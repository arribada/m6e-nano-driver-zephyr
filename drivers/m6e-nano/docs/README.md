# Documentation

## Overview

This driver provides a set of APIs to control the [ThingMagic M6e Nano](https://www.jadaktech.com/products/rfid/thingmagic-rfid/embedded-rfid-readers/m6e-nano/) RFID reader.

## Example Command & Response

The following example shows a command and response exchange between the host and the reader. The command is sent by the host to the reader and the response is sent by the reader to the host.

### Command

The following command is sent by the host to the reader:

```text
FF  0A  97  03  E8  00  00  00  00  00  03  00  EE  58  9D
FF 0A 97 = Header, LEN, Opcode
03 E8 = Timeout in ms
08 = Data
58 9D = CRC
```

### Response

The following response is sent by the reader to the host:

```text

```

## Usage

### Initialization

The driver can be initialized by calling the `m6e_nano_init()` function. This function takes a pointer to a `struct m6e_nano_dev_cfg` structure as argument. This structure contains the configuration of the device. The following code snippet shows how to initialize the driver:

```c

```
