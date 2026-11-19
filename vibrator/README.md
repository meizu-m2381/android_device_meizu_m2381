# M2381 线性马达适配

本服务只在设备仓实现 AIDL Vibrator HAL，不修改 AOSP framework、上游 HAL、
原厂内核模块或固件。M2381 实际马达使用 I2C 地址 `3-005a` 的 Awinic
AW8697（`haptic_hv`）驱动，不能用另一个 `qcom-hv-haptics` 输入设备替代。

## 旧方案评估

旧方案正确接通了独立马达、增益控制和异常退出停止保护，没有把硬件设成 ERM。
但它将四类效果都写入 `enable`，只改变时长和增益，始终留下 `waveform=1`。
短效果经 GPIO trigger 播放同一基础波形，再由用户态计时器停止，轻触的 8 ms
会截断约 15.6 ms 的完整波形，不能保证原厂波形的收尾。
真机旧日志还显示停止请求至 standby 有数毫秒到约 11 ms 的软件等待；这不是
加速度测量，不能直接当作机械拖尾时长。

强度区分、停止保护有用，但不足以证明各场景触感正确。频率校准与组合原语均
未实现；不能仅为避免 framework 回退而宣称支持这些能力。

## 官方参考与取舍

- [LineageOS 344562](https://review.lineageos.org/c/LineageOS/android_hardware_oplus/+/344562)，
  merged，`e1d08a1858071bda70547ea2cff39e6a4ae0b0b9`：同为 AW8697，
  区分触感需明确选择 RAM 波形，不能只依赖时长。其固件编号、vmax 单位与本机
  不同，因此只参考控制原则，不复制 OnePlus 的编号或电压值。
- [LineageOS 335282](https://review.lineageos.org/c/LineageOS/android_hardware_oplus/+/335282)，
  merged，`4ed0d7705b08d2087c7c712167b44ee409c504b4`：每个效果应显式配置
  波形和强度，防止上一个效果残留配置改变下一个效果。
- [LineageOS 496379](https://review.lineageos.org/c/LineageOS/android_device_osom_ingot/+/496379)，
  merged：有嗡感的波形不适合直接作为 CLICK/TICK；需按实际波形调校。
  其硬件与固件不同，不能直接迁移波形数据。
- [LineageOS 513314](https://review.lineageos.org/c/LineageOS/android_hardware_lineage_interfaces/+/513314)，
  merged：没有实际完成通知时不应声明 callback 能力。本服务继续只报告已实现的
  amplitude control，完成由 framework 和 HAL 停止保护处理。
- Gerrit 506310/506312 的 `haptic_hv` 波形长度和效果映射改动尚为 NEW，
  仅用来交叉核对问题，不作为已合入或已在 M2381 验证的实现。

## 本机固件与 sysfs 合约

原厂 `vendor/firmware/aw_170hz.bin` 长 4989 字节，SHA-256：
`63017f8303ef5872299931dea40df46700a00416af9f7e6c94abf4ba19142e37`。
前两字节为后续字节和的低 16 位 `0x168c`，RAM 基址 `0x0800`，
波形表从文件偏移 5 开始，每项为大端起止地址（闭区间）。共有 20 项。
本机 PWMDBG `0x2e` 的采样率位为 `0x40`，即 24 kHz。

| AIDL 效果 | RAM 编号（十进制） | 样本数 | 自然播放时长 | STRONG 增益 |
| --- | --- | --- | --- | --- |
| TEXTURE_TICK | 8 | 139 | 5.79 ms | 8 |
| TICK | 12 | 231 | 9.63 ms | 32 |
| CLICK | 17 | 232 | 9.67 ms | 48 |
| HEAVY_CLICK | 16 | 232 | 9.67 ms | 96 |

这些是实际短脉冲，不是空白占位项；结尾样本收敛至 0。LIGHT/MEDIUM 使用
40%/65% 增益；不改升压电压、保护寄存器或校准数据。编号是本机选择，
不是声称复现 Flyme 场景映射；最终力度、清脆度仍需用户握持对比。

- `waveform` 用 `%x` 解析，必须写 `0x11` 表示十进制 17；写 `17` 会选到
  0x17，超出固件范围。每次播放前先停止上一次内核 work，再显式选择波形。
- `enable` 用 `%d %d %d` 解析：毫秒、增益、停止间隔。增益必须随启动提交，
  不能只在启动前写 `gain`，因为启动入口会重置默认增益。
- 选定的短效果以 `enable="30 <gain> 0"` 进入内核 RAM 单次播放路径，
  loop 为 0，由芯片自然结束。30 ms 是向 framework 返回的完成预算，包含实测
  约 8 ms 的异步 I2C 启动余量，不是将脉冲拉长到 30 ms。不能把它增到大于
  50 ms，否则该驱动会将 loop 设为 15，变成持续播放。
- `on(timeout)` 显式恢复 1 号基础波形，避免通知继承上一个点击配置。
  持续播放仍保留 monotonic timer、generation 取消和 init 异常停止保护。
  `on()` 是兼容时间/幅度波形接口，不等同于硬件预置效果。
- 启动时由设备 init 将 `waveform` 节点权限设为 system:system 0660；沿用
  原厂 `vib_strength_sysfs` 标签和已有 HAL 写权限，不扩大其他域权限。

## 验证

在 Android 源码根目录完成 lunch 和预编译头变量导出后：

```sh
python3 device/meizu/m2381/vibrator/tests/verify_firmware.py \
  vendor/meizu/m2381/proprietary/vendor/firmware/aw_170hz.bin
m android.hardware.vibrator-service.meizu m2381-vibrator-check
adb push out/target/product/m2381/vendor/bin/m2381-vibrator-check /data/local/tmp/
adb shell chmod 755 /data/local/tmp/m2381-vibrator-check
adb shell cmd vibrator_manager cancel
adb shell /data/local/tmp/m2381-vibrator-check
```

测试程序只用于 root 真机诊断，不列入 PRODUCT_PACKAGES。它通过实际 Binder HAL
检查四效果三强度、RAM 编号、循环标志、自动停止、时间波形振幅、抢占、旧计时器
取消及非法参数。读取 GO/GLB_STATE 判断硬件待机，不以软件 `state` 冒充实际停止。
测试期间会播放短振动，应避免其他应用同时触发振动。

固件分析、寄存器和 Binder 检查不能代替加速度仪测量，也不证明所有应用的主观
触感、来电可感知度、长期功耗或完整 CTS/VTS。测试记录见 manifest 仓功能状态。
