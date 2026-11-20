// SPDX-License-Identifier: Apache-2.0
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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
        char state[64], mode[64];
        // LP1 resets the vendor brightness state to 2 (not initialized).
        // Flyme's DozeScreenBrightness sends a separate 0/1 brightness command
        // after entering DOZE. Initialize the verified dim mode only; leave
        // existing valid modes and normal display brightness untouched.
        if (readNode(kPanelState, state, sizeof(state)) && strtol(state, nullptr, 10) == 1 &&
            readNode(kLightMode, mode, sizeof(mode)) &&
            strcmp(mode, "aod light mode is : 2\n") == 0) {
            int fd = open(kLightMode, O_WRONLY | O_CLOEXEC);
            if (fd >= 0) {
                // The kernel checks LP state again, including across a wake race.
                // A failed write leaves mode 2 and is retried next cycle.
                (void)write(fd, "0\n", 2);
                close(fd);
            }
        }
        // CLOCK_MONOTONIC does not wake the system from suspend. No wakelock.
        const timespec delay{1, 0};
        nanosleep(&delay, nullptr);
    }
    return 0;
}
