#include "extruder_panel.h"
#include "state.h"
#include "config.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <limits>

// How close (in C) the hotend must be to the target before we fire a pending
// extrude/retract. Klipper still enforces its own min_extrude_temp.
static const int HEAT_TOLERANCE = 5;
// Max time to wait for the hotend to reach the requested temperature before
// giving up and clearing the pending action.
static const uint32_t HEAT_TIMEOUT_MS = 5 * 60 * 1000;
// Safety cap on how long we keep buttons locked while waiting for a gcode
// response. Long enough for a 50mm slow load; trips only if the ws dies.
static const uint32_t ACTION_TIMEOUT_MS = 5 * 60 * 1000;

LV_IMG_DECLARE(back);
LV_IMG_DECLARE(spoolman_img);
LV_IMG_DECLARE(extrude_img);
LV_IMG_DECLARE(retract_img);
LV_IMG_DECLARE(unload_filament_img);
LV_IMG_DECLARE(load_filament_img);
LV_IMG_DECLARE(extruder);
LV_IMG_DECLARE(cooldown_img);

ExtruderPanel::ExtruderPanel(KWebSocketClient &websocket_client,
  std::mutex &lock,
  Numpad &numpad,
  SpoolmanPanel &sm)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , panel_cont(lv_obj_create(lv_scr_act()))
  , spoolman_panel(sm)
  , extruder_temp(ws, panel_cont, &extruder, 150,
    "Extruder", lv_palette_main(LV_PALETTE_RED), false, true, numpad, "extruder", NULL, NULL)
  , temp_selector(panel_cont, "Extruder Temperature (C)",
    {"180", "200", "220", "240", "260", "280", "300", ""}, 3, &ExtruderPanel::_handle_callback, this)
  , length_selector(panel_cont, "Extrude Length (mm)",
    {"5", "10", "15", "20", "25", "30", "35", ""}, 1, &ExtruderPanel::_handle_callback, this)
  , speed_selector(panel_cont, "Extrude Speed (mm/s)",
    {"1", "2", "5", "10", "25", "35", "50", ""}, 2, &ExtruderPanel::_handle_callback, this)
  , rightside_btns_cont(lv_obj_create(panel_cont))
  , leftside_btns_cont(lv_obj_create(panel_cont))
  , load_btn(leftside_btns_cont, &load_filament_img, "Load", &ExtruderPanel::_handle_callback, this)
  , unload_btn(leftside_btns_cont, &unload_filament_img, "Unload", &ExtruderPanel::_handle_callback, this)
  , cooldown_btn(leftside_btns_cont, &cooldown_img, "Cooldown", &ExtruderPanel::_handle_callback, this)
  , spoolman_btn(rightside_btns_cont, &spoolman_img, "Spoolman", &ExtruderPanel::_handle_callback, this)
  , extrude_btn(rightside_btns_cont, &extrude_img, "Extrude", &ExtruderPanel::_handle_callback, this)
  , retract_btn(rightside_btns_cont, &retract_img, "Retract", &ExtruderPanel::_handle_callback, this)
  , back_btn(rightside_btns_cont, &back, "Back", &ExtruderPanel::_handle_callback, this)
  , load_filament_macro("LOAD_FILAMENT")
  , unload_filament_macro("UNLOAD_FILAMENT")
  , cooldown_macro("SET_HEATER_TEMPERATURE HEATER=extruder TARGET=0")
{
  Config *conf = Config::get_instance();
  auto df = conf->get_json("/default_printer");
  if (!df.empty()) {
    auto v = conf->get_json(conf->df() + "default_macros/load_filament");
    if (!v.is_null()) {
      load_filament_macro = v.template get<std::string>();
    }

    v = conf->get_json(conf->df() + "default_macros/unload_filament");
    if (!v.is_null()) {
      unload_filament_macro = v.template get<std::string>();
    }

    v = conf->get_json(conf->df() + "default_macros/cooldown");
    if (!v.is_null()) {
      cooldown_macro = v.template get<std::string>();
    }
  }

  lv_obj_move_background(panel_cont);
  lv_obj_clear_flag(panel_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(panel_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(panel_cont, 0, 0);

  lv_obj_set_size(rightside_btns_cont, LV_PCT(20), LV_PCT(100));
  lv_obj_set_flex_flow(rightside_btns_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(rightside_btns_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(rightside_btns_cont, LV_OBJ_FLAG_SCROLLABLE);
  /* Zero padding so the inner buttons (LV_PCT(100)) actually fill the column. */
  lv_obj_set_style_pad_all(rightside_btns_cont, 0, 0);

  lv_obj_set_size(leftside_btns_cont, LV_PCT(20), LV_PCT(100));
  // lv_obj_set_style_pad_row(leftside_btns_cont, 15, 0);
  lv_obj_set_flex_flow(leftside_btns_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(leftside_btns_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(leftside_btns_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(leftside_btns_cont, 0, 0);

  lv_obj_set_flex_grow(load_btn.get_container(), 1);
  lv_obj_set_flex_grow(unload_btn.get_container(), 1);
  lv_obj_set_flex_grow(cooldown_btn.get_container(), 1);
  lv_obj_set_flex_grow(spoolman_btn.get_container(), 1);
  lv_obj_set_flex_grow(extrude_btn.get_container(), 1);
  lv_obj_set_flex_grow(retract_btn.get_container(), 1);
  lv_obj_set_flex_grow(back_btn.get_container(), 1);

  /* Stretch each button to fill its flex column. ButtonContainer's default
   * 110*width_scale (~66 px on 480-wide) is too narrow for 7-8 char labels
   * like "Spoolman" / "Cooldown" / "Extrude" — they truncate or wrap. */
  lv_obj_t *btns[] = {
    load_btn.get_container(), unload_btn.get_container(), cooldown_btn.get_container(),
    spoolman_btn.get_container(), extrude_btn.get_container(),
    retract_btn.get_container(), back_btn.get_container()
  };
  for (auto *b : btns) {
    lv_obj_set_width(b, lv_pct(100));
  }
  /* Note: ButtonContainer applies the small-screen m10 font globally. */

  spoolman_btn.disable();

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(3), LV_GRID_FR(6), LV_GRID_FR(6), LV_GRID_FR(6),
    LV_GRID_TEMPLATE_LAST};
  /* FR(2)/FR(8)/FR(2): side cols ~80 px (still room for 8-char labels with
   * pad_all=0 on the side containers) and middle ~320 px so each of the 7
   * selector pills gets ~45 px — a usable touch target on the KE display. */
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(2), LV_GRID_FR(8), LV_GRID_FR(2), LV_GRID_TEMPLATE_LAST};

  lv_obj_clear_flag(panel_cont, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_grid_dsc_array(panel_cont, grid_main_col_dsc, grid_main_row_dsc);
  lv_obj_add_flag(extruder_temp.get_sensor(), LV_OBJ_FLAG_FLOATING);
  lv_obj_align(extruder_temp.get_sensor(), LV_ALIGN_TOP_LEFT, 20, 0);

  // lv_obj_set_size(extruder_temp.get_sensor(), 350, 60);
  // col 0
  // lv_obj_set_grid_cell(spoolman_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_START, 0, 2);
  // lv_obj_set_grid_cell(load_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_END, 0, 2);
  // lv_obj_set_grid_cell(unload_btn.get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_START, 2, 2);
  // lv_obj_set_grid_cell(cooldown_btn.get_container(), LV_GRID_ALIGN_END, 0, 1, LV_GRID_ALIGN_END, 2, 2);

  lv_obj_set_grid_cell(leftside_btns_cont, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 3);

  // col 1
  // lv_obj_set_grid_cell(extruder_temp.get_sensor(), LV_GRID_ALIGN_CENTER, 0, 2, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(speed_selector.get_container(), LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(length_selector.get_container(), LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
  lv_obj_set_grid_cell(temp_selector.get_container(), LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 3, 1);

  // col 2
  // lv_obj_set_grid_cell(spoolman_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 0, 2);
  // lv_obj_set_grid_cell(retract_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_END, 0, 2);
  // lv_obj_set_grid_cell(extrude_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 2, 2);
  // lv_obj_set_grid_cell(back_btn.get_container(), LV_GRID_ALIGN_END, 2, 1, LV_GRID_ALIGN_END, 2, 2);

  lv_obj_set_grid_cell(rightside_btns_cont, LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 0, 4);
  // lv_obj_set_grid_cell(retract_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_END, 0, 2);
  // lv_obj_set_grid_cell(extrude_btn.get_container(), LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_START, 2, 2);
  // lv_obj_set_grid_cell(back_btn.get_container(), LV_GRID_ALIGN_END, 2, 1, LV_GRID_ALIGN_END, 2, 2);


  // Busy spinner: floating overlay centered on the panel, big enough to be
  // unmissable but not so big it covers the selectors behind it. Hidden until
  // refresh_button_state() shows it.
  busy_spinner = lv_spinner_create(panel_cont, 1000, 60);
  lv_obj_add_flag(busy_spinner, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_size(busy_spinner, 80, 80);
  lv_obj_align(busy_spinner, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_flag(busy_spinner, LV_OBJ_FLAG_HIDDEN);

  ws.register_notify_update(this);

  auto def_ext_temp_v = conf->get_json("/default_extruder_temp");
  if (!def_ext_temp_v.is_null()) {
    int def_temp = def_ext_temp_v.template get<int>();
    int temps[] = {180, 200, 220, 240, 260, 280, 300};
    for (int i = 0; i < 7; i++) {
      if (temps[i] == def_temp) {
        temp_selector.set_selected_idx(i);
        break;
      }
    }
  }
}

ExtruderPanel::~ExtruderPanel() {
  cancel_safety_timer();
  if (panel_cont != NULL) {
    lv_obj_del(panel_cont);
    panel_cont = NULL;
  }
}

void ExtruderPanel::foreground() {
  lv_obj_move_foreground(panel_cont);
}

void ExtruderPanel::enable_spoolman() {
  spoolman_btn.enable();
}

void ExtruderPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  auto target_value = j["/params/0/extruder/target"_json_pointer];
  if (!target_value.is_null()) {
    current_target = target_value.template get<int>();
    extruder_temp.update_target(current_target);
  }

  auto temp_value = j["/params/0/extruder/temperature"_json_pointer];
  if (!temp_value.is_null()) {
    current_temp = temp_value.template get<int>();
    extruder_temp.update_value(current_temp);
  }

  if (pending_kind != PA_NONE && current_temp + HEAT_TOLERANCE >= pending_want) {
    fire_pending();
  }
}

int ExtruderPanel::effective_temp() {
  int sel = 0;
  try {
    sel = std::stoi(lv_btnmatrix_get_btn_text(temp_selector.get_selector(),
      temp_selector.get_selected_idx()));
  } catch (const std::exception &) {
    sel = 0;
  }
  return std::max(sel, current_target);
}

void ExtruderPanel::refresh_button_state() {
  bool busy = action_in_flight || pending_kind != PA_NONE;

  if (action_in_flight) {
    // A gcode is executing on klipper; lock everything until the response.
    extrude_btn.disable();
    retract_btn.disable();
    load_btn.disable();
    unload_btn.disable();
    cooldown_btn.disable();
  } else if (pending_kind != PA_NONE) {
    // Waiting for the hotend to heat. Cooldown stays clickable so the user
    // can cancel; the other actions are blocked until heat-up completes.
    extrude_btn.disable();
    retract_btn.disable();
    load_btn.disable();
    unload_btn.disable();
    cooldown_btn.enable();
  } else {
    extrude_btn.enable();
    retract_btn.enable();
    load_btn.enable();
    unload_btn.enable();
    cooldown_btn.enable();
  }

  if (busy_spinner != NULL) {
    if (busy) {
      lv_obj_clear_flag(busy_spinner, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(busy_spinner, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void ExtruderPanel::arm_safety_timer(uint32_t ms) {
  cancel_safety_timer();
  safety_timer = lv_timer_create([](lv_timer_t *t) {
    auto *self = (ExtruderPanel *)t->user_data;
    self->safety_timer = NULL;  // one-shot, LVGL deletes after this callback
    if (self->action_in_flight) {
      // Response never arrived — assume the ws/klipper lost it and recover.
      spdlog::warn("extruder action response timeout, unlocking buttons");
      self->action_in_flight = false;
      self->refresh_button_state();
    } else if (self->pending_kind != PA_NONE) {
      spdlog::warn("extruder heat-up timed out, clearing pending action");
      self->clear_pending();
      KUtils::notify_toast("Heating timed out.");
    }
  }, ms, this);
  lv_timer_set_repeat_count(safety_timer, 1);
}

void ExtruderPanel::cancel_safety_timer() {
  if (safety_timer != NULL) {
    lv_timer_del(safety_timer);
    safety_timer = NULL;
  }
}

void ExtruderPanel::send_action(const std::string &gcode) {
  action_in_flight = true;
  refresh_button_state();
  arm_safety_timer(ACTION_TIMEOUT_MS);
  // Moonraker only sends the JSON-RPC response for printer.gcode.script once
  // the script has finished executing — that's our reliable "done" signal.
  ws.gcode_script(gcode, [this](json &) {
    std::lock_guard<std::mutex> lock(lv_lock);
    on_action_response();
  });
}

void ExtruderPanel::on_action_response() {
  if (!action_in_flight) {
    return;  // safety timer already recovered us
  }
  action_in_flight = false;
  cancel_safety_timer();
  refresh_button_state();
}

void ExtruderPanel::clear_pending() {
  if (pending_kind == PA_NONE) {
    return;
  }
  pending_kind = PA_NONE;
  pending_len.clear();
  pending_speed.clear();
  pending_want = 0;
  cancel_safety_timer();
  refresh_button_state();
}

void ExtruderPanel::fire_pending() {
  PendingKind k = pending_kind;
  std::string len = pending_len;
  std::string speed = pending_speed;
  clear_pending();  // resets state + button enables; send_action re-locks them

  int feed = 300;  // 5 mm/s fallback if the speed text fails to parse
  try {
    feed = std::stoi(speed) * 60;
  } catch (const std::exception &) {}

  send_action(fmt::format("M83\nG1 E{}{} F{}",
    k == PA_RETRACT ? "-" : "", len, feed));
}

void ExtruderPanel::start_extrude_action(PendingKind kind, const char *len, const char *speed) {
  int want = effective_temp();

  // Non-blocking heat request. Avoids the M109 hang from #65 — we'll watch the
  // temp stream ourselves and fire the move once we're hot enough.
  if (want > current_target) {
    ws.gcode_script(fmt::format("M104 S{}", want));
  }

  if (current_temp + HEAT_TOLERANCE >= want) {
    int feed = 300;
    try {
      feed = std::stoi(speed) * 60;
    } catch (const std::exception &) {}
    send_action(fmt::format("M83\nG1 E{}{} F{}",
      kind == PA_RETRACT ? "-" : "", len, feed));
    return;
  }

  // Cold — queue it. consume() will fire it once we reach the threshold.
  pending_kind = kind;
  pending_len = len;
  pending_speed = speed;
  pending_want = want;
  refresh_button_state();
  arm_safety_timer(HEAT_TIMEOUT_MS);
  KUtils::notify_toast(fmt::format("Heating to {}C, {} when ready.",
    want, kind == PA_RETRACT ? "retracting" : "extruding"));
}

void ExtruderPanel::handle_callback(lv_event_t *e) {
  spdlog::trace("handling extruder panel callback");
  if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t *selector = lv_event_get_target(e);
    uint32_t idx = lv_btnmatrix_get_selected_btn(selector);
    const char *v = lv_btnmatrix_get_btn_text(selector, idx);

    if (selector == temp_selector.get_selector()) {
      temp_selector.set_selected_idx(idx);
    }

    if (selector == length_selector.get_selector()) {
      length_selector.set_selected_idx(idx);
    }

    if (selector == speed_selector.get_selector()) {
      speed_selector.set_selected_idx(idx);
    }

    spdlog::trace("selector {} {} {}, {} {} {}", fmt::ptr(selector), idx, v,
      fmt::ptr(temp_selector.get_selector()),
      fmt::ptr(length_selector.get_selector()),
      fmt::ptr(speed_selector.get_selector()));

  } else if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(e);

    if (btn == back_btn.get_container()) {
      clear_pending();  // don't auto-extrude after the user navigates away
      lv_obj_move_background(panel_cont);
      return;
    }

    if (btn == spoolman_btn.get_container()) {
      spoolman_panel.foreground();
      return;
    }

    if (btn == cooldown_btn.get_container()) {
      // Cooldown doubles as the cancel for a pending heat-up. Always allowed
      // (unless a gcode is already in flight, in which case refresh disables it).
      if (action_in_flight) {
        return;
      }
      clear_pending();
      send_action(cooldown_macro);
      return;
    }

    // Remaining actions: blocked while another action is running OR while a
    // heat-up is pending. refresh_button_state() also disables their widgets,
    // but check the flags too in case of an event in flight before refresh.
    if (action_in_flight || pending_kind != PA_NONE) {
      return;
    }

    if (btn == extrude_btn.get_container()) {
      const char *len = lv_btnmatrix_get_btn_text(length_selector.get_selector(),
        length_selector.get_selected_idx());
      const char *speed = lv_btnmatrix_get_btn_text(speed_selector.get_selector(),
        speed_selector.get_selected_idx());
      start_extrude_action(PA_EXTRUDE, len, speed);
      return;
    }

    if (btn == retract_btn.get_container()) {
      const char *len = lv_btnmatrix_get_btn_text(length_selector.get_selector(),
        length_selector.get_selected_idx());
      const char *speed = lv_btnmatrix_get_btn_text(speed_selector.get_selector(),
        speed_selector.get_selected_idx());
      start_extrude_action(PA_RETRACT, len, speed);
      return;
    }

    if (btn == unload_btn.get_container()) {
      // Klipper's _GUPPY_QUIT_MATERIAL macro handles its own heating + wait;
      // a plain UNLOAD_FILAMENT macro is whatever the user defined. In both
      // cases we just dispatch and unlock when klipper returns.
      if (unload_filament_macro == "_GUPPY_QUIT_MATERIAL") {
        send_action(fmt::format("{} EXTRUDER_TEMP={}",
          unload_filament_macro, effective_temp()));
      } else {
        send_action(unload_filament_macro);
      }
      return;
    }

    if (btn == load_btn.get_container()) {
      if (load_filament_macro == "_GUPPY_LOAD_MATERIAL") {
        const char *len = lv_btnmatrix_get_btn_text(length_selector.get_selector(),
          length_selector.get_selected_idx());
        send_action(fmt::format("{} EXTRUDER_TEMP={} EXTRUDE_LEN={}",
          load_filament_macro, effective_temp(), len));
      } else {
        send_action(load_filament_macro);
      }
      return;
    }
  }
}
