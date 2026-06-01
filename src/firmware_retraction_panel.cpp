#include "firmware_retraction_panel.h"
#include "state.h"
#include "spdlog/spdlog.h"

#include <cmath>

FirmwareRetractionPanel::FirmwareRetractionPanel(KWebSocketClient &websocket_client, std::mutex &l)
  : NotifyConsumer(l)
  , ws(websocket_client)
  , panel_cont(lv_obj_create(lv_scr_act()))
  , body(lv_obj_create(panel_cont))
  , list_area(lv_obj_create(body))
  , step_row(lv_obj_create(body))
  , bottom_row(lv_obj_create(body))
  , empty_cont(lv_obj_create(panel_cont))
  , reset_all_btn(lv_btn_create(bottom_row))
  , back_btn(lv_btn_create(bottom_row))
  , empty_back_btn(lv_btn_create(empty_cont))
  , length_step_selector(step_row, "Length step (mm)", {"0.05", "0.10", "0.25", ""}, 1,
      &FirmwareRetractionPanel::_handle_callback, this)
  , speed_step_selector(step_row, "Speed step (mm/s)", {"1", "5", "10", ""}, 1,
      &FirmwareRetractionPanel::_handle_callback, this)
{
  fields[0] = {"Retract length",   "RETRACT_LENGTH",         "retract_length",         "mm",   false, nullptr, nullptr, nullptr};
  fields[1] = {"Retract speed",    "RETRACT_SPEED",          "retract_speed",          "mm/s", true,  nullptr, nullptr, nullptr};
  fields[2] = {"Unretract extra",  "UNRETRACT_EXTRA_LENGTH", "unretract_extra_length", "mm",   false, nullptr, nullptr, nullptr};
  fields[3] = {"Unretract speed",  "UNRETRACT_SPEED",        "unretract_speed",        "mm/s", true,  nullptr, nullptr, nullptr};

  lv_obj_move_background(panel_cont);
  lv_obj_set_size(panel_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(panel_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(panel_cont, 0, 0);

  // ---- body (live controls) ----
  lv_obj_set_size(body, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(body, 3, 0);
  lv_obj_set_style_pad_row(body, 3, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);

  // list of parameter rows (fills the space above the step/bottom bars)
  lv_obj_set_width(list_area, LV_PCT(100));
  lv_obj_set_flex_grow(list_area, 1);
  lv_obj_clear_flag(list_area, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(list_area, 0, 0);
  lv_obj_set_style_pad_row(list_area, 0, 0);
  lv_obj_set_style_border_width(list_area, 0, 0);
  lv_obj_set_flex_flow(list_area, LV_FLEX_FLOW_COLUMN);

  for (auto &f : fields) {
    build_row(f);
  }

  // ---- step selectors row ----
  lv_obj_set_width(step_row, LV_PCT(100));
  lv_obj_set_height(step_row, LV_SIZE_CONTENT);
  lv_obj_clear_flag(step_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(step_row, 0, 0);
  lv_obj_set_style_pad_column(step_row, 6, 0);
  lv_obj_set_style_border_width(step_row, 0, 0);
  lv_obj_set_flex_flow(step_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_grow(length_step_selector.get_container(), 1);
  lv_obj_set_flex_grow(speed_step_selector.get_container(), 1);

  // ---- bottom row: Reset all | Back ----
  lv_obj_set_width(bottom_row, LV_PCT(100));
  lv_obj_set_height(bottom_row, LV_SIZE_CONTENT);
  lv_obj_clear_flag(bottom_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(bottom_row, 0, 0);
  lv_obj_set_style_pad_column(bottom_row, 6, 0);
  lv_obj_set_style_border_width(bottom_row, 0, 0);
  lv_obj_set_flex_flow(bottom_row, LV_FLEX_FLOW_ROW);

  lv_obj_set_flex_grow(reset_all_btn, 1);
  lv_obj_set_height(reset_all_btn, 36);
  lv_obj_set_style_bg_color(reset_all_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  lv_obj_t *ra_lbl = lv_label_create(reset_all_btn);
  lv_label_set_text(ra_lbl, LV_SYMBOL_REFRESH "  Reset all");
  lv_obj_center(ra_lbl);
  lv_obj_add_event_cb(reset_all_btn, &FirmwareRetractionPanel::_handle_callback, LV_EVENT_CLICKED, this);

  lv_obj_set_flex_grow(back_btn, 1);
  lv_obj_set_height(back_btn, 36);
  lv_obj_t *bk_lbl = lv_label_create(back_btn);
  lv_label_set_text(bk_lbl, LV_SYMBOL_LEFT "  Back");
  lv_obj_center(bk_lbl);
  lv_obj_add_event_cb(back_btn, &FirmwareRetractionPanel::_handle_callback, LV_EVENT_CLICKED, this);

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
  lv_obj_set_height(empty_back_btn, 40);
  lv_obj_set_width(empty_back_btn, 140);
  lv_obj_t *eb_lbl = lv_label_create(empty_back_btn);
  lv_label_set_text(eb_lbl, LV_SYMBOL_LEFT "  Back");
  lv_obj_center(eb_lbl);
  lv_obj_add_event_cb(empty_back_btn, &FirmwareRetractionPanel::_handle_callback, LV_EVENT_CLICKED, this);
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

void FirmwareRetractionPanel::build_row(Field &f) {
  // one line: [ name .......... ] [ value ] [ - ] [ + ], all vertically centred
  lv_obj_t *row = lv_obj_create(list_area);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_flex_grow(row, 1);  // the 4 rows share list_area's height evenly
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_hor(row, 8, 0);
  lv_obj_set_style_pad_ver(row, 0, 0);
  lv_obj_set_style_pad_column(row, 8, 0);
  lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *name = lv_label_create(row);
  lv_label_set_text(name, f.name);
  lv_obj_set_flex_grow(name, 1);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(name, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);

  f.value = lv_label_create(row);
  lv_label_set_text(f.value, "—");
  lv_obj_set_width(f.value, 110);
  lv_obj_set_style_text_font(f.value, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_align(f.value, LV_TEXT_ALIGN_RIGHT, 0);

  // - / + buttons
  auto make_btn = [&](const char *sym) {
    lv_obj_t *b = lv_btn_create(row);
    lv_obj_set_size(b, 58, LV_PCT(78));
    lv_obj_set_style_bg_color(b, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, sym);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(b, &FirmwareRetractionPanel::_handle_callback, LV_EVENT_CLICKED, this);
    return b;
  };
  f.minus = make_btn(LV_SYMBOL_MINUS);
  f.plus = make_btn(LV_SYMBOL_PLUS);
}

double FirmwareRetractionPanel::cur_value(const Field &f) {
  auto v = State::get_instance()->get_data(
    json::json_pointer(std::string("/printer_state/firmware_retraction/") + f.key));
  return v.is_number() ? v.template get<double>() : NAN;
}

double FirmwareRetractionPanel::config_default(const Field &f) {
  auto v = State::get_instance()->get_data(
    json::json_pointer(std::string("/printer_state/configfile/settings/firmware_retraction/") + f.key));
  return v.is_number() ? v.template get<double>() : NAN;
}

void FirmwareRetractionPanel::send_field(const Field &f, double value) {
  value = value < 0 ? 0 : value;
  if (f.is_speed) {
    ws.gcode_script(fmt::format("SET_RETRACTION {}={}", f.param, static_cast<int>(std::lround(value))));
  } else {
    ws.gcode_script(fmt::format("SET_RETRACTION {}={:.2f}", f.param, value));
  }
}

void FirmwareRetractionPanel::apply_step(Field &f, bool up) {
  double cur = cur_value(f);
  if (std::isnan(cur)) return;
  Selector &sel = f.is_speed ? speed_step_selector : length_step_selector;
  double step = std::stod(lv_btnmatrix_get_btn_text(sel.get_selector(), sel.get_selected_idx()));
  send_field(f, cur + (up ? step : -step));
}

void FirmwareRetractionPanel::update_from(json &fr) {
  if (fr.is_null()) return;
  for (auto &f : fields) {
    auto v = fr[f.key];
    if (!v.is_number()) continue;
    if (f.is_speed) {
      lv_label_set_text(f.value,
        fmt::format("{} {}", static_cast<int>(std::lround(v.template get<double>())), f.unit).c_str());
    } else {
      lv_label_set_text(f.value,
        fmt::format("{:.2f} {}", v.template get<double>(), f.unit).c_str());
    }
  }
}

void FirmwareRetractionPanel::refresh_values() {
  auto fr = State::get_instance()->get_data("/printer_state/firmware_retraction"_json_pointer);
  update_from(fr);
}

void FirmwareRetractionPanel::foreground() {
  auto fr = State::get_instance()->get_data("/printer_state/firmware_retraction"_json_pointer);
  if (!fr.is_null()) {
    lv_obj_clear_flag(body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(empty_cont, LV_OBJ_FLAG_HIDDEN);
    refresh_values();
  } else {
    lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(empty_cont, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_move_foreground(panel_cont);
}

void FirmwareRetractionPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  auto fr = j["/params/0/firmware_retraction"_json_pointer];
  update_from(fr);
}

void FirmwareRetractionPanel::handle_callback(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t *sel = lv_event_get_target(e);
    uint32_t idx = lv_btnmatrix_get_selected_btn(sel);
    if (sel == length_step_selector.get_selector()) {
      length_step_selector.set_selected_idx(idx);
    } else if (sel == speed_step_selector.get_selector()) {
      speed_step_selector.set_selected_idx(idx);
    }
    return;
  }

  if (code != LV_EVENT_CLICKED) return;
  lv_obj_t *btn = lv_event_get_current_target(e);

  if (btn == back_btn || btn == empty_back_btn) {
    lv_obj_move_background(panel_cont);
    return;
  }

  if (btn == reset_all_btn) {
    for (auto &f : fields) {
      double d = config_default(f);
      if (!std::isnan(d)) send_field(f, d);
    }
    return;
  }

  for (auto &f : fields) {
    if (btn == f.minus) { apply_step(f, false); return; }
    if (btn == f.plus)  { apply_step(f, true);  return; }
  }
}
