# 
# Copyright (C) 2006 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=camlite
PKG_VERSION:=0.1
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(BUILD_DIR)/camlite

include $(INCLUDE_DIR)/package.mk

define Package/camlite
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=camera lite
endef

define Package/camlite/description
 This package contains an utility for configuring the Broadcom BCM5325E/536x 
 based switches.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Package/camlite/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/camlite $(1)/usr/sbin/
endef

$(eval $(call BuildPackage,camlite))
