/*
* The MIT License (MIT)
*
* Copyright (c) 2013, Marcelo Aquino, https://github.com/mapnull
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*/

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <time.h>
#include <sys/time.h>

#include <mosquitto.h>

#include "mqtt_bridge.h"
#include "utils.h"
#include "arduino-serial-lib.h"
#include "bridge.h"
#include "protocol.h"
#include "error.h"
#include "device.h"
#include "modules.h"
#include "serial.h"
#include "netdev.c"

#define MICRO_PER_SECOND	1000000.0
#define SERIAL_MAX_BUF 100
#define MAX_OUTPUT 256
#define GBUF_SIZE 100

const char version[] = "0.0.2";

struct bridge bridge;

const char eolchar = '\n';

static int run = 1;
static int user_signal = false;
static bool bandwidth = false;
struct bridge_config config;
static double downspeed, upspeed;
static bool every1s = false;
static bool every30s = false;
static bool quiet = false;
static bool connected = true;

char gbuf[GBUF_SIZE];

void handle_signal(int signum)
{
	if (config.debug > 1) printf("Signal: %d\n", signum);

	if (signum == SIGUSR1) {
		user_signal = MODULES_BRIDGE_SIGUSR1;
		return;
	}
	else if(signum == SIGUSR2) {
		user_signal = MODULES_BRIDGE_SIGUSR2;
		return;
	}
    run = 0;
}

void each_sec(int x)
{
	static struct timeval t1, t2;
	static int seconds = 0;
	double drift;
	static unsigned long long int oldrec, oldsent, newrec, newsent;
	static int cnt = 0;

	every1s = true;
	if (++seconds == 60)
		seconds = 0;
	if (seconds % 30 == 0)
		every30s = true;

	if (config.debug > 3) printf("seconds: %d\n", seconds);

	if (bandwidth) {
		if (cnt == 0) {
			gettimeofday( &t1, NULL );
			if (parse_netdev(&newrec, &newsent, config.interface)) {
				fprintf(stderr, "Error when parsing /proc/net/dev file.\n");
				exit(1);
			}
		} else {
			oldrec = newrec;
			oldsent = newsent;
			if (parse_netdev(&newrec, &newsent, config.interface)) {
				fprintf(stderr, "Error when parsing /proc/net/dev file.\n");
				exit(1);
			}

			if (cnt % 2 == 0) {		// Even
				gettimeofday( &t1, NULL );
				drift=(t1.tv_sec - t2.tv_sec) + ((t1.tv_usec - t2.tv_usec)/MICRO_PER_SECOND);
			} else {				// Odd
				gettimeofday( &t2, NULL );
				drift=(t2.tv_sec - t1.tv_sec) + ((t2.tv_usec - t1.tv_usec)/MICRO_PER_SECOND);
			}
			if (config.debug > 3) printf("%.6lf seconds elapsed\n", drift);

			downspeed = (newrec - oldrec) / drift / 128.0;		// Kbits = / 128; KBytes = / 1024
			upspeed = (newsent - oldsent) / drift / 128.0;		// Kbits = / 128; KBytes = / 1024
		}
		cnt++;
	}

	alarm(1);
}

int mqtt_publish(struct mosquitto *mosq, char *topic, char *payload)
{
	int rc;

	rc = mosquitto_publish(mosq, NULL, topic, strlen(payload), payload, config.mqtt_qos, false);
	if (rc) {
		fprintf(stderr, "Error: MQTT publish returned: %s\n", mosquitto_strerror(rc));
		return 0;
	}
	return 1;
}

void send_alive(struct mosquitto *mosq) {
	static unsigned int beacon_num = 1;

	snprintf(gbuf, GBUF_SIZE, "%d,%d,%d,%d", PROTOCOL_ALIVE, bridge.bridge_dev->modules, beacon_num, 30);
	if (mqtt_publish(mosq, bridge.bridge_dev->status_topic, gbuf))
		beacon_num++;
}

void on_mqtt_connect(struct mosquitto *mosq, void *obj, int result)
{
	struct device *dev;
	int rc;

	if (!result) {
		connected = true;
		if(config.debug) printf("MQTT Connected.\n");

		rc = mosquitto_subscribe(mosq, NULL, bridge.bridge_dev->config_topic, config.mqtt_qos);
		if (rc) {
			fprintf(stderr, "MQTT - Subscribe ERROR: %s\n", mosquitto_strerror(rc));
			run = 0;
			return;
		}
		if (config.debug > 1) printf("Subscribe topic: %s\n", bridge.bridge_dev->config_topic);

		for (dev = bridge.dev_list; dev != NULL; dev = dev->next) {
			rc = mosquitto_subscribe(mosq, NULL, dev->config_topic, config.mqtt_qos);
			if (rc) {
				fprintf(stderr, "MQTT - Subscribe ERROR: %s\n", mosquitto_strerror(rc));
				run = 0;
				return;
			}
			if (config.debug > 1) printf("Subscribe topic: %s\n", bridge.bridge_dev->config_topic);
		}

		send_alive(mosq);
	} else {
		fprintf(stderr, "MQTT - Failed to connect: %s\n", mosquitto_connack_string(result));
    }
}

void on_mqtt_disconnect(struct mosquitto *mosq, void *obj, int rc)
{
	connected = false;
	if (config.debug != 0) printf("MQTT Disconnected: %s\n", mosquitto_strerror(rc));
}

