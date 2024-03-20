/*
 * Copyright (c) 2023 Arribada Initiative CIC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT          thingmagic_m6enano
#define M6E_NANO_INIT_PRIORITY 60

#include <errno.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

#include "m6e_nano.h"

LOG_MODULE_REGISTER(M6E_NANO, CONFIG_M6E_NANO_LOG_LEVEL);

static const uint16_t crc_table[] = {
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
	0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
};

/**
 * @brief Calculate the CRC of the command being transmitted.
 *
 * @param u8Buf Partial of the command to calculate the CRC for.
 * @param len Length of the partial command.
 * @return uint16_t CRC of the command. To be appended as the last two bytes of the outbound
 * command.
 */
static uint16_t _calculate_crc(uint8_t *u8Buf, uint8_t len)
{
	uint16_t crc = 0xFFFF;

	for (uint8_t i = 0; i < len; i++) {
		crc = ((crc << 4) | (u8Buf[i] >> 4)) ^ crc_table[crc >> 12];
		crc = ((crc << 4) | (u8Buf[i] & 0x0F)) ^ crc_table[crc >> 12];
	}

	return crc;
}

/**
 * @brief Set callback function to be called when a string is received.
 *
 * @param dev UART peripheral device.
 * @param callback New callback function.
 * @param user_data Data to be passed to the callback function.
 */
static void user_set_command_callback(const struct device *dev, m6e_nano_callback_t callback,
				      void *user_data)
{
	struct m6e_nano_data *data = (struct m6e_nano_data *)dev->data;
	data->callback = callback;
	data->user_data = user_data;
}

/**
 * @brief Empty the RX buffer of the UART peripheral.
 *
 * @param dev UART peripheral device.
 */
static void m6e_nano_uart_flush(const struct device *dev)
{
	struct m6e_nano_data *drv_data = dev->data;
	uint8_t buf;

	while (uart_fifo_read(dev, &buf, 1) > 0) {
		;
	}
	memset(&drv_data->response.data, 0, M6E_NANO_BUF_SIZE);

	LOG_DBG("UART RX buffer flushed.");
}

/**
 * @brief Construct the command to be transmitted by the UART peripheral.
 *
 * @param dev UART peripheral device.
 * @param region region to set.
 */
static int m6e_nano_construct_command(const struct device *dev, uint8_t opcode, uint8_t *data,
				      uint8_t size, bool timeout)
{
	uint8_t command[size + 5];
	command[0] = TMR_START_HEADER;
	command[1] = size; // load the length of this operation into msg array
	command[2] = opcode;
	command[3] = *data;

	// Copy the data into msg array
	for (uint8_t x = 0; x < size; x++) {
		command[3 + x] = data[x];
	}

	// calculate the CRC
	uint16_t crc = _calculate_crc(&command[1], size + 2);
	command[size + 3] = crc >> 8;
	command[size + 4] = crc & 0xFF;

	size_t length = sizeof(command) / sizeof(*command);

	return user_send_command(dev, command, length, timeout); // Send and wait for response
}

/**
 * @brief Set general configuration parameters.
 *
 * @param dev UART peripheral device.
 * @param option1 Byte 1 of the configuration.
 * @param option2 Byte 2 of the configuration.
 */
static void _m6e_nano_set_config(const struct device *dev, uint8_t option1, uint8_t option2)
{
	uint8_t data[3];

	// These are parameters gleaned from inspecting the 'Transport Logs' of the Universal Reader
	// Assistant And from serial_reader_l3.c
	data[0] = 1; // Key value form of command
	data[1] = option1;
	data[2] = option2;

	m6e_nano_construct_command(dev, TMR_SR_OPCODE_SET_READER_OPTIONAL_PARAMS, data,
				   sizeof(data), true);
}

/**
 * @brief Retrieve the number of bytes of embedded tag data.
 *
 * @param dev UART peripheral device.
 * @return uint8_t Number of bytes of embedded tag data.
 */
