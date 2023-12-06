/*
 * Copyright (c) 2023 Arribada Initiative CIC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT          thingmagic_m6enano
#define M6E_NANO_INIT_PRIORITY 41

/* sensor m6e_nano.c - Driver for plantower PMS7003 sensor
 * PMS7003 product: http://www.plantower.com/en/content/?110.html
 * PMS7003 spec: http://aqicn.org/air/view/sensor/spec/pms7003.pdf
 */

#include <errno.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/drivers/uart.h>

#include "m6e_nano.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(M6E_NANO, CONFIG_M6E_NANO_LOG_LEVEL);

/* wait serial output with 1000ms timeout */
#define CFG_M6E_NANO_SERIAL_TIMEOUT 1000

static void m6e_nano_print_hex(uint8_t *msg, uint8_t len)
{
	uint8_t amtToPrint = msg[1] + 5;
	if (amtToPrint > M6E_NANO_BUF_SIZE) {
		amtToPrint = M6E_NANO_BUF_SIZE; // Limit this size
	}

	for (uint16_t x = 0; x < amtToPrint; x++) {
		printk(" [");
		if (msg[x] < 0x10) {
			printk("0");
		}
		printk("msg[%x]", x);
		printk("]");
	}
	printk("\n");
}

/**
 * @brief Empty the RX buffer of the UART peripheral.
 *
 * @param dev UART peripheral device.
 */
static void m6e_nano_uart_flush(const struct device *dev)
{
	struct m6e_nano_data *drv_data = dev->data;
	uint8_t c;

	while (uart_fifo_read(dev, &c, 1) > 0) {
		continue;
	}

	memset(&drv_data->response.data, 0, M6E_NANO_BUF_SIZE);

	LOG_DBG("UART RX buffer flushed.");
}

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
// See parseResponse for breakdown of fields
// Pulls the number of EPC bytes out of the response
// Often this is 12 bytes
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

uint8_t m6e_nano_get_tag_rssi(const struct device *dev)
{
	struct m6e_nano_data *data = (struct m6e_nano_data *)dev->data;
	uint8_t *msg = data->response.data;
	uint8_t rssi = msg[12] - 256;
	return rssi;
}

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

void m6e_nano_disable_read_filter(const struct device *dev)
{
	m6e_nano_set_config(dev, 0x0C, 0x00); // Disable read filter
}

void m6e_nano_set_config(const struct device *dev, uint8_t option1, uint8_t option2)
{
	uint8_t data[3];

	// These are parameters gleaned from inspecting the 'Transport Logs' of the Universal Reader
	// Assistant And from serial_reader_l3.c
	data[0] = 1; // Key value form of command
	data[1] = option1;
	data[2] = option2;

	m6e_nano_construct_command(dev, TMR_SR_OPCODE_SET_READER_OPTIONAL_PARAMS, data,
				   sizeof(data), COMMAND_TIME_OUT);
}

/**
 * @brief Stop a continuous read operation.
 *
 * @param dev UART peripheral device.
 */
void m6e_nano_stop_read(const struct device *dev)
{
	uint8_t data[] = {0x00, 0x00, 0x02};

	m6e_nano_construct_command(dev, TMR_SR_OPCODE_SET_BAUD_RATE, data, sizeof(data),
				   COMMAND_TIME_OUT);
}

void m6e_nano_set_antenna_port(const struct device *dev)
{
	uint8_t data[2] = {0x01, 0x01};
	m6e_nano_construct_command(dev, TMR_SR_OPCODE_SET_ANTENNA_PORT, data, sizeof(data),
				   COMMAND_TIME_OUT);
}

