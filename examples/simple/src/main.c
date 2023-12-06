
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

void read_callback(const struct device *dev, void *user_data)
{
	const struct device *m6e_nano_dev = user_data;
	struct m6e_nano_data *drv_data = m6e_nano_dev->data;

	if (drv_data->status == RESPONSE_PENDING) {
		int res = m6e_nano_parse_response(user_data);
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

			uint8_t rssi =
				m6e_nano_get_tag_rssi(user_data); // Get the RSSI for tag read
			long freq = m6e_nano_get_tag_freq(
				user_data); // Get the frequency tag was detected at
			long timeStamp = m6e_nano_get_tag_timestamp(
				user_data); // Get the time (ms) since last keep-alive
			uint8_t tagEPCBytes = m6e_nano_get_tag_epc_bytes(user_data);

			printk("rssi: -%ddBm | freq: %ldHz | timestamp: %ldms\n", rssi, freq,
			       timeStamp);

			printk("EPC: [");
			for (uint8_t x = 0; x < tagEPCBytes; x++) {
				if (drv_data->response.data[31 + x] < 0x10) {
					printk(" 0%X ", drv_data->response.data[31 + x]);
				} else {
					printk(" %X ", drv_data->response.data[31 + x]);
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

		drv_data->status = RESPONSE_CLEAR;
	}
}

int main(void)
{
	const struct device *dev = DEVICE_DT_GET_ONE(thingmagic_m6enano);

	struct m6e_nano_data *data = (struct m6e_nano_data *)dev->data;

	LOG_INF("Starting demo...");

	LOG_INF("Setting baud rate...");
	m6e_nano_set_baud(dev, 115200);

	LOG_INF("Requesting hardware version...");
	m6e_nano_get_version(dev);

	LOG_INF("Setting tag protocol...");
	m6e_nano_set_tag_protocol(dev);

	LOG_INF("Setting antenna port...");
	m6e_nano_set_antenna_port(dev);

	LOG_INF("Setting RF region...");
	m6e_nano_set_region(dev, REGION_EUROPE);

	LOG_INF("Setting read power...");
	m6e_nano_set_read_power(dev, 1000);

	LOG_INF("Start reading...");
	m6e_nano_start_reading(dev);

	m6e_nano_set_callback(dev, read_callback, &tracker);
}