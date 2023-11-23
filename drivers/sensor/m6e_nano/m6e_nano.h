#include <zephyr/kernel.h>
#include <zephyr/device.h>

#ifndef M6E_NANO_H
#define M6E_NANO_H

#define MAX_MSG_SIZE       255
#define MAX_NUMBER_OF_TAGS 150

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
#define ALL_GOOD                       0
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

uint8_t m6e_nano_payload[MAX_MSG_SIZE]; // Array to hold the outgoing message
bool _printDebug = false;               // Flag to print UART commands and responses to serial

enum sensor_channel_m6e_nano {
	/** Fingerprint template count, ID number for enrolling and searching*/
	SENSOR_CHAN_RFID = SENSOR_CHAN_PRIV_START,
};

enum sensor_attribute_m6e_nano {
	/** Add values to the sensor which are having record storage facility */
	SENSOR_ATTR_M6E_RD_PWR = SENSOR_ATTR_PRIV_START,
	/** To find requested data in record storage */
	SENSOR_ATTR_M6E_WR_PWR,
	/** To delete mentioned data from record storage */
	SENSOR_ATTR_M6E_REGION,
	SENSOR_ATTR_M6E_ANT_PORT,
	SENSOR_ATTR_M6E_ANT_LIST,
	SENSOR_ATTR_M6E_REGION,
	SENSOR_ATTR_M6E_TAG_PROTOCOL,
};

struct m6e_nano_data {
	uint16_t rx_buf[MAX_MSG_SIZE];
	uint16_t tags[MAX_NUMBER_OF_TAGS][12]; // Assumes EPC won't be longer than 12 bytes
	int16_t tagRSSI[MAX_NUMBER_OF_TAGS];
	uint16_t uniqueTags;
	uint16_t success;
};

struct m6e_nano_config {
	const struct device *uart_dev;
};

#endif // M6E_NANO_PERIPHERAL_H