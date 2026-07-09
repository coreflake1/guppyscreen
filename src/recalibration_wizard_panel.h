#ifndef __RECALIBRATION_WIZARD_PANEL_H__
#define __RECALIBRATION_WIZARD_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"

#include <mutex>

// Guided "just changed hardware" recalibration wizard: re-establishes a safe
// baseline after a bed swap, nozzle change, or BLTouch remount, using only
// primitives with a real hardware stop (BLTouch pin) as the mandatory path -
// the strain/pressure probe (self-saves unconditionally, no bounded worst-
// case travel, see project_prtouch_mechanism_research memory) is available
// only as an optional post-baseline refinement, never as the starting point.
//   INTRO          - explanation + "Start"
//   ZOFFSET        - PROBE_CALIBRATE, manual_probe-driven jog + Accept/Abort
//                    (same underlying Klipper helper as Axis Twist's probing)
//   MESH           - automatic BED_MESH_CALIBRATE, no input needed
//   DONE           - review both new values; Save & Restart, Discard, or
//                    optionally "Refine with Load Sensor"
//   SENSOR_RUNNING - single Z_OFFSET_CALIBRATION reading from the now-trusted
//                    baseline. Originally averaged 3 readings, but that was
//                    dropped (2026-07-08): Z_OFFSET_CALIBRATION intermittently
//                    triggers its own MCU reset + Klipper restart as part of
//                    its self-save, and a 2nd/3rd reading sent right after a
//                    prior one's response can race that restart and get lost
//                    with no clean way to recover. A single reading sidesteps
//                    this entirely (nothing sent after the one response that
//                    could race anything), and the accuracy cost is small -
//                    averaging only would have tightened an already-small
//                    ~6.5um noise floor to ~3.7um, well under the BLTouch's
//                    own ~10um mechanical repeatability (see
//                    project_prtouch_mechanism_research Test D and the
//                    2026-07-08 race-condition writeup).
//   SENSOR_CHOICE  - shows paper vs. sensor reading side by side; the user
//                    always picks, never auto-applied (session 2026-07-08
//                    decision - a threshold only guides the recommendation)
// A single SAVE_CONFIG at the end covers z_offset and mesh together (the
// paper-only path). The sensor-refine path instead patches the on-disk
// z_offset line directly and does FIRMWARE_RESTART, since the sensor macro
// self-saves along the way and Klipper's live in-memory offset can't be
// reset to an arbitrary value without either a full config reload or
// replaying the interactive probe flow.
class RecalibrationWizardPanel : public NotifyConsumer {
 public:
  RecalibrationWizardPanel(KWebSocketClient &, std::mutex &);
  ~RecalibrationWizardPanel();

  void foreground();
  void consume(json &j);                 // manual_probe status updates
  void handle_gcode_response(json &j);   // PROBE_CALIBRATE / BED_MESH_CALIBRATE output

  void handle_callback(lv_event_t *event);
  static void _handle_callback(lv_event_t *e) { ((RecalibrationWizardPanel*)e->user_data)->handle_callback(e); }

 private:
  enum Stage { INTRO, ZOFFSET, MESH, DONE, SENSOR_RUNNING, SENSOR_CHOICE };

  struct Jog { const char *label; lv_obj_t *btn; };

  void show_stage(Stage s);
  void start_wizard();
  void start_mesh();
  void update_probe_ui();

  void start_sensor_refine();
  void finish_sensor_reading();
  void apply_sensor_choice(bool use_sensor);
  void apply_discard_all();
  void arm_sensor_timeout();
  void disarm_sensor_timeout();
  static void sensor_timeout_cb(lv_timer_t *t);
  void sensor_refine_failed(const char *reason);

  bool backup_printer_cfg();
  bool restore_printer_cfg_backup();
  bool patch_z_offset_value(double value);

  KWebSocketClient &ws;

  lv_obj_t *panel_cont;
  lv_obj_t *intro_cont;
  lv_obj_t *probe_cont;
  lv_obj_t *mesh_cont;
  lv_obj_t *done_cont;
  lv_obj_t *sensor_cont;
  lv_obj_t *sensor_choice_cont;

  // intro
  lv_obj_t *start_btn;
  lv_obj_t *intro_back_btn;
  // probe (z-offset)
  lv_obj_t *z_label;
  lv_obj_t *instr_label;
  lv_obj_t *accept_btn;
  lv_obj_t *abort_btn;
  Jog jogs[8];
  // mesh
  lv_obj_t *mesh_status_label;
  // done
  lv_obj_t *done_summary_label;
  lv_obj_t *save_btn;
  lv_obj_t *done_back_btn;
  lv_obj_t *sensor_refine_btn;
  // sensor running
  lv_obj_t *sensor_status_label;
  // sensor choice
  lv_obj_t *sensor_choice_label;
  lv_obj_t *keep_paper_btn;
  lv_obj_t *use_sensor_btn;
  lv_obj_t *discard_all_btn;

  Stage stage;
  bool active;          // wizard running (guards stray notify_gcode_response/consume)
  bool probe_active;    // manual_probe.is_active
  double last_z;
  bool have_z;
  double old_z_offset;
  double new_z_offset;
  bool have_new_z_offset;

  double sensor_reading;
  bool have_sensor_reading;
  bool sensor_step_active;   // guards sensor-specific response parsing
  bool config_backed_up;     // whether backup_printer_cfg() succeeded this run
  lv_timer_t *sensor_timeout_timer;

  double sensor_extruder_temp;
  double sensor_bed_temp;
  bool have_sensor_temps;
  void update_sensor_status_text();
};

#endif  // __RECALIBRATION_WIZARD_PANEL_H__
