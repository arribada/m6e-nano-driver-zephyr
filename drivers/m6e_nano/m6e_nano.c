
#include <devicetree.h>
#include <drivers/gpio.h>
#include <drivers/uart.h>
#include <string.h>

#include <errno.h>

#include "m6e-nano.h"

// Compatible with "thing_magic,m6e_nano"
#define DT_DRV_COMPAT          thing_magic_m6e_nano
#define M6E_NANO_INIT_PRIORITY 41

#include <logging/log.h>
LOG_MODULE_REGISTER(m6e_nano, CONFIG_M6E_NANO_LOG_LEVEL);

/**
 * @brief Contains runtime mutable data for the UART peripheral.
 */
struct m6e_nano_data {
	char cmd[CONFIG_M6E_NANO_MAX_STR_LEN +
		 1]; // Command to be transmitted. +1 for null terminator.
	uint8_t rx_buf[CONFIG_M6E_NANO_RX_BUF_SIZE]; // Buffer to hold received data.
	size_t rx_data_len;                          // Length of currently received data.
	m6e_nano_peripheral_callback_t
		command_callback; // Callback function to be called when a string is received.
	void *user_data;          // User data to be passed to the callback function.
};

/**
 * @brief Build time configurations for the UART peripheral.
 */
struct m6e_nano_conf {
	struct m6e_nano_data *data;          // Pointer to runtime data.
	const struct device *uart_dev;       // UART device.
	const struct gpio_dt_spec gpio_spec; // GPIO spec for pin used to start transmitting.
	struct gpio_callback gpio_cb;        // GPIO callback
};

/**
 * @brief Set the command to be transmitted by the UART peripheral.
 *
 * @param dev UART peripheral device.
 * @param string New command.
 * @param length Length of command excluding NULL terminator.
 */
static void m6e_nano_set_command(const struct device *dev, const char *command, size_t length)
{
	struct m6e_nano_data *data = (struct m6e_nano_data *)dev->data;
	__ASSERT(length <= CONFIG_M6E_NANO_MAX_STR_LEN, "String length too long.");
	memcpy(data->cmd, command, length);
	data->cmd[length] = '\0';
}

/**
 * @brief Set callback function to be called when a string is received.
 *
 * @param dev UART peripheral device.
 * @param callback New callback function.
 * @param user_data Data to be passed to the callback function.
 */
static void m6e_nano_set_response_callback(const struct device *dev,
					   m6e_nano_peripheral_callback_t callback, void *user_data)
{
	struct m6e_nano_data *data = (struct m6e_nano_data *)dev->data;
	data->string_callback = callback;
	data->user_data = user_data;
}

const static struct m6e_nano_peripheral_api api = {
	.set_command = m6e_nano_set_command,
	.set_callback = m6e_nano_set_response_callback,
};

/**
 * @brief Gpio callback that starts transmitting the command specified in data->cmd.
 *
 * @param port GPIO port that triggered the callback.
 * @param cb Gpio callback struct.
 * @param pins Pins that triggered the callback.
 */
static void send_command(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	LOG_DBG("Sending command");
	struct m6e_nano_conf *conf = CONTAINER_OF(cb, struct m6e_nano_conf, gpio_cb);
	struct m6e_nano_data *data = conf->data;
	size_t len = strlen(data->cmd);
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(conf->uart_dev, data->cmd[i]);
	}
	uart_poll_out(conf->uart_dev, '\0');
}

/**
 * @brief Handles UART interrupts.
 *
 * @param uart_dev UART bus device.
 * @param user_data Custom data. Should be a pointer to a m6e_nano_peripheral device.
 */
