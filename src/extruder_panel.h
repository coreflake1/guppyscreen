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

  enum PendingKind { PA_NONE = 0, PA_EXTRUDE, PA_RETRACT, PA_LOAD, PA_UNLOAD };

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

  // Pending action waiting for the hotend to heat. Any of extrude/retract/
  // load/unload can be queued: pending_gcode is the exact command to run once
  // we're hot enough. Cleared on fire, cooldown, back, dtor, or heat timeout.
  PendingKind pending_kind = PA_NONE;
  std::string pending_gcode;
  int pending_want = 0;

  // Human-readable name of the action currently heating or running (matches the
  // button the user pressed: "Extrude", "Load", ...). Drives the busy caption.
  std::string action_name;

  // True between sending an action gcode and receiving its JSON-RPC response
  // (Moonraker only replies when the script has finished executing).
  bool action_in_flight = false;

  // Chunked, stoppable filament load. Instead of delegating to the stock
  // LOAD_MATERIAL macro (one uninterruptible G1 E150 move that can't be
  // cancelled once queued), guppyscreen feeds the filament in small bounded
  // chunks it issues itself, so Cooldown can halt it between chunks (<~2s).
  // load_active is true while chunks are being fed; load_stop is set by
  // Cooldown/Back to end the load after the in-flight chunk completes.
  bool load_active = false;
  bool load_stop = false;
  int load_remaining_mm = 0;

  // One timer at a time, used for whichever wait we're currently in:
  //   pending_kind != PA_NONE  -> heat timeout (clears pending if temp stalls)
  //   action_in_flight          -> response timeout (fallback unlock if ws dies)
  lv_timer_t *safety_timer = NULL;

  // Floating spinner shown over the panel while we're heating or executing a
  // gcode script, so the user sees something is still happening (#48).
  lv_obj_t *busy_spinner = NULL;

  // Caption under the spinner describing the current phase ("Heating 25->220C,
  // Extrude when ready" / "Extrude in progress..."), so a heat-then-fire action
  // never looks like nothing happened.
  lv_obj_t *busy_label = NULL;

  void send_action(const std::string &gcode);
  void on_action_response();
  // Heat to effective_temp() if needed, then run gcode; if the hotend is cold,
  // queue it and fire from consume() once we reach temperature.
  void run_when_hot(PendingKind kind, const std::string &name, const std::string &gcode);
  void fire_pending();
  void clear_pending();
  // Chunked filament load: feed LOAD_TOTAL_MM in LOAD_CHUNK_MM steps, each its
  // own gcode_script, stopping between chunks if load_stop is set.
  void begin_load();
  void send_load_chunk();
  void finish_load();
  void refresh_button_state();
  void update_busy_caption();
  void arm_safety_timer(uint32_t ms);
  void cancel_safety_timer();

#ifdef SIMULATOR
 public:
  // Foreground the panel with the busy spinner showing so the #48 spinner
  // change can be visually verified without driving the websocket.
  void sim_show_busy();
#endif
};

#endif // __EXTRUDER_PANEL_H__
