#ifndef __FILAMENT_RUNOUT_PANEL_H__
#define __FILAMENT_RUNOUT_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"

#include <mutex>
#include <string>

// Watches the filament sensor; on runout during a print it pops a styled dialog
// (same look as the app's other msgbox dialogs) offering: Load filament (runs
// the configured load macro), Continue (re-checks the sensor, then RESUME),
// Cancel (CANCEL_PRINT).
class FilamentRunoutPanel : public NotifyConsumer {
 public:
  FilamentRunoutPanel(KWebSocketClient &ws, std::mutex &lock);
  ~FilamentRunoutPanel();

  void consume(json &j);
  void handle_mbox(lv_event_t *e);
  static void _mbox_cb(lv_event_t *e) { ((FilamentRunoutPanel*)e->user_data)->handle_mbox(e); }

#ifdef SIMULATOR
  void sim_show() { show(); }
#endif

 private:
  void show();
  void close();
  bool printing_or_paused() const;
  bool filament_present() const;     // current sensor reading from State
  std::string load_macro_gcode() const;

  KWebSocketClient &ws;
  lv_obj_t *mbox;                    // open runout dialog, or NULL

  bool baselined;
  std::string fil_key;               // "filament_switch_sensor <name>" or ""
  bool last_detected;
};

#endif  // __FILAMENT_RUNOUT_PANEL_H__