void mqtt_to_device(struct mosquitto *mosq, int sd, struct device *to_dev, char *from_dev_id, char *msg)
{
	int proto_code, num, rc;
	char md_id[MODULES_ID_SIZE + 1];
	struct module *md;
	const int from_dev_topic_size = DEVICE_TOPIC_CONFIG_LEN + DEVICE_ID_SIZE + 1;
	char from_dev_topic[from_dev_topic_size];
	int i;

	snprintf(from_dev_topic, from_dev_topic_size, "%s%s", DEVICE_TOPIC_CONFIG, from_dev_id);

	// Get protocol code from msg
	if (!utils_getInt(&msg, &proto_code)) {
		snprintf(gbuf, GBUF_SIZE, "%s,%d,%d", to_dev->id, PROTOCOL_ERROR, ERROR_MIS_PROTOCOL);
		mqtt_publish(mosq, from_dev_topic, gbuf);
		if (config.debug > 1) printf("MQTT - Device: %s - Error: %d\n", from_dev_id, ERROR_MIS_PROTOCOL);
		return;
	}

	switch (proto_code) {
		case PROTOCOL_ERROR:
		case PROTOCOL_MODULES:
		case PROTOCOL_MD_INFO:
		case PROTOCOL_MD_ENABLE:
		case PROTOCOL_MD_DISABLE:
		case PROTOCOL_MD_TOPIC:
		case PROTOCOL_MD_OPTIONS:
			// Loop prevention
			if (!strncmp(to_dev->config_topic, from_dev_topic, from_dev_topic_size)) {
				fprintf(stderr, "MQTT - Loop prevention\n");
				return;
			}
			break;
		case PROTOCOL_ALIVE:
		case PROTOCOL_TIMEOUT:
		case PROTOCOL_MD_RAW:
			return;
		default:
			snprintf(gbuf, GBUF_SIZE, "%s,%d,%d", to_dev->id, PROTOCOL_ERROR, ERROR_INV_PROTOCOL);
			mqtt_publish(mosq, from_dev_topic, gbuf);
			return;
	}

	if (proto_code == PROTOCOL_ERROR) {
		if (config.debug > 1) printf("MQTT - Device: %s - Error received: %s\n", from_dev_id, msg);
		return;
	}
	if (proto_code == PROTOCOL_MODULES) {
		if (to_dev->modules == 0) {
			return;
		}

		i = 0;
		for (md = to_dev->md_list; md != NULL; md = md->next) {
			if (i == 0) {
				i = snprintf(gbuf, GBUF_SIZE, "%s,%d", to_dev->id, PROTOCOL_MODULES);
			}
			if (i > MQTT_MAX_PAYLOAD_LEN - 10) {
				mqtt_publish(mosq, from_dev_topic, gbuf);
				i = 0;
			}
			strcat(gbuf, ",");
			strcat(gbuf, md->id);
			i += MODULES_ID_SIZE + 1;
		}
		if (i > 0) {
			mqtt_publish(mosq, from_dev_topic, gbuf);
		}
		return;
	}

	// Get module id from msg
	if (!utils_getString(&msg, md_id, MODULES_ID_SIZE, ',')) {
		snprintf(gbuf, GBUF_SIZE, "%s,%d,%d", to_dev->id, PROTOCOL_ERROR, ERROR_MIS_MODULE_ID);
		mqtt_publish(mosq, from_dev_topic, gbuf);
		if (config.debug > 1) printf("MQTT - Device: %s - Error: %d\n", from_dev_id, ERROR_MIS_MODULE_ID);
		return;
	}
	if (!bridge_isValid_module_id(md_id)) {
		snprintf(gbuf, GBUF_SIZE, "%s,%d,%d", to_dev->id, PROTOCOL_ERROR, ERROR_MD_INV_ID);
		mqtt_publish(mosq, from_dev_topic, gbuf);
		if (config.debug > 1) printf("MQTT - Device: %s - Error: %d\n", from_dev_id, ERROR_MD_INV_ID);
		return;
	}
	md = bridge_get_module(to_dev, md_id);
	if (!md) {
		snprintf(gbuf, GBUF_SIZE, "%s,%d,%d,%s", to_dev->id, PROTOCOL_ERROR, ERROR_MD_NOT_FOUND, md_id);
		mqtt_publish(mosq, from_dev_topic, gbuf);
		if (config.debug > 1) printf("MQTT - Device: %s - Error: %d\n", from_dev_id, ERROR_MD_NOT_FOUND);
		return;
	}

	if (to_dev->md_deps->type == MODULES_SERIAL) {
		if (!bridge.serial_ready) {
			snprintf(gbuf, GBUF_SIZE, "%s,%d,%d,%s", to_dev->id, PROTOCOL_ERROR, ERROR_MD_NOT_AVAILABLE, md->id);
			mqtt_publish(mosq, from_dev_topic, gbuf);
			return;
		}
	}

	if (proto_code == PROTOCOL_MD_INFO) {
		if (md->specs) {
			snprintf(gbuf, GBUF_SIZE, "%s,%d,%s,%d,%s,%s", to_dev->id, PROTOCOL_MD_INFO, md->id, md->enabled, md->topic, md->specs);
		}
		else {
			snprintf(gbuf, GBUF_SIZE, "%s,%d,%s,%d,%s", to_dev->id, PROTOCOL_MD_INFO, md->id, md->enabled, md->topic);
		}
		mqtt_publish(mosq, from_dev_topic, gbuf);
	}
	else if (proto_code == PROTOCOL_MD_ENABLE) {
		if (md->enabled) {
			snprintf(gbuf, GBUF_SIZE, "%s,%d,%s", to_dev->id, PROTOCOL_MD_ENABLE, md->id);
			mqtt_publish(mosq, from_dev_topic, gbuf);
			return;
		}
		if (to_dev->md_deps->type == MODULES_SERIAL) {
			snprintf(gbuf, GBUF_SIZE, "%s%s,%s,%d,%s", SERIAL_INIT_CONFIG, to_dev->id, from_dev_id, PROTOCOL_MD_ENABLE, md->id);
			serialport_send(sd, gbuf);
		}
	}
	else if (proto_code == PROTOCOL_MD_DISABLE) {
		if (to_dev->md_deps->type == MODULES_SERIAL) {
			if (!md->enabled) {
				snprintf(gbuf, GBUF_SIZE, "%s,%d,%s", to_dev->id, PROTOCOL_MD_DISABLE, md->id);
				mqtt_publish(mosq, from_dev_topic, gbuf);
				return;
			}
			snprintf(gbuf, GBUF_SIZE, "%s%s,%s,%d,%s", SERIAL_INIT_CONFIG, to_dev->id, from_dev_id, PROTOCOL_MD_DISABLE, md->id);
			serialport_send(sd, gbuf);
		}
		else if (!strcmp(to_dev->id,  bridge.bridge_dev->id)) {
			if (!strcmp(md->id, MODULES_BRIDGE_ID)) {
				// Bridge module cannot be disabled
				snprintf(gbuf, GBUF_SIZE, "%s,%d,%d,%s", to_dev->id, PROTOCOL_ERROR, ERROR_MD_NOT_AVAILABLE, md->id);
			} else {
				snprintf(gbuf, GBUF_SIZE, "%s,%d,%d,%s", to_dev->id, PROTOCOL_ERROR, ERROR_MD_NOT_IPM, md->id);
			}
			mqtt_publish(mosq, from_dev_topic, gbuf);
		}
	}
	else if (PROTOCOL_MD_TOPIC) {
		if (!bridge_isValid_module_topic(msg)) {
			snprintf(gbuf, GBUF_SIZE, "%s,%d,%d,%s", to_dev->id, PROTOCOL_ERROR, ERROR_MD_INV_TOPIC, md->id);
			mqtt_publish(mosq, from_dev_topic, gbuf);
			if (config.debug > 1) printf("MQTT - Device: %s - Error: %d\n", from_dev_id, ERROR_MD_INV_TOPIC);
			return;
		}
		if (to_dev->md_deps->type == MODULES_SERIAL) {
			snprintf(gbuf, GBUF_SIZE, "%s%s,%s,%d,%s,%s", SERIAL_INIT_CONFIG, to_dev->id, from_dev_id, PROTOCOL_MD_TOPIC, md->id, msg);
			serialport_send(sd, gbuf);
		}
		else if (!strcmp(to_dev->id,  bridge.bridge_dev->id)) {
			bridge_set_md_topic(md, msg);
			snprintf(gbuf, GBUF_SIZE, "%d,%s,%s", PROTOCOL_MD_TOPIC, md->id, md->topic);
			mqtt_publish(mosq, bridge.bridge_dev->status_topic, gbuf);
		}
	}
	else if (proto_code == PROTOCOL_MD_OPTIONS) {
		if (to_dev->md_deps->type == MODULES_SERIAL) {
			snprintf(gbuf, GBUF_SIZE, "%s%s,%s,%d,%s,%s", SERIAL_INIT_CONFIG, to_dev->id, from_dev_id, PROTOCOL_MD_OPTIONS, md->id, msg);
			serialport_send(sd, gbuf);
			return;
		}
		else if (!strcmp(to_dev->id,  bridge.bridge_dev->id)) {
			if (md->type == MODULES_SCRIPT) {
				if (!utils_getInt(&msg, &num)) {
					snprintf(gbuf, GBUF_SIZE, "%s,%d,%d,%s", to_dev->id, PROTOCOL_ERROR, ERROR_INV_DATA, md->id);
					mqtt_publish(mosq, from_dev_topic, gbuf);
					if (config.debug > 1) printf("MQTT - Device: %s - Invalid script module message\n", from_dev_id);
					return;
				}
				if (num == MODULES_SCRIPT_LIST) {
					snprintf(gbuf, GBUF_SIZE, "%s,%d,%d,%s", to_dev->id, PROTOCOL_ERROR, ERROR_MD_NOT_IPM, md->id);
					mqtt_publish(mosq, from_dev_topic, gbuf);
					return;
				}
				else if (num == MODULES_SCRIPT_EXECUTE) {
					if (strlen(msg) == 0) {
						snprintf(gbuf, GBUF_SIZE, "%s,%d,%d,%s", to_dev->id, PROTOCOL_ERROR, ERROR_MD_INV_OPTS, md->id);
						mqtt_publish(mosq, from_dev_topic, gbuf);
						if (config.debug > 1) printf("MQTT - Device: %s - Invalid script module message\n", from_dev_id);
						return;
					}
					rc = utils_run_script(config.scripts_folder, msg, gbuf, GBUF_SIZE, config.debug);
					if (rc == -1) {
						run = 0;
					}
					else if (rc == 1) {
						mqtt_publish(mosq, md->topic, "0");        
					}
					else if (rc == 0) {
						if (strlen(gbuf) > 0) {
							if (config.debug > 1) printf("Script output:\n-\n%s\n-\n", gbuf);
							mqtt_publish(mosq, md->topic, gbuf);
						} else {
							mqtt_publish(mosq, md->topic, "1");
						}
					}
				}
				else {
					snprintf(gbuf, GBUF_SIZE, "%s,%d,%d,%s", to_dev->id, PROTOCOL_ERROR, ERROR_MD_INV_OPTS, md->id);
					mqtt_publish(mosq, from_dev_topic, gbuf);
					if (config.debug > 1) printf("MQTT - Device: %s - Error: %d\n", from_dev_id, ERROR_MD_INV_OPTS);
					return;
				}
				return;
			}
			else if (md->type == MODULES_BANDWIDTH) {
				snprintf(gbuf, GBUF_SIZE, "%s,%d,%s,%d,%.0f,%.0f", to_dev->id, PROTOCOL_MD_OPTIONS, md->id, MODULES_GENERIC_RAW, upspeed, downspeed);
				mqtt_publish(mosq, from_dev_topic, gbuf);
				return;
			}
			else if (md->type == MODULES_SERIAL) {
				snprintf(gbuf, GBUF_SIZE, "%s,%d,%s,%d,%d", to_dev->id, PROTOCOL_MD_OPTIONS, md->id, MODULES_GENERIC_RAW, bridge.serial_ready);
				mqtt_publish(mosq, from_dev_topic, gbuf);
				return;
			}
			else {
				snprintf(gbuf, GBUF_SIZE, "%s,%d,%d,%s", to_dev->id, PROTOCOL_ERROR, ERROR_MD_NOT_IPM, md->id);
				mqtt_publish(mosq, from_dev_topic, gbuf);
				return;
			}
		}
	}
}

