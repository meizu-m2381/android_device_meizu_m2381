#
# Copyright (C) 2024 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Add common definitions for Qualcomm
$(call inherit-product, hardware/qcom-caf/common/common.mk)

# Get non-open-source specific aspects
$(call inherit-product, vendor/meizu/m2381/m2381-vendor.mk)

# Legacy API 33 vendor binaries use LineageOS's upstream compatibility shim.
PRODUCT_PACKAGES += libbase_shim:32 libbase_shim:64

# Stock DPM uses Parcel::setData, provided by the official LineageOS shim.
PRODUCT_PACKAGES += libhidlbase_shim:64

# qguard must use the current vendor ABI, not VNDK33 libprocessgroup with
# a libbase dependency shared back into the current vendor namespace.
PRODUCT_PACKAGES += libprocessgroup.vendor:64

# Camera HAL dlopen reaches CallStack; keep its unwind dependency in the vendor
# namespace instead of VNDK33 libraries with the old libbase ABI.
PRODUCT_PACKAGES += libutilscallstack.vendor libunwindstack.vendor

# Both stock codec architectures require the upstream GraphicBufferMapper shim.
PRODUCT_PACKAGES += libui_shim.vendor:32 libui_shim.vendor:64
# Keep the legacy 32-bit codec stack separate from the current OMX helper.
PRODUCT_PACKAGES += m2381_bufferqueue_helper_v33

# Source-built vendor libbinder needs this current system LLNDK library.
# Keep the requirement in this product instead of editing the VNDK33 snapshot.
PRODUCT_VENDOR_LINKER_CONFIG_FRAGMENTS += $(LOCAL_PATH)/configs/linker.config.json

# Keep stock HAL source projects available for other products. Only this
# product excludes their Soong definitions; no repo projects are removed.
PRODUCT_SOURCE_ROOT_DIRS += \
    -hardware/qcom-caf/sm8550/display \
    -hardware/qcom-caf/sm8550/audio/agm \
    -hardware/qcom-caf/sm8550/audio/graphservices \
    -hardware/qcom-caf/sm8550/audio/pal \
    -hardware/qcom-caf/sm8550/audio/primary-hal \
    -vendor/qcom/opensource/audio-hal/st-hal \
    -vendor/qcom/opensource/audio-hal/st-hal-ar \
    -vendor/qcom/opensource/audio-hal/st-hal-ar-legacy \
    -hardware/qcom-caf/sm8550/dataipa \
    -hardware/qcom-caf/sm8550/data-ipa-cfg-mgr \
    -vendor/qcom/opensource/core-utils-vendor/fwk-detect

# Enforce generic ramdisk allow list
$(call inherit-product, $(SRC_TARGET_DIR)/product/generic_ramdisk.mk)

# Project quotas and casefolding for emulated storage
$(call inherit-product, $(SRC_TARGET_DIR)/product/emulated_storage.mk)

# Setup dalvik vm configs
$(call inherit-product, frameworks/native/build/phone-xhdpi-6144-dalvik-heap.mk)

$(call inherit-product, $(SRC_TARGET_DIR)/product/virtual_ab_ota/vabc_features.mk)

# A/B
$(call inherit-product, $(SRC_TARGET_DIR)/product/virtual_ab_ota/launch_with_vendor_ramdisk.mk)

AB_OTA_POSTINSTALL_CONFIG += \
    RUN_POSTINSTALL_system=true \
    POSTINSTALL_PATH_system=system/bin/otapreopt_script \
    FILESYSTEM_TYPE_system=erofs \
    POSTINSTALL_OPTIONAL_system=true

AB_OTA_POSTINSTALL_CONFIG += \
    RUN_POSTINSTALL_vendor=true \
    POSTINSTALL_PATH_vendor=bin/checkpoint_gc \
    FILESYSTEM_TYPE_vendor=erofs \
    POSTINSTALL_OPTIONAL_vendor=true

PRODUCT_PACKAGES += \
    otapreopt_script \
    checkpoint_gc

# Graphics
# Feed the stock under-display ALS compensation service with panel-region RGB.
PRODUCT_PACKAGES += m2381-sensor-aux

# Keep both Adreno lookup paths on one driver instance.
PRODUCT_PACKAGES += \
    m2381_adreno_egl_alias_32 \
    m2381_adreno_egl_alias_64 \
    m2381_adreno_gles_alias_32 \
    m2381_adreno_gles_alias_64