static uint8_t _get_tag_data_bytes(const struct device *dev)
{
	struct m6e_nano_data *data = (struct m6e_nano_data *)dev->data;
	uint8_t *msg = data->response.data;
	// Number of bits of embedded tag data
	uint8_t tagDataLength = 0;
	for (uint8_t x = 0; x < 2; x++) {
		tagDataLength |= (uint16_t)msg[24 + x] << (8 * (1 - x));
	}
	uint8_t tagDataBytes = tagDataLength / 8;
	if (tagDataLength % 8 > 0) {
		tagDataBytes++; // Ceiling trick
	}

	return (tagDataBytes);
}

/**
 * @brief Handler for when the UART peripheral receives data.
 *
 * @param dev UART peripheral device.
 * @param dev_m6e Driver device passed to provide access to buffers.
 */
static void uart_rx_handler(const struct device *dev, void *dev_m6e)
{
	const struct device *m6e_nano_dev = dev_m6e;
	struct m6e_nano_data *drv_data = m6e_nano_dev->data;

	int len = 0;
	int offset = 0;

	if (drv_data->status == RESPONSE_CLEAR) {
		drv_data->response.len = 0;
	}

	offset = drv_data->response.len;
	m6e_nano_callback_t callback = drv_data->callback;

	if ((uart_irq_update(dev) > 0) && (uart_irq_is_pending(dev) > 0)) {
		while (uart_irq_rx_ready(dev)) {

			len = uart_fifo_read(dev, &drv_data->response.data[offset], 255 - offset);
			LOG_DBG("Received %d bytes", len);

			while (len > 0) {
				LOG_DBG("Data: %X | Offset: %d", drv_data->response.data[offset],
					offset);
				switch (offset) {
				case 0:
					if (drv_data->response.data[offset] == TMR_START_HEADER) {
						LOG_DBG("Msg Header: %X",
							drv_data->response.data[offset]);
						drv_data->status = RESPONSE_PENDING;
					} else if (drv_data->response.data[offset] ==
						   ERROR_COMMAND_RESPONSE_TIMEOUT) {
						drv_data->status = ERROR_COMMAND_RESPONSE_TIMEOUT;
					}
					break;
				case 1:
					drv_data->response.msg_len =
						drv_data->response.data[offset] + 7;
					LOG_DBG("Msg Total Len: %d", drv_data->response.msg_len);
					break;
				case 2:
					LOG_DBG("Msg Opcode: %x", drv_data->response.data[offset]);
					if (drv_data->response.data[offset] ==
					    TMR_SR_OPCODE_VERSION_STARTUP) {
						drv_data->status = RESPONSE_CLEAR;
					};
					break;
				default:
					break;
				}

				if (drv_data->status == ERROR_COMMAND_RESPONSE_TIMEOUT) {
					len = 0;
					offset = 0;
					break;
				}

				offset++;
				len--;
				drv_data->response.len = offset;
			}
		}
	}

	if (offset > drv_data->response.msg_len - 1) {
		drv_data->response.len = 0;
		drv_data->status = RESPONSE_SUCCESS;
		LOG_DBG("Response success.");
	} else if (offset > M6E_NANO_BUF_SIZE) {
		drv_data->response.len = 0;
		drv_data->status = RESPONSE_FAIL;
		m6e_nano_uart_flush(dev);
		LOG_WRN("Response exceeds buffer, %d.", offset);
	} else if (drv_data->status == ERROR_COMMAND_RESPONSE_TIMEOUT) {
		drv_data->response.len = 0;
		drv_data->status = RESPONSE_FAIL;
		m6e_nano_uart_flush(dev);
		LOG_WRN("Command response timeout.");
	}

	if (callback != NULL) {
		callback(dev, dev_m6e);
	}
}

/**
 * @brief Set the command to be transmitted by the UART peripheral.
 *
 * @param dev UART peripheral device.
 * @param command Command to be transmitted.
 * @param length Length of the command.
 * @return int32_t Status of the response.
 */
