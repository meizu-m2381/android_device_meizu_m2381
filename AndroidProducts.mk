#
# SPDX-License-Identifier: Apache-2.0
#

PRODUCT_MAKEFILES := \
    $(LOCAL_DIR)/lineage_m2381.mk

# Do not register a PixelOS product against a pure LineageOS tree.
ifneq ($(wildcard vendor/custom/config/common_full_phone.mk),)
PRODUCT_MAKEFILES += $(LOCAL_DIR)/custom_m2381.mk

# PixelOS 的 breakfast 会拼成 custom_m2381-<aosp_target_release>-<variant>，
# 当前 aosp_target_release = bp4a（见 vendor/lineage/vars/aosp_target_release）
COMMON_LUNCH_CHOICES := \
    custom_m2381-bp4a-user \
    custom_m2381-bp4a-userdebug \
    custom_m2381-bp4a-eng
endif

# Lineage-based ROMs should use their branch's own release token in lunch:
# lineage_m2381-<release>-userdebug. Do not assume every branch uses bp4a.