# Audio
# 本机的音频 HAL 由 vendor blobs 提供（android.hardware.audio.service_64、
# libagm*/libpal* 等都在 proprietary/vendor 下）。源码侧的 QTI 音频模块依赖
# 内核 uapi 头文件 linux/msm_audio.h，而魅族内核源码未公开，无法编译，
# 因此这里只保留与内核无关的 AOSP 通用 HAL。
PRODUCT_PACKAGES += \
    audio.primary.default \
    audio.r_submix.default \
    audio.usb.default \
    libtinycompress

$(call soong_config_set, android_hardware_audio, run_64bit, true)

PRODUCT_COPY_FILES += \
    frameworks/av/services/audiopolicy/config/a2dp_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/a2dp_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/usb_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/usb_audio_policy_configuration.xml

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.audio.low_latency.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.audio.low_latency.xml \
    frameworks/native/data/etc/android.hardware.audio.pro.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.audio.pro.xml \
    frameworks/native/data/etc/android.software.midi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.midi.xml \
    frameworks/native/data/etc/android.software.sip.voip.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.sip.voip.xml

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/audio/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    $(LOCAL_PATH)/configs/audio/usecaseKvManager.xml:$(TARGET_COPY_OUT_VENDOR)/etc/usecaseKvManager.xml

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/audio/sku_kalama/adsp_sleep_monitor.conf:$(TARGET_COPY_OUT_VENDOR)/etc/audio/sku_kalama/adsp_sleep_monitor.conf \
    $(LOCAL_PATH)/configs/audio/sku_kalama/audio_effects.conf:$(TARGET_COPY_OUT_VENDOR)/etc/audio/sku_kalama/audio_effects.conf \
    $(LOCAL_PATH)/configs/audio/sku_kalama/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio/sku_kalama/audio_policy_configuration.xml \
    $(LOCAL_PATH)/configs/audio/sku_kalama/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio/sku_kalama_qssi/audio_policy_configuration.xml

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/audio/sku_kalama/mixer_paths_kalama_mtp.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio/sku_kalama/mixer_paths_kalama_mtp.xml \
    $(LOCAL_PATH)/configs/audio/sku_kalama/resourcemanager_kalama_mtp.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio/sku_kalama/resourcemanager_kalama_mtp.xml \

PRODUCT_COPY_FILES += \
    device/meizu/m2381/configs/audio/common/bluetooth_qti_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth_qti_audio_policy_configuration.xml \
    device/meizu/m2381/configs/audio/common/bluetooth_qti_hearing_aid_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth_qti_hearing_aid_audio_policy_configuration.xml \
    device/meizu/m2381/configs/audio/common/codec2/media_codecs_c2_audio.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_c2_audio.xml \
    device/meizu/m2381/configs/audio/common/codec2/service/1.0/c2audio.vendor.base-arm64.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/c2audio.vendor.base-arm64.policy \
    device/meizu/m2381/configs/audio/common/codec2/service/1.0/c2audio.vendor.ext-arm64.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/c2audio.vendor.ext-arm64.policy \
    device/meizu/m2381/configs/audio/kalama/card-defs.xml:$(TARGET_COPY_OUT_VENDOR)/etc/card-defs.xml \
    device/meizu/m2381/configs/audio/kalama/audio_effects.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio/sku_kalama/audio_effects.xml \
    device/meizu/m2381/configs/audio/kalama/microphone_characteristics.xml:$(TARGET_COPY_OUT_VENDOR)/etc/microphone_characteristics.xml

# Bluetooth
PRODUCT_PACKAGES += m2381_ssgtzd_rc

PRODUCT_PACKAGES += \
    android.hardware.bluetooth.audio-impl \
    audio.bluetooth.default

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml

# lib_bt_aptx / lib_bt_ble / lib_bt_bundle 由 vendor blobs 提供
# (vendor/lib{,64}/lib_bt_*.so)，不编译源码版：源码版位于 QTI 音频 PAL
# 的 plugins/codecs 下，而该源码树依赖魅族未公开的内核头，已在本产品排除。

# Boot control
PRODUCT_PACKAGES += \
    android.hardware.boot-service.qti \
    android.hardware.boot-service.qti.recovery

# Camera
$(call soong_config_set_bool,camera,override_format_from_reserved,true)

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.camera.concurrent.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.concurrent.xml \
    frameworks/native/data/etc/android.hardware.camera.flash-autofocus.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.flash-autofocus.xml \
    frameworks/native/data/etc/android.hardware.camera.front.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.front.xml \
    frameworks/native/data/etc/android.hardware.camera.full.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.full.xml \
    frameworks/native/data/etc/android.hardware.camera.raw.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.raw.xml

# Display
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/display/display_id_4630946981250209410.xml:$(TARGET_COPY_OUT_PRODUCT)/etc/displayconfig/display_id_4630946981250209410.xml

