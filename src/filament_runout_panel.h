#ifndef __FILAMENT_RUNOUT_PANEL_H__
#define __FILAMENT_RUNOUT_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"

#include <mutex>
#include <string>

// Watches the filament sensor; on runout during a print it pops a styled dialog
// (same look as the app's other msgbox dialogs). The first button feeds filament
// in stoppable chunks and toggles its label Load <-> Stop while feeding; the
// others are Continue (re-checks the sensor, then RESUME) and Cancel
// (CANCEL_PRINT).
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

  // Chunked, stoppable filament feed. The stock LOAD_MATERIAL macro feeds 150mm
  // as one uninterruptible G1 E150 move; instead we feed it in small bounded
  // chunks driven from each chunk's response, so a second Load tap stops it.
  void begin_load();
  void send_load_chunk();
  void finish_load();
  void set_mbox_text(const char *txt);
  void set_load_btn(bool loading);   // relabel the first button Load <-> Stop

  KWebSocketClient &ws;
  lv_obj_t *mbox;                    // open runout dialog, or NULL

  bool baselined;
  std::string fil_key;               // "filament_switch_sensor <name>" or ""
  bool last_detected;

  bool load_active = false;          // chunked feed in progress
  bool load_stop = false;            // set to halt the feed after the in-flight chunk
  int load_remaining_mm = 0;

  // Cancel-in-progress: set between the user pressing Cancel (CANCEL_PRINT sent)
  // and print_stats actually leaving printing/paused. CANCEL_PRINT takes a few
  // seconds, during which the sensor can throw another runout edge that would
  // instantly re-pop a freshly-closed dialog. So while cancelling we keep the
  // dialog up showing "Cancelling print...", lock its buttons, and let consume()
  // tear it down once the print really ends. cancel_timeout is a fallback that
  // force-closes if state never transitions (e.g. a CANCEL_PRINT macro error).
  bool cancelling = false;
  lv_timer_t *cancel_timeout = NULL;
};

#endif  // __FILAMENT_RUNOUT_PANEL_H__
