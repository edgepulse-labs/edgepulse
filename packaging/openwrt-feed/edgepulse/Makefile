include $(TOPDIR)/rules.mk

PKG_NAME:=edgepulse
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)

ifeq ($(EDGEPULSE_LOCAL_SOURCE),)
PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/yihua1218/edgepulse.git
PKG_SOURCE_VERSION:=main
PKG_MIRROR_HASH:=skip
endif

include $(INCLUDE_DIR)/package.mk

define Package/edgepulse
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=EdgePulse telemetry daemon
  DEPENDS:=+libsqlite3 +libubox +libubus +libblobmsg-json +EDGEPULSE_ENABLE_AI_AGENT:ubus +EDGEPULSE_ENABLE_AI_AGENT:uclient-fetch +EDGEPULSE_ENABLE_AI_AGENT:libustream-mbedtls
endef

define Package/edgepulse/config
  config EDGEPULSE_ENABLE_AI_AGENT
	bool "Enable EdgePulse AI agent MVP"
	default n
	help
	  Include the optional read-only AI agent MVP commands and default
	  UCI configuration. The runtime agent remains disabled until
	  /etc/config/edgepulse enables it.

  config EDGEPULSE_AI_DEFAULT_BASE_URL
	string "Default AI agent model API base URL"
	depends on EDGEPULSE_ENABLE_AI_AGENT
	default ""
	help
	  Optional OpenAI-compatible model API base URL for development images.

  config EDGEPULSE_AI_DEFAULT_MODEL
	string "Default AI agent model name"
	depends on EDGEPULSE_ENABLE_AI_AGENT
	default ""
	help
	  Optional default model name for development images.
endef

define Package/edgepulse/description
 EdgePulse collects local device telemetry for OpenWrt and prepares
 time-window features for later AI training workflows.
endef

EDGEPULSE_AGENT_CFLAGS:=
ifeq ($(CONFIG_EDGEPULSE_ENABLE_AI_AGENT),y)
EDGEPULSE_AGENT_CFLAGS+=-DEDGEPULSE_ENABLE_AI_AGENT
ifneq ($(CONFIG_EDGEPULSE_AI_DEFAULT_BASE_URL),)
EDGEPULSE_AGENT_CFLAGS+=-DEDGEPULSE_AI_DEFAULT_BASE_URL=\"$(CONFIG_EDGEPULSE_AI_DEFAULT_BASE_URL)\"
endif
ifneq ($(CONFIG_EDGEPULSE_AI_DEFAULT_MODEL),)
EDGEPULSE_AGENT_CFLAGS+=-DEDGEPULSE_AI_DEFAULT_MODEL=\"$(CONFIG_EDGEPULSE_AI_DEFAULT_MODEL)\"
endif
endif

ifeq ($(EDGEPULSE_LOCAL_SOURCE),)
define Build/Prepare
	$(call Build/Prepare/Default)
endef
else
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(EDGEPULSE_LOCAL_SOURCE)/. $(PKG_BUILD_DIR)/
	$(MAKE) -C $(PKG_BUILD_DIR) clean
endef
endif

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) \
		CC="$(TARGET_CC)" \
		CPPFLAGS="$(TARGET_CPPFLAGS) -Iinclude" \
		CFLAGS="$(TARGET_CFLAGS) -Wall -Wextra -DEDGEPULSE_ENABLE_UBUS $(EDGEPULSE_AGENT_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		LDLIBS="-lsqlite3 -lubus -lubox -lblobmsg_json -lm"
endef

define Package/edgepulse/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/edgepulse $(1)/usr/bin/edgepulse
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/edgepulse-ctl $(1)/usr/bin/edgepulse-ctl
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/edgepulse $(1)/etc/config/edgepulse
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/edgepulse $(1)/etc/init.d/edgepulse
endef

$(eval $(call BuildPackage,edgepulse))
