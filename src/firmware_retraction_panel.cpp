#include "firmware_retraction_panel.h"
#include "state.h"
#include "spdlog/spdlog.h"

#include <cmath>

LV_IMG_DECLARE(retract_img);
LV_IMG_DECLARE(refresh_img);
LV_IMG_DECLARE(flow_up_img);
LV_IMG_DECLARE(flow_down_img);
LV_IMG_DECLARE(speed_up_img);
LV_IMG_DECLARE(speed_down_img);
LV_IMG_DECLARE(back);

FirmwareRetractionPanel::FirmwareRetractionPanel(KWebSocketClient &websocket_client, std::mutex &l)
  : NotifyConsumer(l)
  , ws(websocket_client)
  , panel_cont(lv_obj_create(lv_scr_act()))
  , body(lv_obj_create(panel_cont))
  , empty_cont(lv_obj_create(panel_cont))
  , values_cont(lv_obj_create(body))
  , rl_reset_btn(body, &refresh_img, "Reset", &FirmwareRetractionPanel::_handle_retract_length, this)
  , rl_up_btn(body, &flow_up_img, "Length +", &FirmwareRetractionPanel::_handle_retract_length, this)
  , rl_down_btn(body, &flow_down_img, "Length -", &FirmwareRetractionPanel::_handle_retract_length, this)
  , rs_reset_btn(body, &refresh_img, "Reset", &FirmwareRetractionPanel::_handle_retract_speed, this)
  , rs_up_btn(body, &speed_up_img, "Speed +", &FirmwareRetractionPanel::_handle_retract_speed, this)
  , rs_down_btn(body, &speed_down_img, "Speed -", &FirmwareRetractionPanel::_handle_retract_speed, this)
  , ue_reset_btn(body, &refresh_img, "Reset", &FirmwareRetractionPanel::_handle_unretract_extra, this)
  , ue_up_btn(body, &flow_up_img, "Extra +", &FirmwareRetractionPanel::_handle_unretract_extra, this)
  , ue_down_btn(body, &flow_down_img, "Extra -", &FirmwareRetractionPanel::_handle_unretract_extra, this)
  , us_reset_btn(body, &refresh_img, "Reset", &FirmwareRetractionPanel::_handle_unretract_speed, this)
  , us_up_btn(body, &speed_up_img, "Un-Spd +", &FirmwareRetractionPanel::_handle_unretract_speed, this)
  , us_down_btn(body, &speed_down_img, "Un-Spd -", &FirmwareRetractionPanel::_handle_unretract_speed, this)
  , back_btn(body, &back, "Back", &FirmwareRetractionPanel::_handle_callback, this)
  , empty_back_btn(empty_cont, &back, "Back", &FirmwareRetractionPanel::_handle_callback, this)
  , length_step_selector(body, "Length step (mm)", {"0.05", "0.10", "0.25", ""}, 1, 40, 15,
      &FirmwareRetractionPanel::_handle_callback, this)
  , speed_step_selector(body, "Speed step (mm/s)", {"1", "5", "10", ""}, 1, 40, 15,
      &FirmwareRetractionPanel::_handle_callback, this)
  , retract_length_lbl(values_cont, &retract_img, 100, 100, 15, "Len —")
  , retract_speed_lbl(values_cont, &speed_up_img, 100, 100, 15, "RSpd —")
  , unretract_extra_lbl(values_cont, &retract_img, 100, 100, 15, "Extra —")
  , unretract_speed_lbl(values_cont, &speed_up_img, 100, 100, 15, "USpd —")
{
  lv_obj_move_background(panel_cont);

  lv_obj_set_size(panel_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(panel_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(panel_cont, 0, 0);

  // ---- body (the live controls) ----
  lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(body, 0, 0);
  lv_obj_set_style_border_width(body, 0, 0);

  lv_obj_set_size(values_cont, LV_PCT(20), LV_PCT(80));
  lv_obj_clear_flag(values_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(values_cont, 0, 0);
  lv_obj_set_flex_flow(values_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(values_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_TEMPLATE_LAST};
  static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(body, col_dsc, row_dsc);

  // col 0: retract_length
  lv_obj_set_grid_cell(rl_reset_btn.get_container(), LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_grid_cell(rl_up_btn.get_container(),    LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(rl_down_btn.get_container(),  LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
  lv_obj_set_grid_cell(length_step_selector.get_container(), LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 3, 1);

  // col 1: unretract_extra_length (the other length -> shares the length step)
  lv_obj_set_grid_cell(ue_reset_btn.get_container(), LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_grid_cell(ue_up_btn.get_container(),    LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(ue_down_btn.get_container(),  LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 2, 1);

  // col 2: retract_speed
  lv_obj_set_grid_cell(rs_reset_btn.get_container(), LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_grid_cell(rs_up_btn.get_container(),    LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(rs_down_btn.get_container(),  LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
  lv_obj_set_grid_cell(speed_step_selector.get_container(), LV_GRID_ALIGN_STRETCH, 2, 2, LV_GRID_ALIGN_STRETCH, 3, 1);

  // col 3: unretract_speed (shares the speed step)
  lv_obj_set_grid_cell(us_reset_btn.get_container(), LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_grid_cell(us_up_btn.get_container(),    LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(us_down_btn.get_container(),  LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_STRETCH, 2, 1);

  // col 4: live values + back
  lv_obj_set_grid_cell(values_cont, LV_GRID_ALIGN_STRETCH, 4, 1, LV_GRID_ALIGN_STRETCH, 0, 3);
  lv_obj_set_grid_cell(back_btn.get_container(), LV_GRID_ALIGN_STRETCH, 4, 1, LV_GRID_ALIGN_STRETCH, 3, 1);

  // ---- empty state ----
  lv_obj_set_size(empty_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(empty_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(empty_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(empty_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(empty_cont, 16, 0);

  lv_obj_t *empty_lbl = lv_label_create(empty_cont);
  lv_label_set_text(empty_lbl,
    "Firmware retraction is not enabled.\n\n"
    "Add a [firmware_retraction] section to\n"
    "printer.cfg and restart Klipper.");
  lv_obj_set_style_text_align(empty_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(empty_lbl, &lv_font_montserrat_16, 0);

  lv_obj_add_flag(empty_cont, LV_OBJ_FLAG_HIDDEN);

  ws.register_notify_update(this);
}

FirmwareRetractionPanel::~FirmwareRetractionPanel() {
  if (panel_cont != NULL) {
    lv_obj_del(panel_cont);
    panel_cont = NULL;
  }
  ws.unregister_notify_update(this);
}

double FirmwareRetractionPanel::cur_value(const char *field) {
  auto v = State::get_instance()->get_data(
    json::json_pointer(std::string("/printer_state/firmware_retraction/") + field));
  return v.is_number() ? v.template get<double>() : NAN;
}

double FirmwareRetractionPanel::config_default(const char *field) {
  auto v = State::get_instance()->get_data(
    json::json_pointer(std::string("/printer_state/configfile/settings/firmware_retraction/") + field));
  return v.is_number() ? v.template get<double>() : NAN;
}

void FirmwareRetractionPanel::update_values_from(json &src, const char *base_ptr) {
  auto fr = src[json::json_pointer(base_ptr)];
  if (fr.is_null()) {
    return;
  }
  if (fr["retract_length"].is_number()) {
    retract_length_lbl.update_label(
      fmt::format("Len {:.2f}", fr["retract_length"].template get<double>()).c_str());
  }
  if (fr["unretract_extra_length"].is_number()) {
    unretract_extra_lbl.update_label(
      fmt::format("Extra {:.2f}", fr["unretract_extra_length"].template get<double>()).c_str());
  }
  if (fr["retract_speed"].is_number()) {
    retract_speed_lbl.update_label(
      fmt::format("RSpd {}", static_cast<int>(std::lround(fr["retract_speed"].template get<double>()))).c_str());
  }
  if (fr["unretract_speed"].is_number()) {
    unretract_speed_lbl.update_label(
      fmt::format("USpd {}", static_cast<int>(std::lround(fr["unretract_speed"].template get<double>()))).c_str());
  }
}

void FirmwareRetractionPanel::foreground() {
  auto fr = State::get_instance()->get_data("/printer_state/firmware_retraction"_json_pointer);
  bool present = !fr.is_null();

  if (present) {
    lv_obj_clear_flag(body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(empty_cont, LV_OBJ_FLAG_HIDDEN);
    auto root = State::get_instance()->get_data("/printer_state"_json_pointer);
    update_values_from(root, "/firmware_retraction");
  } else {
    lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(empty_cont, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_move_foreground(panel_cont);
}

void FirmwareRetractionPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  update_values_from(j, "/params/0/firmware_retraction");
}

void FirmwareRetractionPanel::handle_callback(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t *selector = lv_event_get_target(e);
    uint32_t idx = lv_btnmatrix_get_selected_btn(selector);
    if (selector == length_step_selector.get_selector()) {
      length_step_selector.set_selected_idx(idx);
    } else if (selector == speed_step_selector.get_selector()) {
      speed_step_selector.set_selected_idx(idx);
    }
  }

  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(e);
    if (btn == back_btn.get_container() || btn == empty_back_btn.get_container()) {
      lv_obj_move_background(panel_cont);
    }
  }
}

// Apply a delta to one SET_RETRACTION field (clamped >= 0). step_sel picks the
// increment; sends only that field so the others are untouched.
static void set_field(KWebSocketClient &ws, const char *param, double value, bool is_speed) {
  value = value < 0 ? 0 : value;
  if (is_speed) {
    ws.gcode_script(fmt::format("SET_RETRACTION {}={}", param, static_cast<int>(std::lround(value))));
  } else {
    ws.gcode_script(fmt::format("SET_RETRACTION {}={:.2f}", param, value));
  }
}

void FirmwareRetractionPanel::handle_retract_length(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t *btn = lv_event_get_current_target(e);
  if (btn == rl_reset_btn.get_container()) {
    double d = config_default("retract_length");
    if (!std::isnan(d)) set_field(ws, "RETRACT_LENGTH", d, false);
    return;
  }
  double cur = cur_value("retract_length");
  if (std::isnan(cur)) return;
  double step = std::stod(lv_btnmatrix_get_btn_text(length_step_selector.get_selector(),
    length_step_selector.get_selected_idx()));
  set_field(ws, "RETRACT_LENGTH", cur + (btn == rl_up_btn.get_container() ? step : -step), false);
}

void FirmwareRetractionPanel::handle_unretract_extra(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t *btn = lv_event_get_current_target(e);
  if (btn == ue_reset_btn.get_container()) {
    double d = config_default("unretract_extra_length");
    if (!std::isnan(d)) set_field(ws, "UNRETRACT_EXTRA_LENGTH", d, false);
    return;
  }
  double cur = cur_value("unretract_extra_length");
  if (std::isnan(cur)) return;
  double step = std::stod(lv_btnmatrix_get_btn_text(length_step_selector.get_selector(),
    length_step_selector.get_selected_idx()));
  set_field(ws, "UNRETRACT_EXTRA_LENGTH", cur + (btn == ue_up_btn.get_container() ? step : -step), false);
}

void FirmwareRetractionPanel::handle_retract_speed(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t *btn = lv_event_get_current_target(e);
  if (btn == rs_reset_btn.get_container()) {
    double d = config_default("retract_speed");
    if (!std::isnan(d)) set_field(ws, "RETRACT_SPEED", d, true);
    return;
  }
  double cur = cur_value("retract_speed");
  if (std::isnan(cur)) return;
  double step = std::stod(lv_btnmatrix_get_btn_text(speed_step_selector.get_selector(),
    speed_step_selector.get_selected_idx()));
  set_field(ws, "RETRACT_SPEED", cur + (btn == rs_up_btn.get_container() ? step : -step), true);
}

void FirmwareRetractionPanel::handle_unretract_speed(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t *btn = lv_event_get_current_target(e);
  if (btn == us_reset_btn.get_container()) {
    double d = config_default("unretract_speed");
    if (!std::isnan(d)) set_field(ws, "UNRETRACT_SPEED", d, true);
    return;
  }
  double cur = cur_value("unretract_speed");
  if (std::isnan(cur)) return;
  double step = std::stod(lv_btnmatrix_get_btn_text(speed_step_selector.get_selector(),
    speed_step_selector.get_selected_idx()));
  set_field(ws, "UNRETRACT_SPEED", cur + (btn == us_up_btn.get_container() ? step : -step), true);
}
