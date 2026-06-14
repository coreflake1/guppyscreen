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

// Chunked load: total filament fed and the size of each individually-issued
// step. The stock LOAD_MATERIAL macro feeds 150mm as one G1 E150 move that
// can't be interrupted; we feed the same total in LOAD_CHUNK_MM steps so the
// user can stop between chunks. LOAD_FEED is the per-chunk feedrate (mm/min);
// 300 = 5 mm/s, so a chunk takes ~2s = the worst-case stop latency.
static const int LOAD_TOTAL_MM = 150;
static const int LOAD_CHUNK_MM = 10;
static const int LOAD_FEED = 300;

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
  , temp_selector(panel_cont, "Hotend target (°C)",
    {"180", "200", "220", "240", "260", "280", "300", ""}, 3, &ExtruderPanel::_handle_callback, this)
  , length_selector(panel_cont, "Extrude / Retract length (mm)",
    {"5", "10", "15", "20", "25", "30", "35", ""}, 1, &ExtruderPanel::_handle_callback, this)
  , speed_selector(panel_cont, "Extrude / Retract speed (mm/s)",
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
  lv_obj_align(busy_spinner, LV_ALIGN_CENTER, 0, -10);
  lv_obj_add_flag(busy_spinner, LV_OBJ_FLAG_HIDDEN);

  // Caption just below the spinner. Wrapped + centered so the two-line heating
  // message fits. Uses the shared dialog-card styling so it matches the app's
  // toasts/dialogs (grey card, radius 8, border) instead of an ad-hoc look.
  busy_label = lv_label_create(panel_cont);
  lv_obj_add_flag(busy_label, LV_OBJ_FLAG_FLOATING);
  lv_label_set_long_mode(busy_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(busy_label, 200);
  lv_obj_set_style_text_align(busy_label, LV_TEXT_ALIGN_CENTER, 0);
  KUtils::style_dialog_box(busy_label);
  lv_obj_align(busy_label, LV_ALIGN_CENTER, 0, 55);
  lv_obj_add_flag(busy_label, LV_OBJ_FLAG_HIDDEN);

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

  build_filament_dialog();
}

ExtruderPanel::~ExtruderPanel() {
  cancel_safety_timer();
  if (panel_cont != NULL) {
    lv_obj_del(panel_cont);
    panel_cont = NULL;
  }
  if (fil_cont != NULL) {
    lv_obj_del(fil_cont);
    fil_cont = NULL;
  }
}

void ExtruderPanel::foreground() {
  lv_obj_move_foreground(panel_cont);
}

void ExtruderPanel::enable_spoolman() {
  spoolman_btn.enable();
  spoolman_enabled = true;
}

void ExtruderPanel::build_filament_dialog() {
  fil_cont = lv_obj_create(lv_scr_act());
  fil_box = lv_obj_create(fil_cont);
  fil_content_cont = lv_obj_create(fil_box);
  fil_row_cont = lv_obj_create(fil_content_cont);
  fil_swatch = lv_obj_create(fil_row_cont);
  fil_name_label = lv_label_create(fil_row_cont);
  fil_detail_label = lv_label_create(fil_content_cont);
  fil_yes_btn = lv_btn_create(fil_box);
  fil_no_btn = lv_btn_create(fil_box);

  KUtils::style_dialog_overlay(fil_cont);
  lv_obj_add_flag(fil_cont, LV_OBJ_FLAG_HIDDEN);

  KUtils::style_dialog_box(fil_box);
  lv_obj_set_size(fil_box, LV_PCT(80), LV_PCT(62));
  lv_obj_align(fil_box, LV_ALIGN_CENTER, 0, 0);

  // header (text swapped per spool state in show_load_filament_dialog)
  fil_title_label = lv_label_create(fil_box);
  lv_label_set_text(fil_title_label, "Use this filament?");
  KUtils::style_dialog_title(fil_title_label);

  lv_obj_remove_style_all(fil_content_cont);
  lv_obj_set_width(fil_content_cont, LV_PCT(100));
  lv_obj_set_height(fil_content_cont, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(fil_content_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(fil_content_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(fil_content_cont, 7, 0);
  lv_obj_clear_flag(fil_content_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(fil_content_cont, LV_ALIGN_CENTER, 0, 2);

  lv_obj_remove_style_all(fil_row_cont);
  lv_obj_set_width(fil_row_cont, LV_SIZE_CONTENT);
  lv_obj_set_height(fil_row_cont, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(fil_row_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(fil_row_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(fil_row_cont, 8, 0);
  lv_obj_clear_flag(fil_row_cont, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_size(fil_swatch, 20, 20);
  lv_obj_set_style_border_width(fil_swatch, 1, 0);
  lv_obj_set_style_border_color(fil_swatch, lv_color_white(), 0);
  lv_obj_set_style_radius(fil_swatch, 4, 0);
  lv_obj_clear_flag(fil_swatch, LV_OBJ_FLAG_SCROLLABLE);

  lv_label_set_long_mode(fil_name_label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(fil_name_label, 280, 0);
  lv_obj_set_style_text_font(fil_name_label, &lv_font_montserrat_14, 0);
  lv_label_set_text(fil_name_label, "");

  lv_obj_set_style_text_align(fil_detail_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(fil_detail_label, "");

  lv_obj_set_size(fil_yes_btn, 96, LV_SIZE_CONTENT);
  lv_obj_add_event_cb(fil_yes_btn, &ExtruderPanel::_handle_filament_btns, LV_EVENT_CLICKED, this);
  lv_obj_align(fil_yes_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_t *yl = lv_label_create(fil_yes_btn);
  lv_label_set_text(yl, "Yes");
  lv_obj_center(yl);

  lv_obj_set_size(fil_no_btn, 96, LV_SIZE_CONTENT);
  lv_obj_add_event_cb(fil_no_btn, &ExtruderPanel::_handle_filament_btns, LV_EVENT_CLICKED, this);
  lv_obj_align(fil_no_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_t *nl = lv_label_create(fil_no_btn);
  lv_label_set_text(nl, "No");
  lv_obj_center(nl);
}

void ExtruderPanel::show_load_filament_dialog() {
  json spool = spoolman_panel.get_active_spool();
  if (spool.is_null()) {
    lv_label_set_text(fil_title_label, "No spool selected - load anyway?");
    lv_obj_add_flag(fil_swatch, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(fil_name_label, "");
    lv_label_set_text(fil_detail_label, "");
  } else {
    lv_label_set_text(fil_title_label, "Use this filament?");
    auto vendor_json = spool["/filament/vendor/name"_json_pointer];
    auto vendor = !vendor_json.is_null() ? vendor_json.template get<std::string>() : "";
    auto name_json = spool["/filament/name"_json_pointer];
    auto name = !name_json.is_null() ? name_json.template get<std::string>() : "";
    lv_label_set_text(fil_name_label, fmt::format("{} - {}", vendor, name).c_str());

    auto color_json = spool["/filament/color_hex"_json_pointer];
    if (!color_json.is_null()) {
      uint32_t rgb = static_cast<uint32_t>(strtol(color_json.template get<std::string>().c_str(), NULL, 16));
      lv_obj_set_style_bg_color(fil_swatch, lv_color_hex(rgb), 0);
      lv_obj_clear_flag(fil_swatch, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(fil_swatch, LV_OBJ_FLAG_HIDDEN);
    }

    auto material_json = spool["/filament/material"_json_pointer];
    auto material = !material_json.is_null() ? material_json.template get<std::string>() : "?";
    auto remaining_json = spool["/remaining_weight"_json_pointer];
    if (!remaining_json.is_null()) {
      lv_label_set_text(fil_detail_label,
        fmt::format("{}, {:.0f} g left", material, remaining_json.template get<double>()).c_str());
    } else {
      lv_label_set_text(fil_detail_label, material.c_str());
    }
  }
  lv_obj_clear_flag(fil_cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(fil_cont);
}

void ExtruderPanel::handle_filament_btns(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  lv_obj_t *btn = lv_event_get_current_target(event);

  // dismiss the dialog regardless of choice
  lv_obj_move_background(fil_cont);
  lv_obj_add_flag(fil_cont, LV_OBJ_FLAG_HIDDEN);

  if (btn == fil_yes_btn) {
    run_when_hot(PA_LOAD, "Load", "");
  } else if (btn == fil_no_btn) {
    // open Spoolman to pick/fix the active spool; re-show this confirm once a
    // spool is chosen so the user re-confirms before the load starts.
    spoolman_panel.request_select_for_print([this]() { this->show_load_filament_dialog(); });
    spoolman_panel.foreground();
  }
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
  } else {
    // Refresh the live "Heating X->YC" readout as the hotend climbs.
    update_busy_caption();
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
  bool busy = action_in_flight || pending_kind != PA_NONE || load_active;

  if (action_in_flight) {
    // A gcode is executing on klipper; lock everything until the response.
    extrude_btn.disable();
    retract_btn.disable();
    load_btn.disable();
    unload_btn.disable();
    cooldown_btn.disable();
  } else if (load_active) {
    // Chunked load feeding. Cooldown stays clickable and doubles as Stop — it
    // halts the load after the in-flight chunk; the rest are blocked.
    extrude_btn.disable();
    retract_btn.disable();
    load_btn.disable();
    unload_btn.disable();
    cooldown_btn.enable();
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

  update_busy_caption();
}

void ExtruderPanel::update_busy_caption() {
  if (busy_label == NULL) {
    return;
  }

  if (load_active) {
    // Show progress + how to stop, so the slow feed never looks like a hang.
    int done = LOAD_TOTAL_MM - load_remaining_mm;
    lv_label_set_text(busy_label,
      fmt::format("Loading filament {}/{} mm\nTap Cooldown to stop",
        done, LOAD_TOTAL_MM).c_str());
    lv_obj_clear_flag(busy_label, LV_OBJ_FLAG_HIDDEN);
  } else if (pending_kind != PA_NONE) {
    // Heating toward the action. Show live current->target so the number
    // visibly climbs and it's obvious the press registered. (ASCII arrow/dots:
    // the small montserrat font lacks the U+2192 / U+2026 glyphs.)
    lv_label_set_text(busy_label,
      fmt::format("Heating {}°C -> {}°C\n{} when ready",
        current_temp, pending_want, action_name).c_str());
    lv_obj_clear_flag(busy_label, LV_OBJ_FLAG_HIDDEN);
  } else if (action_in_flight) {
    lv_label_set_text(busy_label,
      fmt::format("{} in progress...", action_name).c_str());
    lv_obj_clear_flag(busy_label, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(busy_label, LV_OBJ_FLAG_HIDDEN);
  }
}

void ExtruderPanel::arm_safety_timer(uint32_t ms) {
  cancel_safety_timer();
  safety_timer = lv_timer_create([](lv_timer_t *t) {
    auto *self = (ExtruderPanel *)t->user_data;
    self->safety_timer = NULL;  // one-shot, LVGL deletes after this callback
    if (self->load_active) {
      // A load chunk's response never arrived — stop feeding rather than
      // leaving the load stuck mid-way with the buttons locked.
      spdlog::warn("load chunk response timeout, stopping load");
      self->finish_load();
      KUtils::notify_toast("Load timed out.");
    } else if (self->action_in_flight) {
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
  maybe_auto_cooldown();  // extrude/retract/unload finished -> drop the heater
}

void ExtruderPanel::maybe_auto_cooldown() {
  if (!auto_cool_after) {
    return;
  }
  auto_cool_after = false;  // clear first so the cooldown action can't re-trigger
  action_name = "Cooldown";
  send_action(cooldown_macro);
}

void ExtruderPanel::clear_pending() {
  if (pending_kind == PA_NONE) {
    return;
  }
  pending_kind = PA_NONE;
  pending_gcode.clear();
  pending_want = 0;
  cancel_safety_timer();
  refresh_button_state();
}

void ExtruderPanel::fire_pending() {
  PendingKind kind = pending_kind;
  std::string gcode = pending_gcode;
  clear_pending();   // resets pending state + button enables; the call below re-locks
  // action_name carries over so the caption stays correct.
  if (kind == PA_LOAD) {
    begin_load();    // chunked, stoppable — not a one-shot macro
  } else {
    send_action(gcode);
  }
}

void ExtruderPanel::begin_load() {
  load_active = true;
  load_stop = false;
  load_remaining_mm = LOAD_TOTAL_MM;
  action_name = "Load";
  refresh_button_state();  // spinner + caption; Cooldown stays live as Stop
  send_load_chunk();
}

void ExtruderPanel::send_load_chunk() {
  if (!load_active) {
    return;
  }
  if (load_stop || load_remaining_mm <= 0) {
    finish_load(!load_stop);  // natural completion only when not stopped
    return;
  }
  int chunk = std::min(LOAD_CHUNK_MM, load_remaining_mm);
  load_remaining_mm -= chunk;
  update_busy_caption();
  // Per-chunk watchdog: if the response never arrives (ws died), stop feeding
  // rather than hang. The next chunk is only sent from the response callback,
  // so the feed naturally halts the moment load_stop is set.
  arm_safety_timer(ACTION_TIMEOUT_MS);
  ws.gcode_script(fmt::format("M83\nG1 E{} F{}", chunk, LOAD_FEED),
    [this](json &) {
      std::lock_guard<std::mutex> lock(lv_lock);
      cancel_safety_timer();
      send_load_chunk();  // feed the next chunk, or finish_load() when done/stopped
    });
}

void ExtruderPanel::finish_load(bool natural) {
  load_active = false;
  load_stop = false;
  load_remaining_mm = 0;
  cancel_safety_timer();
  refresh_button_state();
  if (natural) {
    maybe_auto_cooldown();  // load fed to completion -> drop the heater
  }
}

void ExtruderPanel::run_when_hot(PendingKind kind, const std::string &name,
                                 const std::string &gcode) {
  action_name = name;
  // Extrude/retract/load/unload all heat the hotend just for the action; cool it
  // back down once the action completes (on_action_response / finish_load).
  auto_cool_after = true;
  int want = effective_temp();

  // Non-blocking heat request. Avoids the M109 hang from #65 — we watch the
  // temp stream ourselves and run the action once we're hot enough. Applies to
  // load/unload too, so the user's selected temp is honoured regardless of
  // which filament macro is configured.
  if (want > current_target) {
    ws.gcode_script(fmt::format("M104 S{}", want));
  }

  if (current_temp + HEAT_TOLERANCE >= want) {
    if (kind == PA_LOAD) {
      begin_load();  // chunked, stoppable feed instead of a one-shot macro
    } else {
      send_action(gcode);
    }
    return;
  }

  // Cold — queue it. consume() will fire it once we reach the threshold.
  pending_kind = kind;
  pending_gcode = gcode;
  pending_want = want;
  refresh_button_state();
  arm_safety_timer(HEAT_TIMEOUT_MS);
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
      clear_pending();   // don't auto-extrude after the user navigates away
      load_stop = true;  // and stop any in-flight chunked load
      lv_obj_move_background(panel_cont);
      return;
    }

    if (btn == spoolman_btn.get_container()) {
      spoolman_panel.foreground();
      return;
    }

    if (btn == cooldown_btn.get_container()) {
      // Cooldown doubles as the cancel for a pending heat-up and the Stop for a
      // running chunked load. Always allowed unless a one-shot gcode is already
      // in flight (in which case refresh_button_state disables the button).
      if (action_in_flight) {
        return;
      }
      // Manual cooldown: this IS the cooldown, so don't let it auto-chain another.
      auto_cool_after = false;
      if (load_active) {
        // Stop the load: send_load_chunk() won't queue another chunk, and we
        // drop the heater now. finish_load() runs from the in-flight chunk's
        // response, so motion ceases within ~one chunk.
        load_stop = true;
        ws.gcode_script(cooldown_macro);
        KUtils::notify_toast("Stopping load...");
        return;
      }
      clear_pending();
      action_name = "Cooldown";
      send_action(cooldown_macro);
      return;
    }

    // Remaining actions: blocked while another action is running OR while a
    // heat-up is pending. refresh_button_state() also disables their widgets,
    // but check the flags too in case of an event in flight before refresh.
    if (action_in_flight || pending_kind != PA_NONE || load_active) {
      return;
    }

    if (btn == extrude_btn.get_container() || btn == retract_btn.get_container()) {
      bool retract = btn == retract_btn.get_container();
      const char *len = lv_btnmatrix_get_btn_text(length_selector.get_selector(),
        length_selector.get_selected_idx());
      const char *speed = lv_btnmatrix_get_btn_text(speed_selector.get_selector(),
        speed_selector.get_selected_idx());
      int feed = 300;  // 5 mm/s fallback if the speed text fails to parse
      try {
        feed = std::stoi(speed) * 60;
      } catch (const std::exception &) {}
      run_when_hot(retract ? PA_RETRACT : PA_EXTRUDE,
        retract ? "Retract" : "Extrude",
        fmt::format("M83\nG1 E{}{} F{}", retract ? "-" : "", len, feed));
      return;
    }

    if (btn == unload_btn.get_container()) {
      // Heat to the selected temp first (run_when_hot), then run the macro.
      // _GUPPY_QUIT_MATERIAL also takes EXTRUDER_TEMP; a plain UNLOAD_FILAMENT
      // macro is whatever the user defined, but now always runs hot.
      std::string gcode = unload_filament_macro == "_GUPPY_QUIT_MATERIAL"
        ? fmt::format("{} EXTRUDER_TEMP={}", unload_filament_macro, effective_temp())
        : unload_filament_macro;
      run_when_hot(PA_UNLOAD, "Unload", gcode);
      return;
    }

    if (btn == load_btn.get_container()) {
      // Loading deducts from the active Spoolman spool (Moonraker tracks raw
      // E-axis movement, not just prints), so confirm the spool first when
      // Spoolman is configured. Yes -> load, No -> open the Spoolman panel.
      // The load itself heats then feeds in bounded, stoppable chunks
      // (run_when_hot -> begin_load), bypassing the uninterruptible stock macro.
      if (spoolman_enabled) {
        show_load_filament_dialog();
      } else {
        run_when_hot(PA_LOAD, "Load", "");
      }
      return;
    }
  }
}

#ifdef SIMULATOR
void ExtruderPanel::sim_show_busy() {
  // Pretend Extrude was pressed cold: heating toward target, action buttons
  // disabled, spinner + caption visible. We don't send_action() (no websocket;
  // the safety timer would also fire ACTION_TIMEOUT_MS later) — just stage the
  // pending state so the heat-up caption can be visually verified.
  action_name = "Extrude";
  pending_kind = PA_EXTRUDE;
  pending_want = 220;
  current_temp = 25;
  refresh_button_state();
  lv_obj_move_foreground(panel_cont);
}
#endif
