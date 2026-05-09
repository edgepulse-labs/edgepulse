include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-edgepulse
PKG_RELEASE:=1
PKG_LICENSE:=MIT

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-edgepulse
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=LuCI support for EdgePulse
  DEPENDS:=+edgepulse +luci-base +rpcd +rpcd-mod-file
  PKGARCH:=all
endef

define Package/luci-app-edgepulse/description
 LuCI application for viewing EdgePulse telemetry status.
endef

define Build/Compile
endef

define Package/luci-app-edgepulse/install
	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/edgepulse
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/edgepulse/overview.js $(1)/www/luci-static/resources/view/edgepulse/overview.js
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/edgepulse/metrics.js $(1)/www/luci-static/resources/view/edgepulse/metrics.js
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/edgepulse/features.js $(1)/www/luci-static/resources/view/edgepulse/features.js
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/edgepulse/settings.js $(1)/www/luci-static/resources/view/edgepulse/settings.js
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d
	$(INSTALL_DATA) ./root/usr/share/luci/menu.d/luci-app-edgepulse.json $(1)/usr/share/luci/menu.d/luci-app-edgepulse.json
	$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d
	$(INSTALL_DATA) ./root/usr/share/rpcd/acl.d/luci-app-edgepulse.json $(1)/usr/share/rpcd/acl.d/luci-app-edgepulse.json
endef

$(eval $(call BuildPackage,luci-app-edgepulse))
