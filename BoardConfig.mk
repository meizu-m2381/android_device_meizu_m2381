#
# SPDX-FileCopyrightText: The Android Open Source Project
# SPDX-License-Identifier: Apache-2.0
#
# Meizu 20 (m2381 / SM8550 / kalama) 设备板级配置
# 分区尺寸、屏幕密度、vendor 安全补丁级别均来自本机实测，非推测。
#

DEVICE_PATH := device/meizu/m2381

BUILD_BROKEN_DUP_RULES := true
BUILD_BROKEN_ELF_PREBUILT_PRODUCT_COPY_FILES := true

# A/B（实测 ro.build.ab_update=true、ro.virtual_ab.enabled=true）
AB_OTA_UPDATER := true
# dtbo 沿用与预编译内核配套的原厂分区，不列入 OTA 更新范围。
#  - dtbo：本设备 TARGET_NEEDS_DTBOIMAGE := false，直接沿用原厂 dtbo，
#    不重建也就不纳入 OTA，否则 add_img_to_target_files 会因找不到
#    dtbo.img 而 AssertionError: Failed to find dtbo.img。
AB_OTA_PARTITIONS += \
    boot \
    init_boot \
    odm \
    product \
    recovery \
    system \
    system_dlkm \
    system_ext \
    vbmeta \
    vbmeta_system \
    vendor \
    vendor_boot \
    vendor_dlkm

# Architecture
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-2a-dotprod
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_VARIANT := kryo
TARGET_CPU_VARIANT_RUNTIME := kryo300

# 原厂 OMX/CAS/外置相机 HAL 为 32 位，必须提供对应 linker 与基础库。
TARGET_2ND_ARCH := arm
TARGET_2ND_ARCH_VARIANT := armv8-2a
TARGET_2ND_CPU_ABI := armeabi-v7a
TARGET_2ND_CPU_ABI2 := armeabi
TARGET_2ND_CPU_VARIANT := cortex-a55

# Audio
AUDIO_FEATURE_ENABLED_PROXY_DEVICE := true
AUDIO_FEATURE_ENABLED_DTS_EAGLE := false
AUDIO_FEATURE_ENABLED_PAL_HIDL := true
AUDIO_FEATURE_ENABLED_AGM_HIDL := true
AUDIO_FEATURE_ENABLED_HW_ACCELERATED_EFFECTS := false
AUDIO_FEATURE_ENABLED_DLKM := true
AUDIO_FEATURE_ENABLED_INSTANCE_ID := true
AUDIO_FEATURE_ENABLED_KEEP_ALIVE := true
AUDIO_FEATURE_ENABLED_GEF_SUPPORT := true
AUDIO_FEATURE_ENABLED_SVA_MULTI_STAGE := true
BOARD_SUPPORTS_SOUND_TRIGGER := true
# 使用 blobs 提供的 sound_trigger.primary.kalama.so，
# 不编译开源 STHAL（依赖 QTI 音频 PAL 头，设备产品排除其源码）
BOARD_SUPPORTS_OPENSOURCE_STHAL := false
# 本机的音频 HAL 全部来自 vendor blobs（android.hardware.audio.service_64、
# libagm*/libpal* 等），不编译 QTI 源码音频栈——它依赖未公开的内核 uapi 头
# （linux/msm_audio.h），无法在本环境编译。
TARGET_USES_QCOM_MM_AUDIO := false

# ipacm 由 vendor blobs 提供；置 true 让 BoardConfigQcom.mk 不再把
# hardware/qcom-caf/sm8550/data-ipa-cfg-mgr 加进 PRODUCT_SOONG_NAMESPACES。
# 设备产品排除该源码，命名空间不能再指向它；其他产品仍可使用该项目。
USE_DEVICE_SPECIFIC_DATA_IPA_CFG_MGR := true

# Stock display HALs replace CAF SM8550; resolve the source power HAL's
# displayconfig dependency through the upstream shared display namespace.
QCOM_SOONG_NAMESPACE := vendor/qcom/opensource/display

# Bootloader
TARGET_NO_BOOTLOADER := true

# Hardware
BOARD_USES_QCOM_HARDWARE := true

# Charger
BOARD_CHARGER_ENABLE_SUSPEND := true

# Display（实测 ro.sf.lcd_density=450，物理 1080x2400）
TARGET_SCREEN_DENSITY := 450

