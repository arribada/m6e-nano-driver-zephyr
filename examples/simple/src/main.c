
#include <zephyr.h>
#include <devicetree.h>
#include <device.h>

#include "../m6e_nano.h"

struct counter {
	int strings;
	int overflows;
};

static struct counter tracker = {0};

void peripheral_callback(const struct device *dev, char *data, size_t length, bool is_string,
			 void *user_data)
{
	struct counter *c = (struct counter *)user_data;
	if (is_string) {
		printk("Recieved string \"%s\"\n", data);
		c->strings++;
	} else {
		printk("Buffer full. Recieved fragment %.*s\n", length, data);
		c->overflows++;
	}
	printk("Strings: %d\nOverflows: %d\n", c->strings, c->overflows);
}

void main(void)
{
	const struct device *dev = device_get_binding(DT_LABEL(DT_NODELABEL(rfid)));
	m6e_nano_set_callback(dev, peripheral_callback, &tracker);
}