# composer/allocator/demura 服务与 mapper 实现都在 vendor blobs 里
# （proprietary/vendor/bin/hw 与 lib64/hw 均有对应文件）。源码侧显示 HAL 依赖
# 未公开的内核 uapi 头（display/drm/sde_drm.h），因此不编译。

# LLVM emulated TLS used by the pinned SLIM compatibility copy.
PRODUCT_PACKAGES += \
    libemutls_get_address

# DRM
PRODUCT_PACKAGES += \
    android.hardware.drm-service.clearkey

# Dynamic partitions
PRODUCT_USE_DYNAMIC_PARTITIONS := true

# 通用工具必须与当前 vendor 运行库一起构建。
PRODUCT_PACKAGES += \
    toolbox_vendor \
    toybox_vendor \
    boringssl_self_test_vendor \
    sh_vendor \
    awk_vendor \
    dumpsys_vendor \
    vndservice \
    vndservicemanager \
    logwrapper_vendor

# Fastbootd
PRODUCT_PACKAGES += \
    fastbootd

# Fingerprint
# 本机由 qfp-daemon 提供高通屏下超声波指纹 AIDL IFingerprint/default，
# vendor.meizu.hardware.fido@2.0 是独立的魅族扩展服务。
# 21 Note 用的是 JIIOV 的 HIDL 服务，本机不适用，故不打包。
# 权限文件由 vendor blobs 自带（/vendor/etc/permissions/android.hardware.fingerprint.xml
# 与 qti_fingerprint_interface.xml），这里不再重复安装。

# Fstab
PRODUCT_PACKAGES += \
    charger_fw_fstab.qti

PRODUCT_PACKAGES += \
    fstab.qcom \
    fstab.qcom.vendor_ramdisk

# Gesture
PRODUCT_PACKAGES += \
    sensors.m2381 \
    mz_fp_gesture_init

# Graphics
# gralloc 由 vendor blobs 提供（lib64/hw/android.hardware.graphics.mapper@4.0-impl-qti-display.so）。
# 源码侧的 gralloc.default 会拉入整套 QTI 显示栈（libdrmutils → 内核 uapi sde_drm.h），
# 本环境无内核源码无法编译，故不打包。

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.opengles.aep.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.opengles.aep.xml \
    frameworks/native/data/etc/android.hardware.vulkan.compute-0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.compute-0.xml \
    frameworks/native/data/etc/android.hardware.vulkan.level-1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.level-1.xml \
    frameworks/native/data/etc/android.hardware.vulkan.version-1_1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.version-1_1.xml \
    frameworks/native/data/etc/android.hardware.vulkan.version-1_3.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.version-1_3.xml \
    frameworks/native/data/etc/android.software.opengles.deqp.level-2022-03-01.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.opengles.deqp.level.xml \
    frameworks/native/data/etc/android.software.vulkan.deqp.level-2022-03-01.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.vulkan.deqp.level.xml

# GNSS
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.location.gps.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.location.gps.xml

# Health
PRODUCT_PACKAGES += \
    android.hardware.health-service.m2381 \
    android.hardware.health-service.qti_recovery

# Init
PRODUCT_PACKAGES += \
    init.qcom.rc \
    init.qti.kernel.rc \
    init.m2381.rc \
    init.target.rc \
    init.qcom.factory.rc \
    init.recovery.qcom.rc

PRODUCT_PACKAGES += \
    init.class_main.sh \
    init.kernel.post_boot-kalama.sh \
    init.kernel.post_boot.sh \
    init.qcom.class_core.sh \
    init.qcom.early_boot.sh \
    init.qcom.post_boot.sh \
    init.qcom.sh \
    init.qti.kernel.sh \
    init.qti.write.sh \
    system_dlkm_modprobe.sh \
    vendor_modprobe.sh

# Keep the vendor module blocklist in the device tree so blob regeneration
# cannot re-enable the TZ logging driver that stalls early m2381 boot.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/modules/modules.blocklist.vendor_dlkm:$(TARGET_COPY_OUT_VENDOR_DLKM)/lib/modules/modules.blocklist

# IPA
# ipacm / IPACM_cfg.xml / IPACM_Filter_cfg.xml 全部由 vendor blobs 提供
# (mz_vendor_bin_ipacm / mz_vendor_etc_IPACM_cfg_xml 等，已列入 m2381-vendor.mk)。
# 源码版 data-ipa-cfg-mgr 依赖魅族未公开的内核头 linux/msm_ipa.h，已移出源码树。

