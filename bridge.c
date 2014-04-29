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

const char *modules_name[MODULES_NAME_SIZE] = {"dummy", "temp", "ldr", "hum", "alarmsys1", "smon", "acpower", "dcpower"
	, "amps", "volts", "watts", "rain", "sonar", "led", "ledrgb", "lcd", "bts", "btl", "script", "bandwidth", "serial"
	, "mqtt", "bridge"};

int bridge_init(struct bridge *bdev, char *id, char *md_id)
{
	struct device *dev;
	struct module *md;

	if (!bridge_isValid_device_id(id))
		return 1;

	if ((dev = malloc(sizeof(struct device))) == NULL) {
		fprintf(stderr, "No memory left.\n");
		exit(1);
	}

	dev->id = strdup(id);
	if (!dev->id) {
		fprintf(stderr, "Error: No memory left.\n");
		exit(1);
	}
	dev->alive = 0;
	dev->md_topic_sub = 0;
	dev->modules = 0;			// Set 0 before calling bridge_add_module
	dev->md_list = NULL;		// Set NULL before calling bridge_add_module
	md = bridge_add_module(dev, md_id, true);
	if (!md) {
		return -1;
	}
	dev->md_deps = md;
	bridge_set_device_topics(dev);
	dev->next = NULL;

	bdev->bridge_dev = dev;
	bdev->serial_ready = 0;
	bdev->serial_alive = 0;
	bdev->total_devices = 0;		//CHECK
	bdev->dev_list = NULL;			//CHECK

	return 0;
}

struct device *bridge_add_device(struct bridge *bdev, char *dev_id, char *md_id)
{
	struct device *dev;
	struct module *md;

	if (!bridge_isValid_device_id(dev_id))
		return NULL;
	if (!bridge_isValid_module_id(md_id))
		return NULL;

	md = bridge_get_module(bdev->bridge_dev, md_id);
	if (!md)
		return NULL;

	// Search devices with same id, return if true
	for (dev = bdev->dev_list; dev != NULL; dev = dev->next) {
		if (!strcmp(dev->id, dev_id))
			return dev;
	}

	if ((dev = malloc(sizeof(struct device))) == NULL) {
		fprintf(stderr, "No memory left.\n");
		exit(1);
	}

	bdev->total_devices++;
	dev->next = bdev->dev_list;
	bdev->dev_list = dev;

	dev->id = strdup(dev_id);
	if (!dev->id) {
		fprintf(stderr, "Error: No memory left.\n");
		exit(1);
	}
	dev->alive = DEVICE_ALIVE_MAX;
	dev->md_topic_sub = 0;
	dev->modules = 0;
	dev->md_deps = md;
	dev->md_list = NULL;
	dev->next = NULL;

	bridge_set_device_topics(dev);

	return dev;
}

struct module* bridge_add_module(struct device *dev, char *md_id, bool enabled)
{
	struct module *md;
	int len;

	if (!bridge_isValid_module_id(md_id))
		return NULL;

	// Search modules with same id
	for (md = dev->md_list; md != NULL; md = md->next) {
		if (!strcmp(md->id, md_id))
			return md;
	}

	if ((md = malloc(sizeof(struct module))) == NULL) {
		fprintf(stderr, "No memory left.\n");
		exit(1);
	}

	md->id = strdup(md_id);
	if (!md->id) {
		fprintf(stderr, "Error: No memory left.\n");
		exit(1);
	}

	md->enabled = enabled;
	md->type = utils_htoi(md->id[0]) * 16 + utils_htoi(md->id[1]);
	md->specs = NULL;

	len = snprintf(NULL, 0, "raw/%s/%s", dev->id, md_id);
	if ((md->topic = malloc((len + 1)* (sizeof(char)))) == NULL) {
		fprintf(stderr, "No memory left.\n");
		exit(1);
	}
	snprintf(md->topic, len + 1, "raw/%s/%s", dev->id, md_id);

	md->next = dev->md_list;
	dev->md_list = md;
	dev->modules++;

	return md;
}

struct device *bridge_get_device(struct bridge *bdev, char *dev_id)
{
	struct device *dev;

	if (!bridge_isValid_device_id(dev_id))
		return NULL;

	for (dev = bdev->dev_list; dev != NULL; dev = dev->next) {
		if (!strcmp(dev->id, dev_id))
			return dev;
	}
	return NULL;
}

struct module* bridge_get_module(struct device *dev, char *md_id)
{
	struct module *md;

	for (md = dev->md_list; md != NULL; md = md->next) {
		if (!strcmp(md->id, md_id))
			return md;
	}
	return NULL;
}

void bridge_set_device_topics(struct device *dev)
{
	int len;

	// config topic
	len = snprintf(NULL, 0, "%s%s", DEVICE_TOPIC_CONFIG, dev->id);
	if ((dev->config_topic = malloc((len + 1)* (sizeof(char)))) == NULL) {
		fprintf(stderr, "No memory left.\n");
		exit(1);
	}
	snprintf(dev->config_topic, len + 1, "%s%s", DEVICE_TOPIC_CONFIG, dev->id);

	// status topic
	len = snprintf(NULL, 0, "%s%s", DEVICE_TOPIC_STATUS, dev->id);
	if ((dev->status_topic = malloc((len + 1)* (sizeof(char)))) == NULL) {
		fprintf(stderr, "No memory left.\n");
		exit(1);
	}
	snprintf(dev->status_topic, len + 1, "%s%s", DEVICE_TOPIC_STATUS, dev->id);

}

