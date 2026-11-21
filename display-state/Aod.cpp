// SPDX-License-Identifier: Apache-2.0
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/system_properties.h>
#include "AodMode.h"

namespace {
constexpr char kPanelState[] = "/sys/class/meizu/lcm/display/aod";
constexpr char kLightMode[] = "/sys/kernel/display_drivers/aod_light_mode";
volatile sig_atomic_t running = 1;
void stop(int) { running = 0; }

bool readNode(const char* path, char* buffer, size_t size) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ssize_t count = read(fd, buffer, size - 1);
    close(fd);
    if (count <= 0) return false;
    buffer[count] = '\0';
    return true;
}
}  // namespace

int main() {
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    while (running) {
        char state[64], mode[64], ambient[PROP_VALUE_MAX]{};
        // The framework device service publishes ambient hysteresis only.
        // Keep the night override and hardware writes in this vendor helper.
        __system_property_get("sys.m2381.aod.ambient_mode", ambient);
        time_t now = time(nullptr);
        tm local{};
        int hour = localtime_r(&now, &local) ? local.tm_hour : -1;
        int target = meizu::aodMode(ambient, hour);
        int current = -1;
        if (readNode(kPanelState, state, sizeof(state)) && strtol(state, nullptr, 10) == 1 &&
            readNode(kLightMode, mode, sizeof(mode)) &&
            sscanf(mode, "aod light mode is : %d", &current) == 1 &&
            current >= 0 && current <= 2 && current != target) {
            int fd = open(kLightMode, O_WRONLY | O_CLOEXEC);
            if (fd >= 0) {
                // The kernel checks LP state again, including across a wake race.
                // A failed write leaves the old mode and is retried next cycle.
                const char command[]{static_cast<char>('0' + target), '\n'};
                (void)write(fd, command, sizeof(command));
                close(fd);
            }
        }
        // CLOCK_MONOTONIC does not wake the system from suspend. No wakelock.
        const timespec delay{1, 0};
        nanosleep(&delay, nullptr);
    }
    return 0;
}