static void uart_int_handler(const struct device *uart_dev, void *user_data)
{
	uart_irq_update(uart_dev);
	const struct device *dev = (const struct device *)user_data; // Uart peripheral device.
	struct m6e_nano_data *data = dev->data;
	if (uart_irq_rx_ready(uart_dev)) {
		m6e_nano_peripheral_callback_t callback = data->command_callback;
		char c;
		while (!uart_poll_in(uart_dev, &c)) {
			data->rx_buf[data->rx_data_len] = c;
			data->rx_data_len++;
			size_t rx_buf_capacity = CONFIG_M6E_NANO_RX_BUF_SIZE - data->rx_data_len;
			if (c == 0) // String found.
			{
				if (callback != NULL) {
					callback(dev, data->rx_buf, data->rx_data_len, true,
						 data->user_data);
				}
				data->rx_data_len = 0;
				memset(data->rx_buf, 0, sizeof(data->rx_buf));
			} else if (rx_buf_capacity == 0) // Buffer full. No string found.
			{
				if (callback != NULL) {
					callback(dev, data->rx_buf, data->rx_data_len, false,
						 data->user_data);
				}
				data->rx_data_len = 0;
				memset(data->rx_buf, 0, sizeof(data->rx_buf));
			}
		}
	}
}

/**
 * @brief Initializes the uart bus for the UART peripheral.
 *
 * @param dev UART peripheral device.
 * @return 0 if successful, negative errno value otherwise.
 */
static int init_uart(const struct device *dev)
{
	struct m6e_nano_conf *conf = (struct m6e_nano_conf *)dev->config;
	uart_irq_callback_user_data_set(conf->uart_dev, uart_int_handler, (void *)dev);
	uart_irq_rx_enable(conf->uart_dev);
	return 0;
}

/**
 * @brief Initializes GPIO for the UART peripheral.
 *
 * @param dev UART peripheral device.
 * @return 0 on success, negative errno value on failure.
 */
static int init_gpio(const struct device *dev)
{
	struct m6e_nano_conf *conf = (struct m6e_nano_conf *)dev->config;
	int ret;
	ret = gpio_pin_configure_dt(&conf->gpio_spec, GPIO_INPUT | GPIO_INT_DEBOUNCE);
	if (ret) {
		return ret;
	}
	ret = gpio_pin_interrupt_configure_dt(&conf->gpio_spec,
					      GPIO_INT_EDGE_TO_ACTIVE | GPIO_INT_DEBOUNCE);
	if (ret) {
		return ret;
	}
	gpio_init_callback(&conf->gpio_cb, transmit_string, BIT(conf->gpio_spec.pin));
	ret = gpio_add_callback(conf->gpio_spec.port, &conf->gpio_cb);
	return ret;
}

/**
 * @brief Initializes UART peripheral.
 *
 * @param dev UART peripheral device.
 * @return 0 on success, negative error code otherwise.
 */
static int init_m6e_nano_peripheral(const struct device *dev)
{
	struct m6e_nano_conf *conf = (struct m6e_nano_conf *)dev->config;
	if (!device_is_ready(conf->uart_dev)) {
		__ASSERT(false, "UART device not ready");
		return -ENODEV;
	}
	if (init_uart(dev)) {
		__ASSERT(false, "Failed to initialize UART device");
		return -ENODEV;
	}
	if (!device_is_ready(conf->gpio_spec.port)) {
		__ASSERT(false, "GPIO device not ready");
		return -ENODEV;
	}
	if (init_gpio(dev)) {
		__ASSERT(false, "Failed to initialize GPIO device.");
		return -ENODEV;
	}
	LOG_DBG("M6E Nano initialized");
	return 0;
}

#define INIT_M6E_NANO(inst)                                                                        \
	static struct m6e_nano_data m6e_nano_peripheral_data_##inst = {                            \
		.cmd = DT_INST_PROP_OR(inst, initial_string, ""),                                  \
		.command_callback = NULL,                                                          \
		.rx_data_len = 0,                                                                  \
		.rx_buf = {0},                                                                     \
	};                                                                                         \
	static struct m6e_nano_conf m6e_nano_peripheral_conf_##inst = {                            \
		.data = &m6e_nano_peripheral_data_##inst,                                          \
		.uart_dev = DEVICE_DT_GET(DT_INST_BUS(inst)),                                      \
		.gpio_spec = GPIO_DT_SPEC_INST_GET(inst, button_gpios),                            \
		.gpio_cb = {{0}},                                                                  \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, init_m6e_nano_peripheral, NULL,                                \
			      &m6e_nano_peripheral_data_##inst, &m6e_nano_peripheral_conf_##inst,  \
			      POST_KERNEL, M6E_NANO_INIT_PRIORITY, &api);

DT_INST_FOREACH_STATUS_OKAY(INIT_M6E_NANO);