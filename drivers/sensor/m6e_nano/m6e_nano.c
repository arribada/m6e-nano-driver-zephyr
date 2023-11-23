/*
 * Copyright (c) 2023 Arribada Initiative CIC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT thingmagic_m6enano

/* sensor m6e_nano.c - Driver for plantower PMS7003 sensor
 * PMS7003 product: http://www.plantower.com/en/content/?110.html
 * PMS7003 spec: http://aqicn.org/air/view/sensor/spec/pms7003.pdf
 */

#include <errno.h>

#include <zephyr/arch/cpu.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

#include "m6e_nano.h"

LOG_MODULE_REGISTER(M6E_NANO, CONFIG_SENSOR_LOG_LEVEL);

/* wait serial output with 1000ms timeout */
#define CFG_M6E_NANO_SERIAL_TIMEOUT 1000

/**
 * @brief wait for an array data from uart device with a timeout
 *
 * @param dev the uart device
 * @param data the data array to be matched
 * @param len the data array len
 * @param timeout the timeout in milliseconds
 * @return 0 if success; -ETIME if timeout
 */
static int uart_wait_for(const struct device *dev, uint8_t *data, int len, int timeout)
{
	int matched_size = 0;
	int64_t timeout_time = k_uptime_get() + timeout;

	while (1) {
		uint8_t c;

		if (k_uptime_get() > timeout_time) {
			return -ETIME;
		}

		if (uart_poll_in(dev, &c) == 0) {
			if (c == data[matched_size]) {
				matched_size++;

				if (matched_size == len) {
					break;
				}
			} else if (c == data[0]) {
				matched_size = 1;
			} else {
				matched_size = 0;
			}
		}
	}
	return 0;
}

/**
 * @brief read bytes from uart
 *
 * @param data the data buffer
 * @param len the data len
 * @param timeout the timeout in milliseconds
 * @return 0 if success; -ETIME if timeout
 */
static int uart_read_bytes(const struct device *dev, uint8_t *data, int len, int timeout)
{
	int read_size = 0;
	int64_t timeout_time = k_uptime_get() + timeout;

	while (1) {
		uint8_t c;

		if (k_uptime_get() > timeout_time) {
			return -ETIME;
		}

		if (uart_poll_in(dev, &c) == 0) {
			data[read_size++] = c;
			if (read_size == len) {
				break;
			}
		}
	}
	return 0;
}

static int send_command(const struct device *dev, uint8_t opcode, uint8_t *data, uint8_t size,
			uint16_t timeOut, bool wait_response)
{
	m6e_nano_payload[1] = size; // Load the length of this operation into msg array
	m6e_nano_payload[2] = opcode;

	// Copy the data into msg array
	for (uint8_t x = 0; x < size; x++) {
		m6e_nano_payload[3 + x] = data[x];
	}

	transmit_command(timeOut, wait_response); // Send and wait for response
	return 0;
}

