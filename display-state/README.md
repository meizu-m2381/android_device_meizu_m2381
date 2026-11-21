# M2381 AOD 亮度

面板进入低功耗后需要独立的 `aod_light_mode` 命令，值 0 为暗档、1 为亮档，
2 为尚未初始化。正常背光设置不能替代这条命令。

系统专属 `DisplayAuxService` 在启动完成后加载 `AodBrightnessController`。
仅在默认显示处于 DOZE/DOZE_SUSPEND 时订阅本机普通非唤醒 ALS；亮屏或显示
移除后取消订阅、清理旧照度。每次会话使用独立监听器和代次标识，丢弃上一会话
迟到的回调；允许 on-change 启用时合法交付的缓存样本。
有效照度初次按 2 lux 分类，后续 <=2 lux 进入暗档、>=6 lux 回到亮档，
区间内保持原档位。NaN、无穷和负照度不改变状态。

原厂 Flyme `SensorPolicy.updateAmbientLux()` 使用相同 2/6 lux 回差；其默认
TYPE_LIGHT 也是非唤醒传感器。原厂 `AODStateController.checkNightMode()` 的夜间
时段为本地 22:00–06:00，`DozeScreenBrightness.updateDozeBrightness()` 将两种
状态组合后写入面板节点。本实现独立编写，没有引入原厂 Java 类或修改公共 AOSP。
参考并核对了 [LineageOS DozeScreenBrightness](https://github.com/LineageOS/android_frameworks_base/blob/lineage-24.0/packages/SystemUI/src/com/android/systemui/doze/DozeScreenBrightness.java)
与官方 Gerrit 240585；它们使用设备亮度传感器和档位映射，本机还需要上述面板命令。

框架只通过系统拥有的只读对外属性 `sys.m2381.aod.ambient_mode` 发布环境档位。
`m2381-aod` 是唯一面板写入者，在有效 AOD 状态中应用档位，同时覆盖夜间暗档；
缺失或无效属性退回暗档。使用本地时区，22:00 含、06:00 不含；仅当内核读回值
与目标不同才发送命令，写入失败下轮重试。源码策略和实际安装的 API33 CIL 均允许
守护进程读取该属性，只有 system_server 获准写入。

没有新增唤醒光感、唤醒锁或唤醒定时器。HAL 低功耗采集的普通 ALS 读数在系统已有
唤醒时处理；挂起期间改变环境光，档位可能等到下一次已有唤醒才更新。
夜间边界同样在已有唤醒后应用。不能把这项设计替代实际长时功耗测量。

诊断：`adb shell dumpsys m2381_display_aux` 显示传感器、订阅状态、最近照度和环境
档位；面板最终档位需要读取 `/sys/kernel/display_drivers/aod_light_mode`。
`display-state/tests/` 覆盖回差、精确边界、会话重置、无效输入与全日夜间窗口，
编译/纯逻辑检查不等同于实体 AOD 可见性或光感硬件验收。