void on_mqtt_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
	char *payload;
	int *sd;
	char dev_id[DEVICE_ID_SIZE + 1];
	char from_dev_id[DEVICE_ID_SIZE + 1];
	struct device *to_dev;

	sd = (int *)obj;
	payload  = (char *)msg->payload;

	if (config.debug > 2) printf("MQTT IN - topic: %s - payload: %s\n", msg->topic, payload);

	// Get device id from payload
	if (utils_getString(&payload, from_dev_id, DEVICE_ID_SIZE, ',') != DEVICE_ID_SIZE) {
		if (config.debug > 1) printf("MQTT - Error: %d\n", ERROR_MIS_DEVICE_ID);
		return;
	}

	strncpy(dev_id, &msg->topic[DEVICE_TOPIC_CONFIG_LEN], DEVICE_ID_SIZE + 1);
	if (!strcmp(dev_id, bridge.bridge_dev->id)) {
		// Message to bridge
		to_dev = bridge.bridge_dev;
	} else {
		// Message to a device
		to_dev = bridge_get_device(&bridge, dev_id);
	}

	if (!to_dev) {
		fprintf(stderr, "MQTT - Error: Failed to get device: %s\n", dev_id);
		return;
	}

	mqtt_to_device(mosq, *sd, to_dev, from_dev_id, payload);
}

