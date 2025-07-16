/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <app_version.h>

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

// Uncomment the line below to run the board-specific example instead
#define USE_BOARD_SPECIFIC_EXAMPLE

#ifdef USE_BOARD_SPECIFIC_EXAMPLE
// Forward declaration for board-specific example
extern int juxta5_example_main(void);
#else
// Original example includes
#include <zephyr/drivers/sensor.h>
#include <app/drivers/blink.h>

#define BLINK_PERIOD_MS_STEP 100U
#define BLINK_PERIOD_MS_MAX 1000U
#endif

int main(void)
{
#ifdef USE_BOARD_SPECIFIC_EXAMPLE
	// Run the board-specific example for Juxta5-1_ADC
	printk("Running Juxta5-1_ADC Board Example %s\n", APP_VERSION_STRING);
	return juxta5_example_main();
#else
	// Run the original example application
	int ret;
	unsigned int period_ms = BLINK_PERIOD_MS_MAX;
	const struct device *sensor, *blink;
	struct sensor_value last_val = {0}, val;

	printk("Zephyr Example Application %s\n", APP_VERSION_STRING);

	sensor = DEVICE_DT_GET(DT_NODELABEL(example_sensor));
	if (!device_is_ready(sensor))
	{
		LOG_ERR("Sensor not ready");
		return 0;
	}

	blink = DEVICE_DT_GET(DT_NODELABEL(blink_led));
	if (!device_is_ready(blink))
	{
		LOG_ERR("Blink LED not ready");
		return 0;
	}

	ret = blink_off(blink);
	if (ret < 0)
	{
		LOG_ERR("Could not turn off LED (%d)", ret);
		return 0;
	}

	printk("Use the sensor to change LED blinking period\n");

	while (1)
	{
		ret = sensor_sample_fetch(sensor);
		if (ret < 0)
		{
			LOG_ERR("Could not fetch sample (%d)", ret);
			return 0;
		}

		ret = sensor_channel_get(sensor, SENSOR_CHAN_PROX, &val);
		if (ret < 0)
		{
			LOG_ERR("Could not get sample (%d)", ret);
			return 0;
		}

		if ((last_val.val1 == 0) && (val.val1 == 1))
		{
			if (period_ms == 0U)
			{
				period_ms = BLINK_PERIOD_MS_MAX;
			}
			else
			{
				period_ms -= BLINK_PERIOD_MS_STEP;
			}

			printk("Proximity detected, setting LED period to %u ms\n",
				   period_ms);
			blink_set_period_ms(blink, period_ms);
		}

		last_val = val;

		k_sleep(K_MSEC(100));
	}

	return 0;
#endif
}
