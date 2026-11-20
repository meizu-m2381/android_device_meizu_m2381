// SPDX-License-Identifier: Apache-2.0
#define LOG_TAG "M2381SensorAux"

#include <android-base/file.h>
#include <android-base/strings.h>
#include <android/gui/BnRegionSamplingListener.h>
#include <android/gui/BnScreenCaptureListener.h>
#include <android/hidl/base/1.0/IBase.h>
#include <android/os/IPowerManager.h>
#include <binder/IServiceManager.h>
#include <binder/Parcel.h>
#include <binder/ProcessState.h>
#include <gui/SurfaceComposerClient.h>
#include <hidl/HidlBinderSupport.h>
#include <hidl/ServiceManagement.h>
#include <hwbinder/IBinder.h>
#include <hwbinder/Parcel.h>
#include <log/log.h>
#include <ui/GraphicBuffer.h>
#include <ui/DisplayState.h>

#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <future>
#include <optional>
#include <thread>

using namespace android;
using namespace std::chrono_literals;

namespace {
constexpr char kDescriptor[] = "vendor.meizu.sensor_aux@1.0::ISensorAux";
constexpr char kPower[] = "/sys/class/drm/card0-DSI-1/dpms";
// Flyme's libsensorservice uses this physical panel region on m2381.
const Rect kRegion(672, 62, 684, 74);
volatile std::sig_atomic_t running = 1;

void stop(int) { running = 0; }

class SensorAux {
public:
    bool connect() {
        auto service = hardware::details::getRawServiceInternal(kDescriptor, "default", false, false);
        if (!service) return false;
        binder_ = hardware::toBinder(service);
        return binder_ != nullptr;
    }

    bool control(bool enabled) {
        hardware::Parcel request, reply;
        if (request.writeInterfaceToken(kDescriptor) != OK || request.writeBool(enabled) != OK) {
            return false;
        }
        return transact(4, request, &reply);
    }

