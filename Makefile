CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
CPPFLAGS ?= -Iinclude
LDFLAGS ?=
LDLIBS ?= -lsqlite3 -lm
INSTALL ?= install
INSTALL_DIR ?= $(INSTALL) -d -m 0755
INSTALL_BIN ?= $(INSTALL) -m 0755
DESTDIR ?=

TARGET := edgepulse
CTL_TARGET := edgepulse-ctl
TEST_TARGET := tests/unit/test_edgepulse
LIB_SRCS := src/edgepulse-lib/edgepulse.c
DAEMON_SRCS := src/edgepulse-daemon/main.c
CTL_SRCS := src/edgepulse-ctl/main.c
TEST_SRCS := tests/unit/test_edgepulse.c

.PHONY: all clean install test

all: $(TARGET) $(CTL_TARGET)

$(TARGET): $(DAEMON_SRCS) $(LIB_SRCS) include/edgepulse.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(DAEMON_SRCS) $(LIB_SRCS) $(LDFLAGS) $(LDLIBS)

$(CTL_TARGET): $(CTL_SRCS) $(LIB_SRCS) include/edgepulse.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CTL_SRCS) $(LIB_SRCS) $(LDFLAGS) $(LDLIBS)

$(TEST_TARGET): $(TEST_SRCS) $(LIB_SRCS) include/edgepulse.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(TEST_SRCS) $(LIB_SRCS) $(LDFLAGS) $(LDLIBS)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(TARGET) $(CTL_TARGET) $(TEST_TARGET)

install: $(TARGET) $(CTL_TARGET)
	$(INSTALL_DIR) $(DESTDIR)/usr/bin
	$(INSTALL_BIN) $(TARGET) $(DESTDIR)/usr/bin/$(TARGET)
	$(INSTALL_BIN) $(CTL_TARGET) $(DESTDIR)/usr/bin/$(CTL_TARGET)
