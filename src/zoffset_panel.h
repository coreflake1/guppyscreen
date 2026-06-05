#ifndef __ZOFFSET_PANEL_H__
#define __ZOFFSET_PANEL_H__

#include "websocket_client.h"
#include "notify_consumer.h"
#include "selector.h"
#include "lvgl/lvgl.h"

#include <mutex>

// First-layer Z-offset / baby-stepping aid. A focused live control reachable
// from the Tune tab and by tapping the Z-offset value on the print-status
// screen. Adjusts the live gcode offset (SET_GCODE_OFFSET Z_ADJUST ... MOVE=1),
// same as FineTune's Z column, but with a big readout, clear closer/farther
// direction, and fine first-layer steps.
//
// Persistence is handled by the Creality Helper Script's Save Z-Offset macros,
// which override SET_GCODE_OFFSET to mirror every adjustment into variables.cfg
// and reload it on boot. So there is no Save button or SAVE_CONFIG/restart: a
// baby-step is saved the instant it is applied, mid-print or idle.
class ZOffsetPanel : public NotifyConsumer {
 public:
  ZOffsetPanel(KWebSocketClient &ws, std::mutex &lock);
  ~ZOffsetPanel();

  void foreground();
  void consume(json &j);
  void handle_callback(lv_event_t *e);
  static void _handle_callback(lv_event_t *e) {
    ((ZOffsetPanel *)e->user_data)->handle_callback(e);
  }

 private:
  double cur_offset();
  void update_value(double z);
  void apply_step(bool raise); // raise = nozzle up / farther from bed

  KWebSocketClient &ws;
  lv_obj_t *panel_cont;
  lv_obj_t *value_label;       // big live readout
  lv_obj_t *up_btn;            // raise — farther from bed / less squish
  lv_obj_t *down_btn;          // lower — closer to bed / more squish
  lv_obj_t *reset_btn;
  lv_obj_t *auto_save_hint;    // "Adjustments are saved automatically"
  lv_obj_t *back_btn;
  Selector step_selector;
};

#endif  // __ZOFFSET_PANEL_H__
