CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
CPPFLAGS ?= -Iinclude -DEDGEPULSE_ENABLE_AI_AGENT
LDFLAGS ?=
LDLIBS ?= -lsqlite3 -lm
INSTALL ?= install
INSTALL_DIR ?= $(INSTALL) -d -m 0755
INSTALL_BIN ?= $(INSTALL) -m 0755
INSTALL_DATA ?= $(INSTALL) -m 0644
DESTDIR ?=

TARGET := edgepulse
CTL_TARGET := edgepulse-ctl
TEST_TARGET := tests/unit/test_edgepulse
AGENT_TEST_TARGET := tests/unit/test_agent_ctl
MOCK_OPENAI_SERVER := tests/integration/mock_openai_server
SKILL_MANIFESTS := $(wildcard skills.d/*.json)
LIB_SRCS := src/edgepulse-lib/edgepulse.c
DAEMON_SRCS := src/edgepulse-daemon/main.c
CTL_SRCS := src/edgepulse-ctl/main.c
TEST_SRCS := tests/unit/test_edgepulse.c
AGENT_TEST_SRCS := tests/unit/test_agent_ctl.c
MOCK_OPENAI_SERVER_SRCS := tests/integration/mock_openai_server.c

.PHONY: all clean install test security-check integration-agent-model integration-agent-operations integration-agent-mcp integration-agent-shared-transcript integration-agent-local openwrt-agent-e2e

all: $(TARGET) $(CTL_TARGET)

$(TARGET): $(DAEMON_SRCS) $(LIB_SRCS) include/edgepulse.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(DAEMON_SRCS) $(LIB_SRCS) $(LDFLAGS) $(LDLIBS)

$(CTL_TARGET): $(CTL_SRCS) $(LIB_SRCS) include/edgepulse.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CTL_SRCS) $(LIB_SRCS) $(LDFLAGS) $(LDLIBS)

$(TEST_TARGET): $(TEST_SRCS) $(LIB_SRCS) include/edgepulse.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(TEST_SRCS) $(LIB_SRCS) $(LDFLAGS) $(LDLIBS)

$(AGENT_TEST_TARGET): $(AGENT_TEST_SRCS) $(LIB_SRCS) include/edgepulse.h $(CTL_SRCS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(AGENT_TEST_SRCS) $(LIB_SRCS) $(LDFLAGS) $(LDLIBS)

$(MOCK_OPENAI_SERVER): $(MOCK_OPENAI_SERVER_SRCS)
	$(CC) $(CFLAGS) -o $@ $(MOCK_OPENAI_SERVER_SRCS)

test: security-check $(TEST_TARGET) $(AGENT_TEST_TARGET)
	./$(TEST_TARGET)
	./$(AGENT_TEST_TARGET)

security-check:
	sh scripts/check-agent-safety.sh

integration-agent-model: $(CTL_TARGET) $(MOCK_OPENAI_SERVER)
	sh tests/integration/agent_model_integration.sh

integration-agent-operations: $(CTL_TARGET)
	sh tests/integration/agent_operations_fixture.sh

integration-agent-mcp: $(CTL_TARGET)
	sh tests/integration/agent_mcp_fixture.sh

integration-agent-shared-transcript: $(CTL_TARGET)
	sh tests/integration/agent_shared_transcript.sh

integration-agent-local: integration-agent-operations integration-agent-mcp integration-agent-shared-transcript

openwrt-agent-e2e:
	sh scripts/ai-agent-openwrt-e2e.sh

clean:
	rm -f $(TARGET) $(CTL_TARGET) $(TEST_TARGET) $(AGENT_TEST_TARGET) $(MOCK_OPENAI_SERVER)

install: $(TARGET) $(CTL_TARGET)
	$(INSTALL_DIR) $(DESTDIR)/usr/bin
	$(INSTALL_BIN) $(TARGET) $(DESTDIR)/usr/bin/$(TARGET)
	$(INSTALL_BIN) $(CTL_TARGET) $(DESTDIR)/usr/bin/$(CTL_TARGET)
ifneq ($(SKILL_MANIFESTS),)
	$(INSTALL_DIR) $(DESTDIR)/usr/share/edgepulse/skills.d
	$(INSTALL_DATA) $(SKILL_MANIFESTS) $(DESTDIR)/usr/share/edgepulse/skills.d/
endif