void device_status_to_mqtt(struct mosquitto *mosq, int sd, struct device *from_dev, char *msg)
{
	char md_id[MODULES_ID_SIZE + 1];
	
	struct module *md;
	int proto_code, num, beacon_num, next_alive;

	// Get protocol code from msg
	if (!utils_getInt(&msg, &proto_code)) {
		if (config.debug > 1) printf("Device: %s - Error: %d\n", from_dev->id, ERROR_MIS_PROTOCOL);
		return;
	}

	switch (proto_code) {
		case PROTOCOL_ERROR:
			if (config.debug > 1) printf("Device: %s - Error received: %s\n", from_dev->id, msg);
			snprintf(gbuf, GBUF_SIZE, "%d,%s", PROTOCOL_ERROR, msg);
			mqtt_publish(mosq, from_dev->status_topic, gbuf);
			return;
		case PROTOCOL_ALIVE:
			if (!utils_getInt(&msg, &num)) {
				if (config.debug > 1) printf("Device: %s - Invalid alive message.\n", from_dev->id);
				return;
			}
			if (!utils_getInt(&msg, &beacon_num)) {
				if (config.debug > 1) printf("Device: %s - Invalid alive message.\n", from_dev->id);
				return;
			}
			if (!utils_getInt(&msg, &next_alive)) {
				if (config.debug > 1) printf("Device: %s - Invalid alive message.\n", from_dev->id);
				return;
			}
			if (from_dev->modules != num) {
				if (from_dev->md_deps->type == MODULES_SERIAL) {
					if (config.debug > 2) printf("Sending get modules to serial.\n");
					snprintf(gbuf, GBUF_SIZE, "%s%s,%s,%d", SERIAL_INIT_CONFIG, from_dev->id, bridge.bridge_dev->id, PROTOCOL_MODULES);
					serialport_send(sd, gbuf);
				}
				return;
			}
			from_dev->alive = next_alive * 2;
			snprintf(gbuf, GBUF_SIZE, "%d,%d,%d,%d", PROTOCOL_ALIVE, from_dev->modules, beacon_num, next_alive);
			mqtt_publish(mosq, from_dev->status_topic, gbuf);
			return;
		case PROTOCOL_MD_ENABLE:
		case PROTOCOL_MD_DISABLE:
		case PROTOCOL_MD_TOPIC:
		case PROTOCOL_MD_OPTIONS:
		case PROTOCOL_MD_RAW:
			// Get module id from msg
			if (!utils_getString(&msg, md_id, MODULES_ID_SIZE, ',')) {
				if (config.debug > 1) printf("Device: %s - Error: %d\n", from_dev->id, ERROR_MIS_MODULE_ID);
				return;
			}
			if (!bridge_isValid_module_id(md_id)) {
				if (config.debug > 1) printf("Device: %s - Error: %d\n", from_dev->id, ERROR_MD_INV_ID); 
				return;
			}
			md = bridge_get_module(from_dev, md_id);
			if (!md) {
				if (config.debug > 1) printf("Device: %s - Error: %d\n", from_dev->id, ERROR_MD_NOT_FOUND);
				return;
			}

			if (proto_code == PROTOCOL_MD_ENABLE) {
				if (md->enabled)
					return;
				md->enabled = true;
				snprintf(gbuf, GBUF_SIZE, "%d,%s", PROTOCOL_MD_ENABLE, md->id);
				mqtt_publish(mosq, from_dev->status_topic, gbuf);
			}
			else if (proto_code == PROTOCOL_MD_DISABLE) {
				if (!md->enabled)
					return;
				md->enabled = false;
				snprintf(gbuf, GBUF_SIZE, "%d,%s", PROTOCOL_MD_DISABLE, md->id);
				mqtt_publish(mosq, from_dev->status_topic, gbuf);
			}
			else if (proto_code == PROTOCOL_MD_TOPIC) {
				if (!bridge_set_md_topic(md, msg)) {
					if (config.debug > 1) printf("Device: %s - Error: %d\n", from_dev->id, ERROR_MD_INV_TOPIC);
					return;
				}
				snprintf(gbuf, GBUF_SIZE, "%d,%s,%s", PROTOCOL_MD_TOPIC, md->id, md->topic);
				mqtt_publish(mosq, from_dev->status_topic, gbuf);
			}	
			else if (proto_code == PROTOCOL_MD_OPTIONS) {
				snprintf(gbuf, GBUF_SIZE, "%d,%s,%s", PROTOCOL_MD_OPTIONS, md->id, msg);
				mqtt_publish(mosq, from_dev->status_topic, gbuf);
			}
			else if (proto_code == PROTOCOL_MD_RAW) {
				mqtt_publish(mosq, md->topic, msg);
			}
			return;
		default:
			if (config.debug > 1) printf("Device: %s - Error: %d\n", from_dev->id, ERROR_INV_DATA);
			return;
	}
}

