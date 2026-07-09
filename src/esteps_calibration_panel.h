#ifndef __ESTEPS_CALIBRATION_PANEL_H__
#define __ESTEPS_CALIBRATION_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"

#include <mutex>

// Guided E-Steps (extruder rotation_distance) calibration, structured like
// RecalibrationWizardPanel. The underlying gcode macros
// (k1/k1_mods/klipper_mods/esteps_calibration/esteps_calibration.cfg) do the
// actual heating/extruding/math; this panel drives its own UI from plain
// RESPOND markers instead of Klipper's action:prompt protocol. action:prompt
// is rendered globally and unconditionally by prompt_panel.cpp with no way
// for another panel to suppress it - reusing it here would double up the UI
// (which is exactly what happened before this panel existed).
//   INTRO      - explanation + "Start"
//   HEATING    - live temp while M109 runs, real Cancel
//   MARK       - "wrap tape at Xmm", Extrude button, real Cancel
//   EXTRUDING  - live progress, Cancel (takes effect once the current
//                already-queued chunk sequence finishes - Klipper runs gcode
//                serially, nothing can interrupt a macro mid-flight short of
//                an emergency stop, which is too drastic for this)
//   MEASURE    - numeric keypad entry for the measured remaining gap
//   RESULT     - old -> new rotation_distance, Verify Again / Discard / Done
// Discard on the RESULT screen restores the rotation_distance from before
// this whole run started (captured once from the first apply response's OLD=
// value, survives repeated Verify Again loops) via the existing
// SET_EXTRUDER_ROTATION_DISTANCE macro - no new gcode needed, since that
// macro already persists via SAVE_VARIABLE the same way the calibration
// itself does.
class EstepsCalibrationPanel : public NotifyConsumer {
 public:
  EstepsCalibrationPanel(KWebSocketClient &, std::mutex &);
  ~EstepsCalibrationPanel();

  void foreground();
  void consume(json &j);                 // live extruder temp during HEATING
  void handle_gcode_response(json &j);   // ESTEPS_* markers

  void handle_callback(lv_event_t *event);
  static void _handle_callback(lv_event_t *e) { ((EstepsCalibrationPanel*)e->user_data)->handle_callback(e); }

  void handle_kb(lv_event_t *event);
  static void _handle_kb(lv_event_t *e) { ((EstepsCalibrationPanel*)e->user_data)->handle_kb(e); }
  void handle_ta(lv_event_t *event);
  static void _handle_ta(lv_event_t *e) { ((EstepsCalibrationPanel*)e->user_data)->handle_ta(e); }

 private:
  enum Stage { INTRO, HEATING, MARK, EXTRUDING, MEASURE, RESULT };

  void show_stage(Stage s);
  void start_calibration();
  void update_heating_status();
  void apply_measurement();
  void cancel_active_run();

  KWebSocketClient &ws;

  lv_obj_t *panel_cont;
  lv_obj_t *intro_cont;
  lv_obj_t *heating_cont;
  lv_obj_t *mark_cont;
  lv_obj_t *extruding_cont;
  lv_obj_t *measure_cont;
  lv_obj_t *result_cont;

  // intro
  lv_obj_t *start_btn;
  lv_obj_t *intro_back_btn;
  // heating
  lv_obj_t *heating_status_label;
  lv_obj_t *heating_cancel_btn;
  // mark
  lv_obj_t *mark_instr_label;
  lv_obj_t *extrude_btn;
  lv_obj_t *mark_cancel_btn;
  // extruding
  lv_obj_t *extruding_status_label;
  lv_obj_t *extruding_cancel_btn;
  // measure
  lv_obj_t *measure_instr_label;
  lv_obj_t *measure_ta;
  lv_obj_t *measure_apply_btn;
  lv_obj_t *measure_cancel_btn;
  lv_obj_t *kb;
  // result
  lv_obj_t *result_label;
  lv_obj_t *verify_again_btn;
  lv_obj_t *discard_btn;
  lv_obj_t *done_btn;

  Stage stage;
  bool active;              // guards stray gcode responses when this panel isn't driving anything
  double mark_mm;
  double length_mm;
  double speed_mm_min;
  double extruder_temp;
  bool have_extruder_temp;
  double target_temp;
  bool have_target_temp;
  double old_rotation_distance;
  double new_rotation_distance;
  double commanded_mm;
  double actual_mm;
  bool have_original_rd;    // captured once per run, survives Verify Again loops
  double original_rotation_distance;
};

#endif  // __ESTEPS_CALIBRATION_PANEL_H__
