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
  DEPENDS:=+libsqlite3 +libubox +libubus +libblobmsg-json
endef

define Package/edgepulse/description
 EdgePulse collects local device telemetry for OpenWrt and prepares
 time-window features for later AI training workflows.
endef

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
		CFLAGS="$(TARGET_CFLAGS) -Wall -Wextra" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		LDLIBS="-lsqlite3"
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
