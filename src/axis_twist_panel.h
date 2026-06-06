#ifndef __AXIS_TWIST_PANEL_H__
#define __AXIS_TWIST_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"

#include <mutex>

// Guided calibration wizard for Klipper's [axis_twist_compensation], reached
// from the Tune tab. Three stages share one container:
//   INTRO   - explanation + "Start Calibration"
//   PROBING - driven by the manual_probe object: live Z, jog steps, Accept/Abort
//   DONE    - shows the resulting offsets + "Save & Restart" (SAVE_CONFIG)
// Detection is via the [axis_twist_compensation] CONFIG section (the module has
// no get_status, so it never appears as a live printer object). Completion and
// the offset values are read from the AXIS_TWIST_COMPENSATION_CALIBRATE gcode
// response; the interactive jog UI is driven by the manual_probe status object.
class AxisTwistPanel : public NotifyConsumer {
 public:
  AxisTwistPanel(KWebSocketClient &, std::mutex &);
  ~AxisTwistPanel();

  // True if [axis_twist_compensation] is configured in Klipper. The Tune panel
  // uses this to show a toast instead of opening an inert wizard.
  static bool is_enabled();

  void foreground();
  void consume(json &j);                 // manual_probe status updates
  void handle_gcode_response(json &j);   // AXIS_TWIST_COMPENSATION_CALIBRATE output

  void handle_callback(lv_event_t *event);
  static void _handle_callback(lv_event_t *e) { ((AxisTwistPanel*)e->user_data)->handle_callback(e); }

 private:
  enum Stage { INTRO, PROBING, DONE };

  struct Jog { const char *label; lv_obj_t *btn; };

  void show_stage(Stage s);
  void start_calibration();
  void update_probe_ui();   // reflect last_active / last_z into the probe view

  KWebSocketClient &ws;

  lv_obj_t *panel_cont;
  lv_obj_t *intro_cont;
  lv_obj_t *probe_cont;
  lv_obj_t *done_cont;
  lv_obj_t *empty_cont;

  // intro
  lv_obj_t *start_btn;
  lv_obj_t *intro_back_btn;
  // probe
  lv_obj_t *point_label;
  lv_obj_t *z_label;
  lv_obj_t *instr_label;
  lv_obj_t *accept_btn;
  lv_obj_t *abort_btn;
  Jog jogs[8];
  // done
  lv_obj_t *done_offsets_label;
  lv_obj_t *save_btn;
  lv_obj_t *done_back_btn;
  // empty
  lv_obj_t *empty_back_btn;

  Stage stage;
  bool calibrating;
  bool last_active;     // manual_probe.is_active (tracked per-delta)
  double last_z;        // manual_probe.z_position
  bool have_z;
  int accepted;         // ACCEPTs issued so far
  int total;            // SAMPLE_COUNT
};

#endif  // __AXIS_TWIST_PANEL_H__
