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

强度区分、停止保护有用，但不足以证明各场景触感正确。频率校准未实现。
组合原语现通过下述 Tiktap 后端接入，不依赖另一个高通输入设备。

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
  merged：没有实际完成通知时不应声明 callback 能力。本服务的 on/perform 仍不
  声明回调；compose 则在隔离的 RTP 生产进程完成并停止硬件后通知完成。
- [Oplus 强度处理](https://github.com/LineageOS/android_hardware_oplus/blob/e1d08a1858071bda70547ea2cff39e6a4ae0b0b9/aidl/vibrator/Vibrator.cpp#L307)
  的输入 FF 通路按 LIGHT/MEDIUM/STRONG 选择约 1/2、3/4、1 的相对幅度。
  本机只参考这组三档比例，使用自己的 128 基准；不复制其他硬件的寄存器或电压。
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
| TEXTURE_TICK | 8 | 139 | 5.79 ms | 128 |
| TICK | 11 | 231 | 9.63 ms | 128 |
| CLICK | 13 | 231 | 9.63 ms | 128 |
| HEAVY_CLICK | 3 | 374 | 15.58 ms | 128 |

这些是实际短脉冲，不是空白占位项；结尾样本收敛至 0。不改升压电压、保护寄存器
或校准数据。TICK/CLICK/HEAVY_CLICK
依据本机 Flyme 12.7 的效果 21000/31008/30900 及驱动映射选择 RAM 11/13/3，
替代第一轮的 12/17/16。标准 DRAG_START 通过 HEAVY_CLICK 使用 RAM 3。
TEXTURE_TICK 沿用 RAM 8。STRONG 使用原厂驱动的 0x80 基准增益，取消此前
8/32/48/96 的每效果额外衰减。Android 系统仍通过 effect strength 表达用户档位，
必须在 HAL 落实：LIGHT/MEDIUM/STRONG 分别使用 64/96/128，关闭由 framework
拦截。1/2、3/4、1 的相对比例参考 LineageOS Oplus 输入 FF 通路的强度等级；
这是保留 Android 调节能力的设备适配，不是声称 Flyme 本身有此三档 RAM 增益。
第三轮曾照搬 Flyme 仅传 ID 的策略使三档全部 128，已修正该兼容性问题。
时间/幅度接口仍可调节力度，setAmplitude 的 1.0 对应 128、0.25 对应 32，按 MzVibrateThread 的
amplitude * 128 后转整数的比例处理（向零截断，合法正幅度最低保留 1）；on()
和启动默认增益也恢复 128。
原厂 on_off 可继承先前增益，原厂 enable 的默认及停止入口设置 128；本服务在每次
效果启动时显式设置基准，避免继承先前的时间波形幅度。真机直接核对芯片寄存器
0x39 的增益，不能只以 gain_show 缓存判断。播放仍走 waveform/enable，未移植
Flyme 的 GPIO on_off 完整通路，因此不能宣称全部机械手感完全相同。
原厂证据详见 manifest 仓 `docs/flyme-haptics.md`；最终触感需用户握持对比。

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

## Tiktap 组合后端

原厂算法和 AW8697 RTP 接口的还原及真机证据见 manifest 仓
`docs/tiktap-composition.md`。仅在算法库、proc 节点可访问且原厂 Tiktap HIDL
未运行时报告 CAP_COMPOSE_EFFECTS；设备 init 覆盖并禁用原厂 HIDL 服务，避免
两个算法实例争用全局共享区。普通 RAM 预设、三档 gain 和 on/setAmplitude 保留。

| 原语 | 时长 | 设备映射 |
| --- | --- | --- |
| NOOP | 0 ms | 只等待请求中的 delay，不启动马达 |
| CLICK | 20 ms | 原厂 frequency=50 的 467 样本瞬态 |
| THUD | 60 ms | 0/6/18/60 ms，强度 0/100/35/0、frequency=20 的撞击衰减包络 |
| SPIN | 100 ms | 0/25/70/100 ms，强度 0/70/100/0、frequency=10/30/80/100 的动量包络 |
| LIGHT_TICK | 12 ms | 原厂 frequency=100 的 272 样本带制动瞬态 |
| LOW_TICK | 36 ms | 原厂 frequency=20 的 564 样本瞬态，后留约 12.5 ms 静默 |
| QUICK_RISE | 30 ms | 0/12/25/30 ms 上升包络 |
| SLOW_RISE | 100 ms | 0/40/90/100 ms 上升包络 |
| QUICK_FALL | 25 ms | 0/3/12/25 ms 下降包络 |

这些是当前 Android 标准语义的设备适配参数，不声称是 Flyme 的标准原语映射。
THUD/SPIN 也是设备自定曲线，并非提取出的 Flyme 原语参数；frequency 是原厂
抽象参数，不是 Hz。全部九项原语最多 16 项，每项 delay 0–1000 ms，严格拒绝非法 scale、
NaN、未知原语和超限输入。全局 amplitude 固定 128，scale 通过各点强度表达；
scale=0 使用最小输出而非触发原厂 amplitude=0 的默认峰值分支。用户关闭仍由
framework 拦截；低/中/高由 framework 缩放传入的 scale。

每次 compose 通过 posix_spawn 在同一可执行文件的工作模式中运行原厂库，
避免其 detached 线程与下个请求共享状态。父进程取消时 kill/reap 旧生产进程，
再发 STOP ioctl，随后才允许新的 RAM/RTP 播放。子进程设父进程死亡信号，
init 在 HAL 异常退出时额外执行 STOP，保留原来的 enable=0 保护。

完成不使用 on 返回、算法生成结束或最后一个 mmap 块被消费作为依据。以首次
驱动消费之后的完整时间线加 10 ms 余量为截止点，子进程同步 off 后退出，父进程
确认退出并再发 STOP 后才回调。另有 500 ms 余量的看门狗。该策略偏保守，不能
替代硬件完成 IRQ；进程启动开销、长序列欠载和高负载时序仍需进一步测量。
播放中不轮询整块 reg，因为它会占驱动锁约 110 ms。

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
当前诊断还覆盖九个原语的完成回调、THUD/SPIN 混合时间线及满长度 SPIN 取消、
五段 LOW_TICK、取消等待中的请求、
RAM/RTP 双向抢占、非法组合参数及连续 20 次滑条请求替换。

固件分析、寄存器和 Binder 检查不能代替加速度仪测量，也不证明所有应用的主观
触感、来电可感知度、长期功耗或完整 CTS/VTS。测试记录见 manifest 仓功能状态。