void device_config_to_mqtt(struct mosquitto *mosq, int sd, struct device *from_dev, char *to_dev_id, char *msg)
{
	char md_id[MODULES_ID_SIZE + 1];
	char md_topic[DEVICE_TOPIC_MAX_SIZE + 1];
	char md_specs[DEVICE_SPECS_MAX_SIZE + 1];
	struct module *md;
	const int to_dev_topic_size = DEVICE_TOPIC_CONFIG_LEN + DEVICE_ID_SIZE + 1;
	char to_dev_topic[to_dev_topic_size];
	int proto_code, num;

	if (!utils_getInt(&msg, &proto_code)) {
		if (config.debug > 1) printf("Device: %s - Error: %d\n", from_dev->id, ERROR_MIS_PROTOCOL);
		return;
	}

	snprintf(to_dev_topic, to_dev_topic_size, "%s%s", DEVICE_TOPIC_CONFIG, to_dev_id);

	switch (proto_code) {
		case PROTOCOL_ALIVE:
		case PROTOCOL_TIMEOUT:
		case PROTOCOL_MD_ENABLE:
		case PROTOCOL_MD_DISABLE:
		case PROTOCOL_MD_TOPIC:
		case PROTOCOL_MD_RAW:
			if (config.debug > 1) printf("Device: %s - Error: %d\n", from_dev->id, ERROR_INV_DATA);
			return;
		case PROTOCOL_ERROR:
			snprintf(gbuf, GBUF_SIZE, "%s,%d,%s", from_dev->id, PROTOCOL_ERROR, msg);
			mqtt_publish(mosq, to_dev_topic, gbuf);
			if (config.debug > 1) printf("Device: %s - Error received: %s\n", from_dev->id, msg);
			return;
		case PROTOCOL_MODULES:
			while (utils_getString(&msg, md_id, MODULES_ID_SIZE, ',')) {
				if (!bridge_isValid_module_id(md_id)) {
					if (config.debug > 1) printf("Device: %s - Error: %d\n", from_dev->id, ERROR_MD_INV_ID); 
					continue;
				}
				md = bridge_add_module(from_dev, md_id, false);
				if (!md) {
					if (config.debug > 1) printf("Failed to add module: %s\n", md_id);
					continue;
				}
				printf("New Module: %s - from: %s\n", md->id, from_dev->id);

				snprintf(gbuf, GBUF_SIZE, "%s%s,%s,%d,%s", SERIAL_INIT_CONFIG, from_dev->id, bridge.bridge_dev->id, PROTOCOL_MD_INFO, md->id);
				serialport_send(sd, gbuf);
			}
			return;
	}

	// Get module id from msg
	if (!utils_getString(&msg, md_id, MODULES_ID_SIZE, ',')) {
		if (config.debug > 1) printf("Device: %s - Error: %d\n", from_dev->id, ERROR_MIS_MODULE_ID);
		return;
	}
	if (!bridge_isValid_module_id(md_id)) {
		if (config.debug > 1) printf("Device: %s - Error: %d\n", from_dev->id, ERROR_MD_INV_ID); 
		return;
	}
	md = bridge_get_module(from_dev, md_id);
	if (!md) {
		if (config.debug > 1) printf("Device: %s - Error: %d\n", from_dev->id, ERROR_MD_NOT_FOUND);
		return;
	}

	switch (proto_code) {
		case PROTOCOL_MD_INFO:
			// Module enable/disable
			if (!utils_getInt(&msg, &num) || (num != 0 && num != 1)) {
				if (config.debug > 1) printf("Device: %s - Error: %d\n", from_dev->id, ERROR_MD_INV_INFO);
				return;
			}
			// Module topic
			if (!utils_getString(&msg, md_topic, DEVICE_TOPIC_MAX_SIZE, ',')) {
				if (config.debug > 1) printf("Device: %s - Error: %d\n", from_dev->id, ERROR_MD_INV_INFO);
				return;
			}
			// Module specs
			md_specs[0] = 0;
			utils_getString(&msg, md_specs, DEVICE_SPECS_MAX_SIZE, ',');

			md->enabled = false;
			if (!bridge_set_md_topic(md, md_topic)) {
				if (config.debug > 1) printf("Device: %s - Error: %d\n", from_dev->id, ERROR_MD_INV_INFO);
				return;
			}
			if (strlen(md_specs) == 0) {
				if (md->specs) {
					free(md->specs);
					md->specs = NULL;
				}
			} else {
				if (!bridge_set_module_specs(md, md_specs)) {
					if (config.debug > 1) printf("Device: %s - Error: %d\n", from_dev->id, ERROR_MD_INV_INFO);
					return;
				}
			}
			md->enabled = num;

			if (config.debug > 1) {
				printf("Module:\n");
				bridge_print_module(md);
			}
			return;
		case PROTOCOL_MD_OPTIONS:
			snprintf(gbuf, GBUF_SIZE, "%s,%d,%s", from_dev->id, PROTOCOL_MD_OPTIONS, msg);
			mqtt_publish(mosq, to_dev_topic, gbuf);
			return;
		default:
			if (config.debug > 1) printf("Device: %s - Error: %d\n", from_dev->id, ERROR_INV_PROTOCOL);
			return;
	}
}

