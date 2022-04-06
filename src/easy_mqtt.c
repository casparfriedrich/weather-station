#include "easy_mqtt.h"

#include <errno.h>
#include <stdio.h>

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socketutils.h>
#include <zephyr/random/rand32.h>

#define DEVICE_ID_MAX_SIZE 8
#define CLIENT_ID_MAX_SIZE (DEVICE_ID_MAX_SIZE * 2 + 1)

#define MQTT_DEFAULT_PORT "1883"
#define MQTT_BUF_SIZE     KB(2)

LOG_MODULE_REGISTER(easy_mqtt, LOG_LEVEL_DBG);

static struct mqtt_client mqtt_client;

static struct k_poll_signal sig_connack = K_POLL_SIGNAL_INITIALIZER(sig_connack);
static struct k_poll_event evt_connack[] = {K_POLL_EVENT_STATIC_INITIALIZER(
        K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig_connack, 0)};

static struct k_poll_signal sig_disconnect = K_POLL_SIGNAL_INITIALIZER(sig_disconnect);
static struct k_poll_event evt_disconnect[] = {K_POLL_EVENT_STATIC_INITIALIZER(
        K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig_disconnect, 0)};

static struct k_poll_signal sig_puback = K_POLL_SIGNAL_INITIALIZER(sig_puback);
static struct k_poll_event evt_puback[] = {K_POLL_EVENT_STATIC_INITIALIZER(
        K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig_puback, 0)};

static int broker_init_dns(const char *host)
{
	int ret;

	static const struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
	static struct addrinfo *result;

	mqtt_client.broker = NULL;

	freeaddrinfo(result);

	ret = net_getaddrinfo_addr_str(host, MQTT_DEFAULT_PORT, &hints, &result);
	if (ret) {
		return ret;
	}

	mqtt_client.broker = result->ai_addr;

	return 0;
}

int easy_mqtt_connect(const char *host)
{
	int ret;

	sys_mutex_lock(&mqtt_client.internal.mutex, K_FOREVER);

	ret = broker_init_dns(host);
	if (ret) {
		LOG_ERR("broker_init_dns failed: %d", ret);
		return ret;
	}

	sys_mutex_unlock(&mqtt_client.internal.mutex);

	k_poll_signal_reset(&sig_connack);

	ret = mqtt_connect(&mqtt_client);
	if (ret) {
		LOG_ERR("mqtt_connect failed: %d", ret);
		return ret;
	}

	ret = k_poll(evt_connack, ARRAY_SIZE(evt_connack), K_SECONDS(30));
	if (ret) {
		LOG_ERR("k_poll failed: %d", ret);
		return ret;
	}

	return evt_connack[0].signal->result;
}

int easy_mqtt_disconnect(void)
{
	int ret;

	k_poll_signal_reset(&sig_disconnect);

	ret = mqtt_disconnect(&mqtt_client);
	if (ret) {
		LOG_ERR("mqtt_disconnect failed: %d", ret);
		return ret;
	}

	ret = k_poll(evt_disconnect, ARRAY_SIZE(evt_disconnect), K_SECONDS(30));
	if (ret) {
		LOG_ERR("k_poll failed: %d", ret);
		return ret;
	}

	return evt_disconnect[0].signal->result;
}

int easy_mqtt_publish(const char *topic, const void *data, size_t data_len)
{
	int ret;

	const struct mqtt_publish_param param = {.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE,
	                                         .message.topic.topic.utf8 = (uint8_t *)topic,
	                                         .message.topic.topic.size = strlen(topic),
	                                         .message.payload.data = (uint8_t *)data,
	                                         .message.payload.len = data_len,
	                                         .message_id = sys_rand32_get()};

	k_poll_signal_reset(&sig_puback);

	ret = mqtt_publish(&mqtt_client, &param);
	if (ret) {
		LOG_ERR("mqtt_publish failed: %d", ret);
		return ret;
	}

	ret = k_poll(evt_puback, ARRAY_SIZE(evt_puback), K_SECONDS(30));
	if (ret) {
		LOG_ERR("k_poll failed: %d", ret);
		return ret;
	}

	return evt_puback[0].signal->result;
}

// static void handle_publish(struct mqtt_client *mqtt_client, const struct mqtt_evt *event)
// {
// 	int ret;

// 	const struct mqtt_publish_param *publish_param = &event->param.publish;
// 	const struct mqtt_topic *topic = &publish_param->message.topic;
// 	const struct mqtt_binstr *payload = &publish_param->message.payload;

// 	LOG_HEXDUMP_DBG(topic->topic.utf8, topic->topic.size, "topic");