void m6e_nano_set_read_power(const struct device *dev, uint16_t power)
{
	if (power > 2700) {
		LOG_DBG("Power too high, limiting to 27dBm.");
		power = 2700; // Limit to 27dBm
	}

	// Copy this setting into a temp data array
	uint8_t size = sizeof(power);
	uint8_t data[size];
	for (uint8_t x = 0; x < size; x++) {
		data[x] = (uint8_t)(power >> (8 * (size - 1 - x)));
	}

	m6e_nano_construct_command(dev, TMR_SR_OPCODE_SET_READ_TX_POWER, data, size,
				   COMMAND_TIME_OUT);
}

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
				   COMMAND_TIME_OUT);
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

	m6e_nano_construct_command(dev, TMR_SR_OPCODE_SET_REGION, &region, sizeof(region),
				   COMMAND_TIME_OUT);
}

void m6e_nano_get_version(const struct device *dev)
{
	uint8_t data[] = {};

	m6e_nano_construct_command(dev, TMR_SR_OPCODE_VERSION, data, sizeof(data),
				   COMMAND_TIME_OUT);
}

void m6e_nano_set_tag_protocol(const struct device *dev, uint8_t protocol)
{
	uint8_t data[2];
	data[0] = 0; // Opcode expects 16-bits
	data[1] = protocol;

	m6e_nano_construct_command(dev, TMR_SR_OPCODE_SET_TAG_PROTOCOL, data, sizeof(data),
				   COMMAND_TIME_OUT);
}

void m6e_nano_get_write_power(const struct device *dev)
{
	uint8_t data[] = {0x00}; // Just return power
	// uint8_t data[] = {0x01}; // Return power with limits

	m6e_nano_construct_command(dev, TMR_SR_OPCODE_GET_WRITE_TX_POWER, data, sizeof(data),
				   COMMAND_TIME_OUT);
}

/**
 * @brief Set the baud rate of the M6E Nano.
 *
 * @param dev UART peripheral device.
 * @param baud_rate baud rate to set.
 */
void m6e_nano_set_baud(const struct device *dev, long baud_rate)
{
	uint8_t size = sizeof(baud_rate);
	uint8_t data[size];
	for (uint8_t x = 0; x < size; x++) {
		data[x] = (uint8_t)(baud_rate >> (8 * (size - 1 - x)));
	}

	LOG_DBG("Baud rate: %ld", baud_rate);

	m6e_nano_construct_command(dev, TMR_SR_OPCODE_SET_BAUD_RATE, data, size, COMMAND_TIME_OUT);
}

/**
 * @brief Send a generic command to the M6E Nano.
 *
 * @param dev UART peripheral device.
 * @param data Data to be sent.
 */
void m6e_nano_send_generic_command(const struct device *dev, uint8_t *command, uint8_t size,
				   uint8_t *opcode)
{
	m6e_nano_construct_command(dev, opcode, command, size, COMMAND_TIME_OUT);
}

/**
 * @brief Construct the command to be transmitted by the UART peripheral.
 *
 * @param dev UART peripheral device.
 * @param region region to set.
 */
static void m6e_nano_construct_command(const struct device *dev, uint8_t opcode, uint8_t *data,
				       uint8_t size, uint16_t timeout)
{
	uint8_t command[size + 5];
	command[0] = 0xFF; // universal header
	command[1] = size; // load the length of this operation into msg array
	command[2] = opcode;
	command[3] = data;

	// Copy the data into msg array
	for (uint8_t x = 0; x < size; x++) {
		command[3 + x] = data[x];
	}

	// calculate the CRC
	uint16_t crc = calculate_crc(&command[1], size + 2);
	command[size + 3] = crc >> 8;
	command[size + 4] = crc & 0xFF;

	size_t length = sizeof(command) / sizeof(*command);

	user_send_command(dev, command, length); // Send and wait for response
}

/**
 * @brief Set the command to be transmitted by the UART peripheral.
 *
 * @param dev UART peripheral device.
 * @param command Command to be transmitted.
 * @param length Length of the command.
 */