# Filesystem
TARGET_FS_CONFIG_GEN := $(DEVICE_PATH)/configs/config.fs

# ---------------------------------------------------------------------------
# Kernel —— v0.1 阶段使用原厂预编译内核
#
# 目前没有 m2381 的内核源码与 DTS：meizu-sm8550-kernel 的 DT/config 只覆盖
# m2468（魅族 21 Note）。因此第一阶段直接复用原厂 boot.img 中的 Image，
# boot/init_boot/vendor_boot/recovery 均由标准构建规则生成；dtbo 沿用原厂。
#
# TODO(阶段 2.5)：从 dtbo_b.img 反编译 DTS，基于 sm8550 通用内核补 m2381 DT，
#                  再把这里切回 TARGET_KERNEL_SOURCE 的源码编译路径。
# ---------------------------------------------------------------------------
BOARD_KERNEL_BASE        := 0x00000000
BOARD_KERNEL_PAGESIZE    := 4096
BOARD_KERNEL_IMAGE_NAME  := Image
BOARD_USES_GENERIC_KERNEL_IMAGE := true
BOARD_INCLUDE_DTB_IN_BOOTIMG := true
BOARD_RAMDISK_USE_LZ4 := true

TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)-kernel/Image
# Upstream LineageOS prebuilt-header interface; never replace its global generator.
TARGET_PREBUILT_KERNEL_HEADERS := $(DEVICE_PATH)-kernel/kernel-headers.tar.gz
# dtb 从原厂 vendor_boot 解包得到；dtb.img 由这个目录里的 .dtb 合并生成，
# 供 vendor_boot.img 打包使用。
# 原厂 DT 内嵌 ext4 vendor 规则优先于 ramdisk fstab；独立兼容副本仅禁用它。
# 用内核仓 tools/prepare_android_dtb.py 复现，原始 DTB 保留。
BOARD_PREBUILT_DTBIMAGE_DIR := $(DEVICE_PATH)-kernel/dtb/android

# vendor/lineage/build/tasks/kernel.mk 会用它做 GKI 版本判断，
# 即使使用预编译内核也必须给出（否则 is-version-* 宏报 Argument missing）
TARGET_KERNEL_VERSION := 5.15

# ---------------------------------------------------------------------------
# 预编译内核的模块打包
#
# 原厂内核没有源码，模块无法编译。原厂 vendor_boot 的 ramdisk 里带有
# 339 个与这个 Image 严格对应的 .ko（first-stage 挂了 ufs/clk/rpmh 等，
# 没有它们 first-stage init 挂不上 super，会停在 bootloader 的 logo）。
# 这些 .ko 已从原厂 vendor_boot 提取到 modules_prebuilt/。
# ---------------------------------------------------------------------------
M2381_FIRST_STAGE := $(strip $(shell cat $(DEVICE_PATH)/modules/modules.list.first_stage))

M2381_RECOVERY_MODULES := $(strip $(shell cat $(DEVICE_PATH)/modules/modules.list.recovery))
M2381_RECOVERY_AUDIO_MODULES := \
    snd_event_dlkm.ko q6_pdr_dlkm.ko q6_notifier_dlkm.ko gpr_dlkm.ko \
    spf_core_dlkm.ko adsp_loader_dlkm.ko

BOARD_VENDOR_RAMDISK_KERNEL_MODULES := \
    $(addprefix $(DEVICE_PATH)/modules_prebuilt/, $(filter-out $(M2381_RECOVERY_AUDIO_MODULES) qti_battery_charger.ko,$(sort $(M2381_FIRST_STAGE) $(M2381_RECOVERY_MODULES)))) \
    $(addprefix vendor/meizu/m2381/proprietary/vendor_dlkm/lib/modules/,$(M2381_RECOVERY_AUDIO_MODULES)) \
    vendor/meizu/m2381/compat/vendor_dlkm/lib/modules/qti_battery_charger.ko
BOARD_VENDOR_RAMDISK_KERNEL_MODULES_LOAD := $(M2381_FIRST_STAGE)
BOARD_VENDOR_RAMDISK_RECOVERY_KERNEL_MODULES_LOAD := $(M2381_RECOVERY_MODULES)
BOARD_VENDOR_RAMDISK_KERNEL_MODULES_BLOCKLIST_FILE := $(DEVICE_PATH)/modules/modules.blocklist.vendor

