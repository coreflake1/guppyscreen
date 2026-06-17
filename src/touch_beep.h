#ifndef __TOUCH_BEEP_H__
#define __TOUCH_BEEP_H__

#include "lvgl/lvgl.h"

// Audible UI click feedback, mirroring stock Creality KE behaviour: the KE has
// no soundcard, so a single fixed-pitch pulse of the hardware buzzer (GPIO PC03
// via /usr/bin/beep) is fired on each button tap. Opt-in; off by default.
namespace TouchBeep {
  // Enable/disable the click feedback (driven by the Settings toggle / config).
  void set_enabled(bool enabled);
  bool is_enabled();

  // Fire the buzzer once, non-blocking. No-op in the simulator.
  void beep();

  // LVGL input-device feedback hook: beeps on LV_EVENT_CLICKED when enabled.
  // Registered as indev_drv.feedback_cb so it fires for any clickable widget
  // without per-widget wiring, and never on scroll/drag (no CLICKED there).
  void feedback_cb(lv_indev_drv_t *drv, uint8_t event_code);
}

#endif // __TOUCH_BEEP_H__
