#
# SPDX-License-Identifier: Apache-2.0
#

PRODUCT_MAKEFILES := \
    $(LOCAL_DIR)/custom_m2381.mk

# PixelOS 的 breakfast 会拼成 custom_m2381-<aosp_target_release>-<variant>，
# 当前 aosp_target_release = bp4a（见 vendor/lineage/vars/aosp_target_release）
COMMON_LUNCH_CHOICES := \
    custom_m2381-bp4a-user \
    custom_m2381-bp4a-userdebug \
    custom_m2381-bp4a-eng