int serial_in(int sd, struct mosquitto *mosq, char *md_id)
{
	static char serial_buf[SERIAL_MAX_BUF];
	static int buf_len = 0;
	char *buf_p;
	char id[DEVICE_ID_SIZE + 1];
	struct device *dev;
	int rc, sread;

	if (buf_len)
		buf_p = &serial_buf[buf_len - 1];
	else
		buf_p = &serial_buf[0];

	sread = serialport_read_until(sd, buf_p, eolchar, SERIAL_MAX_BUF - buf_len, config.serial.timeout);
	if (sread == -1) {
		fprintf(stderr, "Serial - Read Error.\n");
		return -1;
	} 
	if (sread == 0)
		return 0;

	buf_len += sread;

	if (serial_buf[buf_len - 1] == eolchar) {
		serial_buf[buf_len - 1] = 0;			//replace eolchar
		buf_len--;								// eolchar was counted, decreasing 1
		if (config.debug > 3) printf("Serial - size:%d, serial_buf:%s\n", buf_len, serial_buf);
		if (buf_len < SERIAL_INIT_LEN) {		// We need at least SERIAL_INIT_LEN to count as a valid command
			if (config.debug > 1) printf("Invalid serial input.\n");
			buf_len = 0;
			return 0;
		}
		sread = buf_len;	// for return
		buf_len = 0;		// reseting for the next input

		buf_p = &serial_buf[SERIAL_INIT_LEN];

		// Serial debug
		if (!strncmp(serial_buf, SERIAL_INIT_DEBUG, SERIAL_INIT_LEN)) {
			if (config.debug) {
				printf("Device Debug: %s\n", buf_p);
				snprintf(gbuf, GBUF_SIZE, "%d,%s,%d,%s", PROTOCOL_MD_OPTIONS, MODULES_BRIDGE_ID, MODULES_BRIDGE_DEBUG, buf_p);
				mqtt_publish(mosq, bridge.bridge_dev->status_topic, gbuf);
			}
			return sread;
		}
		else if ((!strncmp(serial_buf, SERIAL_INIT_STATUS, SERIAL_INIT_LEN)) ||
			(!strncmp(serial_buf, SERIAL_INIT_CONFIG, SERIAL_INIT_LEN)))
		{
			if (config.debug > 2) printf("Serial IN - Message: %s\n", serial_buf);

			// Get device id from buf
			if (utils_getString(&buf_p, id, DEVICE_ID_SIZE, ',') != DEVICE_ID_SIZE) {
				if (config.debug > 1) printf("Serial - Error: %d\n", ERROR_MIS_DEVICE_ID);
				return 0;
			}
			if (!bridge_isValid_device_id(id)) {
				if (config.debug > 1) printf("Serial - Error: %d\n", ERROR_DEV_INV_ID);
				return 0;
			}

			dev = bridge_get_device(&bridge, id);
			if (!dev) {
				dev = bridge_add_device(&bridge, id, md_id);
				if (!dev) {
					if (config.debug > 1) printf("Serial - Failed to add device.\n");
					return sread;
				}

				rc = mosquitto_subscribe(mosq, NULL, dev->config_topic, config.mqtt_qos);
				if (rc) {
					fprintf(stderr, "MQTT - Subscribe ERROR: %s\n", mosquitto_strerror(rc));
					run = 0;
					return sread;
				}
				if (config.debug > 2) printf("Subscribe topic: %s\n", bridge.bridge_dev->config_topic);

				if (config.debug > 2) {
					printf("New device:\n");
					bridge_print_device(dev);
				}
			}

			if (!strncmp(serial_buf, SERIAL_INIT_CONFIG, SERIAL_INIT_LEN)) {
				// Get device id from buf
				if (utils_getString(&buf_p, id, DEVICE_ID_SIZE, ',') != DEVICE_ID_SIZE) {
					if (config.debug > 1) printf("Serial - Error: %d\n", ERROR_MIS_DEVICE_ID);
					return 0;
				}
				if (!bridge_isValid_device_id(id)) {
					if (config.debug > 1) printf("Serial - Error: %d\n", ERROR_DEV_INV_ID);
					return 0;
				}
				device_config_to_mqtt(mosq, sd, dev, id, buf_p);
			} else {
				device_status_to_mqtt(mosq, sd, dev, buf_p);
			}
		} else {
			if (config.debug > 1) printf("Unknown serial data.\n");
		}
		return sread;
	}
	else if (buf_len == SERIAL_MAX_BUF) {
		if (config.debug > 1) printf("Serial buffer full.\n");
		buf_len = 0;
	} else {
		if (config.debug > 1) printf("Serial chunked.\n");
	}
	return 0;
}

