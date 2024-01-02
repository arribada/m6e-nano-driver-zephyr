#include <zephyr/kernel.h>
#include <zephyr/device.h>

#ifndef M6E_NANO_H
#define M6E_NANO_H

#define M6E_NANO_BUF_SIZE 255
#define M6E_NANO_MAX_TAGS 150

// header for M6E Nano module
#define TMR_START_HEADER 0xFF

// Op codes for M6E Nano module
#define TMR_SR_OPCODE_VERSION                    0x03
#define TMR_SR_OPCODE_SET_BAUD_RATE              0x06
#define TMR_SR_OPCODE_READ_TAG_ID_SINGLE         0x21
#define TMR_SR_OPCODE_READ_TAG_ID_MULTIPLE       0x22
#define TMR_SR_OPCODE_WRITE_TAG_ID               0x23
#define TMR_SR_OPCODE_WRITE_TAG_DATA             0x24
#define TMR_SR_OPCODE_KILL_TAG                   0x26
#define TMR_SR_OPCODE_READ_TAG_DATA              0x28
#define TMR_SR_OPCODE_CLEAR_TAG_ID_BUFFER        0x2A
#define TMR_SR_OPCODE_MULTI_PROTOCOL_TAG_OP      0x2F
#define TMR_SR_OPCODE_GET_READ_TX_POWER          0x62
#define TMR_SR_OPCODE_GET_WRITE_TX_POWER         0x64
#define TMR_SR_OPCODE_GET_USER_GPIO_INPUTS       0x66
#define TMR_SR_OPCODE_GET_POWER_MODE             0x68
#define TMR_SR_OPCODE_GET_READER_OPTIONAL_PARAMS 0x6A
#define TMR_SR_OPCODE_GET_PROTOCOL_PARAM         0x6B
#define TMR_SR_OPCODE_SET_ANTENNA_PORT           0x91
#define TMR_SR_OPCODE_SET_TAG_PROTOCOL           0x93
#define TMR_SR_OPCODE_SET_READ_TX_POWER          0x92
#define TMR_SR_OPCODE_SET_WRITE_TX_POWER         0x94
#define TMR_SR_OPCODE_SET_USER_GPIO_OUTPUTS      0x96
#define TMR_SR_OPCODE_SET_REGION                 0x97
#define TMR_SR_OPCODE_SET_READER_OPTIONAL_PARAMS 0x9A
#define TMR_SR_OPCODE_SET_PROTOCOL_PARAM         0x9B

#define COMMAND_TIME_OUT 2000 // Number of ms before stop waiting for response from module

// Define all the ways functions can return
#define RESPONSE_PENDING               0
#define ERROR_COMMAND_RESPONSE_TIMEOUT 1
#define ERROR_CORRUPT_RESPONSE         2
#define ERROR_WRONG_OPCODE_RESPONSE    3
#define ERROR_UNKNOWN_OPCODE           4
#define RESPONSE_IS_TEMPERATURE        5
#define RESPONSE_IS_KEEPALIVE          6
#define RESPONSE_IS_TEMPTHROTTLE       7
#define RESPONSE_IS_TAGFOUND           8
#define RESPONSE_IS_NOTAGFOUND         9
#define RESPONSE_IS_UNKNOWN            10
#define RESPONSE_SUCCESS               11
#define RESPONSE_FAIL                  12
#define RESPONSE_CLEAR                 12

// Define the allowed regions - these set the internal freq of the module
#define REGION_INDIA        0x04
#define REGION_JAPAN        0x05
#define REGION_CHINA        0x06
#define REGION_EUROPE       0x08
#define REGION_KOREA        0x09
#define REGION_AUSTRALIA    0x0B
#define REGION_NEWZEALAND   0x0C
#define REGION_NORTHAMERICA 0x0D
#define REGION_OPEN         0xFF

static uint16_t crc_table[] = {
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
	0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
};

// Set command to be transmitted
typedef void (*m6e_nano_send_command_t)(const struct device *dev, const uint8_t *command,
					const uint8_t length);

// Callback
typedef void (*m6e_nano_callback_t)(const struct device *dev, void *user_data);

// Set the data callback function for the device
typedef void (*m6e_nano_set_callback_t)(const struct device *dev, m6e_nano_callback_t callback,
					void *user_data);

struct m6e_nano_api {
	m6e_nano_send_command_t send_command;
	m6e_nano_set_callback_t set_callback;
};

/**
 * @brief Set command to be transmitted.
 *
 * @param dev Pointer to the device structure.
 * @param command Command to be transmitted.
 * @param length Length of the command (excluding null byte).
 */
static inline void m6e_nano_send_command(const struct device *dev, const uint8_t *command,
					 const uint8_t length)
{
	struct m6e_nano_api *api = (struct m6e_nano_api *)dev->api;
	return api->send_command(dev, command, length);
}

/**
 * @brief Set the data callback function for the device
 *
 * @param dev Pointer to the device structure.
 * @param callback Callback function pointer.
 * @param user_data Pointer to data accessible from the callback function.
 */
static inline void m6e_nano_set_callback(const struct device *dev, m6e_nano_callback_t callback,
					 void *user_data)
{
	struct m6e_nano_api *api = (struct m6e_nano_api *)dev->api;
	return api->set_callback(dev, callback, user_data);
}

struct m6e_nano_buf {
	uint8_t data[M6E_NANO_BUF_SIZE];
	size_t len;
	size_t msg_len;
};

struct m6e_nano_epc {
	uint8_t len;
	uint16_t *epc;
};

struct m6e_nano_tag {
	long freq;
	long timestamp;
	struct m6e_nano_epc;
};

struct m6e_nano_data {
	bool debug;
	bool ready;

	struct m6e_nano_tag tag;

	u_int8_t status;
	struct m6e_nano_buf command;
	struct m6e_nano_buf response;
	bool has_response;

	m6e_nano_callback_t callback;
	void *user_data;
};

struct m6e_nano_config {
	struct m6e_nano_data *data; // Pointer to runtime data.
	const struct device *uart_dev;
};

static void user_send_command(const struct device *dev, uint8_t *command, const uint8_t length);
static void m6e_nano_construct_command(const struct device *dev, uint8_t opcode, uint8_t *data,
				       uint8_t size, uint16_t timeout);
static uint16_t calculate_crc(uint8_t *u8Buf, uint8_t len);

#endif // M6E_NANO_PERIPHERAL_H