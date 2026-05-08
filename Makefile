CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
CPPFLAGS ?= -Iinclude
LDFLAGS ?=

TARGET := edgepulse
SRCS := src/edgepulse-daemon/main.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS) include/edgepulse.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET)