int user_send_command(const struct device *dev, uint8_t *command, const uint8_t length,
		      const bool timeout)
{
	int32_t timeout_in_ms = CFG_M6E_NANO_SERIAL_TIMEOUT;
	struct m6e_nano_data *data = (struct m6e_nano_data *)dev->data;
	const struct m6e_nano_config *cfg = dev->config;
	struct m6e_nano_buf *tx = &data->command;

	memset(tx->data, 0, M6E_NANO_BUF_SIZE);

	memcpy(tx->data, command, sizeof(uint8_t) * length);
	tx->len = length;
	LOG_DBG("Length of command: %d", tx->len);

	__ASSERT(tx->len <= 255, "Command length too long.");

	while (data->status == RESPONSE_STARTUP) {
		if (timeout_in_ms < 0) {
			LOG_DBG("Startup event missed...");
			data->status = RESPONSE_CLEAR;
			break;
		}
		timeout_in_ms -= 10;
		k_msleep(10);
	}

	if (CONFIG_M6E_NANO_LOG_LEVEL >= LOG_LEVEL_DBG) {
		for (size_t i = 0; i < length; i++) {
			switch (i) {
			case 0:
				LOG_DBG("Header: %X", tx->data[i]);
				break;
			case 1:
				LOG_DBG("Data Length: %X", tx->data[i]);
				break;
			case 2:
				LOG_DBG("Opcode: %X", tx->data[i]);
				break;
			default:
				if (length - 2 <= i) {
					LOG_DBG("CRC[%u]: %X", i - (length - 2), tx->data[i]);
				} else {
					LOG_DBG("Data: %X", tx->data[i]);
				}
				break;
			}
		}
	}

	for (size_t i = 0; i < tx->len; i++) {
		data->status = RESPONSE_CLEAR;
		uart_poll_out(cfg->uart_dev, tx->data[i]);
	}

	if (timeout) {
		while (data->status != RESPONSE_SUCCESS) {
			if (timeout_in_ms < 0) {
				LOG_WRN("Command timeout.");
				data->status = RESPONSE_CLEAR;
				return -ETIMEDOUT;
			}
			timeout_in_ms -= 10;
			k_msleep(10);
		}
		return 0;
	}
	return 0;
}

/**
 * @brief Retrieve the number of bytes from EPC.
 *
 * @param dev UART peripheral device.
 * @return uint8_t Number of bytes from EPC.
 */
uint8_t m6e_nano_get_tag_epc_bytes(const struct device *dev)
{
	struct m6e_nano_data *data = (struct m6e_nano_data *)dev->data;
	uint8_t *msg = data->response.data;

	uint16_t epcBits = 0; // Number of bits of EPC (including PC, EPC, and EPC CRC)

	uint8_t tagDataBytes = _get_tag_data_bytes(dev); // We need this offset

	for (uint8_t x = 0; x < 2; x++) {
		epcBits |= (uint16_t)msg[27 + tagDataBytes + x] << (8 * (1 - x));
	}
	uint8_t epcBytes = epcBits / 8;
	epcBytes -= 4; // Ignore the first two bytes and last two bytes

	return (epcBytes);
}

/**
 * @brief Retrieve the RSSI of the tag.
 *
 * @param dev UART peripheral device.
 * @return uint8_t RSSI of the tag.
 */
uint8_t m6e_nano_get_tag_rssi(const struct device *dev)
{
	struct m6e_nano_data *data = (struct m6e_nano_data *)dev->data;
	uint8_t *msg = data->response.data;
	uint8_t rssi = msg[12] - 256;
	return rssi;
}

/**
 * @brief Retrieve the timestamp of the tag.
 *
 * @param dev UART peripheral device.
 * @return uint16_t Timestamp of the tag.
 */
uint16_t m6e_nano_get_tag_timestamp(const struct device *dev)
{
	struct m6e_nano_data *data = (struct m6e_nano_data *)dev->data;
	uint8_t *msg = data->response.data;
	// Timestamp since last Keep-Alive message
	uint32_t timeStamp = 0;
	for (uint8_t x = 0; x < 4; x++) {
		timeStamp |= (uint32_t)msg[17 + x] << (8 * (3 - x));
	}

	return timeStamp;
}

/**
 * @brief Retrieve the frequency of the tag.
 *
 * @param dev UART peripheral device.
 * @return uint32_t Frequency of the tag.
 */