    bool update(uint32_t rgb) {
        hardware::Parcel request, reply;
        if (request.writeInterfaceToken(kDescriptor) != OK || request.writeInt32(0) != OK ||
            request.writeUint32(rgb) != OK) {
            return false;
        }
        return transact(8, request, &reply);
    }

private:
    // These two void methods and their wire IDs are verified against the stock
    // BpHwSensorAux proxy. The other proprietary methods are not used.
    bool transact(uint32_t code, const hardware::Parcel& request, hardware::Parcel* reply) {
        hardware::Status status;
        return binder_ && binder_->transact(code, request, reply, 0) == OK &&
                hardware::readFromParcel(&status, *reply) == OK && status.isOk();
    }
    sp<hardware::IBinder> binder_;
};

class RegionListener : public gui::BnRegionSamplingListener {
public:
    binder::Status onSampleCollected(float) override {
        // AOSP notifies on sampled frames, including equal-luma color changes.
        // Binder threads never block on another SurfaceFlinger capture.
        dirty.store(true);
        return binder::Status::ok();
    }
    std::atomic<bool> dirty{true};
};

class CaptureListener : public gui::BnScreenCaptureListener {
public:
    binder::Status onScreenCaptureCompleted(const gui::ScreenCaptureResults& result) override {
        promise.set_value(result);
        return binder::Status::ok();
    }
    std::promise<gui::ScreenCaptureResults> promise;
};

std::optional<Rect> samplingRegion(const sp<IBinder>& display) {
    ui::DisplayState state;
    if (SurfaceComposerClient::getDisplayState(display, &state) != OK) return std::nullopt;
    Rect region = kRegion;
    int width = 1080, height = 2400;
    switch (state.orientation) {
        case ui::ROTATION_0: break;
        case ui::ROTATION_90:
            region = Rect(kRegion.top, 1080 - kRegion.right, kRegion.bottom, 1080 - kRegion.left);
            width = 2400; height = 1080;
            break;
        case ui::ROTATION_180:
            region = Rect(1080 - kRegion.right, 2400 - kRegion.bottom,
                          1080 - kRegion.left, 2400 - kRegion.top);
            break;
        case ui::ROTATION_270:
            region = Rect(2400 - kRegion.bottom, kRegion.left,
                          2400 - kRegion.top, kRegion.right);
            width = 2400; height = 1080;
            break;
    }
    // SurfaceFlinger capture crops in logical layer-stack coordinates, while
    // the sensor stays at one physical panel position, including with wm size.
    const float sx = static_cast<float>(state.layerStackSpaceRect.width) / width;
    const float sy = static_cast<float>(state.layerStackSpaceRect.height) / height;
    region = Rect(std::floor(region.left * sx), std::floor(region.top * sy),
                  std::ceil(region.right * sx), std::ceil(region.bottom * sy));
    if (region.isEmpty()) return std::nullopt;
    return region;
}

struct ColorTransform {
    std::array<float, 16> matrix;
    bool linear;
};

std::optional<ColorTransform> colorTransform() {
    static sp<IBinder> service;
    if (!service) {
        service = defaultServiceManager()->checkService(String16("m2381_display_aux"));
    }
    if (!service) return std::nullopt;
    Parcel request, reply;
    request.writeInterfaceToken(String16("meizu.display.IColorTransform"));
    if (service->transact(IBinder::FIRST_CALL_TRANSACTION, request, &reply) != OK) {
        service.clear();
        return std::nullopt;
    }
    if (reply.readExceptionCode() != 0 || reply.readInt32() != 1) return std::nullopt;
    ColorTransform transform;
    transform.linear = reply.readInt32() != 0;
    for (auto& value : transform.matrix) {
        if (reply.readFloat(&value) != OK || !std::isfinite(value)) return std::nullopt;
    }
    return transform;
}

struct Sample {
    std::array<uint32_t, 144> pixels{};
    bool supported = true;
};

std::optional<gui::ScreenCaptureResults> captureFrame(const gui::DisplayCaptureArgs& args) {
    auto listener = sp<CaptureListener>::make();
    auto future = listener->promise.get_future();
    if (ScreenshotClient::captureDisplay(args, listener) != OK ||
            future.wait_for(750ms) != std::future_status::ready) {
        return std::nullopt;
    }
    return future.get();
}

std::optional<Sample> capture(const sp<IBinder>& display, const Rect& region) {
    gui::DisplayCaptureArgs args;
    args.displayToken = display;
    args.captureArgs.sourceCrop.left = region.left;
    args.captureArgs.sourceCrop.top = region.top;
    args.captureArgs.sourceCrop.right = region.right;
    args.captureArgs.sourceCrop.bottom = region.bottom;
    args.width = kRegion.getWidth();
    args.height = kRegion.getHeight();
    args.captureArgs.dataspace = static_cast<int32_t>(ui::Dataspace::V0_SRGB);
    args.captureArgs.secureLayerMode = gui::SecureLayerMode::Redact;
    args.captureArgs.protectedLayerMode = gui::ProtectedLayerMode::Error;
    args.captureArgs.preserveDisplayColors = true;
    // Android 17's GPU path redacts protected buffers even with Error mode,
    // and ScreenCaptureResults has no protected-content flag. Its DPU path
    // explicitly returns PERMISSION_DENIED for protected or secure layers.
    // This sRGB/scaled crop cannot use DPU readback: normal content returns
    // INVALID_OPERATION, without allocating/capturing any protected pixels.
    args.captureArgs.captureMode = gui::CaptureMode::RequireOptimized;
    const auto preflight = captureFrame(args);
    if (!preflight) return std::nullopt;
    if (!preflight->fenceResult.ok() && preflight->fenceResult.error() == PERMISSION_DENIED) {
        Sample sample;
        sample.supported = false;
        return sample;
    }
    if (preflight->fenceResult.ok() || preflight->fenceResult.error() != INVALID_OPERATION) {
        return std::nullopt;
    }
    args.captureArgs.captureMode = gui::CaptureMode::None;
    // Keep the display's SDR primaries instead of clipping wide-gamut content
    // to sRGB before calculating the panel RGB. Both supported outputs use
    // the sRGB transfer function; HDR is handled as unsupported below.
    args.captureArgs.dataspace = static_cast<int32_t>(ui::Dataspace::UNKNOWN);
    const auto captured = captureFrame(args);
    if (!captured) return std::nullopt;
    const auto& result = *captured;
    // A redacted secure layer is not a black emitting pixel. HDR screenshots
    // are tone-mapped and cannot be treated as calibrated panel RGB either.
    if (result.capturedSecureLayers || result.capturedHdrLayers ||
            (result.capturedDataspace != ui::Dataspace::V0_SRGB &&
             result.capturedDataspace != ui::Dataspace::DISPLAY_P3)) {
        Sample sample;
        sample.supported = false;
        return sample;
    }
    if (!result.fenceResult.ok() || !result.buffer ||
            result.fenceResult.value()->wait(250) != OK) {
        return std::nullopt;
    }
    auto& buffer = result.buffer;
    if (buffer->getWidth() != 12 || buffer->getHeight() != 12 ||
            buffer->getPixelFormat() != PIXEL_FORMAT_RGBA_8888) {
        return std::nullopt;
    }
    void* address = nullptr;
    if (buffer->lock(GRALLOC_USAGE_SW_READ_OFTEN, &address) != OK) return std::nullopt;
    Sample sample;
    if (address) {
        const auto* pixels = static_cast<const uint32_t*>(address);
        for (unsigned y = 0; y < 12; ++y) {
            for (unsigned x = 0; x < 12; ++x) {
                sample.pixels[y * 12 + x] = pixels[y * buffer->getStride() + x];
            }
        }
    }
    buffer->unlock();
    if (!address) return std::nullopt;
    return sample;
}

float toLinear(float value) {
    return value <= .04045f ? value / 12.92f : std::pow((value + .055f) / 1.055f, 2.4f);
}

float toSrgb(float value) {
    return value <= .0031308f ? 12.92f * value : 1.055f * std::pow(value, 1.f / 2.4f) - .055f;
}

uint32_t average(const Sample& sample, const ColorTransform& transform) {
    std::array<double, 3> sums{};
    const auto& m = transform.matrix;
    for (uint32_t pixel : sample.pixels) {
        std::array<float, 3> rgb{(pixel & 255) / 255.f, ((pixel >> 8) & 255) / 255.f,
                                 ((pixel >> 16) & 255) / 255.f};
        if (transform.linear) for (auto& value : rgb) value = toLinear(value);
        for (size_t channel = 0; channel < 3; ++channel) {
            float value = std::clamp(m[channel] * rgb[0] + m[4 + channel] * rgb[1] +
                                             m[8 + channel] * rgb[2] + m[12 + channel],
                                     0.f, 1.f);
            if (transform.linear) value = toSrgb(value);
            sums[channel] += value * 255.f;
        }
    }
    // Only the transformed average leaves this process. The last tiny sample
    // stays in memory so matrix animations do not require additional captures.
    return (static_cast<uint32_t>(std::lround(sums[0] / 144)) << 16) |
            (static_cast<uint32_t>(std::lround(sums[1] / 144)) << 8) |
            static_cast<uint32_t>(std::lround(sums[2] / 144));
}

bool displayOn() {
    // AOD can leave DRM DPMS at "On". Gate sampling on Android's interactive
    // state as well, so an always-on panel does not keep the sampler active.
    static sp<os::IPowerManager> powerManager;
    if (!powerManager) {
        powerManager = interface_cast<os::IPowerManager>(
                defaultServiceManager()->checkService(String16("power")));
    }
    bool interactive = false;
    if (!powerManager) return false;
    if (!powerManager->isInteractive(&interactive).isOk()) {
        powerManager.clear();
        return false;
    }
    if (!interactive) return false;
    std::string power;
    return base::ReadFileToString(kPower, &power) && base::Trim(power) == "On";
}

} // namespace