static void user_send_command(const struct device *dev, uint8_t *command, const uint8_t length)
{

	struct m6e_nano_data *data = (struct m6e_nano_data *)dev->data;
	const struct m6e_nano_config *cfg = dev->config;
	struct m6e_nano_buf *tx = &data->command;

	memset(tx->data, 0, M6E_NANO_BUF_SIZE);

	memcpy(tx->data, command, sizeof(uint8_t) * length);
	tx->len = length;
	LOG_DBG("Length of command: %d", tx->len);

	__ASSERT(tx->len <= 255, "Command length too long.");

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

	// uart_irq_tx_enable(cfg->uart_dev);

	for (size_t i = 0; i < tx->len; i++) {
		// printk("[%X] ", buf[i]);
		// uart_tx(cfg->uart_dev, tx->data[i], tx->len, 300);
		uart_poll_out(cfg->uart_dev, tx->data[i]);
	}

	// char buf[5] = {0xFF, 0x00, 0x03, 0x1D, 0x0C};
	// for (size_t i = 0; i < 5; i++) {
	// 	uart_poll_out(cfg->uart_dev, buf[i]);
	// }

	// uart_irq_rx_enable(cfg->uart_dev);

	// uart_poll_out(cfg->uart_dev, '\0');
	// LOG_DBG("Command sent.");
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

const static struct m6e_nano_api api = {
	.send_command = user_send_command,
	.set_callback = user_set_command_callback,
};

/**
 * @brief Calculate the CRC of the command being transmitted.
 *
 * @param u8Buf Partial of the command to calculate the CRC for.
 * @param len Length of the partial command.
 * @return uint16_t CRC of the command. To be transmitted as the last two bytes of the command.
 */
static uint16_t calculate_crc(uint8_t *u8Buf, uint8_t len)
{
	uint16_t crc = 0xFFFF;

	for (uint8_t i = 0; i < len; i++) {
		crc = ((crc << 4) | (u8Buf[i] >> 4)) ^ crc_table[crc >> 12];
		crc = ((crc << 4) | (u8Buf[i] & 0x0F)) ^ crc_table[crc >> 12];
	}

	return crc;
}

uint8_t m6e_nano_get_status(const struct device *dev)
{
	struct m6e_nano_data *data = (struct m6e_nano_data *)dev->data;
	return data->status;
}

/**
 * @brief Handler for when the UART peripheral becomes ready to transmit.
 *
 * @param dev UART peripheral device.
 */
static void uart_cb_tx_handler(const struct device *dev)
{
	const struct m6e_nano_config *config = dev->config;
	struct m6e_nano_data *drv_data = dev->data;
	int sent = 0;
	uint8_t retries = 3;
	int count = 0;
	LOG_DBG("len: %d", drv_data->command.len);

	while (drv_data->command.len) {
		sent = uart_fifo_fill(config->uart_dev, &drv_data->command.data[sent],
				      drv_data->command.len);
		if (sent) {
			count++;
			LOG_DBG("count: %d", count);
		}
		drv_data->command.len -= sent;
	}

	LOG_DBG("new len: %d", drv_data->command.len);

	while (retries--) {
		if (uart_irq_tx_complete(config->uart_dev)) {
			LOG_DBG("tx complete:");

			uart_irq_tx_disable(config->uart_dev);
			drv_data->command.len = 0;
			uart_irq_rx_enable(config->uart_dev);
			break;
		}
	}
}

/**
 * @brief Handler for when the UART peripheral receives data.
 *
 * @param dev UART peripheral device.
 * @param user_data Driver device passed to provide access to buffers.
 */
static void uart_cb_handler(const struct device *dev, void *user_data)
{
	const struct device *m6e_nano_dev = user_data;
	struct m6e_nano_data *drv_data = m6e_nano_dev->data;
	// struct m6e_nano_data *drv_data = dev->data;

	int len, pkt_sz = 0;
	int offset = drv_data->response.len;
	m6e_nano_callback_t callback = drv_data->callback;

	if ((uart_irq_update(dev) > 0) && (uart_irq_is_pending(dev) > 0)) {
		while (uart_irq_rx_ready(dev)) {

			len = uart_fifo_read(dev, &drv_data->response.data[offset], 255 - offset);

			switch (offset) {
			case 0:
				if (drv_data->response.data[offset] == TMR_START_HEADER) {
					LOG_DBG("Msg Header: %X", drv_data->response.data[offset]);
					break;
				} else if (drv_data->response.data[offset] ==
					   ERROR_COMMAND_RESPONSE_TIMEOUT) {
					LOG_WRN("COMMAND RESPONSE TIMEOUT");
					drv_data->status = ERROR_COMMAND_RESPONSE_TIMEOUT;
				}
				len = 0;
				offset = 0;
				break;
			case 1:
				drv_data->response.msg_len = drv_data->response.data[offset] + 7;
				LOG_DBG("Msg Total Len: %d", drv_data->response.msg_len);
				break;
			case 2:
				LOG_DBG("Msg Opcode: %x", drv_data->response.data[offset]);
				break;
			default:
				break;
			}

			offset += len;
			drv_data->response.len = offset;
			break;
		}
	}

	if (offset > drv_data->response.msg_len - 1) {
		drv_data->response.len = 0;
		// for (size_t i = 0; i < drv_data->response.msg_len; i++) {
		// 	printk("[%X] ", drv_data->response.data[i]);
		// }

		drv_data->status = RESPONSE_PENDING;
		if (callback != NULL) {
			callback(dev, user_data);
		}
		LOG_DBG("Response received.");
	} else if (offset > M6E_NANO_BUF_SIZE) {
		drv_data->response.len = 0;
		drv_data->status = RESPONSE_FAIL;
		m6e_nano_uart_flush(dev); // Flush the buffer on overflow
		LOG_WRN("Response too long, %d.", offset);
	}
}

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
	// for (uint8_t x = 0; x < msgLength; x++) {
	// 	printk("%X ", msg[x]);
	// }
	// printk("\n");
	// Check the CRC on this response
	uint16_t messageCRC = calculate_crc(
		&msg[1],
		msgLength - 3); // Ignore header (start spot 1), remove 3 bytes (header + 2 CRC)
	if ((msg[msgLength - 2] != (messageCRC >> 8)) ||
	    (msg[msgLength - 1] != (messageCRC & 0xFF))) {
		LOG_WRN("CRC error.");
		return (ERROR_CORRUPT_RESPONSE);
	}

	if (opCode == TMR_SR_OPCODE_READ_TAG_ID_MULTIPLE) // opCode = 0x22
	{
		// Based on the record length identify if this is a tag record, a temperature sensor
		// record, or a keep-alive?
		if (msg[1] == 0x00) // Keep alive
		{
			// We have a Read cycle reset/keep-alive message
			// Sent once per second
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
		} else if (msg[1] == 0x08) // Unknown
		{
			return (RESPONSE_IS_UNKNOWN);
		} else if (msg[1] == 0x0a) // temperature
		{
			return (RESPONSE_IS_TEMPERATURE);
		} else // Full tag record
		{
			// This is a full tag response
			// User can now pull out RSSI, frequency of tag, timestamp, EPC, Protocol
			// control bits, EPC CRC, CRC
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

	// enables debugging
	// TODO: make this into a KConfig option
	if (CONFIG_M6E_NANO_LOG_LEVEL >= LOG_LEVEL_DBG) {
		LOG_DBG("Debugging for UART packet logging is enabled.");
		drv_data->debug = true;
	}

	// flush the uart rx buffer
	m6e_nano_uart_flush(cfg->uart_dev);
	struct m6e_nano_buf *rx = &drv_data->response;
	rx->len = 0;
	drv_data->status = RESPONSE_CLEAR;

	uart_irq_callback_user_data_set(cfg->uart_dev, uart_cb_handler, (void *)dev);
	uart_irq_rx_enable(cfg->uart_dev);

	return 0;
}

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