static int transmit_command(const struct device *dev, uint8_t timeout, bool wait_response)
{
	const struct m6e_nano_config *cfg = dev->config;

	m6e_nano_payload[0] = 0xFF; // Universal header
	uint8_t messageLength = m6e_nano_payload[1];

	uint8_t opcode =
		m6e_nano_payload[2]; // Used to see if response from module has the same opcode

	// Attach CRC
	uint16_t crc = calculateCRC(
		&m6e_nano_payload[1],
		messageLength +
			2); // Calc CRC starting from spot 1, not 0. Add 2 for LEN and OPCODE bytes.
	m6e_nano_payload[messageLength + 3] = crc >> 8;
	m6e_nano_payload[messageLength + 4] = crc & 0xFF;

	// Used for debugging: Does the user want us to print the command to serial port?
	if (_printDebug == true) {
		LOG_DBG("Sending command: %s")
		_debugSerial->print(F("sendCommand: "));
		printMessageArray();
	}

	// Remove anything in the incoming buffer
	// TODO this is a bad idea if we are constantly readings tags
	while (_nanoSerial->available()) {
		_nanoSerial->read();
	}

	// Send the command to the module
	// for (uint8_t x = 0; x < messageLength + 5; x++) {
	uart_tx(cfg->uart_dev, m6e_nano_payload, messageLength + 5, timeout);
	// 	_nanoSerial->write(m6e_nano_payload[x]);
	// }

	// There are some commands (setBaud) that we can't or don't want the response
	if (wait_response == false) {
		// _nanoSerial->flush(); // Wait for serial sending to complete
		if (uart_irq_tx_complete(cfg->uart_dev) == 1) {
			return 0;
		}
	}

	// For debugging, probably remove
	// for (uint8_t x = 0 ; x < 100 ; x++) m6e_nano_payload[x] = 0;

	// Wait for response with timeout
	uint32_t startTime = millis();
	while (_nanoSerial->available() == false) {
		if (millis() - startTime > timeOut) {
			if (_printDebug == true) {
				_debugSerial->println(F("Time out 1: No response from module"));
			}
			m6e_nano_payload[0] = ERROR_COMMAND_RESPONSE_TIMEOUT;
			return;
		}
		delay(1);
	}

	// Layout of response in data array:
	// [0] [1] [2] [3]      [4]      [5] [6]  ... [LEN+4] [LEN+5] [LEN+6]
	// FF  LEN OP  STATUSHI STATUSLO xx  xx   ... xx      CRCHI   CRCLO
	messageLength = MAX_MSG_SIZE -
			1; // Make the max length for now, adjust it when the actual len comes in
	uint8_t spot = 0;
	while (spot < messageLength) {
		if (millis() - startTime > timeOut) {
			if (_printDebug == true) {
				_debugSerial->println(F("Time out 2: Incomplete response"));
			}

			m6e_nano_payload[0] = ERROR_COMMAND_RESPONSE_TIMEOUT;
			return;
		}

		if (_nanoSerial->available()) {
			m6e_nano_payload[spot] = _nanoSerial->read();

			if (spot == 1) { // Grab the length of this response (spot 1)
				messageLength = m6e_nano_payload[1] +
						7; // Actual length of response is ? + 7 for extra
						   // stuff (header, Length, opcode, 2 status
						   // bytes, ..., 2 bytes CRC = 7)
			}

			spot++;

			// There's a case were we miss the end of one message and spill into another
			// message. We don't want spot pointing at an illegal spot in the array
			spot %= MAX_MSG_SIZE; // Wrap condition
		}
	}

	// Used for debugging: Does the user want us to print the command to serial port?
	if (_printDebug == true) {
		_debugSerial->print(F("response: "));
		printMessageArray();
	}

	// Check CRC
	crc = calculateCRC(&m6e_nano_payload[1],
			   messageLength - 3); // Remove header, remove 2 crc bytes
	if ((m6e_nano_payload[messageLength - 2] != (crc >> 8)) ||
	    (m6e_nano_payload[messageLength - 1] != (crc & 0xFF))) {
		m6e_nano_payload[0] = ERROR_CORRUPT_RESPONSE;
		if (_printDebug == true) {
			_debugSerial->println(F("Corrupt response"));
		}
		return;
	}

	// If crc is ok, check that opcode matches (did we get a response to the command we sent or
	// a different one?)
	if (m6e_nano_payload[2] != opcode) {
		m6e_nano_payload[0] = ERROR_WRONG_OPCODE_RESPONSE;
		if (_printDebug == true) {
			_debugSerial->println(F("Wrong opcode response"));
		}
		return;
	}

	// If everything is ok, load all ok into msg array
	m6e_nano_payload[0] = ALL_GOOD;
	return 0;
}

static int m6e_nano_parse_buffer(uint8_t *data)
{
	// TODO: Parse sensor data
	// drv_data->pm_1_0 = (m6e_nano_receive_buffer[8] << 8) + m6e_nano_receive_buffer[9];

	return 0;
}

static int m6e_nano_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct m6e_nano_data *drv_data = dev->data;
	const struct m6e_nano_config *cfg = dev->config;

	/* sample output */
	/* 42 4D 00 1C 00 01 00 01 00 01 00 01 00 01 00 01 01 92
	 * 00 4E 00 03 00 00 00 00 00 00 71 00 02 06
	 */

	uint8_t m6e_nano_start_bytes[] = {0x42, 0x4d};
	uint8_t m6e_nano_receive_buffer[30];

	if (uart_wait_for(cfg->uart_dev, m6e_nano_start_bytes, sizeof(m6e_nano_start_bytes),
			  CFG_M6E_NANO_SERIAL_TIMEOUT) < 0) {
		LOG_WRN("waiting for start bytes is timeout");
		return -ETIME;
	}

	if (uart_read_bytes(cfg->uart_dev, m6e_nano_receive_buffer, 30,
			    CFG_M6E_NANO_SERIAL_TIMEOUT) < 0) {
		return -ETIME;
	}

	m6e_nano_parse_buffer(m6e_nano_receive_buffer);

	LOG_DBG("Unique Tags found = %d", drv_data->uniqueTags);
	for (size_t i = drv_data->uniqueTags; i < drv_data->uniqueTags; i++) {
		LOG_DBG("Tags = %x, RSSI = %d\n", (unsigned int)drv_data->tags[i],
			drv_data->tagRSSI[i]);
	}

	return 0;
}

static int m6e_nano_channel_get(const struct device *dev, enum sensor_channel chan,
				struct sensor_value *val)
{
	struct m6e_nano_data *drv_data = dev->data;

	if (chan == SENSOR_CHAN_PM_1_0) {
		val->val1 = drv_data->pm_1_0;
		val->val2 = 0;
	} else if (chan == SENSOR_CHAN_PM_2_5) {
		val->val1 = drv_data->pm_2_5;
		val->val2 = 0;
	} else if (chan == SENSOR_CHAN_PM_10) {
		val->val1 = drv_data->pm_10;
		val->val2 = 0;
	} else {
		return -ENOTSUP;
	}
	return 0;
}

