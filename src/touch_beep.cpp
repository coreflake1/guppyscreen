#include "touch_beep.h"
#include "spdlog/spdlog.h"

#include <atomic>

#ifndef SIMULATOR
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

namespace {
  std::atomic<bool> s_enabled{false};

  // Suppress a follow-up beep that lands within this window of the last one.
  // The buzzer pulse is short and the panel reports at ~50Hz, so back-to-back
  // CLICKED events from a single jittery tap shouldn't double-fire, and a burst
  // of rapid taps shouldn't queue overlapping beep processes on the GPIO.
  constexpr uint32_t DEBOUNCE_MS = 120;
  uint32_t s_last_tick = 0;

  const char *BEEP_PATH = "/usr/bin/beep";
}

namespace TouchBeep {

void set_enabled(bool enabled) {
  s_enabled.store(enabled);
}

bool is_enabled() {
  return s_enabled.load();
}

void beep() {
#ifndef SIMULATOR
  // Double-fork: the grandchild execs /usr/bin/beep and is reparented to init
  // (which reaps it), so we leave no zombie and never block the UI thread on
  // the buzzer pulse. The parent only waits on the short-lived middle child,
  // which exits immediately.
  pid_t pid = fork();
  if (pid == 0) {
    pid_t grandchild = fork();
    if (grandchild == 0) {
      execl(BEEP_PATH, "beep", (char *)NULL);
      _exit(127); // exec failed (beep missing) - nothing we can do here
    }
    _exit(0);
  } else if (pid > 0) {
    int status;
    waitpid(pid, &status, 0);
  }
#else
  spdlog::debug("TouchBeep::beep() (no-op in simulator)");
#endif
}

void feedback_cb(lv_indev_drv_t * /*drv*/, uint8_t event_code) {
  if (!s_enabled.load()) {
    return;
  }
  // CLICKED only fires when a clickable widget is pressed and released without
  // scrolling - i.e. a real button tap, matching when stock Creality beeped.
  if (event_code != LV_EVENT_CLICKED) {
    return;
  }
  uint32_t now = lv_tick_get();
  if (s_last_tick != 0 && (now - s_last_tick) < DEBOUNCE_MS) {
    return;
  }
  s_last_tick = now;
  beep();
}

}