# IR
# 原厂 blobs 已提供 vendor.meizu.ir@1.0-service（真实硬件 HAL）。
# AOSP 的 android.hardware.ir-service.example 会与它争用
# IConsumerIr/default，导致 check_vintf 报 HAL 冲突，故不启用。

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.consumerir.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.consumerir.xml

# Keylayout
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/keylayout/main_touch.kl:$(TARGET_COPY_OUT_VENDOR)/usr/keylayout/main_touch.kl

# Lineage Health
$(call soong_config_set_bool,lineage_health,charging_control_supports_bypass,false)

# Media
PRODUCT_PACKAGES += \
    libmedia_codeclist \
    libstagefright_codecbase \
    libstagefright_framecapture_utils \
    libstagefright_surface_utils \
    qcodec2_shim

# Network
PRODUCT_PACKAGES += \
    libnetfilter_conntrack \
    libnfnetlink

# NFC: the stock HIDL service owns nq-nci and the vendor eSE extensions.
# Do not start a second source AIDL controller stack beside it.
PRODUCT_PACKAGES += com.android.nfc_extras
# nqnfcinfo 由 vendor blobs 提供 (mz_vendor_bin_nqnfcinfo，已在 m2381-vendor.mk)。
# 源码版依赖魅族未公开的内核头 linux/nfc/nfcinfo.h，已移出源码树。

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.nfc.ese.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.ese.xml \
    frameworks/native/data/etc/android.hardware.nfc.hce.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.hce.xml \
    frameworks/native/data/etc/android.hardware.nfc.hcef.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.hcef.xml \
    frameworks/native/data/etc/android.hardware.nfc.uicc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.uicc.xml \
    frameworks/native/data/etc/android.hardware.nfc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.nfc.xml \
    frameworks/native/data/etc/android.hardware.se.omapi.ese.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.se.omapi.ese.xml \
    frameworks/native/data/etc/android.hardware.se.omapi.uicc.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.se.omapi.uicc.xml \
    frameworks/native/data/etc/com.android.nfc_extras.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/com.android.nfc_extras.xml \
    frameworks/native/data/etc/com.nxp.mifare.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/com.nxp.mifare.xml

# Overlay
PRODUCT_PACKAGES += \
    ApertureOverlayM2381 \
    CarrierConfigOverlayM2381 \
    FrameworkOverlayM2381 \
    SecureElementOverlayM2381 \
    SettingsOverlayM2381 \
    SystemUIOverlayM2381 \
    TelephonyOverlayM2381 \
    WifiOverlayM2381

# Partitions
PRODUCT_PACKAGES += \
    vendor_bt_firmware_mountpoint \
    vendor_dsp_mountpoint \
    vendor_firmware_mnt_mountpoint

# Power
PRODUCT_PACKAGES += \
    android.hardware.power-service-qti

PRODUCT_COPY_FILES += \
    vendor/qcom/opensource/power/config/kalama/powerhint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/powerhint.xml

# PSI
PRODUCT_PACKAGES += \
    libpsi

# QSPA
PRODUCT_PACKAGES += \
    vendor.qti.qspa-service

# Radio
PRODUCT_PACKAGES += \
    rfs_msm_mpss_readonly_mbnconfig_symlink

PRODUCT_PACKAGES += \
    libril

# SecureElement
PRODUCT_PACKAGES += \
    secure_element-service.meizu.xml

# Security
PRODUCT_PACKAGES += \
    android.hardware.hardware_keystore.xml

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.keystore.app_attest_key.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.keystore.app_attest_key.xml \
    frameworks/native/data/etc/android.software.device_id_attestation.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.device_id_attestation.xml \
    frameworks/native/data/etc/android.software.verified_boot.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.verified_boot.xml

# Sensors
# sensors.ssc 是从 QTI 源码编译的 SSC HAL，本源码树里没有对应仓库；
# 本机的传感器 HAL 由 vendor blobs 提供（sensors.dynamic_sensor_hal.so 等），
# 因此不再打包 sensors.ssc。
PRODUCT_PACKAGES += \
    android.hardware.sensors-service.multihal

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/sensors/hals.conf:$(TARGET_COPY_OUT_VENDOR)/etc/sensors/hals.conf

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.sensor.dynamic.head_tracker.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/sku_kalama/android.hardware.sensor.dynamic.head_tracker.xml \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/sku_kalama/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.compass.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/sku_kalama/android.hardware.sensor.compass.xml \
    frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/sku_kalama/android.hardware.sensor.gyroscope.xml \
    frameworks/native/data/etc/android.hardware.sensor.light.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/sku_kalama/android.hardware.sensor.light.xml \
    frameworks/native/data/etc/android.hardware.sensor.proximity.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/sku_kalama/android.hardware.sensor.proximity.xml \
    frameworks/native/data/etc/android.hardware.sensor.stepcounter.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/sku_kalama/android.hardware.sensor.stepcounter.xml \
    frameworks/native/data/etc/android.hardware.sensor.stepdetector.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/sku_kalama/android.hardware.sensor.stepdetector.xml

