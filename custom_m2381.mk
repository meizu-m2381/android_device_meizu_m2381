#
# SPDX-License-Identifier: Apache-2.0
#
# Meizu 20 (m2381) PixelOS 产品配置
# 命名遵循 PixelOS 约定：custom_<device>，由 vendor/custom/build/envsetup.sh 的
# breakfast 推导为 lunch custom_<device>-<aosp_target_release>-<variant>
#

# Inherit common AOSP configurations
$(call inherit-product, build/make/target/product/core_64_bit.mk)
$(call inherit-product, build/make/target/product/full_base_telephony.mk)

# Inherit device-specific configurations
$(call inherit-product, device/meizu/m2381/device.mk)

# Inherit PixelOS configurations
$(call inherit-product, vendor/custom/config/common_full_phone.mk)

# 与原厂一致：使用未压缩 APEX（原厂 system 里是 .apex，不是 .capex）
PRODUCT_COMPRESSED_APEX := false

PRODUCT_DEVICE := m2381
PRODUCT_NAME := custom_m2381
PRODUCT_BRAND := Meizu
PRODUCT_MODEL := MEIZU 20
PRODUCT_MANUFACTURER := Meizu

# 原厂实测指纹：meizu/meizu_20_CN/meizu20:16/BQ2A.251016.001-BP2A.250605.031.A3
BUILD_FINGERPRINT := meizu/meizu_20_CN/meizu20:16/BQ2A.251016.001-BP2A.250605.031.A3/1764128685:user/release-keys