uint32_t m6e_nano_get_tag_freq(const struct device *dev)
{
	struct m6e_nano_data *data = (struct m6e_nano_data *)dev->data;
	uint8_t *msg = data->response.data;
	// Frequency of the tag detected is loaded over three bytes
	uint32_t freq = 0;
	for (uint8_t x = 0; x < 3; x++) {
		freq |= (uint32_t)msg[14 + x] << (8 * (2 - x));
	}

	return freq;
}

/**
 * @brief Disable the read filter.
 *
 * @param dev UART peripheral device.
 */
void m6e_nano_disable_read_filter(const struct device *dev)
{
	_m6e_nano_set_config(dev, 0x0C, 0x00); // Disable read filter
}

/**
 * @brief Stop a continuous read operation.
 *
 * @param dev UART peripheral device.
 * @brief Stop a continuous read operation. No timeout required.
 */
void m6e_nano_stop_reading(const struct device *dev)
{
	uint8_t data[] = {0x00, 0x00, 0x02};

	m6e_nano_construct_command(dev, TMR_SR_OPCODE_MULTI_PROTOCOL_TAG_OP, data, sizeof(data),
				   false);
}

/**
 * @brief Set the power mode of the M6E Nano.
 *
 * @param dev UART peripheral device.
 * @param mode Power mode to set. See docs for valid modes.
 */
void m6e_nano_set_power_mode(const struct device *dev, uint8_t mode)
{
	uint8_t data[1] = {mode};
	m6e_nano_construct_command(dev, TMR_SR_OPCODE_SET_POWER_MODE, data, sizeof(data), true);
}

/**
 * @brief Set the antenna port of the M6E Nano.
 *
 * @param dev UART peripheral device.
 */
void m6e_nano_set_antenna_port(const struct device *dev)
{
	uint8_t data[2] = {0x01, 0x01};
	m6e_nano_construct_command(dev, TMR_SR_OPCODE_SET_ANTENNA_PORT, data, sizeof(data), true);
}

/**
 * @brief Set the read power of the M6E Nano.
 *
 * @param dev UART peripheral device.
 * @param power Power to set. Between 0 and 27dBm.
 */
void m6e_nano_set_read_power(const struct device *dev, uint16_t power)
{
	if (power > 2700) {
		LOG_DBG("Limit exceeded (27dBm), restricting to 27dBm.");
		power = 2700;
	}

	uint8_t size = sizeof(power);
	uint8_t data[size];
	for (uint8_t x = 0; x < size; x++) {
		data[x] = (uint8_t)(power >> (8 * (size - 1 - x)));
	}

	m6e_nano_construct_command(dev, TMR_SR_OPCODE_SET_READ_TX_POWER, data, size, true);
}

/**
 * @brief Start a continuous read operation.
 *
 * @param dev UART peripheral device.
 */
void m6e_nano_start_reading(const struct device *dev)
{
	m6e_nano_disable_read_filter(dev);

	uint8_t data[] = {0x00, 0x00, 0x01, 0x22, 0x00, 0x00, 0x05, 0x07,
			  0x22, 0x10, 0x00, 0x1B, 0x03, 0xE8, 0x01, 0xFF};

	/*
SETU16(newMsg, i, 0);
SETU8(newMsg, i, (uint8_t)0x1); // TM Option 1, for continuous reading
SETU8(newMsg, i, (uint8_t)TMR_SR_OPCODE_READ_TAG_ID_MULTIPLE); // sub command opcode
SETU16(newMsg, i, (uint16_t)0x0000); // search flags, only 0x0001 is supported
SETU8(newMsg, i, (uint8_t)TMR_TAG_PROTOCOL_GEN2); // protocol ID
*/

	m6e_nano_construct_command(dev, TMR_SR_OPCODE_MULTI_PROTOCOL_TAG_OP, data, sizeof(data),
				   true);
}

/**
 * @brief Set the operating region of the M6E Nano. This controls the transmission frequency of the
 * RFID reader.
 *
 * @param dev UART peripheral device.
 * @param region Operating region to set.
 */