// 	char buf[payload->len];
// 	ret = mqtt_readall_publish_payload(mqtt_client, buf, payload->len);
// 	if (ret) {
// 		LOG_WRN("mqtt_readall_publish_payload failed: %d", ret);
// 	}

// 	LOG_HEXDUMP_DBG(buf, payload->len, "payload");

// 	if (topic->qos == MQTT_QOS_1_AT_LEAST_ONCE) {
// 		const struct mqtt_puback_param puback_param = {
// 			.message_id = publish_param->message_id,
// 		};

// 		ret = mqtt_publish_qos1_ack(mqtt_client, &puback_param);
// 		if (ret) {
// 			LOG_WRN("mqtt_publish_qos1_ack failed: %d", ret);
// 		}
// 	}
// }

static void mqtt_event_cb(struct mqtt_client *mqtt_client, const struct mqtt_evt *event)
{
	int ret;

	switch (event->type) {
	case MQTT_EVT_CONNACK:
		LOG_DBG("MQTT_EVT_CONNACK");
		ret = k_poll_signal_raise(&sig_connack, event->result);
		if (ret) {
			LOG_ERR("signaling (CONNACK) failed: %d", ret);
		}
		break;
	case MQTT_EVT_DISCONNECT:
		LOG_DBG("MQTT_EVT_DISCONNECT");
		ret = k_poll_signal_raise(&sig_disconnect, event->result);
		if (ret) {
			LOG_ERR("signaling (DISCONNECT) failed: %d", ret);
		}
		break;
	case MQTT_EVT_PUBLISH:
		LOG_DBG("MQTT_EVT_PUBLISH");
		// handle_publish(mqtt_client, event);
		break;
	case MQTT_EVT_PUBACK:
		LOG_DBG("MQTT_EVT_PUBACK");
		ret = k_poll_signal_raise(&sig_puback, event->result);
		if (ret) {
			LOG_ERR("signaling (PUBACK) failed: %d", ret);
		}
		break;
	case MQTT_EVT_PUBREC:
		LOG_DBG("MQTT_EVT_PUBREC");
		break;
	case MQTT_EVT_PUBREL:
		LOG_DBG("MQTT_EVT_PUBREL");
		break;
	case MQTT_EVT_PUBCOMP:
		LOG_DBG("MQTT_EVT_PUBCOMP");
		break;
	case MQTT_EVT_SUBACK:
		LOG_DBG("MQTT_EVT_SUBACK");
		break;
	case MQTT_EVT_UNSUBACK:
		LOG_DBG("MQTT_EVT_UNSUBACK");
		break;
	case MQTT_EVT_PINGRESP:
		LOG_DBG("MQTT_EVT_PINGRESP");
		break;
	}
}

static void easy_mqtt_fn()
{
	int ret;

	while (1) {
		ret = mqtt_live(&mqtt_client);
		switch (ret) {
		case 0:
		case -EAGAIN:
		case -ENOTCONN:
			break;
		default:
			LOG_WRN("mqtt_live failed: %d", ret);
			break;
		}

		ret = mqtt_input(&mqtt_client);
		switch (ret) {
		case 0:
		case -ENOTCONN:
			break;
		default:
			LOG_WRN("mqtt_input failed: %d", ret);
			break;
		}

		k_sleep(K_MSEC(100));
	}
}

K_THREAD_DEFINE(easy_mqtt, KB(4), easy_mqtt_fn, NULL, NULL, NULL, 0, 0, 0);

static int easy_mqtt_init(const struct device *dev)
{
	static uint8_t device_id[DEVICE_ID_MAX_SIZE] = {0};
	ssize_t device_id_len = hwinfo_get_device_id(device_id, sizeof(device_id));
	if (device_id_len < 0) {
		return -EINVAL;
	}

	static uint8_t client_id[CLIENT_ID_MAX_SIZE] = {0};
	size_t client_id_len = bin2hex(device_id, device_id_len, client_id, sizeof(client_id));
	if (client_id_len == 0) {
		return -EINVAL;
	}

	mqtt_client_init(&mqtt_client);

	mqtt_client.evt_cb = mqtt_event_cb;
	mqtt_client.client_id.utf8 = client_id;
	mqtt_client.client_id.size = client_id_len;

	static uint8_t rx_buf[MQTT_BUF_SIZE] = {0};
	mqtt_client.rx_buf = rx_buf;
	mqtt_client.rx_buf_size = sizeof(rx_buf);
	static uint8_t tx_buf[MQTT_BUF_SIZE] = {0};
	mqtt_client.tx_buf = tx_buf;
	mqtt_client.tx_buf_size = sizeof(tx_buf);

	return 0;
}

SYS_INIT(easy_mqtt_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
