SRC_NAME= wireguard-1.0.20210914

CFLAGS +=$(COPT) -ffunction-sections -fdata-sections -fvisibility=hidden
LDFLAGS += -Wl,--gc-sections
#LDFLAGS += -Wl,--gc-sections,-rpath-link,$(STAGEDIR)/lib -lcrypto -lssl

all: config_test
	$(MAKE) -j$(HOST_NCPU) -C $(SRC_NAME)

config_test:
	( if [ -f ./config_done ]; then \
		echo "the same configuration"; \
	else \
		touch config_done; \
	fi )

clean:
	if [ -f $(SRC_NAME)/Makefile ] ; then \
		$(MAKE) -C $(SRC_NAME) clean ; \
	fi ; \
	rm config_done

romfs:
	$(ROMFSINST) $(SRC_NAME)/wg /usr/sbin/wg
	$(ROMFSINST) -p +x /usr/bin/wgw.sh
	$(ROMFSINST) -p +x /usr/bin/wg-quick.sh
	$(ROMFSINST) -p +x /usr/bin/wireguard.sh