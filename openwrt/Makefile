include ../config.mk

.PHONY: all install uninstall reallyclean clean

all : mqtt_bridge

mqtt_bridge : mqtt_bridge.o conf.o arduino-serial-lib.o bridge.o utils.o cJSON.o ../lib/libmosquitto.so.${SOVERSION}
	${CC} $< -o $@ conf.o arduino-serial-lib.o bridge.o utils.o cJSON.o ${CLIENT_LDFLAGS} -lm

mqtt_bridge.o : mqtt_bridge.c mqtt_bridge.h ../lib/libmosquitto.so.${SOVERSION}
	${CC} -c $< -o $@ ${CLIENT_CFLAGS}

conf.o : conf.c mqtt_bridge.h bridge.h
	${CC} -c $< -o $@ ${CLIENT_CFLAGS}

arduino-serial-lib.o : arduino-serial-lib.c arduino-serial-lib.h
	${CC} -c $< -o $@ ${CLIENT_CFLAGS}

bridge.o : bridge.c bridge.h mqtt_bridge.h utils.h
	${CC} -c $< -o $@ ${CLIENT_CFLAGS}
	
utils.o : utils.c utils.h
	${CC} -c $< -o $@ ${CLIENT_CFLAGS}

cJSON.o : cJSON.c cJSON.h
	${CC} -c $< -o $@ ${CLIENT_CFLAGS}

../lib/libmosquitto.so.${SOVERSION} :
	$(MAKE) -C ../lib

install : all
	$(INSTALL) -d ${DESTDIR}$(prefix)/bin
	$(INSTALL) -s mqtt_bridge ${DESTDIR}${prefix}/bin/mqtt_bridge

uninstall :
	-rm -f ${DESTDIR}${prefix}/bin/mqtt_bridge

reallyclean : clean

clean : 
	-rm -f *.o mqtt_bridge