void signal_usr(int sd, struct mosquitto *mosq)
{
	struct device *dev;

	if (user_signal == MODULES_BRIDGE_SIGUSR1) {
		if (config.remap_usr1_dev) {
			dev = bridge_get_device(&bridge, config.remap_usr1_dev);
			if (dev) {
				snprintf(gbuf, GBUF_SIZE, "%s%s,%s,%d,%s,%d", SERIAL_INIT_CONFIG, dev->id, bridge.bridge_dev->id, PROTOCOL_MD_OPTIONS, config.remap_usr1_md, config.remap_usr1_md_code);
				serialport_send(sd, gbuf);
			}
		} else if (connected) {
			snprintf(gbuf, GBUF_SIZE, "%d,%s,%d", PROTOCOL_MD_OPTIONS, MODULES_BRIDGE_ID, MODULES_BRIDGE_SIGUSR1);
			mqtt_publish(mosq, bridge.bridge_dev->status_topic, gbuf);
		}
	}

	if (user_signal == MODULES_BRIDGE_SIGUSR2) {
		if (config.remap_usr2_dev) {
			dev = bridge_get_device(&bridge, config.remap_usr2_dev);
			if (dev) {
				snprintf(gbuf, GBUF_SIZE, "%s%s,%s,%d,%s,%d", SERIAL_INIT_CONFIG, dev->id, bridge.bridge_dev->id, PROTOCOL_MD_OPTIONS, config.remap_usr2_md, config.remap_usr2_md_code);
				serialport_send(sd, gbuf);
			}
		} else if (connected) {
			snprintf(gbuf, GBUF_SIZE, "%d,%s,%d", PROTOCOL_MD_OPTIONS, MODULES_BRIDGE_ID, MODULES_BRIDGE_SIGUSR2);
			mqtt_publish(mosq, bridge.bridge_dev->status_topic, gbuf);
		}
	}

	user_signal = 0;
}

void serial_hang(struct mosquitto *mosq)
{
	struct module *md;

	bridge.serial_ready = false;
	bridge.serial_alive = 0;

	if (connected) {
		md = bridge_get_module(bridge.bridge_dev, MODULES_SERIAL_ID);
		if (md) {
			snprintf(gbuf, GBUF_SIZE, "%d", MODULES_SERIAL_ERROR);
			mqtt_publish(mosq, md->topic, gbuf);
		}
	}
}

void print_usage(char *prog_name)
{
	printf("Usage: %s [-c file] [--quiet]\n", prog_name);
	printf(" -c : config file path.\n");
}

