
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include <app_version.h>

#include <m6e_nano.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

struct counter {
	int strings;
	int overflows;
};

static struct counter tracker = {0};

void peripheral_callback(const struct device *dev, uint8_t *data, size_t length, bool is_string,
			 void *user_data)
{

	struct counter *c = (struct counter *)user_data;
	if (is_string) {
		for (size_t i = 0; i < length; i++) {
			printk("%d-%c|", i, (char)data[i]);
		}
		printk("\n");

		c->strings++;
	} else {
		printk("Buffer full. Received fragment %.*s\n", length, data);
		c->overflows++;
	}
	printk("Strings: %d\nOverflows: %d\n", c->strings, c->overflows);
}

int main(void)
{
	const struct device *dev = DEVICE_DT_GET_ONE(thingmagic_m6enano);

	m6e_nano_set_callback(dev, peripheral_callback, &tracker);
	struct m6e_nano_data *data = (struct m6e_nano_data *)dev->data;

	// m6e_nano_send_command(dev, command, n);
	// m6e_nano_send_generic_command(dev, command, n, opcode);
	// m6e_nano_set_baud(dev, 115200);
	LOG_INF("Starting demo...");

	LOG_INF("Requesting hardware version...");
	m6e_nano_get_version(dev);

	LOG_INF("Setting tag protocol...");
	m6e_nano_set_tag_protocol(dev);

	LOG_INF("Setting antenna port...");
	m6e_nano_set_antenna_port(dev);

	LOG_INF("Setting RF region...");
	m6e_nano_set_region(dev, REGION_EUROPE);

	LOG_INF("Setting read power...");
	m6e_nano_set_read_power(dev, 500);

	LOG_INF("Start reading...");
	m6e_nano_start_reading(dev);

	while (true) {
		if (data->status == RESPONSE_PENDING) {
			int res = m6e_nano_parse_response(dev);
			char *res_str = "";
			switch (res) {
			case ERROR_CORRUPT_RESPONSE:
				res_str = "ERROR_CORRUPT_RESPONSE";
				break;
			case RESPONSE_IS_KEEPALIVE:
				res_str = "RESPONSE_IS_KEEPALIVE";
				break;
			case RESPONSE_IS_TAGFOUND:
				res_str = "RESPONSE_IS_TAGFOUND";
				uint8_t tagEPCBytes = m6e_nano_get_tag_epc_bytes(dev);
				printk("EPC: [");
				for (uint8_t x = 0; x < tagEPCBytes; x++) {
					if (data->response.data[31 + x] < 0x10) {
						printk(" 0%X ", data->response.data[31 + x]);
					} else {
						printk(" %X ", data->response.data[31 + x]);
					}
				}
				printk("]\n");
				break;
			case ERROR_UNKNOWN_OPCODE:
				res_str = "ERROR_UNKNOWN_OPCODE";

				break;
			default:
				break;
			}
			LOG_INF("%s", res_str);

			data->status = RESPONSE_CLEAR;
		}
		k_sleep(K_MSEC(100));
	};
}
// printk("ret:\n");
// m6e_nano_get_write_power(dev);

// while (true) {
// 	if (data->response.is_complete) {
// 		LOG_DBG("Data!");
// 	}
// };
// for (size_t i = 0; i < data->response.msg_len; i++) {
// 	printk("[%x] ", data->response.data[i]);
// }
// printk("\n");
