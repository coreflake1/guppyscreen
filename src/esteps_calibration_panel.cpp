#include "esteps_calibration_panel.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <cstdlib>
#include <string>

// Klipper's idle_timeout is effectively disabled on this printer (see the
// esteps_calibration.cfg header), so nothing else stops a forgotten heater -
// same 2-minute backstop the underlying macro already enforces on its own via
// _CALIBRATE_ESTEPS_TIMEOUT; this is just the UI side of that same guarantee.

EstepsCalibrationPanel::EstepsCalibrationPanel(KWebSocketClient &websocket_client, std::mutex &l)
  : NotifyConsumer(l)
  , ws(websocket_client)
  , panel_cont(lv_obj_create(lv_scr_act()))
  , intro_cont(lv_obj_create(panel_cont))
  , heating_cont(lv_obj_create(panel_cont))
  , mark_cont(lv_obj_create(panel_cont))
  , extruding_cont(lv_obj_create(panel_cont))
  , measure_cont(lv_obj_create(panel_cont))
  , result_cont(lv_obj_create(panel_cont))
  , stage(INTRO)
  , active(false)
  , mark_mm(120.0)
  , length_mm(100.0)
  , speed_mm_min(60.0)
  , extruder_temp(0.0)
  , have_extruder_temp(false)
  , target_temp(0.0)
  , have_target_temp(false)
  , old_rotation_distance(0.0)
  , new_rotation_distance(0.0)
  , commanded_mm(0.0)
  , actual_mm(0.0)
  , have_original_rd(false)
  , original_rotation_distance(0.0)
{
  lv_obj_move_background(panel_cont);
  lv_obj_set_size(panel_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(panel_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(panel_cont, 0, 0);

  auto fill = [](lv_obj_t *c) {
    lv_obj_set_size(c, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  };

  // ---------- INTRO ----------
  fill(intro_cont);
  lv_obj_set_style_pad_all(intro_cont, 10, 0);
  lv_obj_set_style_pad_row(intro_cont, 10, 0);

  lv_obj_t *title = lv_label_create(intro_cont);
  lv_label_set_text(title, "E-Steps Calibration");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

  lv_obj_t *intro_body = lv_label_create(intro_cont);
  lv_label_set_long_mode(intro_body, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(intro_body, LV_PCT(94));
  lv_label_set_text(intro_body,
    "Checks how much filament the extruder actually pushes vs. what it's told "
    "to. Fixes prints that look uniformly over- or under-extruded.\n\n"
    "You'll need a ruler and a small piece of tape. The hotend heats up first "
    "- this is a direct-drive extruder, so there's no cold test.");
  lv_obj_set_style_text_font(intro_body, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(intro_body, LV_TEXT_ALIGN_CENTER, 0);

  start_btn = lv_btn_create(intro_cont);
  lv_obj_set_size(start_btn, 220, 46);
  lv_obj_t *sb_lbl = lv_label_create(start_btn);
  lv_label_set_text(sb_lbl, LV_SYMBOL_OK "  Start");
  lv_obj_set_style_text_font(sb_lbl, &lv_font_montserrat_16, 0);
  lv_obj_center(sb_lbl);
  lv_obj_add_event_cb(start_btn, &EstepsCalibrationPanel::_handle_callback, LV_EVENT_CLICKED, this);

  intro_back_btn = lv_btn_create(intro_cont);
  lv_obj_set_size(intro_back_btn, 140, 36);
  lv_obj_set_style_bg_color(intro_back_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  lv_obj_t *ib_lbl = lv_label_create(intro_back_btn);
  lv_label_set_text(ib_lbl, LV_SYMBOL_LEFT "  Back");
  lv_obj_center(ib_lbl);
  lv_obj_add_event_cb(intro_back_btn, &EstepsCalibrationPanel::_handle_callback, LV_EVENT_CLICKED, this);

  // ---------- HEATING ----------
  fill(heating_cont);
  lv_obj_set_style_pad_row(heating_cont, 12, 0);

  lv_obj_t *heating_title = lv_label_create(heating_cont);
  lv_label_set_text(heating_title, "Heating");
  lv_obj_set_style_text_font(heating_title, &lv_font_montserrat_16, 0);

  heating_status_label = lv_label_create(heating_cont);
  lv_label_set_long_mode(heating_status_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(heating_status_label, LV_PCT(90));
  lv_label_set_text(heating_status_label, "Heating...");
  lv_obj_set_style_text_font(heating_status_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(heating_status_label, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *heating_spinner = lv_spinner_create(heating_cont, 1000, 60);
  lv_obj_set_size(heating_spinner, 50, 50);

  heating_cancel_btn = lv_btn_create(heating_cont);
  lv_obj_set_size(heating_cancel_btn, 160, 40);
  lv_obj_set_style_bg_color(heating_cancel_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  lv_obj_t *hc_lbl = lv_label_create(heating_cancel_btn);
  lv_label_set_text(hc_lbl, LV_SYMBOL_CLOSE "  Cancel");
  lv_obj_center(hc_lbl);
  lv_obj_add_event_cb(heating_cancel_btn, &EstepsCalibrationPanel::_handle_callback, LV_EVENT_CLICKED, this);

  // ---------- MARK ----------
  fill(mark_cont);
  lv_obj_set_style_pad_all(mark_cont, 10, 0);
  lv_obj_set_style_pad_row(mark_cont, 10, 0);

  lv_obj_t *mark_title = lv_label_create(mark_cont);
  lv_label_set_text(mark_title, "Mark the Filament");
  lv_obj_set_style_text_font(mark_title, &lv_font_montserrat_16, 0);

  mark_instr_label = lv_label_create(mark_cont);
  lv_label_set_long_mode(mark_instr_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(mark_instr_label, LV_PCT(94));
  lv_label_set_text(mark_instr_label, "");
  lv_obj_set_style_text_font(mark_instr_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(mark_instr_label, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *mark_act = lv_obj_create(mark_cont);
  lv_obj_set_width(mark_act, LV_PCT(94));
  lv_obj_set_height(mark_act, LV_SIZE_CONTENT);
  lv_obj_clear_flag(mark_act, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(mark_act, 0, 0);
  lv_obj_set_style_pad_all(mark_act, 0, 0);
  lv_obj_set_style_pad_column(mark_act, 6, 0);
  lv_obj_set_flex_flow(mark_act, LV_FLEX_FLOW_ROW);

  mark_cancel_btn = lv_btn_create(mark_act);
  lv_obj_set_flex_grow(mark_cancel_btn, 1);
  lv_obj_set_height(mark_cancel_btn, 44);
  lv_obj_set_style_bg_color(mark_cancel_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  lv_obj_t *mc_lbl = lv_label_create(mark_cancel_btn);
  lv_label_set_text(mc_lbl, LV_SYMBOL_CLOSE "  Cancel");
  lv_obj_center(mc_lbl);
  lv_obj_add_event_cb(mark_cancel_btn, &EstepsCalibrationPanel::_handle_callback, LV_EVENT_CLICKED, this);

  extrude_btn = lv_btn_create(mark_act);
  lv_obj_set_flex_grow(extrude_btn, 1);
  lv_obj_set_height(extrude_btn, 44);
  lv_obj_t *ex_lbl = lv_label_create(extrude_btn);
  lv_label_set_text(ex_lbl, LV_SYMBOL_OK "  Extrude");
  lv_obj_center(ex_lbl);
  lv_obj_add_event_cb(extrude_btn, &EstepsCalibrationPanel::_handle_callback, LV_EVENT_CLICKED, this);

  // ---------- EXTRUDING ----------
  fill(extruding_cont);
  lv_obj_set_style_pad_row(extruding_cont, 12, 0);

  lv_obj_t *extruding_title = lv_label_create(extruding_cont);
  lv_label_set_text(extruding_title, "Extruding");
  lv_obj_set_style_text_font(extruding_title, &lv_font_montserrat_16, 0);

  extruding_status_label = lv_label_create(extruding_cont);
  lv_label_set_long_mode(extruding_status_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(extruding_status_label, LV_PCT(90));
  lv_label_set_text(extruding_status_label, "Extruding... please wait, do not touch the printer.");
  lv_obj_set_style_text_font(extruding_status_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(extruding_status_label, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *extruding_spinner = lv_spinner_create(extruding_cont, 1000, 60);
  lv_obj_set_size(extruding_spinner, 50, 50);

  extruding_cancel_btn = lv_btn_create(extruding_cont);
  lv_obj_set_size(extruding_cancel_btn, 160, 40);
  lv_obj_set_style_bg_color(extruding_cancel_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  lv_obj_t *ec_lbl = lv_label_create(extruding_cancel_btn);
  lv_label_set_text(ec_lbl, LV_SYMBOL_CLOSE "  Cancel");
  lv_obj_center(ec_lbl);
  lv_obj_add_event_cb(extruding_cancel_btn, &EstepsCalibrationPanel::_handle_callback, LV_EVENT_CLICKED, this);

  // ---------- MEASURE ----------
  fill(measure_cont);
  lv_obj_set_style_pad_all(measure_cont, 10, 0);
  lv_obj_set_style_pad_row(measure_cont, 10, 0);

  lv_obj_t *measure_title = lv_label_create(measure_cont);
  lv_label_set_text(measure_title, "Enter the Measurement");
  lv_obj_set_style_text_font(measure_title, &lv_font_montserrat_16, 0);

  measure_instr_label = lv_label_create(measure_cont);
  lv_label_set_long_mode(measure_instr_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(measure_instr_label, LV_PCT(94));
  lv_label_set_text(measure_instr_label, "");
  lv_obj_set_style_text_font(measure_instr_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(measure_instr_label, LV_TEXT_ALIGN_CENTER, 0);

  measure_ta = lv_textarea_create(measure_cont);
  lv_textarea_set_one_line(measure_ta, true);
  lv_textarea_set_placeholder_text(measure_ta, "mm remaining");
  lv_obj_set_width(measure_ta, LV_PCT(60));
  lv_obj_add_event_cb(measure_ta, &EstepsCalibrationPanel::_handle_ta, LV_EVENT_ALL, this);

  lv_obj_t *measure_act = lv_obj_create(measure_cont);
  lv_obj_set_width(measure_act, LV_PCT(94));
  lv_obj_set_height(measure_act, LV_SIZE_CONTENT);
  lv_obj_clear_flag(measure_act, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(measure_act, 0, 0);
  lv_obj_set_style_pad_all(measure_act, 0, 0);
  lv_obj_set_style_pad_column(measure_act, 6, 0);
  lv_obj_set_flex_flow(measure_act, LV_FLEX_FLOW_ROW);

  measure_cancel_btn = lv_btn_create(measure_act);
  lv_obj_set_flex_grow(measure_cancel_btn, 1);
  lv_obj_set_height(measure_cancel_btn, 44);
  lv_obj_set_style_bg_color(measure_cancel_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  lv_obj_t *mcx_lbl = lv_label_create(measure_cancel_btn);
  lv_label_set_text(mcx_lbl, LV_SYMBOL_CLOSE "  Cancel");
  lv_obj_center(mcx_lbl);
  lv_obj_add_event_cb(measure_cancel_btn, &EstepsCalibrationPanel::_handle_callback, LV_EVENT_CLICKED, this);

  measure_apply_btn = lv_btn_create(measure_act);
  lv_obj_set_flex_grow(measure_apply_btn, 1);
  lv_obj_set_height(measure_apply_btn, 44);
  lv_obj_t *ap_lbl = lv_label_create(measure_apply_btn);
  lv_label_set_text(ap_lbl, LV_SYMBOL_OK "  Apply");
  lv_obj_center(ap_lbl);
  lv_obj_add_event_cb(measure_apply_btn, &EstepsCalibrationPanel::_handle_callback, LV_EVENT_CLICKED, this);

  // Own decimal numeric keyboard (the shared Numpad is integer-only), same
  // construction as skew_correction_panel.cpp's.
  kb = lv_keyboard_create(panel_cont);
  static const char *kb_map[] = {"1", "2", "3", "\n",
                                 "4", "5", "6", "\n",
                                 "7", "8", "9", "\n",
                                 LV_SYMBOL_BACKSPACE, "0", ".", LV_SYMBOL_OK, NULL};
  static const lv_btnmatrix_ctrl_t kb_ctrl[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, kb_map, kb_ctrl);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(kb, LV_PCT(100), LV_PCT(52));
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(kb, &EstepsCalibrationPanel::_handle_kb, LV_EVENT_ALL, this);

  // ---------- RESULT ----------
  fill(result_cont);
  lv_obj_set_style_pad_all(result_cont, 8, 0);
  lv_obj_set_style_pad_row(result_cont, 6, 0);

  lv_obj_t *result_title = lv_label_create(result_cont);
  lv_label_set_text(result_title, LV_SYMBOL_OK "  Result");
  lv_obj_set_style_text_font(result_title, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(result_title, lv_palette_main(LV_PALETTE_GREEN), 0);

  result_label = lv_label_create(result_cont);
  lv_label_set_long_mode(result_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(result_label, LV_PCT(94));
  lv_label_set_text(result_label, "");
  lv_obj_set_style_text_font(result_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(result_label, LV_TEXT_ALIGN_CENTER, 0);

  verify_again_btn = lv_btn_create(result_cont);
  lv_obj_set_size(verify_again_btn, 240, 40);
  lv_obj_t *va_lbl = lv_label_create(verify_again_btn);
  lv_label_set_text(va_lbl, LV_SYMBOL_REFRESH "  Verify Again");
  lv_obj_set_style_text_font(va_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(va_lbl);
  lv_obj_add_event_cb(verify_again_btn, &EstepsCalibrationPanel::_handle_callback, LV_EVENT_CLICKED, this);

  done_btn = lv_btn_create(result_cont);
  lv_obj_set_size(done_btn, 240, 36);
  lv_obj_set_style_bg_color(done_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  lv_obj_t *dn_lbl = lv_label_create(done_btn);
  lv_label_set_text(dn_lbl, LV_SYMBOL_OK "  Done");
  lv_obj_set_style_text_font(dn_lbl, &lv_font_montserrat_12, 0);
  lv_obj_center(dn_lbl);
  lv_obj_add_event_cb(done_btn, &EstepsCalibrationPanel::_handle_callback, LV_EVENT_CLICKED, this);

  discard_btn = lv_btn_create(result_cont);
  lv_obj_set_size(discard_btn, 240, 32);
  lv_obj_set_style_bg_color(discard_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  lv_obj_t *ds_lbl = lv_label_create(discard_btn);
  lv_label_set_text(ds_lbl, LV_SYMBOL_LEFT "  Discard, Keep Previous");
  lv_obj_set_style_text_font(ds_lbl, &lv_font_montserrat_12, 0);
  lv_obj_center(ds_lbl);
  lv_obj_add_event_cb(discard_btn, &EstepsCalibrationPanel::_handle_callback, LV_EVENT_CLICKED, this);

  show_stage(INTRO);

  ws.register_notify_update(this);
  ws.register_method_callback("notify_gcode_response", "EstepsCalibrationPanel",
    [this](json &d) { this->handle_gcode_response(d); });
}

EstepsCalibrationPanel::~EstepsCalibrationPanel() {
  if (panel_cont != NULL) {
    lv_obj_del(panel_cont);
    panel_cont = NULL;
  }
  ws.unregister_notify_update(this);
}

void EstepsCalibrationPanel::show_stage(Stage s) {
  stage = s;
  lv_obj_t *all[6] = { intro_cont, heating_cont, mark_cont, extruding_cont, measure_cont, result_cont };
  for (auto *c : all) lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
  switch (s) {
    case INTRO:     lv_obj_clear_flag(intro_cont, LV_OBJ_FLAG_HIDDEN); break;
    case HEATING:   lv_obj_clear_flag(heating_cont, LV_OBJ_FLAG_HIDDEN); break;
    case MARK:      lv_obj_clear_flag(mark_cont, LV_OBJ_FLAG_HIDDEN); break;
    case EXTRUDING: lv_obj_clear_flag(extruding_cont, LV_OBJ_FLAG_HIDDEN); break;
    case MEASURE:   lv_obj_clear_flag(measure_cont, LV_OBJ_FLAG_HIDDEN); break;
    case RESULT:    lv_obj_clear_flag(result_cont, LV_OBJ_FLAG_HIDDEN); break;
  }
}

void EstepsCalibrationPanel::foreground() {
  active = false;
  have_extruder_temp = false;
  have_target_temp = false;
  have_original_rd = false;
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_textarea_set_text(measure_ta, "");
  show_stage(INTRO);
  lv_obj_move_foreground(panel_cont);
}

void EstepsCalibrationPanel::start_calibration() {
  if (KUtils::is_printing()) {
    KUtils::notify_toast("Can't calibrate e-steps while printing.", 3000);
    return;
  }
  active = true;
  have_extruder_temp = false;
  have_target_temp = false;
  show_stage(HEATING);
  update_heating_status();
  ws.gcode_script("CALIBRATE_ESTEPS");
}

void EstepsCalibrationPanel::update_heating_status() {
  if (!have_extruder_temp) {
    lv_label_set_text(heating_status_label, "Heating the hotend...");
    return;
  }
  // The hotend may already be hotter than this calibration's target (e.g.
  // left over from printing), in which case M109 is correctly *cooling*
  // toward it, not heating - say so, rather than always claiming "heating"
  // regardless of which direction the temperature is actually moving.
  if (have_target_temp) {
    const double settle_band = 2.0;
    if (extruder_temp < target_temp - settle_band) {
      lv_label_set_text(heating_status_label,
        fmt::format("Heating to {:.0f}C...\nExtruder: {:.0f}C", target_temp, extruder_temp).c_str());
    } else if (extruder_temp > target_temp + settle_band) {
      lv_label_set_text(heating_status_label,
        fmt::format("Cooling to {:.0f}C (was hotter already)...\nExtruder: {:.0f}C", target_temp, extruder_temp).c_str());
    } else {
      lv_label_set_text(heating_status_label,
        fmt::format("At {:.0f}C, almost ready...\nExtruder: {:.0f}C", target_temp, extruder_temp).c_str());
    }
  } else {
    lv_label_set_text(heating_status_label,
      fmt::format("Heating the hotend...\nExtruder: {:.0f}C", extruder_temp).c_str());
  }
}

void EstepsCalibrationPanel::cancel_active_run() {
  active = false;
  ws.gcode_script("_CALIBRATE_ESTEPS_CANCEL");
  show_stage(INTRO);
  KUtils::notify_toast("E-Steps calibration cancelled.", 3000);
}

void EstepsCalibrationPanel::apply_measurement() {
  const char *txt = lv_textarea_get_text(measure_ta);
  std::string s = txt ? std::string(txt) : std::string();
  if (s.empty() || std::atof(s.c_str()) <= 0.0) {
    KUtils::notify_toast("Enter the measured gap (mm) first.", 3000);
    return;
  }
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  ws.gcode_script(fmt::format("CALIBRATE_ESTEPS_APPLY MEASURED={} MARK={:.1f} LENGTH={:.1f}",
                              s, mark_mm, length_mm));
}

void EstepsCalibrationPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  if (!active || stage != HEATING) return;

  auto et = j["/params/0/extruder/temperature"_json_pointer];
  if (et.is_number()) {
    extruder_temp = et.template get<double>();
    have_extruder_temp = true;
    update_heating_status();
  }
}

void EstepsCalibrationPanel::handle_gcode_response(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  if (!active) return;
  if (!j.contains("params")) return;

  for (auto &el : j["params"]) {
    if (!el.is_string()) continue;
    std::string line = el.template get<std::string>();

    // RESPOND TYPE=command prefixes every line with "// " before it reaches
    // us, so these must be substring searches (matching how the wizard's own
    // z_offset: parsing already works), not anchored-at-0 prefix checks.
    if (line.find("ESTEPS_HEATING") != std::string::npos) {
      // "ESTEPS_HEATING TARGET=200"
      try {
        auto p = line.find("TARGET=");
        target_temp = std::stod(line.substr(p + 7));
        have_target_temp = true;
        update_heating_status();
      } catch (const std::exception &ex) {
        spdlog::warn("EstepsCalibrationPanel: failed to parse ESTEPS_HEATING from '{}': {}", line, ex.what());
      }
      return;
    }

    if (line.find("ESTEPS_READY") != std::string::npos) {
      // "ESTEPS_READY MARK=120 LENGTH=100 SPEED=60"
      try {
        auto get_val = [&line](const std::string &key) {
          auto p = line.find(key + "=");
          return std::stod(line.substr(p + key.size() + 1));
        };
        mark_mm = get_val("MARK");
        length_mm = get_val("LENGTH");
        speed_mm_min = get_val("SPEED");
      } catch (const std::exception &ex) {
        spdlog::warn("EstepsCalibrationPanel: failed to parse ESTEPS_READY from '{}': {}", line, ex.what());
      }
      lv_label_set_text(mark_instr_label,
        fmt::format(
          "At temp. Measure {:.0f}mm up from where the filament feeds into "
          "the top of the extruder and wrap a small piece of tape around "
          "the filament there. Press Extrude once it's marked - you have "
          "about 2 minutes before this auto-cancels to keep the hotend "
          "from sitting hot too long.",
          mark_mm).c_str());
      show_stage(MARK);
      return;
    }

    if (line.find("ESTEPS_PROGRESS") != std::string::npos) {
      // "ESTEPS_PROGRESS FED=40 TOTAL=100"
      try {
        auto fed_p = line.find("FED=");
        auto total_p = line.find("TOTAL=");
        double fed = std::stod(line.substr(fed_p + 4));
        double total = std::stod(line.substr(total_p + 6));
        lv_label_set_text(extruding_status_label,
          fmt::format("Extruding... {:.0f}/{:.0f}mm\nPlease wait, do not touch the printer.", fed, total).c_str());
      } catch (const std::exception &ex) {
        spdlog::warn("EstepsCalibrationPanel: failed to parse ESTEPS_PROGRESS from '{}': {}", line, ex.what());
      }
      if (stage != EXTRUDING) show_stage(EXTRUDING);
      return;
    }

    if (line.find("ESTEPS_DONE") != std::string::npos) {
      lv_label_set_text(measure_instr_label,
        fmt::format(
          "Grab your ruler. Measure the gap between the extruder entry and "
          "your tape mark - with {:.0f}mm fed from a {:.0f}mm mark, a perfect "
          "result measures ~{:.0f}mm here. Enter what you actually measured below.",
          length_mm, mark_mm, mark_mm - length_mm).c_str());
      lv_textarea_set_text(measure_ta, "");
      show_stage(MEASURE);
      return;
    }

    if (line.find("ESTEPS_CANCELLED") != std::string::npos) {
      active = false;
      show_stage(INTRO);
      return;
    }

    if (line.find("ESTEPS_APPLIED") != std::string::npos) {
      // "ESTEPS_APPLIED OLD=1.2345 NEW=1.2400 COMMANDED=100.0 ACTUAL=99.5"
      try {
        auto get_val = [&line](const std::string &key) {
          auto p = line.find(key + "=");
          return std::stod(line.substr(p + key.size() + 1));
        };
        old_rotation_distance = get_val("OLD");
        new_rotation_distance = get_val("NEW");
        commanded_mm = get_val("COMMANDED");
        actual_mm = get_val("ACTUAL");
      } catch (const std::exception &ex) {
        spdlog::warn("EstepsCalibrationPanel: failed to parse ESTEPS_APPLIED from '{}': {}", line, ex.what());
        return;
      }
      if (!have_original_rd) {
        original_rotation_distance = old_rotation_distance;
        have_original_rd = true;
      }
      active = false;
      bool within_tolerance = std::fabs(actual_mm - commanded_mm) < 0.5;
      lv_label_set_text(result_label,
        fmt::format(
          "Commanded {:.1f}mm, got {:.1f}mm.\nrotation_distance: {:.4f} -> {:.4f}\n{}",
          commanded_mm, actual_mm, old_rotation_distance, new_rotation_distance,
          within_tolerance
            ? "Within 0.5mm - looks good, you can stop here."
            : "More than 0.5mm off target - verify again with the new value.").c_str());
      show_stage(RESULT);
      return;
    }
  }
}

void EstepsCalibrationPanel::handle_ta(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(kb, lv_event_get_target(e));
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(kb);
  }
}

void EstepsCalibrationPanel::handle_kb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
}

void EstepsCalibrationPanel::handle_callback(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t *btn = lv_event_get_current_target(e);

  if (btn == intro_back_btn) {
    lv_obj_move_background(panel_cont);
    return;
  }

  if (btn == start_btn) {
    start_calibration();
    return;
  }

  if (btn == heating_cancel_btn || btn == mark_cancel_btn ||
      btn == extruding_cancel_btn || btn == measure_cancel_btn) {
    cancel_active_run();
    return;
  }

  if (btn == extrude_btn) {
    active = true;
    show_stage(EXTRUDING);
    lv_label_set_text(extruding_status_label, "Extruding... 0mm\nPlease wait, do not touch the printer.");
    ws.gcode_script(fmt::format("_CALIBRATE_ESTEPS_EXTRUDE LENGTH={:.1f} SPEED={:.1f} MARK={:.1f}",
                                length_mm, speed_mm_min, mark_mm));
    return;
  }

  if (btn == measure_apply_btn) {
    active = true;
    apply_measurement();
    return;
  }

  if (btn == verify_again_btn) {
    start_calibration();
    return;
  }

  if (btn == done_btn) {
    ws.gcode_script("M104 S0");
    KUtils::notify_toast("E-Steps calibration saved.", 3000);
    lv_obj_move_background(panel_cont);
    return;
  }

  if (btn == discard_btn) {
    if (have_original_rd) {
      ws.gcode_script(fmt::format("SET_EXTRUDER_ROTATION_DISTANCE DISTANCE={:.4f}", original_rotation_distance));
    }
    ws.gcode_script("M104 S0");
    KUtils::notify_toast("Discarded - restored the previous rotation_distance.", 4000);
    lv_obj_move_background(panel_cont);
    return;
  }
}
