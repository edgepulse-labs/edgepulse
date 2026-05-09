CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
CPPFLAGS ?= -Iinclude
LDFLAGS ?=
INSTALL ?= install
INSTALL_DIR ?= $(INSTALL) -d -m 0755
INSTALL_BIN ?= $(INSTALL) -m 0755
DESTDIR ?=

TARGET := edgepulse
SRCS := src/edgepulse-daemon/main.c

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRCS) include/edgepulse.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	$(INSTALL_DIR) $(DESTDIR)/usr/bin
	$(INSTALL_BIN) $(TARGET) $(DESTDIR)/usr/bin/$(TARGET)
