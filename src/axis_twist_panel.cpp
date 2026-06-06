#include "axis_twist_panel.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <cmath>
#include <string>

// jog step labels (also valid TESTZ Z= arguments)
static const char *JOG_LABELS[8] = {
  "-1", "-0.1", "-0.05", "-0.01",
  "+0.01", "+0.05", "+0.1", "+1",
};

bool AxisTwistPanel::is_enabled() {
  // The module has no get_status, so it never appears as a live object. Detect
  // it through the parsed config section instead.
  auto cfg = State::get_instance()->get_data(
    "/printer_state/configfile/settings/axis_twist_compensation"_json_pointer);
  return !cfg.is_null();
}

AxisTwistPanel::AxisTwistPanel(KWebSocketClient &websocket_client, std::mutex &l)
  : NotifyConsumer(l)
  , ws(websocket_client)
  , panel_cont(lv_obj_create(lv_scr_act()))
  , intro_cont(lv_obj_create(panel_cont))
  , probe_cont(lv_obj_create(panel_cont))
  , done_cont(lv_obj_create(panel_cont))
  , empty_cont(lv_obj_create(panel_cont))
  , stage(INTRO)
  , calibrating(false)
  , last_active(false)
  , last_z(0.0)
  , have_z(false)
  , accepted(0)
  , total(5)
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
  };

  // ---------- INTRO ----------
  fill(intro_cont);
  lv_obj_set_flex_align(intro_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(intro_cont, 10, 0);
  lv_obj_set_style_pad_row(intro_cont, 10, 0);

  lv_obj_t *title = lv_label_create(intro_cont);
  lv_label_set_text(title, "Axis Twist Compensation");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

  lv_obj_t *intro_body = lv_label_create(intro_cont);
  lv_label_set_long_mode(intro_body, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(intro_body, LV_PCT(94));
  lv_label_set_text(intro_body,
    "Corrects left-to-right first-layer error that bed mesh can't fix.\n\n"
    "You'll probe 5 points across X. At each one, lower the nozzle until a "
    "sheet of paper just drags, then Accept. Nozzle must be clean; no heating "
    "needed. Homing and clearing the mesh happen automatically.");
  lv_obj_set_style_text_font(intro_body, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(intro_body, LV_TEXT_ALIGN_CENTER, 0);

  start_btn = lv_btn_create(intro_cont);
  lv_obj_set_size(start_btn, 220, 46);
  lv_obj_set_style_bg_color(start_btn, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
  lv_obj_t *sb_lbl = lv_label_create(start_btn);
  lv_label_set_text(sb_lbl, LV_SYMBOL_OK "  Start Calibration");
  lv_obj_set_style_text_font(sb_lbl, &lv_font_montserrat_16, 0);
  lv_obj_center(sb_lbl);
  lv_obj_add_event_cb(start_btn, &AxisTwistPanel::_handle_callback, LV_EVENT_CLICKED, this);

  intro_back_btn = lv_btn_create(intro_cont);
  lv_obj_set_size(intro_back_btn, 140, 36);
  lv_obj_set_style_bg_color(intro_back_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  lv_obj_t *ib_lbl = lv_label_create(intro_back_btn);
  lv_label_set_text(ib_lbl, LV_SYMBOL_LEFT "  Back");
  lv_obj_center(ib_lbl);
  lv_obj_add_event_cb(intro_back_btn, &AxisTwistPanel::_handle_callback, LV_EVENT_CLICKED, this);

  // ---------- PROBING ----------
  fill(probe_cont);
  lv_obj_set_style_pad_all(probe_cont, 6, 0);
  lv_obj_set_style_pad_row(probe_cont, 5, 0);

  // header row: point (left) + Z (right)
  lv_obj_t *hdr = lv_obj_create(probe_cont);
  lv_obj_set_width(hdr, LV_PCT(100));
  lv_obj_set_height(hdr, LV_SIZE_CONTENT);
  lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(hdr, 0, 0);
  lv_obj_set_style_pad_all(hdr, 0, 0);
  lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  point_label = lv_label_create(hdr);
  lv_label_set_text(point_label, "Point 1 of 5");
  lv_obj_set_style_text_font(point_label, &lv_font_montserrat_16, 0);

  z_label = lv_label_create(hdr);
  lv_label_set_text(z_label, "Z —");
  lv_obj_set_style_text_font(z_label, &lv_font_montserrat_20, 0);

  instr_label = lv_label_create(probe_cont);
  lv_label_set_long_mode(instr_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(instr_label, LV_PCT(100));
  lv_label_set_text(instr_label, "Lower until a sheet of paper just drags, then Accept.");
  lv_obj_set_style_text_font(instr_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(instr_label, LV_TEXT_ALIGN_CENTER, 0);

  // two jog rows of 4 buttons (down steps, then up steps)
  auto make_jog_row = [&](int start_idx) {
    lv_obj_t *r = lv_obj_create(probe_cont);
    lv_obj_set_width(r, LV_PCT(100));
    lv_obj_set_height(r, LV_SIZE_CONTENT);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_style_pad_column(r, 5, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    for (int i = start_idx; i < start_idx + 4; i++) {
      lv_obj_t *b = lv_btn_create(r);
      lv_obj_set_flex_grow(b, 1);
      lv_obj_set_height(b, 40);
      lv_obj_set_style_bg_color(b, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
      lv_obj_t *lbl = lv_label_create(b);
      lv_label_set_text(lbl, JOG_LABELS[i]);
      lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
      lv_obj_center(lbl);
      lv_obj_add_event_cb(b, &AxisTwistPanel::_handle_callback, LV_EVENT_CLICKED, this);
      jogs[i] = { JOG_LABELS[i], b };
    }
  };
  make_jog_row(0);  // -1 .. -0.01
  make_jog_row(4);  // +0.01 .. +1

  // action row: Abort | Accept
  lv_obj_t *act = lv_obj_create(probe_cont);
  lv_obj_set_width(act, LV_PCT(100));
  lv_obj_set_height(act, LV_SIZE_CONTENT);
  lv_obj_clear_flag(act, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(act, 0, 0);
  lv_obj_set_style_pad_all(act, 0, 0);
  lv_obj_set_style_pad_column(act, 6, 0);
  lv_obj_set_flex_flow(act, LV_FLEX_FLOW_ROW);

  abort_btn = lv_btn_create(act);
  lv_obj_set_flex_grow(abort_btn, 1);
  lv_obj_set_height(abort_btn, 44);
  lv_obj_set_style_bg_color(abort_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  lv_obj_t *ab_lbl = lv_label_create(abort_btn);
  lv_label_set_text(ab_lbl, LV_SYMBOL_CLOSE "  Abort");
  lv_obj_set_style_text_font(ab_lbl, &lv_font_montserrat_16, 0);
  lv_obj_center(ab_lbl);
  lv_obj_add_event_cb(abort_btn, &AxisTwistPanel::_handle_callback, LV_EVENT_CLICKED, this);

  accept_btn = lv_btn_create(act);
  lv_obj_set_flex_grow(accept_btn, 1);
  lv_obj_set_height(accept_btn, 44);
  lv_obj_set_style_bg_color(accept_btn, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
  lv_obj_t *ac_lbl = lv_label_create(accept_btn);
  lv_label_set_text(ac_lbl, LV_SYMBOL_OK "  Accept");
  lv_obj_set_style_text_font(ac_lbl, &lv_font_montserrat_16, 0);
  lv_obj_center(ac_lbl);
  lv_obj_add_event_cb(accept_btn, &AxisTwistPanel::_handle_callback, LV_EVENT_CLICKED, this);

  // ---------- DONE ----------
  fill(done_cont);
  lv_obj_set_flex_align(done_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(done_cont, 10, 0);
  lv_obj_set_style_pad_row(done_cont, 10, 0);

  lv_obj_t *done_title = lv_label_create(done_cont);
  lv_label_set_text(done_title, LV_SYMBOL_OK "  Calibration complete");
  lv_obj_set_style_text_font(done_title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(done_title, lv_palette_main(LV_PALETTE_GREEN), 0);

  done_offsets_label = lv_label_create(done_cont);
  lv_label_set_long_mode(done_offsets_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(done_offsets_label, LV_PCT(94));
  lv_label_set_text(done_offsets_label,
    "Review the offsets, then Save to write them to printer.cfg.");
  lv_obj_set_style_text_font(done_offsets_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(done_offsets_label, LV_TEXT_ALIGN_CENTER, 0);

  save_btn = lv_btn_create(done_cont);
  lv_obj_set_size(save_btn, 240, 46);
  lv_obj_set_style_bg_color(save_btn, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
  lv_obj_t *sv_lbl = lv_label_create(save_btn);
  lv_label_set_text(sv_lbl, LV_SYMBOL_SAVE "  Save & Restart");
  lv_obj_set_style_text_font(sv_lbl, &lv_font_montserrat_16, 0);
  lv_obj_center(sv_lbl);
  lv_obj_add_event_cb(save_btn, &AxisTwistPanel::_handle_callback, LV_EVENT_CLICKED, this);

  done_back_btn = lv_btn_create(done_cont);
  lv_obj_set_size(done_back_btn, 140, 36);
  lv_obj_set_style_bg_color(done_back_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  lv_obj_t *db_lbl = lv_label_create(done_back_btn);
  lv_label_set_text(db_lbl, LV_SYMBOL_LEFT "  Done");
  lv_obj_center(db_lbl);
  lv_obj_add_event_cb(done_back_btn, &AxisTwistPanel::_handle_callback, LV_EVENT_CLICKED, this);

  // ---------- EMPTY (not configured) ----------
  fill(empty_cont);
  lv_obj_set_flex_align(empty_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(empty_cont, 16, 0);
  lv_obj_t *empty_lbl = lv_label_create(empty_cont);
  lv_label_set_text(empty_lbl,
    "Axis Twist Compensation is not enabled.\n\n"
    "Add an [axis_twist_compensation] section to\n"
    "printer.cfg and restart Klipper.");
  lv_obj_set_style_text_align(empty_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(empty_lbl, &lv_font_montserrat_16, 0);
  empty_back_btn = lv_btn_create(empty_cont);
  lv_obj_set_size(empty_back_btn, 140, 40);
  lv_obj_t *eb_lbl = lv_label_create(empty_back_btn);
  lv_label_set_text(eb_lbl, LV_SYMBOL_LEFT "  Back");
  lv_obj_center(eb_lbl);
  lv_obj_add_event_cb(empty_back_btn, &AxisTwistPanel::_handle_callback, LV_EVENT_CLICKED, this);

  show_stage(INTRO);

  ws.register_notify_update(this);
  ws.register_method_callback("notify_gcode_response", "AxisTwistPanel",
    [this](json &d) { this->handle_gcode_response(d); });
}

AxisTwistPanel::~AxisTwistPanel() {
  if (panel_cont != NULL) {
    lv_obj_del(panel_cont);
    panel_cont = NULL;
  }
  ws.unregister_notify_update(this);
}

void AxisTwistPanel::show_stage(Stage s) {
  stage = s;
  lv_obj_t *all[4] = { intro_cont, probe_cont, done_cont, empty_cont };
  for (auto *c : all) lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
  switch (s) {
    case INTRO:   lv_obj_clear_flag(intro_cont, LV_OBJ_FLAG_HIDDEN); break;
    case PROBING: lv_obj_clear_flag(probe_cont, LV_OBJ_FLAG_HIDDEN); break;
    case DONE:    lv_obj_clear_flag(done_cont, LV_OBJ_FLAG_HIDDEN); break;
  }
}

void AxisTwistPanel::foreground() {
  if (!is_enabled()) {
    lv_obj_t *all[4] = { intro_cont, probe_cont, done_cont, empty_cont };
    for (auto *c : all) lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(empty_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(panel_cont);
    return;
  }
  // fresh entry -> intro
  calibrating = false;
  accepted = 0;
  have_z = false;
  show_stage(INTRO);
  lv_obj_move_foreground(panel_cont);
}

void AxisTwistPanel::start_calibration() {
  if (KUtils::is_printing()) {
    KUtils::notify_toast("Can't calibrate while printing.", 3000);
    return;
  }
  calibrating = true;
  accepted = 0;
  have_z = false;
  last_active = false;
  lv_label_set_text(point_label, "Point 1 of 5");
  lv_label_set_text(z_label, "Z —");
  lv_label_set_text(instr_label, "Homing and probing the first point...");
  show_stage(PROBING);
  update_probe_ui();

  if (!KUtils::is_homed()) {
    ws.gcode_script("G28");
  }
  ws.gcode_script("BED_MESH_CLEAR");
  ws.gcode_script("AXIS_TWIST_COMPENSATION_CALIBRATE SAMPLE_COUNT=5");
}

void AxisTwistPanel::update_probe_ui() {
  // jog + accept only usable while the manual probe is actually active
  bool active = last_active;
  for (auto &jg : jogs) {
    if (active) lv_obj_clear_state(jg.btn, LV_STATE_DISABLED);
    else        lv_obj_add_state(jg.btn, LV_STATE_DISABLED);
  }
  if (active) lv_obj_clear_state(accept_btn, LV_STATE_DISABLED);
  else        lv_obj_add_state(accept_btn, LV_STATE_DISABLED);

  lv_label_set_text(point_label,
    fmt::format("Point {} of {}", std::min(accepted + 1, total), total).c_str());

  if (active) {
    if (have_z) lv_label_set_text(z_label, fmt::format("Z {:.3f}", last_z).c_str());
    else        lv_label_set_text(z_label, "Z —");
    lv_label_set_text(instr_label, "Lower until a sheet of paper just drags, then Accept.");
  } else {
    lv_label_set_text(instr_label, "Repositioning / probing next point...");
  }
}

void AxisTwistPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  if (!calibrating) return;

  auto mp = j["/params/0/manual_probe"_json_pointer];
  if (mp.is_null()) return;

  if (mp.contains("is_active") && mp["is_active"].is_boolean()) {
    last_active = mp["is_active"].template get<bool>();
  }
  if (mp.contains("z_position")) {
    auto zp = mp["z_position"];
    if (zp.is_number()) { last_z = zp.template get<double>(); have_z = true; }
    else { have_z = false; }
  }

  if (stage == PROBING) update_probe_ui();
}

void AxisTwistPanel::handle_gcode_response(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  if (!calibrating) return;
  if (!j.contains("params")) return;

  for (auto &el : j["params"]) {
    if (!el.is_string()) continue;
    std::string line = el.template get<std::string>();

    if (line.find("Calibration complete") != std::string::npos) {
      calibrating = false;
      // strip the leading "// " action prefix if present
      std::string body = line;
      auto p = body.find("offsets:");
      std::string shown = (p != std::string::npos) ? body.substr(p) : body;
      lv_label_set_text(done_offsets_label,
        fmt::format("{}\n\nSave writes this to printer.cfg (Klipper restarts).", shown).c_str());
      show_stage(DONE);
      return;
    }
    if (line.find("Calibration aborted") != std::string::npos) {
      calibrating = false;
      show_stage(INTRO);
      KUtils::notify_toast("Axis twist calibration aborted.", 3000);
      return;
    }
  }
}

void AxisTwistPanel::handle_callback(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t *btn = lv_event_get_current_target(e);

  if (btn == intro_back_btn || btn == empty_back_btn || btn == done_back_btn) {
    lv_obj_move_background(panel_cont);
    return;
  }

  if (btn == start_btn) {
    start_calibration();
    return;
  }

  if (btn == accept_btn) {
    accepted++;
    ws.gcode_script("ACCEPT");
    update_probe_ui();
    return;
  }

  if (btn == abort_btn) {
    ws.gcode_script("ABORT");
    calibrating = false;
    show_stage(INTRO);
    return;
  }

  if (btn == save_btn) {
    ws.gcode_script("SAVE_CONFIG");
    KUtils::notify_toast("Saving... Klipper will restart.", 3000);
    lv_obj_move_background(panel_cont);
    return;
  }

  for (auto &jg : jogs) {
    if (btn == jg.btn) {
      ws.gcode_script(std::string("TESTZ Z=") + jg.label);
      return;
    }
  }
}
