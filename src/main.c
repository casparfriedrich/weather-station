#include "easy_mqtt.h"
#include "easy_wifi.h"

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/sntp.h>
#include <zephyr/posix/time.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/zephyr.h>

#define MQTT_HOST    "192.168.178.100:1883"
#define SNTP_SERVER  "pool.ntp.org"
#define SNTP_TIMEOUT 3000

#define WIFI_SSID ""
#define WIFI_PSK  ""

LOG_MODULE_REGISTER(app, LOG_LEVEL_DBG);

static int update_system_time(void)
{
	int ret;

	struct sntp_time res_sntp;
	ret = sntp_simple(SNTP_SERVER, SNTP_TIMEOUT, &res_sntp);
	if (ret) {
		return ret;
	}

	struct timespec tspec = {
		.tv_sec = CLAMP(0, res_sntp.seconds, INT64_MAX),
		.tv_nsec = CLAMP(0, ((uint64_t)res_sntp.fraction * NSEC_PER_SEC) >> 32, LONG_MAX)};
	ret = clock_settime(CLOCK_REALTIME, &tspec);
	if (ret) {
		return ret;
	}

	return 0;
}

void main(void)
{
	int ret;

	LOG_INF("Build time: " __DATE__ " " __TIME__);

	const struct device *dev_sensor = DEVICE_DT_GET(DT_NODELABEL(sensor));
	struct sensor_value temp, press, humidity, gas_res;

	while (1) {
		sensor_sample_fetch(dev_sensor);
		sensor_channel_get(dev_sensor, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		sensor_channel_get(dev_sensor, SENSOR_CHAN_PRESS, &press);
		sensor_channel_get(dev_sensor, SENSOR_CHAN_HUMIDITY, &humidity);
		sensor_channel_get(dev_sensor, SENSOR_CHAN_GAS_RES, &gas_res);

		LOG_DBG("T: %d.%06d; P: %d.%06d; H: %d.%06d; G: %d.%06d", temp.val1, temp.val2,
			press.val1, press.val2, humidity.val1, humidity.val2, gas_res.val1,
			gas_res.val2);

		ret = easy_wifi_connect(WIFI_SSID, WIFI_PSK, K_SECONDS(60));
		if (ret && ret != -EALREADY) {
			LOG_ERR("easy_wifi_connect failed: %d", ret);
			goto sleep;
		}

		ret = update_system_time();
		if (ret) {
			LOG_WRN("update_system_time failed: %d", ret);
		}

		// ret = easy_mqtt_connect(MQTT_HOST);
		// if (ret) {
		// 	LOG_WRN("easy_mqtt_connect failed: %d", ret);
		// }

		time_t now = time(NULL);
		char *now_s = ctime(&now);
		LOG_INF("%s", now_s);
		// ret = easy_mqtt_publish("time", now_s, strlen(now_s));
		// if (ret) {
		// 	LOG_WRN("easy_mqtt_publish failed: %d", ret);
		// }

		// ret = easy_mqtt_disconnect();
		// if (ret && ret != -ENOTCONN) {
		// 	LOG_WRN("easy_mqtt_disconnect failed: %d", ret);
		// }

		ret = easy_wifi_disconnect(K_SECONDS(60));
		if (ret && ret != -ENOTCONN) {
			LOG_ERR("easy_wifi_disconnect failed: %d", ret);
			goto sleep;
		}

	sleep:
		k_sleep(K_SECONDS(30));
	}
}
