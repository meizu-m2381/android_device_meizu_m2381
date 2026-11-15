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

## Extracting files

```bash
./extract-files.py
./setup-makefiles.py
```

`proprietary-files.txt` lists 5011 vendor files. Binaries are not stored here;
see `proprietary_vendor_meizu_m2381`.
