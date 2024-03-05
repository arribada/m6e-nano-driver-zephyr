
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include <app_version.h>
#include <stdio.h>
#include <string.h>

#include <m6e_nano.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

#define TAG_TOTAL_LIMIT 100

struct counter {
	char tags[TAG_TOTAL_LIMIT][(12 * 2) + 1];
	uint32_t total;
};

static struct counter seen_tags = {
	.total = 0,
};

void array_to_string(uint8_t *buf, char *str)
{
	char *ptr = &str[0];

	for (int i = 0; i < 12; i++) {
		ptr += sprintf(ptr, "%02X", buf[i]);
	}
}

void read_callback(const struct device *dev, void *user_data)
{
	const struct device *m6e_nano_dev = user_data;
	struct m6e_nano_data *drv_data = m6e_nano_dev->data;

	if (drv_data->status == RESPONSE_SUCCESS) {
		int res = m6e_nano_parse_response(user_data);
		char *res_str = "";
		switch (res) {
		case ERROR_CORRUPT_RESPONSE:
			res_str = "ERROR_CORRUPT_RESPONSE";
			break;
		case RESPONSE_IS_KEEPALIVE:
			res_str = "RESPONSE_IS_KEEPALIVE";
			printk("Tag count: %d\n", seen_tags.total);
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

			char new_tag_str[(12 * 2) + 1];
			array_to_string(drv_data->response.data + 31, new_tag_str);
			printk("Tag found: %s\n", new_tag_str);
			printk("rssi: -%ddBm | freq: %ldHz | timestamp: %ldms | size %d\n", rssi,
			       freq, timeStamp, tagEPCBytes);
			int ret = 0;
			for (size_t i = 0; i < seen_tags.total; i++) {
				if (strcmp(seen_tags.tags[i], new_tag_str) == 0) {
					printk("Tag already exists\n");
					ret = 1;
				}
			}

			if (ret == 1) {
				break;
			} else {
				strcpy(seen_tags.tags[seen_tags.total], new_tag_str);
				seen_tags.total++;
				printk("Tag count: %d\n", seen_tags.total);
			}

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

	m6e_nano_stop_reading(dev);

	LOG_INF("Setting baud rate...");

	m6e_nano_set_baud(dev, 115200);

	LOG_INF("Requesting hardware version...");
	m6e_nano_get_version(dev);

	LOG_INF("Setting tag protocol...");
	m6e_nano_set_tag_protocol(dev, TMR_TAG_PROTOCOL_GEN2);

	LOG_INF("Setting antenna port...");
	m6e_nano_set_antenna_port(dev);

	LOG_INF("Setting RF region...");
	m6e_nano_set_region(dev, REGION_EUROPE);

	LOG_INF("Setting read power...");
	m6e_nano_set_read_power(dev, 1000);

	LOG_INF("Setting power mode...");
	m6e_nano_set_power_mode(dev, TMR_SR_POWER_MODE_MED_SAVE);

	LOG_INF("Start reading...");
	m6e_nano_start_reading(dev);

	m6e_nano_set_callback(dev, read_callback, &seen_tags);

	return 0;
}

// Headers      Status     Bootloader 
// hFF h14 h03 | h00 h00 | h14 h12 h08 h00 h30 | h00 h00 h02 h20 h22 h08 | h04 h01 h0B h01 h25 h00 h00 h00 h10 | h79 h62 