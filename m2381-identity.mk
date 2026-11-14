# Copyright (C) 2024 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
# Include directly after ROM inheritance so local identity overrides defaults.

PRODUCT_COMPRESSED_APEX := false
PRODUCT_DEVICE := m2381
PRODUCT_BRAND := Meizu
PRODUCT_MODEL := MEIZU 20
PRODUCT_MANUFACTURER := Meizu

# Donor identity; this does not make a custom ROM an official firmware build.
BUILD_FINGERPRINT := meizu/meizu_20_CN/meizu20:16/BQ2A.251016.001-BP2A.250605.031.A3/1764128685:user/release-keys
