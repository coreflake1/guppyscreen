#ifndef __EXTRUDER_PANEL_H__
#define __EXTRUDER_PANEL_H__

#include "websocket_client.h"
#include "notify_consumer.h"
#include "spoolman_panel.h"
#include "selector.h"
#include "button_container.h"
#include "sensor_container.h"
#include "numpad.h"
#include "lvgl/lvgl.h"

class ExtruderPanel : public NotifyConsumer {
 public:
  ExtruderPanel(KWebSocketClient &ws, std::mutex &l, Numpad &np, SpoolmanPanel &sm);
  ~ExtruderPanel();

  void foreground();
  void enable_spoolman();
  void consume(json &j);
  void handle_callback(lv_event_t *e);

  // Highest of the selected temp and the current target, so a manually-set
  // hotter temperature is never lowered by an extrude/load/unload action.
  int effective_temp();

  enum PendingKind { PA_NONE = 0, PA_EXTRUDE, PA_RETRACT };

  static void _handle_callback(lv_event_t *event) {
    ExtruderPanel *panel = (ExtruderPanel*)event->user_data;
    panel->handle_callback(event);
  };

 private:
  KWebSocketClient &ws;
  lv_obj_t *panel_cont;
  SpoolmanPanel &spoolman_panel;
  SensorContainer extruder_temp;
  Selector temp_selector;
  Selector length_selector;
  Selector speed_selector;
  lv_obj_t *rightside_btns_cont;
  lv_obj_t *leftside_btns_cont;
  ButtonContainer load_btn;
  ButtonContainer unload_btn;
  ButtonContainer cooldown_btn;
  ButtonContainer spoolman_btn;
  ButtonContainer extrude_btn;
  ButtonContainer retract_btn;
  ButtonContainer back_btn;
  std::string load_filament_macro;
  std::string unload_filament_macro;
  std::string cooldown_macro;

  // Live extruder telemetry, updated from consume().
  int current_temp = 0;
  int current_target = 0;

  // Pending Extrude/Retract waiting for the hotend to heat. Cleared on fire,
  // cooldown, back, dtor, or heat timeout.
  PendingKind pending_kind = PA_NONE;
  std::string pending_len;
  std::string pending_speed;
  int pending_want = 0;

  // True between sending an action gcode and receiving its JSON-RPC response
  // (Moonraker only replies when the script has finished executing).
  bool action_in_flight = false;

  // One timer at a time, used for whichever wait we're currently in:
  //   pending_kind != PA_NONE  -> heat timeout (clears pending if temp stalls)
  //   action_in_flight          -> response timeout (fallback unlock if ws dies)
  lv_timer_t *safety_timer = NULL;

  // Floating spinner shown over the panel while we're heating or executing a
  // gcode script, so the user sees something is still happening (#48).
  lv_obj_t *busy_spinner = NULL;

  void send_action(const std::string &gcode);
  void on_action_response();
  void start_extrude_action(PendingKind kind, const char *len, const char *speed);
  void fire_pending();
  void clear_pending();
  void refresh_button_state();
  void arm_safety_timer(uint32_t ms);
  void cancel_safety_timer();
};

#endif // __EXTRUDER_PANEL_H__
