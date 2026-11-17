# device/meizu/m2381

Meizu 20 (`m2381`) device tree for LineageOS-derived ROMs.

| Item | Value |
| ---- | ----- |
| SoC | Qualcomm SM8550 / kalama |
| Display | 1080 × 2400, 450 dpi |
| Kernel | Stock prebuilt Linux 5.15 (`device/meizu/m2381-kernel`) |
| Vendor API | 33 |
| LineageOS branch | `lineage-24.0` (Android 17) |

## Products

| Product | Makefile | Status |
| ------- | -------- | ------ |
| `lineage_m2381` | `lineage_m2381.mk` | Product entry present, build pending |
| `custom_m2381` | `custom_m2381.mk` | PixelOS 17.0 booted on hardware |

## Soong options

| Namespace | Variable | Description | Value |
| --------- | -------- | ----------- | ----- |
| android_hardware_audio | run_64bit | Build the audio HAL as 64-bit | true |
| camera | override_format_from_reserved | Allow camera format override | true |
| lineage_health | charging_control_supports_bypass | Expose charging bypass control | false |

## Dependencies

```
device/meizu/m2381-kernel    # Image, dtb, dtb.img, UAPI headers
vendor/meizu/m2381           # proprietary vendor blobs
```

## Stock components

Audio, display, camera, GNSS and boot-control services are taken from vendor
blobs. The matching QTI source stacks depend on kernel UAPI headers that the
Meizu kernel source does not publish, so they are not built from source.

## Vendor inputs

The matching fixed vendor inputs are maintained in
`proprietary_vendor_meizu_m2381`; retrieve them with Git LFS and verify them
using the manifest repository's input checks.
`proprietary-files.txt` records the extracted paths; this device tree does not
regenerate vendor makefiles during a normal build.

## 平台隔离

设备产品使用官方 LineageOS libbase_shim（32/64 位）、设备 linker 配置和 TARGET_PREBUILT_KERNEL_HEADERS。HAL 源码只通过 PRODUCT_SOURCE_ROOT_DIRS 在本产品排除，不移除上游项目。固定 blob 修补清单位于 configs/compat/libbase-fixups.json，tools/configure_blob_compat.py 生成独立 vendor/compat 副本，原厂输入保持不变。内核头选择器需要在 lunch 后导出实际 TARGET_PREBUILT_KERNEL_HEADERS；使用 manifest 仓库的新构建入口完成此步骤。
