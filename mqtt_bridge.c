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
#include "error.h"
#include "device.h"
#include "serial.h"
#include "netdev.c"
#include "cJSON.h"

#define MICRO_PER_SECOND	1000000.0
#define SERIAL_MAX_BUF 100
#define MAX_OUTPUT 256
#define GBUF_SIZE 100

const char version[] = "0.3.2";

struct bridge_t bridge;

const char eolchar = '\n';

static int run = 1;
static int user_signal = false;
static bool bandwidth = false;
struct bridge_config config;
static double downspeed, upspeed;
static unsigned long seconds = 0;
static bool quiet = false;
static bool connected = true;

char gbuf[GBUF_SIZE];

void handle_signal(int signum)
{
	if (config.debug > 1) printf("Signal: %d\n", signum);

	if (signum == SIGUSR1) {
		user_signal = SIGUSR1;
		return;
	}
	else if(signum == SIGUSR2) {
		user_signal = SIGUSR2;
		return;
	}
    run = 0;
}

void each_sec(int x)
{
	static struct timeval t1, t2;
	double drift;
	static unsigned long long int oldrec, oldsent, newrec, newsent;
	static int cnt = 0;

	seconds++;

	if (config.debug > 3) printf("seconds: %lu\n", seconds);

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

	snprintf(gbuf, GBUF_SIZE, "{\"beacon\":[%d,30]}", beacon_num);
	if (mqtt_publish(mosq, MAIN_TOPIC, gbuf))
		beacon_num++;
}