static int m6e_nano_attr_set(const struct device *dev, enum sensor_channel chan,
			     enum sensor_attribute attr, const struct sensor_value *val)
{
	if ((enum sensor_channel_m6e_nano)chan != SENSOR_CHAN_RFID) {
		LOG_ERR("Channel not supported");
		return -ENOTSUP;
	}

	switch ((enum sensor_attribute_m6e_nano)attr) {
	case SENSOR_ATTR_R502A_RECORD_ADD:
		return fps_enroll(dev, val);
	case SENSOR_ATTR_R502A_RECORD_DEL:
		return fps_delete(dev, val);
	case SENSOR_ATTR_R502A_RECORD_EMPTY:
		return fps_empty_db(dev);
	default:
		LOG_ERR("Sensor attribute not supported");
		return -ENOTSUP;
	}
}

static int m6e_nano_attr_get(const struct device *dev, enum sensor_channel chan,
			     enum sensor_attribute attr, struct sensor_value *val)
{
	int ret;
	struct grow_r502a_data *drv_data = dev->data;

	if ((enum sensor_channel_grow_r502a)chan != SENSOR_CHAN_FINGERPRINT) {
		LOG_ERR("Channel not supported");
		return -ENOTSUP;
	}

	switch ((enum sensor_attribute_grow_r502a)attr) {
	case SENSOR_ATTR_R502A_RECORD_FIND:
		ret = fps_match(dev, val);
		break;
	case SENSOR_ATTR_R502A_RECORD_FREE_IDX:
		ret = fps_read_template_table(dev);
		val->val1 = drv_data->free_idx;
		break;
	default:
		LOG_ERR("Sensor attribute not supported");
		ret = -ENOTSUP;
		break;
	}

	return ret;
}

static const struct sensor_driver_api m6e_nano_api = {
	.sample_fetch = &m6e_nano_sample_fetch,
	.channel_get = &m6e_nano_channel_get,
	.attr_set = &m6e_nano_attr_set,
	.attr_get = &m6e_nano_attr_get,
};

static int m6e_nano_init(const struct device *dev)
{
	const struct m6e_nano_config *cfg = dev->config;

	if (!device_is_ready(cfg->uart_dev)) {
		LOG_ERR("Bus device is not ready");
		return -ENODEV;
	}

	return 0;
}

#define M6E_NANO_DEFINE(inst)                                                                      \
	static struct m6e_nano_data m6e_nano_data_##inst;                                          \
                                                                                                   \
	static const struct m6e_nano_config m6e_nano_config_##inst = {                             \
		.uart_dev = DEVICE_DT_GET(DT_INST_BUS(inst)),                                      \
	};                                                                                         \
                                                                                                   \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, &m6e_nano_init, NULL, &m6e_nano_data_##inst,            \
				     &m6e_nano_config_##inst, POST_KERNEL,                         \
				     CONFIG_SENSOR_INIT_PRIORITY, &m6e_nano_api);

DT_INST_FOREACH_STATUS_OKAY(M6E_NANO_DEFINE)

// #define GROW_R502A_INIT(index)									\
// 	static struct grow_r502a_data grow_r502a_data_##index;					\
// 												\
// 	static struct grow_r502a_config grow_r502a_config_##index = {				\
// 		.dev = DEVICE_DT_GET(DT_INST_BUS(index)),					\
// 		.comm_addr = DT_INST_REG_ADDR(index),						\
// 		IF_ENABLED(CONFIG_GROW_R502A_GPIO_POWER,					\
// 		(.vin_gpios = GPIO_DT_SPEC_INST_GET_OR(index, vin_gpios, {}),			\
// 		 .act_gpios = GPIO_DT_SPEC_INST_GET_OR(index, act_gpios, {}),))			\
// 		IF_ENABLED(CONFIG_GROW_R502A_TRIGGER,						\
// 		(.int_gpios = GPIO_DT_SPEC_INST_GET_OR(index, int_gpios, {}),))			\
// 	};											\
// 												\
// 	DEVICE_DT_INST_DEFINE(index, &grow_r502a_init, NULL, &grow_r502a_data_##index,		\
// 			      &grow_r502a_config_##index, POST_KERNEL,				\
// 			      CONFIG_SENSOR_INIT_PRIORITY, &grow_r502a_api);

// DT_INST_FOREACH_STATUS_OKAY(GROW_R502A_INIT)

// #define EXAMPLESENSOR_INIT(i)						       \
// 	static struct examplesensor_data examplesensor_data_##i;	       \
// 									       \
// 	static const struct examplesensor_config examplesensor_config_##i = {  \
// 		.input = GPIO_DT_SPEC_INST_GET(i, input_gpios),		       \
// 	};								       \
// 									       \
// 	DEVICE_DT_INST_DEFINE(i, examplesensor_init, NULL,		       \
// 			      &examplesensor_data_##i,			       \
// 			      &examplesensor_config_##i, POST_KERNEL,	       \
// 			      CONFIG_SENSOR_INIT_PRIORITY, &examplesensor_api);

// DT_INST_FOREACH_STATUS_OKAY(EXAMPLESENSOR_INIT)
