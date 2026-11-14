# Copyright (C) 2024 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
# Shared hardware configuration; ROM branding and final identity are separate.

$(call inherit-product, build/make/target/product/core_64_bit.mk)
$(call inherit-product, build/make/target/product/full_base_telephony.mk)
$(call inherit-product, device/meizu/m2381/device.mk)
