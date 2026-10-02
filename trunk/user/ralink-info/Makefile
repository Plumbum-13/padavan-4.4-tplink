APP_NAME=ralink-info

ifndef ROOTDIR
ROOTDIR=../..
endif

USERDIR = $(ROOTDIR)/user
SHDIR = $(ROOTDIR)/user/shared
INSTALLDIR = $(ROOTDIR)/romfs

include $(SHDIR)/boards.mk
include $(SHDIR)/cflags.mk

CFLAGS += -s -I. -I$(SHDIR) -I$(SHDIR)/include
CFLAGS += -I$(USERDIR)/wireless_tools
CFLAGS += -I$(ROOTDIR)/$(LINUXDIR)/include/ralink

# Disable/Enable ugly httpd_check
#CFLAGS += -DHTTPD_CHECK

LDFLAGS += -L. -lm
LDFLAGS += -L$(USERDIR)/wireless_tools -liw
LDFLAGS += -L$(SHDIR) -lshared

CFLAGS += -ffunction-sections -fdata-sections
LDFLAGS += -Wl,--gc-sections

all:
	$(CC) $(CFLAGS) $(LDFLAGS) src/*.c -o $(APP_NAME)

clean:
	rm -f $(APP_NAME) *.o

romfs:
	$(ROMFSINST) /usr/sbin/$(APP_NAME)
