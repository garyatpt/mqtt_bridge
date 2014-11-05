#!/bin/bash
rm -rf mqtt_bridge
gcc -Wall -lmosquitto mqtt_bridge.c utils.c conf.c bridge.c arduino-serial-lib.c cJSON.c -o mqtt_bridge -lm
