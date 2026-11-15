#
# Copyright (C) 2024 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

LOCAL_PATH := $(call my-dir)

ifneq ($(filter m2381,$(TARGET_DEVICE)),)

# 本机复用 API 33 的原厂 vendor CIL；它仍拥有 /class/udc 和
# /power/mem_sleep 的 genfscon。新版 202504 标签会与这些规则冲突。
# 覆盖安装版本文件，保持 libgenfslabelsversion 对旧 vendor 的 202404 行为。
# BOARD_GENFS_LABELS_VERSION 受当前 BOARD_API_LEVEL 下限约束，不能在此降级。
include $(CLEAR_VARS)
LOCAL_MODULE := m2381_legacy_genfs_labels_version
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES := configs/selinux/genfs_labels_version.txt
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/etc/selinux
LOCAL_MODULE_STEM := genfs_labels_version.txt
LOCAL_VENDOR_MODULE := true
LOCAL_OVERRIDES_MODULES := genfs_labels_version.txt
include $(BUILD_PREBUILT)

# 构建时追加设备适配权限，不直接改写本地原厂策略输入。
include $(CLEAR_VARS)
LOCAL_MODULE := m2381_vendor_sepolicy
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/etc/selinux
LOCAL_MODULE_STEM := vendor_sepolicy.cil
LOCAL_VENDOR_MODULE := true
LOCAL_OVERRIDES_MODULES := vendor_sepolicy.cil
m2381_vibrator_policy := $(call intermediates-dir-for,ETC,$(LOCAL_MODULE))/generated/vendor_sepolicy.cil
$(m2381_vibrator_policy): vendor/meizu/m2381/proprietary/vendor/etc/selinux/vendor_sepolicy.cil $(LOCAL_PATH)/configs/selinux/vibrator.cil
	@mkdir -p $(dir $@)
	cat $^ > $@
LOCAL_PREBUILT_MODULE_FILE := $(m2381_vibrator_policy)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := m2381_vendor_file_contexts
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/etc/selinux
LOCAL_MODULE_STEM := vendor_file_contexts
LOCAL_VENDOR_MODULE := true
LOCAL_OVERRIDES_MODULES := vendor_file_contexts
m2381_vibrator_contexts := $(call intermediates-dir-for,ETC,$(LOCAL_MODULE))/generated/vendor_file_contexts
$(m2381_vibrator_contexts): vendor/meizu/m2381/proprietary/vendor/etc/selinux/vendor_file_contexts $(LOCAL_PATH)/configs/selinux/vibrator_file_contexts
	@mkdir -p $(dir $@)
	cat $^ > $@
LOCAL_PREBUILT_MODULE_FILE := $(m2381_vibrator_contexts)
include $(BUILD_PREBUILT)

# 诊断版本预编译与 init 完全相同的分离策略，不能安装原厂整包二进制，
# 也不能用源码 vendor 策略替代实际安装的 API 33 厂商策略。
# check_split_policy.py 的 -N 与 init 运行时保持一致；不表示 neverallow 适配完成。
include $(CLEAR_VARS)
LOCAL_MODULE := m2381_precompiled_sepolicy
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT_ODM)/etc/selinux
LOCAL_MODULE_STEM := precompiled_sepolicy
LOCAL_ODM_MODULE := true
LOCAL_OVERRIDES_MODULES := precompiled_sepolicy
LOCAL_REQUIRED_MODULES := sepolicy_neverallows
m2381_combined_policy_dir := $(call intermediates-dir-for,ETC,$(LOCAL_MODULE))/generated
m2381_policy_tool := $(LOCAL_PATH)/tools/compile_split_policy.py
m2381_policy_inputs := \
    $(TARGET_OUT)/etc/selinux/plat_sepolicy.cil \
    $(TARGET_OUT)/etc/selinux/mapping/33.0.cil \
    $(TARGET_OUT)/etc/selinux/mapping/33.0.compat.cil \
    $(TARGET_OUT_SYSTEM_EXT)/etc/selinux/system_ext_sepolicy.cil \
    $(TARGET_OUT_SYSTEM_EXT)/etc/selinux/mapping/33.0.cil \
    $(TARGET_OUT_SYSTEM_EXT)/etc/selinux/mapping/33.0.compat.cil \
    $(TARGET_OUT_PRODUCT)/etc/selinux/product_sepolicy.cil \
    $(TARGET_OUT_PRODUCT)/etc/selinux/mapping/33.0.cil \
    $(TARGET_OUT_VENDOR)/etc/selinux/plat_sepolicy_vers.txt \
    $(TARGET_OUT_VENDOR)/etc/selinux/plat_pub_versioned.cil \
    $(TARGET_OUT_VENDOR)/etc/selinux/vendor_sepolicy.cil \
    $(TARGET_OUT_VENDOR)/etc/selinux/genfs_labels_version.txt \
    $(TARGET_OUT_ODM)/etc/selinux/odm_sepolicy.cil
$(m2381_combined_policy_dir)/policy: PRIVATE_POLICY_TOOL := $(m2381_policy_tool)
$(m2381_combined_policy_dir)/policy: $(m2381_policy_inputs) $(m2381_policy_tool) $(HOST_OUT_EXECUTABLES)/secilc
	python3 $(PRIVATE_POLICY_TOOL) --secilc $(HOST_OUT_EXECUTABLES)/secilc \
	    --system $(TARGET_OUT) --system-ext $(TARGET_OUT_SYSTEM_EXT) \
	    --product $(TARGET_OUT_PRODUCT) --vendor $(TARGET_OUT_VENDOR) \
	    --odm $(TARGET_OUT_ODM) --output $(dir $@)
LOCAL_PREBUILT_MODULE_FILE := $(m2381_combined_policy_dir)/policy
include $(BUILD_PREBUILT)

include $(call all-makefiles-under,$(LOCAL_PATH))

endif