void m6e_nano_set_region(const struct device *dev, uint8_t region)
{

	m6e_nano_construct_command(dev, TMR_SR_OPCODE_SET_REGION, &region, sizeof(region), true);
}

/**
 * @brief Retrieve the firmware version of the M6E Nano.
 *
 * @param dev UART peripheral device.
 */
int m6e_nano_get_version(const struct device *dev)
{
	uint8_t data[] = {};

	return m6e_nano_construct_command(dev, TMR_SR_OPCODE_VERSION, data, sizeof(data), true);
}

/**
 * @brief Set the tag protocol of the M6E Nano.
 *
 * @param dev UART peripheral device.
 * @param protocol Tag protocol to set.
 */
void m6e_nano_set_tag_protocol(const struct device *dev, uint8_t protocol)
{
	uint8_t data[2];
	data[0] = 0; // Opcode expects padding for 16-bits
	data[1] = protocol;

	m6e_nano_construct_command(dev, TMR_SR_OPCODE_SET_TAG_PROTOCOL, data, sizeof(data), true);
}

/**
 * @brief Retrieve the write power of the M6E Nano.
 *
 * @param dev UART peripheral device.
 */
void m6e_nano_get_write_power(const struct device *dev)
{
	uint8_t data[] = {0x00}; // Just return power
	// uint8_t data[] = {0x01}; // Return power with limits

	m6e_nano_construct_command(dev, TMR_SR_OPCODE_GET_WRITE_TX_POWER, data, sizeof(data), true);
}

/**
 * @brief Set the baudrate of the M6E Nano.
 *
 * @param dev UART peripheral device.
 * @param baud_rate baudrate to set.
 */
void m6e_nano_set_baud(const struct device *dev, long baud_rate)
{
	uint8_t size = sizeof(baud_rate);
	uint8_t data[size];
	for (uint8_t x = 0; x < size; x++) {
		data[x] = (uint8_t)(baud_rate >> (8 * (size - 1 - x)));
	}

	LOG_DBG("Baud rate: %ld", baud_rate);

	m6e_nano_construct_command(dev, TMR_SR_OPCODE_SET_BAUD_RATE, data, size, true);
}

/**
 * @brief Send a generic command to the M6E Nano.
 *
 * @param dev UART peripheral device.
 * @param data Data to be sent.
 */
void m6e_nano_send_generic_command(const struct device *dev, uint8_t *command, uint8_t size,
				   uint8_t opcode)
{
	m6e_nano_construct_command(dev, opcode, command, size, true);
}

uint8_t m6e_nano_get_status(const struct device *dev)
{
	struct m6e_nano_data *data = (struct m6e_nano_data *)dev->data;
	return data->status;
}

/**
 * @brief Parse the tag response from the M6E Nano.
 *
 * @param dev UART peripheral device.
 * @return uint8_t Status of the response.
 */