int main() {
    std::signal(SIGTERM, stop);
    std::signal(SIGINT, stop);
    ProcessState::self()->startThreadPool();
    SensorAux aux;
    auto listener = sp<RegionListener>::make();
    sp<IBinder> display;
    Rect activeRegion;
    bool connected = false, enabled = false, registered = false;
    std::optional<Sample> lastSample;
    auto lastCapture = std::chrono::steady_clock::now() - 10s;
    while (running) {
        const auto cycle = std::chrono::steady_clock::now();
        if (!connected) {
            connected = aux.connect();
            if (!connected) {
                std::this_thread::sleep_for(1s);
                continue;
            }
            // Discard compensation left by a previous process before reconnecting.
            if (!aux.control(false)) return 1;
        }
        if (!displayOn()) {
            if (registered) {
                SurfaceComposerClient::removeRegionSamplingListener(listener);
                registered = false;
            }
            if (enabled && !aux.control(false)) connected = false;
            enabled = false;
            lastSample.reset();
            // CLOCK_MONOTONIC sleeps do not wake a suspended device. No wakelock.
            std::this_thread::sleep_for(1s);
            continue;
        }
        if (!display) {
            const auto ids = SurfaceComposerClient::getPhysicalDisplayIds();
            if (!ids.empty()) display = SurfaceComposerClient::getPhysicalDisplayToken(ids.front());
            if (!display) {
                std::this_thread::sleep_for(1s);
                continue;
            }
        }
        const auto region = samplingRegion(display);
        if (!region) {
            if (registered) SurfaceComposerClient::removeRegionSamplingListener(listener);
            registered = false;
            aux.control(false);
            enabled = false;
            lastSample.reset();
            display.clear();
            std::this_thread::sleep_for(1s);
            continue;
        }
        if (registered && activeRegion != *region) {
            SurfaceComposerClient::removeRegionSamplingListener(listener);
            registered = false;
        }
        if (!registered) {
            registered = SurfaceComposerClient::addRegionSamplingListener(*region, nullptr,
                                                                          listener) == OK;
            if (!registered) return 1;
            activeRegion = *region;
            listener->dirty.store(true);
        }
        const auto transform = colorTransform();
        if (!transform) {
            if (enabled && !aux.control(false)) connected = false;
            enabled = false;
            std::this_thread::sleep_for(1s);
            continue;
        }
        if (!enabled) {
            // Stock updateRGBAvg skips unchanged RGB and backlight. Force a
            // transition so waking to the same frame re-enables DSP compensation.
            if (!aux.control(true) || !aux.update(0)) return 1;
            enabled = true;
            listener->dirty.store(true);
        }
        const bool changed = listener->dirty.exchange(false);
        // Rare fallback also recovers a lost composer listener after SF restarts.
        if (changed || cycle - lastCapture > 5s) {
            const auto sample = capture(display, *region);
            if (!sample) {
                ALOGW("Region capture unavailable; reconnecting compositor");
                aux.control(false);
                enabled = false;
                SurfaceComposerClient::removeRegionSamplingListener(listener);
                registered = false;
                display.clear();
                std::this_thread::sleep_for(1s);
                continue;
            }
            lastSample = sample;
            lastCapture = cycle;
        }
        if (lastSample && !lastSample->supported) {
            if (enabled && !aux.control(false)) connected = false;
            enabled = false;
            std::this_thread::sleep_for(1s);
            continue;
        }
        // The stock HAL reads backlight and skips SSC updates if both RGB and
        // backlight are unchanged. Keep that vendor access inside the HAL;
        // brightness-only changes do not need another screen capture.
        if (lastSample && !aux.update(average(*lastSample, *transform))) {
            connected = false;
            enabled = false;
            lastSample.reset();
            std::this_thread::sleep_for(1s);
            continue;
        }
        // Coalesce animations and let the stock HAL detect backlight changes.
        std::this_thread::sleep_until(cycle + 250ms);
    }
    if (registered) SurfaceComposerClient::removeRegionSamplingListener(listener);
    if (connected) aux.control(false);
    return 0;
}
