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

#ifndef MODULES_H
#define MODULES_H

#define MODULES_ID_SIZE 4

#define MODULES_DUMMY 0					// Dummy module
#define MODULES_TEMP 1					// Temperature Celsius
#define MODULES_LDR 2					// Light sense
#define MODULES_HUM 3					// Humidity
#define MODULES_ALARMSYS1 4				// Alarm system with 24H, 24H Silent, ARM, ARM Silent
#define MODULES_SMON 5					// Sensors monitoring
#define MODULES_AC 6					// AC Power
#define MODULES_DC 7					// DC Power
#define MODULES_AMP 8					// Amperemeter
#define MODULES_VOLT 9					// Voltmeter
#define MODULES_WATT 10					// Wattmeter
#define MODULES_RAIN 11					// Rain sensor
#define MODULES_SONAR 12				// Sonar
#define MODULES_LED 13					// Led
#define MODULES_LEDRGB 14				// RGB Led
#define MODULES_LCD 15					// LCD
#define MODULES_BUTTON 16				// Button press
#define MODULES_FLAG 17					// Custom flag
#define MODULES_SCRIPT 18
#define MODULES_BANDWIDTH 19
#define MODULES_SERIAL 20
#define MODULES_MQTT 21
#define MODULES_BRIDGE 22

#define MODULES_NAME_SIZE 23

#define MODULES_SCRIPT_ID "12FF"		// Type 12 Hex - 18 Dec
#define MODULES_BANDWIDTH_ID "13FF"		// Type 13 Hex - 19 Dec
#define MODULES_SERIAL_ID "14FF"		// Type 14 Hex - 20 Dec
#define MODULES_MQTT_ID "15FF"			// Type 15 Hex - 21 Dec
#define MODULES_BRIDGE_ID "16FF"		// Type 16 Hex - 22 Dec

#define MODULES_GENERIC_RAW 0

#define MODULES_SCRIPT_LIST 1
#define MODULES_SCRIPT_EXECUTE 2

#define MODULES_BRIDGE_DEBUG 1
#define MODULES_BRIDGE_SIGUSR1 2
#define MODULES_BRIDGE_SIGUSR2 3

#define MODULES_SERIAL_OPEN 1
#define MODULES_SERIAL_ERROR 2

struct module {
	char *id;
	bool enabled;
	int type;
	char *specs;
	char *topic;
	struct module *next;
};

#endif