# 原厂 cmdline 实测子集（完整见 backup/meta/cmdline.txt）
BOARD_KERNEL_CMDLINE := \
    qcom_geni_serial.con_enabled=1 \
    nosoftlockup \
    page_poison=1 \
    sysrq_always_enabled \
    cgroup_disable=pressure \
    msm_rtb.filter=0x237 \
    fw_devlink.strict=1

BOARD_BOOTCONFIG := \
    androidboot.hardware=qcom \
    androidboot.memcg=1 \
    androidboot.usbcontroller=a600000.dwc3 \
    androidboot.vendor.qspa.modem=enabled \
    androidboot.console=ttyMSM0

BOARD_BOOT_HEADER_VERSION := 4
BOARD_MKBOOTIMG_ARGS += --header_version $(BOARD_BOOT_HEADER_VERSION)
BOARD_INIT_BOOT_HEADER_VERSION := 4
BOARD_MKBOOTIMG_INIT_ARGS += --header_version $(BOARD_INIT_BOOT_HEADER_VERSION)

# 预编译内核阶段不重建 DTBO
BOARD_KERNEL_SEPARATED_DTBO := false
TARGET_NEEDS_DTBOIMAGE := false

# OTA assert
TARGET_OTA_ASSERT_DEVICE := meizu20,m2381

# ---------------------------------------------------------------------------
# Partitions —— 全部来自 blockdev 实测（backup/meta/mapper-sizes.txt）
# ---------------------------------------------------------------------------
BOARD_BOOTIMAGE_PARTITION_SIZE := 100663296          # 96 MB
BOARD_DTBOIMG_PARTITION_SIZE := 25165824             # 24 MB（21 Note 只有 12 MB）
BOARD_INIT_BOOT_IMAGE_PARTITION_SIZE := 8388608      # 8 MB
BOARD_VENDOR_BOOTIMAGE_PARTITION_SIZE := 100663296   # 96 MB
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 104857600      # 100 MB
BOARD_SUPER_PARTITION_SIZE := 8589934592             # 8 GB

BOARD_FLASH_BLOCK_SIZE := 262144

BOARD_USES_METADATA_PARTITION := true

MEIZU_SSI_PARTITIONS := product system system_ext system_dlkm
MEIZU_TREBLE_PARTITIONS := odm vendor vendor_dlkm

BOARD_MEIZU_DYNAMIC_PARTITIONS_PARTITION_LIST := $(MEIZU_SSI_PARTITIONS) $(MEIZU_TREBLE_PARTITIONS)
BOARD_MEIZU_DYNAMIC_PARTITIONS_SIZE := 8585740288    # super - 4 MiB
BOARD_SUPER_PARTITION_GROUPS := meizu_dynamic_partitions

$(foreach p, $(call to-upper, $(BOARD_MEIZU_DYNAMIC_PARTITIONS_PARTITION_LIST)), \
    $(eval BOARD_$(p)IMAGE_FILE_SYSTEM_TYPE := erofs))

$(foreach p, $(call to-upper, $(BOARD_MEIZU_DYNAMIC_PARTITIONS_PARTITION_LIST)), \
    $(eval TARGET_COPY_OUT_$(p) := $(call to-lower, $(p))))

BOARD_PRODUCTIMAGE_MINIMAL_PARTITION_RESERVED_SIZE := false
ifneq ($(wildcard vendor/lineage/config/BoardConfigReservedSize.mk),)
include vendor/lineage/config/BoardConfigReservedSize.mk
endif

# Platform
TARGET_BOARD_PLATFORM := kalama
TARGET_BOOTLOADER_BOARD_NAME := kalama

# Recovery
# 与原厂及 LineageOS SM8550 一致：独立 recovery ramdisk 复用 boot 的内核。
BOARD_EXCLUDE_KERNEL_FROM_RECOVERY_IMAGE := true
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/init/fstab.qcom
TARGET_RECOVERY_PIXEL_FORMAT := RGBX_8888
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true

# RIL
ENABLE_VENDOR_RIL_SERVICE := true

# SEPolicy
# PixelOS/Lineage 23.x 把 qcom vndr sepolicy 放在按 SoC 分的子目录里，老的分支在顶层。
# 两者都兼容，按实际存在的路径选择，避免靠分支名猜。
QCOM_SEPOLICY_PATH := $(firstword $(wildcard device/qcom/sepolicy_vndr/sm8550) device/qcom/sepolicy_vndr)
include $(QCOM_SEPOLICY_PATH)/SEPolicy.mk
BOARD_VENDOR_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy/vendor
SYSTEM_EXT_PUBLIC_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy/system_ext/public
SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy/system_ext/private

