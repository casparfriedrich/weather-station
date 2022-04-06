#ifndef EASY_MQTT_H
#define EASY_MQTT_H

#include <stddef.h>
#include <stdint.h>

int easy_mqtt_connect(const char *host);
int easy_mqtt_disconnect(void);
int easy_mqtt_publish(const char *topic, const void *data, size_t data_len);

#endif // EASY_MQTT_H
