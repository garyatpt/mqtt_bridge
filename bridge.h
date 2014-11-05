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

#ifndef BRIDGE_H
#define BRIDGE_H

#include <stdbool.h>
#include "device.h"

#define BRIDGE_ALIVE_CNT 360				// 6 minutes
#define BRIDGE_TOPIC "0"

struct bridge_t {
	char *uuid;
	bool serial_ready;
	int serial_alive;
	char *serial_uuid;
	int devices;
	struct device_t *device_list;
};

int bridge_init(struct bridge_t *, char *);
struct device_t *bridge_add_device(struct bridge_t *, char *);
struct device_t *bridge_get_device(struct bridge_t *, char *);
struct device_t *bridge_get_device_by_id(struct bridge_t *, int);
int bridge_remove_device(struct bridge_t *, char *);
void bridge_print_device(struct device_t *);
void bridge_print_devices(struct bridge_t *);
int bridge_isValid_uuid(char *);

#endif