uint8_t m6e_nano_parse_response(const struct device *dev)
{
	// See
	// http://www.thingmagic.com/images/Downloads/Docs/AutoConfigTool_1.2-UserGuide_v02RevA.pdf
	// for a breakdown of the response packet

	// Example response:
	// FF  28  22  00  00  10  00  1B  01  FF  01  01  C4  11  0E  16
	// 40  00  00  01  27  00  00  05  00  00  0F  00  80  30  00  00
	// 00  00  00  00  00  00  00  00  00  15  45  E9  4A  56  1D
	//   [0] FF = Header
	//   [1] 28 = Message length
	//   [2] 22 = OpCode
	//   [3, 4] 00 00 = Status
	//   [5 to 11] 10 00 1B 01 FF 01 01 = RFU 7 bytes
	//   [12] C4 = RSSI
	//   [13] 11 = Antenna ID (4MSB = TX, 4LSB = RX)
	//   [14, 15, 16] 0E 16 40 = Frequency in kHz
	//   [17, 18, 19, 20] 00 00 01 27 = Timestamp in ms since last keep alive msg
	//   [21, 22] 00 00 = phase of signal tag was read at (0 to 180)
	//   [23] 05 = Protocol ID
	//   [24, 25] 00 00 = Number of bits of embedded tag data [M bytes]
	//   [26 to M] (none) = Any embedded data
	//   [26 + M] 0F = RFU reserved future use
	//   [27, 28 + M] 00 80 = EPC Length [N bytes]  (bits in EPC including PC and CRC bits). 128
	//   bits = 16 bytes [29, 30 + M] 30 00 = Tag EPC Protocol Control (PC) bits [31 to 42 + M +
	//   N] 00 00 00 00 00 00 00 00 00 00 15 45 = EPC ID [43, 44 + M + N] 45 E9 = EPC CRC [45,
	//   46 + M + N] 56 1D = Message CRC

	struct m6e_nano_data *data = (struct m6e_nano_data *)dev->data;
	uint8_t *msg = data->response.data;
	uint8_t msgLength = msg[1] + 7; // Add 7 (the header, length, opcode, status, and CRC) to
					// the LEN field to get total bytes
	LOG_DBG("Msg length: %d", msgLength);
	uint8_t opCode = msg[2];
	uint16_t messageCRC = _calculate_crc(
		&msg[1],
		msgLength - 3); // Ignore header (start spot 1), remove 3 bytes (header + 2 CRC)
	if ((msg[msgLength - 2] != (messageCRC >> 8)) ||
	    (msg[msgLength - 1] != (messageCRC & 0xFF))) {
		LOG_WRN("CRC error.");
		return (ERROR_CORRUPT_RESPONSE);
	}

	if (opCode == TMR_SR_OPCODE_READ_TAG_ID_MULTIPLE) {
		switch (msg[1]) {
		case 0x00:
			uint16_t statusMsg = 0;
			for (uint8_t x = 0; x < 2; x++) {
				statusMsg |= (uint32_t)msg[3 + x] << (8 * (1 - x));
			}

			if (statusMsg == 0x0400) {
				return (RESPONSE_IS_KEEPALIVE);
			} else if (statusMsg == 0x0504) {
				return (RESPONSE_IS_TEMPTHROTTLE);
			} else {
				return (RESPONSE_IS_UNKNOWN);
			}
		case 0x08:
			return (RESPONSE_IS_UNKNOWN);
		case 0x0a:
			return (RESPONSE_IS_TEMPERATURE);
		default:
			return (RESPONSE_IS_TAGFOUND);
		}
	} else {
		// m6e_nano_uart_flush(dev);
		return (ERROR_UNKNOWN_OPCODE);
	}
}

/**
 * @brief Initialize the M6E Nano.
 *
 * @param dev UART peripheral device.
 */
static int m6e_nano_init(const struct device *dev)
{
	const struct m6e_nano_config *cfg = dev->config;
	struct m6e_nano_data *drv_data = dev->data;

	if (!device_is_ready(cfg->uart_dev)) {
		LOG_ERR("Bus device is not ready");
		return -ENODEV;
	}

	while (uart_irq_rx_ready(cfg->uart_dev)) {
		m6e_nano_uart_flush(cfg->uart_dev);
	}

	struct m6e_nano_buf *rx = &drv_data->response;
	rx->len = 0;
	drv_data->status = RESPONSE_STARTUP;

	uart_irq_callback_user_data_set(cfg->uart_dev, uart_rx_handler, (void *)dev);
	uart_irq_rx_enable(cfg->uart_dev);

	return 0;
}

const static struct m6e_nano_api api = {
	// .send_command = user_send_command,
	.set_callback = user_set_command_callback,
};

#define M6E_NANO_DEFINE(inst)                                                                      \
	static struct m6e_nano_data m6e_nano_data_##inst = {                                       \
		.response.msg_len = 255,                                                           \
	};                                                                                         \
	static const struct m6e_nano_config m6e_nano_config_##inst = {                             \
		.uart_dev = DEVICE_DT_GET(DT_INST_BUS(inst)),                                      \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, &m6e_nano_init, NULL, &m6e_nano_data_##inst,                   \
			      &m6e_nano_config_##inst, POST_KERNEL, M6E_NANO_INIT_PRIORITY, &api);

DT_INST_FOREACH_STATUS_OKAY(M6E_NANO_DEFINE)