int main(int argc, char *argv[])
{
	int sd = -1;
	char *conf_file = NULL;
	struct mosquitto *mosq;
	struct module *md;
	struct device *dev;
	int rc;
	int i;
	
	gbuf[0] = 0;

	if (!quiet) printf("Version: %s\n", version);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
	signal(SIGUSR1, handle_signal);
	signal(SIGUSR2, handle_signal);
	signal(SIGALRM, each_sec);
	
	for (i=1; i<argc; i++) {
		if(!strcmp(argv[i], "-c") || !strcmp(argv[i], "--config")){
			if(i==argc-1){
                fprintf(stderr, "Error: -c argument given but no file specified.\n\n");
				print_usage(argv[0]);
                return 1;
            }else{
				conf_file = argv[i+1];
			}
			i++;
		}else if(!strcmp(argv[i], "--quiet")){
				quiet = true;
		}else{
				fprintf(stderr, "Error: Unknown option '%s'.\n",argv[i]);
				print_usage(argv[0]);
				return 1;
		}
	}
	
	if (!conf_file) {
		fprintf(stderr, "Error: No config file given.\n");
		return 1;
	}

	memset(&config, 0, sizeof(struct bridge_config));
	if (config_parse(conf_file, &config)) return 1;
	
	if (quiet) config.debug = 0;
	if (config.debug != 0) printf("Debug: %d\n", config.debug);

	rc = bridge_init(&bridge, config.id, MODULES_BRIDGE_ID);
	if (rc) {
		if (config.debug) printf("Error: Failed to initialize bridge: %d\n", rc);
		return 1;
	}

	mosquitto_lib_init();
	mosq = mosquitto_new(config.id, true, NULL);
	if(!mosq){
		fprintf(stderr, "Error creating mqtt instance.\n");
		switch(errno){
			case ENOMEM:
				fprintf(stderr, " out of memory.\n");
				break;
			case EINVAL:
				fprintf(stderr, " invalid id.\n");
				break;
		}
		return 1;
	}
	snprintf(gbuf, GBUF_SIZE, "%d", PROTOCOL_TIMEOUT);
	mosquitto_will_set(mosq, bridge.bridge_dev->status_topic, strlen(gbuf), gbuf, config.mqtt_qos, MQTT_RETAIN);

	mosquitto_connect_callback_set(mosq, on_mqtt_connect);
	mosquitto_disconnect_callback_set(mosq, on_mqtt_disconnect);
	mosquitto_message_callback_set(mosq, on_mqtt_message);
	mosquitto_user_data_set(mosq, &sd);

	md = bridge_add_module(bridge.bridge_dev, MODULES_MQTT_ID, true);
	if (!md) {
		fprintf(stderr, "Failed to add MQTT module.\n");
		return 1;
	}

	if (config.scripts_folder) {
		if (access(config.scripts_folder, R_OK )) {
			fprintf(stderr, "Couldn't open scripts folder: %s\n", config.scripts_folder);
			return 1;
		}
		md = bridge_add_module(bridge.bridge_dev, MODULES_SCRIPT_ID, true);
		if (!md) {
			fprintf(stderr, "Failed to add script module.\n");
			return 1;
		}
	}

	if (config.interface) {
		//TODO: check if interface exists
		if (access("/proc/net/dev", R_OK )) {
			fprintf(stderr, "Couldn't open /proc/net/dev\n");
			return 1;
		}
		md = bridge_add_module(bridge.bridge_dev, MODULES_BANDWIDTH_ID, true);
		if (!md) {
			fprintf(stderr, "Failed to add bandwidth module.\n");
			return 1;
		}
		bandwidth = true;
	}

	if (config.serial.port) {
		sd = serialport_init(config.serial.port, config.serial.baudrate);
		if( sd == -1 ) {
			fprintf(stderr, "Couldn't open serial port.\n");
			return 1;
		} else {
			md = bridge_add_module(bridge.bridge_dev, MODULES_SERIAL_ID, true);
			if (!md) {
				fprintf(stderr, "Failed to add serial module.\n");
				return 1;
			}
			serialport_flush(sd);
			bridge.serial_ready = true;

			if (config.debug) printf("Serial ready.\n");
		}
	}

	if (config.debug > 2) bridge_print_modules(bridge.bridge_dev);

	rc = mosquitto_connect(mosq, config.mqtt_host, config.mqtt_port, 60);
	if (rc) {
		fprintf(stderr, "ERROR: %s\n", mosquitto_strerror(rc));
		return -1;
	}

	alarm(1);

	while (run) {
		if (bridge.serial_ready) {
			rc = serial_in(sd, mosq, MODULES_SERIAL_ID);
			if (rc == -1) {
				serial_hang(mosq);
			} else if (rc > 0) {
				bridge.serial_alive = DEVICE_ALIVE_MAX;
			}
		}

		if (user_signal) {
			if (config.debug > 2) printf("Signal - SIGUSR: %d\n", user_signal);
			signal_usr(sd, mosq);
		}

		rc = mosquitto_loop(mosq, 100, 1);
		if (run && rc) {
			if (config.debug > 2) printf("MQTT loop: %s\n", mosquitto_strerror(rc));
			usleep(100000);	// wait 100 msec
			mosquitto_reconnect(mosq);
		}
		usleep(20);

		if (every1s) {
			every1s = false;

			for (dev = bridge.dev_list; dev != NULL; dev = dev->next) {
				if (dev->alive) {
					dev->alive--;
					if (!dev->alive) {
						if (connected) {
							snprintf(gbuf, GBUF_SIZE, "%d", PROTOCOL_TIMEOUT);
							mqtt_publish(mosq, dev->status_topic, gbuf);
							rc = mosquitto_unsubscribe(mosq, NULL, dev->config_topic);
							if (rc)
								fprintf(stderr, "Error: MQTT unsubscribe returned: %s\n", mosquitto_strerror(rc));
						}
						if (config.debug) printf("Device: %s - Timeout.\n", dev->id);
						bridge_remove_device(&bridge, dev->id);
					}
				}
			}
		}

		if (every30s) {
			every30s = false;

			if (connected) {
				send_alive(mosq);

				if (bandwidth) {
					md = bridge_get_module(bridge.bridge_dev, MODULES_BANDWIDTH_ID);
					if (md) {
						snprintf(gbuf, GBUF_SIZE, "%.0f,%.0f", upspeed, downspeed);
						mqtt_publish(mosq, md->topic, gbuf);
						if (config.debug > 2) printf("down: %f - up: %f\n", downspeed, upspeed);
					}
				}
			} else {
				if (config.debug) printf("MQTT Offline.\n");
			}

			if (bridge.serial_alive) {
				bridge.serial_alive--;
				if (!bridge.serial_alive) {
					if (config.debug > 1) printf("Serial timeout.\n");
					serial_hang(mosq);
				}
			} else {
				if (config.serial.port && !bridge.serial_ready) {
					if (config.debug > 1) printf("Trying to reconnect serial port.\n");
					serialport_close(sd);
					sd = serialport_init(config.serial.port, config.serial.baudrate);
					if( sd == -1 ) {
						fprintf(stderr, "Couldn't open serial port.\n");
					} else {
						serialport_flush(sd);
						bridge.serial_ready = true;
						snprintf(gbuf, GBUF_SIZE, "%d", MODULES_SERIAL_OPEN);
						mqtt_publish(mosq, md->topic, gbuf);
						if (config.debug) printf("Serial reopened.\n");
					}
				}
			}
		}
	}

	if (bridge.serial_ready) {
		serialport_close(sd);
	}

	mosquitto_destroy(mosq);

	mosquitto_lib_cleanup();
	config_cleanup(&config);

	printf("Exiting..\n\n");

	return 0;
}