void on_mqtt_connect(struct mosquitto *mosq, void *obj, int result)
{
	struct device_t *device;
	int rc;

	if (!result) {
		connected = true;
		if(config.debug) printf("MQTT Connected.\n");

		rc = mosquitto_subscribe(mosq, NULL, bridge.uuid, config.mqtt_qos);
		if (rc) {
			fprintf(stderr, "MQTT - Subscribe ERROR: %s\n", mosquitto_strerror(rc));
			run = 0;
			return;
		}

		for (device = bridge.device_list; device != NULL; device = device->next) {
			device->server_id = 0;
			rc = mosquitto_subscribe(mosq, NULL, device->uuid, config.mqtt_qos);
			if (rc) {
				fprintf(stderr, "MQTT - Subscribe ERROR: %s\n", mosquitto_strerror(rc));
				run = 0;
				return;
			}
			if (config.debug > 1) printf("Subscribed to uuid: %s\n", device->uuid);
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

void mqtt_to_bridge(struct mosquitto *mosq, cJSON *json, int tid)
{
	char *value;
	int rc, id;
	char script_output[20];
	cJSON *json_item;
	struct device_t *device;

	if ((json_item = cJSON_GetObjectItem(json, "set")) && (value = json_item->valuestring)) {
		// Set operation
		if (config.debug > 2) printf("MQTT - bridge options: set\n");
		
		if (!strcmp(value, "id")) {
			if (!(json_item = cJSON_GetObjectItem(json, "id")) || (json_item->type != cJSON_Number)) {
				if (config.debug > 1) printf("Invalid or missing id.\n");
				return;
			}
			id = json_item->valueint;
			if (!(json_item = cJSON_GetObjectItem(json, "uuid")) || !(value = json_item->valuestring)) {
				if (config.debug > 1) printf("Invalid or missing uuid.\n");
				return;
			}
			device = bridge_get_device(&bridge, value);
			if (device) {
				device->server_id = id;		// With the id set, we don't need to send the entire uuid
											// to distinguished the relay device 
				if (config.debug > 2) printf("Device server id updated.\n");
			}
		}
	} else if ((json_item = cJSON_GetObjectItem(json, "run")) && (value = json_item->valuestring)) {
		// Run operation
		if (config.debug > 2) printf("MQTT - bridge options: run\n");

		if (config.scripts_folder) {
			rc = utils_run_script(config.scripts_folder, value, script_output, 20, config.debug);
			if (rc == -1) {
				// No memory left
				run = 0;
			} else if (rc == 1) {
				snprintf(gbuf, GBUF_SIZE, "{\"tid:\":%d,\"error\":%d}", tid, ERROR_UNKNOWN);
				mqtt_publish(mosq, MAIN_TOPIC, gbuf );        
			} else if (rc == 0) {
				if (strlen(gbuf) > 0) {
					if (config.debug > 1) printf("Script output:\n-\n%s\n-\n", script_output);
					snprintf(gbuf, GBUF_SIZE, "{\"tid\":%d,\"run\":\"%s\"}", tid, script_output);
					mqtt_publish(mosq, MAIN_TOPIC, gbuf);
				} else {
					snprintf(gbuf, GBUF_SIZE, "{\"tid\":%d}", tid);
					mqtt_publish(mosq, MAIN_TOPIC, gbuf);
				}
			}
		}
	} else {
		if (config.debug > 1) printf("MQTT - Unknown bridge option.\n");
		snprintf(gbuf, GBUF_SIZE, "{\"tid:\":%d,\"error\":%d}", tid, ERROR_UNKNOWN_JSON);
		mqtt_publish(mosq, MAIN_TOPIC, gbuf );
		return;
	}
}

void on_mqtt_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
	char *payload, *topic;
	int *sd;
	struct device_t *device;
	cJSON *json, *json_item;
	char *value;
	int tid;

	sd = (int *)obj;
	payload  = (char *)msg->payload;
	topic = msg->topic;

	if (config.debug > 2) printf("MQTT IN - topic: %s - payload: %s\n", msg->topic, payload);

	json = cJSON_Parse(payload);
	if (!json) {
		if (config.debug > 1) printf("MQTT: Parse error - before: [%s]\n", cJSON_GetErrorPtr());
		//TODO: free json?
		return;
	}

	if (!(json_item = cJSON_GetObjectItem(json, "tid")) || (json_item->type != cJSON_Number)) {
		if (config.debug > 1) printf("MQTT: Invalid or missing tid.\n");
		cJSON_Delete(json);
		return;
	}
	tid = json_item->valueint;

	if (!strcmp(topic, bridge.uuid)) {
		if (config.debug > 1) printf("Message for the bridge: %s\n", payload);
		mqtt_to_bridge(mosq, json, tid);
	} else {
		device = bridge_get_device(&bridge, topic);
		if (!device) {
			fprintf(stderr, "MQTT - Error: Failed to get device: %s\n", topic);
		} else {
			if ((json_item = cJSON_GetObjectItem(json, "comma")) && (value = json_item->valuestring)) {
				if (device->id == 0) {
					snprintf(gbuf, GBUF_SIZE, "%s%s", SERIAL_SINGLE_COMMA_STR, value);
				} else {
					snprintf(gbuf, GBUF_SIZE, "%s%d%s", SERIAL_MULTI_COMMA_STR, device->id, value);
				}
			} else {
				if (device->id == 0) {
					snprintf(gbuf, GBUF_SIZE, "%s%s", SERIAL_SINGLE_JSON_STR, payload);
				} else {
					snprintf(gbuf, GBUF_SIZE, "%s%d%s", SERIAL_SINGLE_JSON_STR, device->id, payload);
				}
			}
			serialport_send(*sd, gbuf);
		}
	}
	cJSON_Delete(json);
}

int serial_in(int sd, struct mosquitto *mosq)
{
	static char serial_buf[SERIAL_MAX_BUF];
	static int buf_len = 0;
	char *serial_buf_ptr;
	int id;
	struct device_t *device;
	int rc, sread;

	if (buf_len)
		serial_buf_ptr = &serial_buf[buf_len - 1];
	else
		serial_buf_ptr = &serial_buf[0];

	sread = serialport_read_until(sd, serial_buf_ptr, eolchar, SERIAL_MAX_BUF - buf_len, config.serial.timeout);
	if (sread == -1) {
		fprintf(stderr, "Serial - Read Error.\n");
		return -1;
	} 
	if (sread == 0)
		return 0;

	buf_len += sread;

	if (serial_buf[buf_len - 1] == eolchar) {
		serial_buf[buf_len - 1] = 0;			// replace end of line
		buf_len--;
		if (buf_len > 0 && serial_buf[buf_len - 1] == '\r') {
			serial_buf[buf_len - 1] = 0;		// replace carriage return
			buf_len--;
		}
		if (buf_len == 0) return 0;
		if (config.debug > 3) printf("Serial - size:%d, serial_buf:%s\n", buf_len, serial_buf);

		if (buf_len < SERIAL_INIT_LEN || serial_buf[0] != SERIAL_INIT_0 || 
				serial_buf[2] != SERIAL_INIT_2) {
			if (config.debug > 1) printf("Invalid serial input.\n");
			buf_len = 0;
			return 0;
		}
		sread = buf_len;	// if this is a valid message we will return sread
		buf_len = 0;		// resetting for the next input

		serial_buf_ptr = serial_buf + SERIAL_INIT_LEN;

		switch (serial_buf[1]) {
			case SERIAL_DEBUG_C:
				if (config.debug) printf("Serial - Debug: %s\n", serial_buf_ptr);
				break;
			case SERIAL_UUID_C:
				if (!bridge_isValid_uuid(serial_buf_ptr)) {
					if (config.debug > 1) printf("Serial - Invalid uuid.\n");
					return 0;
				}
				device = bridge_get_device(&bridge, serial_buf_ptr);
				if (!device) {
					device = bridge_add_device(&bridge, serial_buf_ptr);
					if (connected) {
						rc = mosquitto_subscribe(mosq, NULL, device->uuid, config.mqtt_qos);
						if (rc) {
							fprintf(stderr, "MQTT - Subscribe ERROR: %s\n", mosquitto_strerror(rc));
							run = 0;
							break;
						}
						if (config.debug > 1) printf("Subscribed to uuid: %s\n", device->uuid);
					}
				}
				if (!bridge.serial_uuid) {
					bridge.serial_uuid = strdup(device->uuid);
					if (!bridge.serial_uuid) {
						fprintf(stderr, "Error: Out of memory.\n");
						exit(1);
					}
					if (config.debug > 1) printf("Bridge serial_uuid: %s\n", device->uuid);
				}
				break;
			case SERIAL_SINGLE_JSON_C:
				if (!bridge.serial_uuid) {
					serialport_send(sd, SERIAL_UUID_STR);
					if (config.debug > 2) printf("Serial - Sent get uuid.\n");
					break;
				}
				device = bridge_get_device(&bridge, bridge.serial_uuid);
				device->alive = BRIDGE_ALIVE_CNT;		// Reset alive count
				if (device->server_id != 0)
					snprintf(gbuf, GBUF_SIZE, "%d", device->server_id);
				else
					snprintf(gbuf, GBUF_SIZE, "b/%s", bridge.serial_uuid);
				mqtt_publish(mosq, gbuf, serial_buf_ptr);
				break;
			case SERIAL_MULTI_JSON_C:
				if (!utils_getInt_dlm(&serial_buf_ptr, &id, '{')) {
					if (config.debug > 1) printf("Serial - Invalid id.\n");
					return 0;
				}
				device = bridge_get_device_by_id(&bridge, id);
				if (!device) {
					snprintf(gbuf, GBUF_SIZE, "%s%d", SERIAL_UUID_STR, id);
					serialport_send(sd, gbuf);
					break;
				}
				device->alive = BRIDGE_ALIVE_CNT;		// Reset alive count
				if (device->server_id != 0)
					snprintf(gbuf, GBUF_SIZE, "%d", device->server_id);
				else
					snprintf(gbuf, GBUF_SIZE, "b/%s", device->uuid);
				mqtt_publish(mosq, gbuf, serial_buf_ptr);
				break;
			case SERIAL_SINGLE_COMMA_C:
			case SERIAL_MULTI_COMMA_C:
				//TODO
				break;
			default:
				if (config.debug > 1) printf("Unknown serial data.\n");
		}
		return sread;
	} else if (buf_len == SERIAL_MAX_BUF) {
		if (config.debug > 1) printf("Serial buffer full.\n");
		buf_len = 0;
	} else {
		if (config.debug > 1) printf("Serial chunked.\n");
	}
	return 0;
}

void signal_usr(int sd, struct mosquitto *mosq)
{
	struct device_t *device;
	
	if (user_signal == SIGUSR1) {
		if (config.usr1_remap_uuid) {
			device = bridge_get_device(&bridge, config.usr1_remap_uuid);
			if (device) {
				if (device->id == 0)
					snprintf(gbuf, GBUF_SIZE, "%s%s", SERIAL_SINGLE_JSON_STR, config.usr1_json);
				else
					snprintf(gbuf, GBUF_SIZE, "%s%d%s", SERIAL_MULTI_JSON_STR, device->id, config.usr1_json);
				serialport_send(sd, gbuf);
			}
		} else if (connected) {
			snprintf(gbuf, GBUF_SIZE, "{\"push\":\"signal\",\"signal\":\"usr1\"}");
			mqtt_publish(mosq, MAIN_TOPIC, gbuf);
		}
	}

	if (user_signal == SIGUSR2) {
		if (config.usr2_remap_uuid) {
			device = bridge_get_device(&bridge, config.usr2_remap_uuid);
			if (device) {
				if (device->id == 0)
					snprintf(gbuf, GBUF_SIZE, "%s%s", SERIAL_SINGLE_JSON_STR, config.usr1_json);
				else
					snprintf(gbuf, GBUF_SIZE, "%s%d%s", SERIAL_MULTI_JSON_STR, device->id, config.usr1_json);
				serialport_send(sd, gbuf);
			}
		} else if (connected) {
			snprintf(gbuf, GBUF_SIZE, "{\"push\":\"signal\",\"signal\":\"usr2\"}");
			mqtt_publish(mosq, MAIN_TOPIC, gbuf);
		}
	}

	user_signal = 0;
}

void serial_hang(struct mosquitto *mosq)
{
	bridge.serial_ready = false;
	bridge.serial_alive = 0;

	if (connected) {
		mqtt_publish(mosq, MAIN_TOPIC, "{\"error\":\"serial\"}");
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
	struct device_t *device;
	static unsigned long last_second = 0;
	int rc, i;
	
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

	rc = bridge_init(&bridge, config.uuid);
	if (rc) {
		if (config.debug) printf("Error: Failed to initialize bridge: %d\n", rc);
		return 1;
	}

	mosquitto_lib_init();
	mosq = mosquitto_new(config.uuid, true, NULL);
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

	mosquitto_connect_callback_set(mosq, on_mqtt_connect);
	mosquitto_disconnect_callback_set(mosq, on_mqtt_disconnect);
	mosquitto_message_callback_set(mosq, on_mqtt_message);
	mosquitto_user_data_set(mosq, &sd);

	if (config.scripts_folder) {
		if (access(config.scripts_folder, R_OK )) {
			fprintf(stderr, "Couldn't open scripts folder: %s\n", config.scripts_folder);
			return 1;
		}
	}

	if (config.interface) {
		//TODO: check if interface exists
		if (access("/proc/net/dev", R_OK )) {
			fprintf(stderr, "Couldn't open /proc/net/dev\n");
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
			serialport_flush(sd);
			bridge.serial_ready = true;
			if (config.debug) printf("Serial ready.\n");
		}
	}

	rc = mosquitto_connect(mosq, config.mqtt_host, config.mqtt_port, 60);
	if (rc) {
		//TODO: ERROR: Error defined by errno.
		fprintf(stderr, "ERROR: %s\n", mosquitto_strerror(rc));
		return -1;
	}

	alarm(1);

	while (run) {
		if (bridge.serial_ready) {
			rc = serial_in(sd, mosq);
			if (rc == -1) {
				serial_hang(mosq);
			} else if (rc > 0) {
				bridge.serial_alive = BRIDGE_ALIVE_CNT;
			}
		}

		if (user_signal) {
			if (config.debug > 2) printf("Signal - SIGUSR: %d\n", user_signal);
			signal_usr(sd, mosq);
		}

		rc = mosquitto_loop(mosq, 100, 1);
		if (run && rc) {
			if (config.debug > 2) printf("MQTT loop: %s\n", mosquitto_strerror(rc));
			usleep(1000000);	// wait 100 msec
			mosquitto_reconnect(mosq);
		}
		//usleep(20);
		
		if (seconds != last_second) {
			last_second = seconds;
			if (bridge.serial_alive) {
				bridge.serial_alive--;
				if (!bridge.serial_alive) {
					if (config.debug > 1) printf("Serial timeout.\n");
					serial_hang(mosq);
				}
			}

			if (seconds % 30 == 0) {
				if (connected) {
					send_alive(mosq);

					if (bandwidth) {
						snprintf(gbuf, GBUF_SIZE, "{\"push\":\"bandwidth\",\"up\":%.0f,\"down\":%.0f}", upspeed, downspeed);
						mqtt_publish(mosq, MAIN_TOPIC, gbuf);
						if (config.debug > 2) printf("down: %f - up: %f\n", downspeed, upspeed);
					}
				} else {
					if (config.debug) printf("MQTT Offline.\n");
				}

				for (device = bridge.device_list; device != NULL; device = device->next) {
					device->alive -= 30;
					if (device->alive < 0) {
						if (connected) {
							if (device->server_id != 0)
								snprintf(gbuf, GBUF_SIZE, "%d", device->server_id);
							else
								snprintf(gbuf, GBUF_SIZE, "b/%s", bridge.serial_uuid);
							mqtt_publish(mosq, gbuf, "{\"timeout\":1}");
						}
						if (config.debug) printf("Device: %s - Timeout.\n", device->uuid);
						bridge_remove_device(&bridge, device->uuid);
					}
				}

				if (!bridge.serial_alive) {
					if (config.serial.port && !bridge.serial_ready) {
						if (config.debug > 1) printf("Trying to reconnect serial port.\n");
						serialport_close(sd);
						sd = serialport_init(config.serial.port, config.serial.baudrate);
						if( sd == -1 ) {
							fprintf(stderr, "Couldn't open serial port.\n");
						} else {
							serialport_flush(sd);
							bridge.serial_ready = true;
							if (connected)
								mqtt_publish(mosq, MAIN_TOPIC, "{\"trig\":\"serial\",\"serial\":\"open\"}");
							if (config.debug) printf("Serial reopened.\n");
						}
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
