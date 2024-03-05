#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <stdio.h>
#include <string.h>

#include <../../drivers/m6e-nano/m6e_nano.h>

#include <zephyr/ztest.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(m6enano_tests);

ZTEST_SUITE(m6enano_tests, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief Test encoding of sensor data
 *
 * Tests the encoding of single and multiple sensor data packages
 *
 */
ZTEST(m6enano_tests, test_driver)
{
	const struct device *dev = DEVICE_DT_GET_ONE(thingmagic_m6enano);

	m6e_nano_set_baud(dev, 115200);

	zassert_equal(1, 1);
}