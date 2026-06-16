#ifndef __INPUTSHAPER_PANEL_H__
#define __INPUTSHAPER_PANEL_H__

#include "websocket_client.h"
#include "button_container.h"
#include "lvgl/lvgl.h"

#include <vector>
#include <mutex>

class InputShaperPanel {
 public:
  InputShaperPanel(KWebSocketClient &c, std::mutex &l);
  ~InputShaperPanel();

  void foreground();
  void handle_callback(lv_event_t *event);
  void handle_image_clicked(lv_event_t *event);
  void handle_macro_response(json &j);
  void handle_update_slider(lv_event_t *event);
  
  static void _handle_callback(lv_event_t *event) {
    InputShaperPanel *panel = (InputShaperPanel*)event->user_data;
    panel->handle_callback(event);
  };

  static void _handle_image_clicked(lv_event_t *event) {
    InputShaperPanel *panel = (InputShaperPanel*)event->user_data;
    panel->handle_image_clicked(event);
  };

  static void _handle_update_slider(lv_event_t *event) {
    InputShaperPanel *panel = (InputShaperPanel*)event->user_data;
    panel->handle_update_slider(event);
  };

  uint32_t find_shaper_index(const std::vector<std::string> &s,
			     const std::string &shaper);

  void set_shaper_detail(json &res,
			 lv_obj_t *label,
			 lv_obj_t *slider,
			 lv_obj_t *slider_label,
			 lv_obj_t *dd);

  // re-enable Calibrate/Save, hide spinners, clear the watchdog timer
  void end_calibration_ui();
  static void _watchdog_cb(lv_timer_t *t) {
    InputShaperPanel *panel = (InputShaperPanel*)t->user_data;
    panel->handle_watchdog();
  };
  void handle_watchdog();
  // kick off TEST_RESONANCES for one axis (gcode + spinner/graph reset)
  void start_axis(bool is_x);
  // lock controls + home (if needed) + watchdog + run one axis
  void begin_axis(bool is_x);
  // confirm accelerometer placement before an axis; runs it on Continue.
  // moving=true is the "Y done, relocate for X" wording.
  void show_placement_prompt(bool is_x, bool moving);
  static void _placement_prompt_cb(lv_event_t *e);
  // assign the top result cells for the run: graph + single axis -> that axis's
  // graph on the left (col1) + its text console on the right (col2); otherwise
  // axis-home columns (X col1, Y col2, graphs only when both selected).
  void layout_panes(bool graph, bool x_sel, bool y_sel);

 private:
  KWebSocketClient &ws;
  std::mutex &lv_lock;
  lv_obj_t *cont;

  // xgraph
  lv_obj_t *xgraph_cont;
  lv_obj_t *xgraph;
  lv_obj_t *xoutput; // calibrate shaper output x
  lv_obj_t *xspinner;

  // y graph
  lv_obj_t *ygraph_cont;
  lv_obj_t *ygraph;
  lv_obj_t *youtput; // calibrate shaper output y
  lv_obj_t *yspinner;

  // x controls
  lv_obj_t *xcontrol;
  lv_obj_t *xaxis_label;
  lv_obj_t *x_switch;
  lv_obj_t *xslider_cont;
  lv_obj_t *xslider;
  lv_obj_t *xlabel;
  lv_obj_t *xshaper_dd;

  // y controls
  lv_obj_t *ycontrol;
  lv_obj_t *yaxis_label;
  lv_obj_t *y_switch;
  lv_obj_t *yslider_cont;
  lv_obj_t *yslider;
  lv_obj_t *ylabel;
  lv_obj_t *yshaper_dd;

  lv_obj_t *button_cont;
  lv_obj_t *switch_cont;
  lv_obj_t *graph_switch_label;
  lv_obj_t *graph_switch;
  ButtonContainer calibrate_btn;
  ButtonContainer save_btn;
  ButtonContainer emergency_btn;
  ButtonContainer back_btn;
  bool ximage_fullsized;
  bool yimage_fullsized;
  json calibrate_output;

  // calibration progress tracking (for feedback + button locking)
  bool x_pending;
  bool y_pending;
  bool y_after_move;   // both axes selected: Y is deferred until the sensor is moved (X runs first)
  bool next_axis_is_x; // which axis the active placement prompt will start on Continue
  lv_timer_t *cal_watchdog;

  static std::vector<std::string> shapers;
  
};

#endif // __INPUTSHAPER_PANEL_H__
