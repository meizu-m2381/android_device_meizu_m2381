# SPDX-License-Identifier: Apache-2.0
# custom product entry; hardware settings live in m2381-common.mk.

$(call inherit-product, device/meizu/m2381/m2381-common.mk)
$(call inherit-product, vendor/custom/config/common_full_phone.mk)
# Assign final identity in this product; inherited single-value properties use
# first-parent precedence and otherwise keep full_base's generic device.
include device/meizu/m2381/m2381-identity.mk
PRODUCT_NAME := custom_m2381