# System properties
TARGET_ODM_PROP += $(DEVICE_PATH)/configs/properties/odm.prop
TARGET_PRODUCT_PROP += $(DEVICE_PATH)/configs/properties/product.prop
TARGET_SYSTEM_PROP += $(DEVICE_PATH)/configs/properties/system.prop
TARGET_SYSTEM_EXT_PROP += $(DEVICE_PATH)/configs/properties/system_ext.prop
TARGET_VENDOR_PROP += $(DEVICE_PATH)/configs/properties/vendor.prop

# VINTF
DEVICE_MATRIX_FILE := hardware/qcom-caf/common/compatibility_matrix.xml

DEVICE_FRAMEWORK_COMPATIBILITY_MATRIX_FILE += \
    hardware/qcom-caf/common/vendor_framework_compatibility_matrix.xml \
    $(DEVICE_PATH)/configs/vintf/compatibility_matrix_meizu_hal.xml

# 注意：故意不并入 21 Note 的 configs/vintf/compatibility_matrix.device.xml。
# 那份矩阵声明的是 m2468 的 framework 侧 HAL 要求，与本机 stock vendor 不匹配时
# 会在开机 VINTF 校验阶段失败；GSI 阶段（无自定义矩阵）能正常启动，说明默认矩阵够用。
# 首次开机跑通后再按 logcat 的 vintf 报错逐条补。

DEVICE_FRAMEWORK_MANIFEST_FILE += $(DEVICE_PATH)/configs/vintf/framework_manifest.xml
DEVICE_MANIFEST_FILE := $(DEVICE_PATH)/configs/vintf/manifest_kalama.xml

# Vendor security patch（实测 ro.vendor.build.security_patch=2023-10-01）
VENDOR_SECURITY_PATCH := 2023-10-01

# 注意：BOARD_SEPOLICY_VERS 无法在设备树设置。
# build/make/core/config.mk:881 硬性赋值为 $(PLATFORM_SEPOLICY_VERSION)
# 并 .KATI_READONLY，任何设备树赋值都会被丢弃。
# 因此原厂 manifest_kalama.xml 里写死的 <sepolicy><version>33.0</version></sepolicy>
# 已删除，交由 assemble_vintf 用平台值填充，避免
# "Cannot override existing value 33.0 with BOARD_SEPOLICY_VERS" 报错。

# Verified Boot
BOARD_AVB_ENABLE := true
BOARD_AVB_MAKE_VBMETA_IMAGE_ARGS += --flags 3
BOARD_MOVE_GSI_AVB_KEYS_TO_VENDOR_BOOT := true

BOARD_AVB_VBMETA_SYSTEM := product system system_ext
BOARD_AVB_VBMETA_SYSTEM_KEY_PATH := external/avb/test/data/testkey_rsa2048.pem
BOARD_AVB_VBMETA_SYSTEM_ALGORITHM := SHA256_RSA2048
BOARD_AVB_VBMETA_SYSTEM_ROLLBACK_INDEX := 1764892800
# 原厂 vbmeta_b 的 vbmeta_system Chain Partition descriptor 实测为 2。
BOARD_AVB_VBMETA_SYSTEM_ROLLBACK_INDEX_LOCATION := 2

# WiFi
BOARD_WLAN_DEVICE := qcwcn
BOARD_HOSTAPD_DRIVER := NL80211
BOARD_HOSTAPD_PRIVATE_LIB := lib_driver_cmd_$(BOARD_WLAN_DEVICE)
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_$(BOARD_WLAN_DEVICE)
WIFI_DRIVER_DEFAULT := qca_cld3
WIFI_DRIVER_STATE_CTRL_PARAM := "/dev/wlan"
WIFI_DRIVER_STATE_OFF := "OFF"
WIFI_DRIVER_STATE_ON := "ON"
WIFI_HIDL_FEATURE_AWARE := true
WIFI_HIDL_FEATURE_DUAL_INTERFACE := true
WIFI_FEATURE_HOSTAPD_11AX := true
WIFI_HIDL_UNIFIED_SUPPLICANT_SERVICE_RC_ENTRY := true
WPA_SUPPLICANT_VERSION := VER_0_8_X

include vendor/meizu/m2381/BoardConfigVendor.mk
