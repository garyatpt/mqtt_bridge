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

#ifndef MQTT_BRIDGE_H
#define MQTT_BRIDGE_H

#define MQTT_RETAIN 0
#define MQTT_MAX_PAYLOAD_LEN 128
#define UUID_LEN 36

struct bridge_serial{
	char *port;
	int baudrate;
	int timeout;
	int qos;
};

struct bridge_config{
	int debug;
	char *uuid;
	char *mqtt_host;
	int mqtt_port;
	int mqtt_qos;
	struct bridge_serial serial;
	char *scripts_folder;
	char *interface;
	char *usr1_remap_uuid;
	char *usr2_remap_uuid;
	char *usr1_json;
	char *usr2_json;
};

int config_parse(const char *conffile, struct bridge_config *config);
void config_cleanup(struct bridge_config *config);

#endif
