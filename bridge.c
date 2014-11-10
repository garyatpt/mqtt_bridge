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

#include "bridge.h"
#include "mqtt_bridge.h"
#include "utils.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

int bridge_init(struct bridge_t *bridge, char *uuid)
{
	bridge->uuid = strdup(uuid);
	if (!bridge->uuid) {
		fprintf(stderr, "Error: No memory left.\n");
		exit(1);
	}
	bridge->devices = 0;
	bridge->device_list = NULL;
	bridge->serial_ready = 0;
	bridge->serial_alive = 0;
	bridge->serial_uuid = NULL;

	return 0;
}

struct device_t* bridge_add_device(struct bridge_t *bridge, char *uuid) {
	struct device_t *device;

	if ((device = malloc(sizeof(struct device_t))) == NULL) {
		fprintf(stderr, "No memory left.\n");
		exit(1);
	}

	device->uuid = strdup(uuid);
	if (!device->uuid) {
		fprintf(stderr, "Error: No memory left.\n");
		exit(1);
	}

	device->id = 0;
	device->server_id = 0;
	device->alive = BRIDGE_ALIVE_CNT;
	device->next = bridge->device_list;
	bridge->device_list = device;
	bridge->devices++;

	return device;
}

struct device_t* bridge_get_device(struct bridge_t *bridge, char *uuid)
{
	struct device_t *device;

	for (device = bridge->device_list; device != NULL; device = device->next) {
		if (!strcmp(device->uuid, uuid))
			return device;
	}
	return NULL;
}

struct device_t *bridge_get_device_by_id(struct bridge_t *bridge, int id)
{
	struct device_t *device;

	for (device = bridge->device_list; device != NULL; device = device->next) {
		if (device->id == id)
			return device;
	}
	return NULL;
}

int bridge_remove_device(struct bridge_t *bridge, char *uuid)
{
	struct device_t *prev_device, *device;

	for (prev_device = device = bridge->device_list; device != NULL; device = device->next) {
		if (strcmp(device->uuid, uuid)) {
			prev_device = device;
			continue;
		}

		if (!strcmp(bridge->serial_uuid, uuid)) {
			free(bridge->serial_uuid);
			bridge->serial_uuid = NULL;
		}

		if (!strcmp(device->uuid, prev_device->uuid)) {		// First of the list
			bridge->device_list = device->next;
		} else {
			prev_device->next = device->next;
		}
		bridge->devices--;
		free(device->uuid);
		free(device);
		return 1;
	}
	return 0;
}

void bridge_print_device(struct device_t *device)
{
	printf("       uuid: %s\n       id: %d\n       alive: %d\n",
		device->uuid, device->id, device->alive);
}

void bridge_print_devices(struct bridge_t *bridge)
{
	struct device_t *device;

	printf("Devices:\n");
	
	for (device = bridge->device_list; device != NULL; device = device->next) {
		bridge_print_device(device);
	}
}

int bridge_isValid_uuid(char *uuid)
{
	int i, valid;

	if (uuid == NULL)
		return 0;

	for (i = 0, valid = 1; uuid[i] && valid; i++) {
		switch (i) {
		case 8: case 13: case 18: case 23:
			valid = (uuid[i] == '-');
			break;
		default:
			valid = isxdigit(uuid[i]);
			break;
		}
	}

	if (i != 36 || !valid)
		return 0;

	return 1;
}
