SRC_NAME=usbutils-017
SRC_URL=https://mirror.yandex.ru/pub/OpenBSD/OpenSSH/portable/$(SRC_NAME).tar.gz

CFLAGS += -ffunction-sections -fdata-sections
LDFLAGS += -Wl,--gc-sections

THISDIR = $(shell pwd)

all: download_test extract_test config_test
	$(MAKE) -j$(HOST_NCPU) -C $(SRC_NAME)
	$(MAKE) -j$(HOST_NCPU) -C $(SRC_NAME) install

download_test:

extract_test:
	( if [ ! -d $(SRC_NAME) ]; then \
		tar xzf $(SRC_NAME).tar.gz; \
	fi )

config_test:
	( if [ -f ./config_done ]; then \
		echo "the same configuration"; \
	else \
		make configure && touch config_done; \
	fi )

configure:
	( cd $(SRC_NAME) ; \
	autoreconf -fi ; \
	./configure \
		--prefix=/usr \
		--with-libusb-1.0=$(STAGEDIR) \
		--with-pkgconfigdir=$(STAGEDIR) \
		--host=$(HOST_TARGET) \
		--build=$(HOST_BUILD) ; \
	)

clean:
	if [ -f $(SRC_NAME)/Makefile ] ; then \
		$(MAKE) -C $(SRC_NAME) distclean ; \
	fi ; \
	rm -f config_done

romfs:
