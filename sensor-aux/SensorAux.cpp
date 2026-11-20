// SPDX-License-Identifier: Apache-2.0
#define LOG_TAG "M2381SensorAux"

#include <android-base/file.h>
#include <android-base/strings.h>
#include <android/gui/BnRegionSamplingListener.h>
#include <android/gui/BnScreenCaptureListener.h>
#include <android/hidl/base/1.0/IBase.h>
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

std::optional<uint32_t> capture(const sp<IBinder>& display, const Rect& region) {
    gui::DisplayCaptureArgs args;
    args.displayToken = display;
    args.captureArgs.sourceCrop.left = region.left;
    args.captureArgs.sourceCrop.top = region.top;
    args.captureArgs.sourceCrop.right = region.right;
    args.captureArgs.sourceCrop.bottom = region.bottom;
    args.width = kRegion.getWidth();
    args.height = kRegion.getHeight();
    args.captureArgs.dataspace = static_cast<int32_t>(ui::Dataspace::V0_SRGB);
    args.captureArgs.secureLayerMode = gui::SecureLayerMode::Capture;
    args.captureArgs.preserveDisplayColors = true;
    auto listener = sp<CaptureListener>::make();
    auto future = listener->promise.get_future();
    if (ScreenshotClient::captureDisplay(args, listener) != OK ||
            future.wait_for(750ms) != std::future_status::ready) {
        return std::nullopt;
    }
    auto result = future.get();
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
    uint32_t red = 0, green = 0, blue = 0;
    if (address) {
        const auto* pixels = static_cast<const uint32_t*>(address);
        for (unsigned y = 0; y < 12; ++y) {
            for (unsigned x = 0; x < 12; ++x) {
                const uint32_t pixel = pixels[y * buffer->getStride() + x];
                red += pixel & 255;
                green += (pixel >> 8) & 255;
                blue += (pixel >> 16) & 255;
            }
        }
    }
    buffer->unlock();
    if (!address) return std::nullopt;
    // Only this average leaves the process. No image or RGB history is stored.
    return ((red / 144) << 16) | ((green / 144) << 8) | (blue / 144);
}

bool displayOn() {
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
    std::optional<uint32_t> lastRgb;
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
            lastRgb.reset();
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
            lastRgb.reset();
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
            const auto rgb = capture(display, *region);
            if (!rgb) {
                ALOGW("Region capture unavailable; reconnecting compositor");
                aux.control(false);
                enabled = false;
                SurfaceComposerClient::removeRegionSamplingListener(listener);
                registered = false;
                display.clear();
                std::this_thread::sleep_for(1s);
                continue;
            }
            lastRgb = rgb;
            lastCapture = cycle;
        }
        // The stock HAL reads backlight and skips SSC updates if both RGB and
        // backlight are unchanged. Keep that vendor access inside the HAL;
        // brightness-only changes do not need another screen capture.
        if (lastRgb && !aux.update(*lastRgb)) {
            connected = false;
            enabled = false;
            lastRgb.reset();
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
