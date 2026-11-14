# Copyright (C) 2024 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
# lineage product entry; hardware settings live in m2381-common.mk.

$(call inherit-product, device/meizu/m2381/m2381-common.mk)
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)
$(call inherit-product, device/meizu/m2381/m2381-identity.mk)
PRODUCT_NAME := lineage_m2381