# Shipping API level
BOARD_SHIPPING_API_LEVEL := 33
PRODUCT_SHIPPING_API_LEVEL := 33

# 原厂 API 33 vendor CIL 使用旧版 genfs 标签；见 Android.mk 的覆盖模块。
PRODUCT_PACKAGES += m2381_legacy_genfs_labels_version

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(LOCAL_PATH) \
	vendor/qcom/opensource/usb/etc

# Telephony
PRODUCT_PACKAGES += \
    extphonelib \
    extphonelib.xml \
    ims-ext-common \
    ims_ext_common.xml \
    qti-telephony-hidl-wrapper \
    qti-telephony-hidl-wrapper-prd \
    qti-telephony-utils \
    qti_telephony_hidl_wrapper.xml \
    qti_telephony_hidl_wrapper_prd.xml \
    qti_telephony_utils.xml

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.telephony.cdma.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.cdma.xml \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.gsm.xml \
    frameworks/native/data/etc/android.hardware.telephony.ims.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.ims.xml \
    frameworks/native/data/etc/android.hardware.telephony.mbms.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.mbms.xml

# Thermal
# Stock HIDL Thermal is also used by composer/perf/limits clients. Keep its
# hardware thresholds under one owner instead of starting a second AIDL HAL.

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml

# Biometrics
# 魅族 20 使用高通 QFS4008 屏下超声波指纹，不使用 21 Note 的 JIIOV。
# 标准指纹接口由 qfp-daemon 提供，魅族另有 fido 扩展服务；人脸走
# vendor.xingji.hardware.biometrics.face@1.0-service。两者都由 vendor blobs 提供，
# 设备树只需保证 SELinux 与 VINTF 允许它们启动。
# 录入、解锁与 BiometricPrompt 须分别进行真机验证。

# Ueventd
PRODUCT_PACKAGES += \
    ueventd.qcom.rc

# Update engine
PRODUCT_PACKAGES += \
    update_engine \
    update_engine_sideload \
    update_verifier

# USB
PRODUCT_PACKAGES += \
    android.hardware.usb-service.qti \
    android.hardware.usb.gadget-service.qti \
    audio.usb.default \
    usb_compositions.conf

PRODUCT_PACKAGES += \
    init.qcom.usb.sh \
    init.qcom.usb.rc

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.usb.host.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.host.xml

# Vendor service manager
PRODUCT_SYSTEM_SERVER_JARS += system_ext:m2381-display-aux

PRODUCT_PACKAGES += \
    m2381-aod \
    m2381-display-aux \
    vndservicemanager

# VNDK
PRODUCT_EXTRA_VNDK_VERSIONS := 33

# 原厂 vendor API 33；仅安装 APEX 不会启用对应的 linker 命名空间。
PRODUCT_VENDOR_PROPERTIES += \
    ro.vndk.version=33

PRODUCT_PACKAGES += \
    android.hardware.vibrator-service.meizu \
    m2381_vendor_sepolicy \
    m2381_precompiled_sepolicy \
    m2381_vendor_file_contexts

PRODUCT_COPY_FILES += \
    vendor/qcom/opensource/vibrator/excluded-input-devices.xml:$(TARGET_COPY_OUT_VENDOR)/etc/excluded-input-devices.xml

# WiFi
PRODUCT_PACKAGES += \
    android.hardware.wifi-service \
    wpa_cli \
    wpa_supplicant \
    wpa_supplicant.conf \
    hostapd \
    hostapd_cli \
    libwifi-hal-ctrl \
    libwifi-hal-qcom \
    libkeystore-engine-wifi-hidl \
    libkeystore-wifi-hidl \
    libwifi-hal

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.wifi.direct.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.direct.xml \
    frameworks/native/data/etc/android.hardware.wifi.passpoint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.passpoint.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.hardware.wifi.aware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.aware.xml \
    frameworks/native/data/etc/android.software.ipsec_tunnels.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.ipsec_tunnels.xml

# WiFi firmware symlinks
PRODUCT_PACKAGES += \
    firmware_wlanmdsp.otaupdate_symlink \
    firmware_wlan_mac.bin_symlink \
    firmware_WCNSS_qcom_cfg.ini_symlink
