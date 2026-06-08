#ifndef __SKEW_CORRECTION_PANEL_H__
#define __SKEW_CORRECTION_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"

// Skew correction helper, reached from the Tune tab. Software-compensates a
// frame that isn't perfectly square (parts come out as faint parallelograms).
// The user prints a skew calibration object, measures the three XY diagonals
// (A-C, B-D, A-D) with calipers, types them in, and Apply & Save issues
//   SET_SKEW XY=<ac>,<bd>,<ad>
// followed by SAVE_CONFIG (persists to [skew_correction], restarts Klipper).
// Detection is via the config section (matches the Axis Twist pattern); the
// Tune panel shows a toast instead of opening this if it isn't enabled.
class SkewCorrectionPanel {
 public:
  SkewCorrectionPanel(KWebSocketClient &ws);
  ~SkewCorrectionPanel();

  static bool is_enabled();   // [skew_correction] present in printer.cfg

  void foreground();

  void handle_callback(lv_event_t *event);
  static void _handle_callback(lv_event_t *e) {
    ((SkewCorrectionPanel*)e->user_data)->handle_callback(e);
  }
  void handle_kb(lv_event_t *event);
  static void _handle_kb(lv_event_t *e) {
    ((SkewCorrectionPanel*)e->user_data)->handle_kb(e);
  }
  void handle_ta(lv_event_t *event);
  static void _handle_ta(lv_event_t *e) {
    ((SkewCorrectionPanel*)e->user_data)->handle_ta(e);
  }

 private:
  void apply_and_save();

  KWebSocketClient &ws;

  lv_obj_t *panel_cont;
  lv_obj_t *ta[3];        // A-C, B-D, A-D measurement inputs (mm)
  lv_obj_t *kb;           // decimal numeric keyboard (hidden until a field is tapped)
  lv_obj_t *apply_btn;
  lv_obj_t *back_btn;
};

#endif  // __SKEW_CORRECTION_PANEL_H__
