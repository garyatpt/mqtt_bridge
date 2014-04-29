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
#include "modules.h"

struct bridge {
	struct device *bridge_dev;
	bool serial_ready;
	int serial_alive;
	int total_devices;
	struct device *dev_list;
};

int bridge_init(struct bridge *, char *, char *);
struct device *bridge_add_device(struct bridge *, char *, char *);
struct module* bridge_add_module(struct device *, char *, bool);
struct device *bridge_get_device(struct bridge *, char *);
struct module *bridge_get_module(struct device *, char *);
void bridge_set_device_topics(struct device *);
int bridge_set_md_topic(struct module *, char *);
int bridge_set_module_specs(struct module *, char *);
int bridge_remove_device(struct bridge *, char *);
int bridge_remove_module(struct bridge *, struct device *, char *);
int bridge_remove_all_modules(struct bridge *, struct device *);
void bridge_print_device(struct device *);
void bridge_print_devices(struct bridge *);
void bridge_print_module(struct module *);
void bridge_print_modules(struct device *);
int bridge_isValid_device_id(char *);
int bridge_isValid_module_id(char *);
int bridge_isValid_module_topic(char *);
int bridge_isValid_module_specs(char *);

#endif
