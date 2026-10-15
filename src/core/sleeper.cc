#include "core/sleeper.h"

#include <unistd.h>

namespace wc {

void RealSleeper::sleep_ms(int ms) {
    if (ms > 0) usleep(static_cast<useconds_t>(ms) * 1000);
}

}  // namespace wc
