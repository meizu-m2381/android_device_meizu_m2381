#
# SPDX-License-Identifier: Apache-2.0
#

PRODUCT_MAKEFILES := \
    $(LOCAL_DIR)/lineage_m2381.mk

# Do not register a PixelOS product against a pure LineageOS tree.
ifneq ($(wildcard vendor/custom/config/common_full_phone.mk),)
PRODUCT_MAKEFILES += $(LOCAL_DIR)/custom_m2381.mk

# Use the target tree's release token (Android 16 bp4a / Android 17 cp2a).
M2381_TARGET_RELEASE := $(strip $(shell sed -n 's/^aosp_target_release=//p' vendor/lineage/vars/aosp_target_release))
ifneq ($(M2381_TARGET_RELEASE),)
COMMON_LUNCH_CHOICES := \
    custom_m2381-$(M2381_TARGET_RELEASE)-user \
    custom_m2381-$(M2381_TARGET_RELEASE)-userdebug \
    custom_m2381-$(M2381_TARGET_RELEASE)-eng
endif
endif

# Lineage-based ROMs should use their branch's own release token in lunch:
# lineage_m2381-<release>-userdebug. Do not assume every branch uses bp4a.