int bridge_set_md_topic(struct module *module, char *topic)
{
	if (!bridge_isValid_module_topic(topic))
		return 0;

	if (!strcmp(topic, module->topic))
		return 1;

	if (module->topic) {
		free(module->topic);
	}

	module->topic = strdup(topic);
	if (!module->topic) {
		fprintf(stderr, "Error: No memory left.\n");
		exit(1);
	}

	return 1;
}

int bridge_set_module_specs(struct module *md, char *specs)
{
	if (!bridge_isValid_module_specs(specs))
		return 0;

	if (md->specs) {
		free(md->specs);
	}

	md->specs = strdup(specs);
	if (!md->specs) {
		fprintf(stderr, "Error: No memory left.\n");
		exit(1);
	}
	return 1;
}

int bridge_remove_device(struct bridge *bdev, char *dev_id)
{
	struct device *dev, *prev_dev;

	if (!bridge_isValid_device_id(dev_id))
		return 0;

	for (prev_dev = dev = bdev->dev_list; dev != NULL; dev = dev->next) {
		if (strcmp(dev->id, dev_id)) {
			prev_dev = dev;
			continue;
		}

		if (!strcmp(dev->id, prev_dev->id)) {		// First element
			bdev->dev_list = dev->next;
		} else {
			prev_dev->next = dev->next;
		}
		bdev->total_devices--;
		free(dev->id);
		free(dev->status_topic);
		free(dev->config_topic);
		free(dev);
		return 1;
	}
	return 0;
}

int bridge_remove_module(struct bridge *bdev, struct device *dev, char *md_id)
{
	struct module *prev_md, *md;

	for (dev = bdev->dev_list; dev != NULL; dev = dev->next) {
		if (!strcmp(dev->md_deps->id, md_id))		//Can't remove module if a device depends it
			return 0;
	}

	for (prev_md = md = dev->md_list; md != NULL; md = md->next) {
		if (strcmp(md->id, md_id)) {
			prev_md = md;
			continue;
		}

		if (!strcmp(md->id, prev_md->id)) {		// First element
			dev->md_list = md->next;
		} else {
			prev_md->next = md->next;
		}
		dev->modules--;
		free(md->id);
		free(md->specs);
		free(md->topic);
		free(md);
		return 1;
	}
	return 0;
}

int bridge_remove_all_modules(struct bridge *bdev, struct device *dev)
{
	struct module *md;

	md = dev->md_list;
	while (md != NULL) {
		if (!bridge_remove_module(bdev, dev, md->id)) {
			return 0;
		}
		md = dev->md_list;
	}
	return 1;
}

void bridge_print_device(struct device *dev)
{
	printf("       id: %s\n       alive: %d\n       depends: %s\n       modules: %d\n       config topic: %s\n",
	dev->id, dev->alive, dev->md_deps->id, dev->modules, dev->config_topic);
}

void bridge_print_devices(struct bridge *bdev)
{
	struct device *dev;

	printf("Devices:\n");

	for (dev = bdev->dev_list; dev != NULL; dev = dev->next) {
		bridge_print_device(dev);
	}
}

void bridge_print_module(struct module *md)
{
	printf("       id: %s\n       enabled: %d\n       type: %s\n       specs: %s\n       topic: %s\n",
		md->id, md->enabled, modules_name[md->type], md->specs, md->topic);
}

void bridge_print_modules(struct device *dev)
{
	struct module *md;

	printf("Modules:\n");
	
	for (md = dev->md_list; md != NULL; md = md->next) {
		bridge_print_module(md);
	}
}

int bridge_isValid_device_id(char *id)
{
	if (strlen(id) != DEVICE_ID_SIZE)
		return 0;

	return 1;
}

int bridge_isValid_module_id(char *md_id)
{
	int type;

	if (strlen(md_id) != MODULES_ID_SIZE)
		return 0;

	//type = (((md_id[0] - 48) * 100) + ((md_id[1] - 48) * 10) + (md_id[2] - 48));
	type = utils_htoi(md_id[0]) * 16 + utils_htoi(md_id[1]);
	if (type < 0 || type > 255 || type >= MODULES_NAME_SIZE)
		return 0;

	return 1;
}

int bridge_isValid_module_topic(char *topic) {
	if (strlen(topic) < DEVICE_TOPIC_MIN_SIZE || strlen(topic) > DEVICE_TOPIC_MAX_SIZE)
		return 0;
	return 1;
}

int bridge_isValid_module_specs(char *specs) {
	if (strlen(specs) < DEVICE_SPECS_MIN_SIZE || strlen(specs) > DEVICE_SPECS_MAX_SIZE)
		return 0;
	return 1;
}
