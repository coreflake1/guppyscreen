#include "recalibration_wizard_panel.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <cmath>
#include <string>
#include <fstream>
#include <iterator>

// jog step labels (also valid TESTZ Z= arguments) - same set as Axis Twist's
// probing UI, since PROBE_CALIBRATE and AXIS_TWIST_COMPENSATION_CALIBRATE
// share the same underlying Klipper manual_probe helper and TESTZ/ACCEPT/
// ABORT commands.
static const char *JOG_LABELS[8] = {
  "-1", "-0.1", "-0.05", "-0.01",
  "+0.01", "+0.05", "+0.1", "+1",
};

// Load-sensor refine step (optional, post-baseline only - see header comment
// and project_prtouch_mechanism_research memory). GuppyScreen runs on the
// same device as Klipper, so backup/restore is plain local file I/O, no
// SSH/network round-trip needed.
static const char *CONFIG_PATH = "/usr/data/printer_data/config/printer.cfg";
static const char *CONFIG_BACKUP_PATH = "/usr/data/printer_data/config/printer.cfg.bak-wizard-sensor";
// Wipe+heat+probe+multi-point bed-tilt cross-check usually takes 1-3min, but
// confirmed live (2026-07-08) to sometimes take 4.5+min - a 4-minute timeout
// fired while the macro was still genuinely running, and because Klipper
// processes gcode serially, the resulting FIRMWARE_RESTART queued behind the
// still-active macro instead of running immediately: the macro's own self-save
// then landed *after* the safety restore, clobbering it. Set well above the
// observed worst case so this failure path stays a true "actually hung" signal.
static const uint32_t SENSOR_TIMEOUT_MS = 480000;
// Random-noise band from repeated same-session runs (see Test D): a gap this
// size or smaller just guides the recommendation shown to the user - it does
// NOT auto-apply either value, the user always picks (2026-07-08 decision).
static const double SENSOR_AGREEMENT_THRESHOLD_MM = 0.1;

RecalibrationWizardPanel::RecalibrationWizardPanel(KWebSocketClient &websocket_client, std::mutex &l)
  : NotifyConsumer(l)
  , ws(websocket_client)
  , panel_cont(lv_obj_create(lv_scr_act()))
  , intro_cont(lv_obj_create(panel_cont))
  , probe_cont(lv_obj_create(panel_cont))
  , mesh_cont(lv_obj_create(panel_cont))
  , done_cont(lv_obj_create(panel_cont))
  , sensor_cont(lv_obj_create(panel_cont))
  , sensor_choice_cont(lv_obj_create(panel_cont))
  , stage(INTRO)
  , active(false)
  , probe_active(false)
  , last_z(0.0)
  , have_z(false)
  , old_z_offset(0.0)
  , new_z_offset(0.0)
  , have_new_z_offset(false)
  , sensor_reading(0.0)
  , have_sensor_reading(false)
  , sensor_step_active(false)
  , config_backed_up(false)
  , sensor_timeout_timer(NULL)
  , sensor_extruder_temp(0.0)
  , sensor_bed_temp(0.0)
  , have_sensor_temps(false)
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
  lv_label_set_text(title, "Recalibration Wizard");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

  lv_obj_t *intro_body = lv_label_create(intro_cont);
  lv_label_set_long_mode(intro_body, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(intro_body, LV_PCT(94));
  lv_label_set_text(intro_body,
    "Just changed your bed, nozzle, or BLTouch mount? Re-establish a safe "
    "baseline before printing again.\n\n"
    "You'll do one paper-test Z-offset check, then a fresh bed mesh happens "
    "automatically. Nothing is saved until you review and confirm at the end.");
  lv_obj_set_style_text_font(intro_body, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(intro_body, LV_TEXT_ALIGN_CENTER, 0);

  start_btn = lv_btn_create(intro_cont);
  lv_obj_set_size(start_btn, 220, 46);
  lv_obj_t *sb_lbl = lv_label_create(start_btn);
  lv_label_set_text(sb_lbl, LV_SYMBOL_OK "  Start");
  lv_obj_set_style_text_font(sb_lbl, &lv_font_montserrat_16, 0);
  lv_obj_center(sb_lbl);
  lv_obj_add_event_cb(start_btn, &RecalibrationWizardPanel::_handle_callback, LV_EVENT_CLICKED, this);

  intro_back_btn = lv_btn_create(intro_cont);
  lv_obj_set_size(intro_back_btn, 140, 36);
  lv_obj_set_style_bg_color(intro_back_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  lv_obj_t *ib_lbl = lv_label_create(intro_back_btn);
  lv_label_set_text(ib_lbl, LV_SYMBOL_LEFT "  Back");
  lv_obj_center(ib_lbl);
  lv_obj_add_event_cb(intro_back_btn, &RecalibrationWizardPanel::_handle_callback, LV_EVENT_CLICKED, this);

  // ---------- ZOFFSET (PROBE_CALIBRATE) ----------
  fill(probe_cont);
  lv_obj_set_style_pad_all(probe_cont, 6, 0);
  lv_obj_set_style_pad_row(probe_cont, 5, 0);

  lv_obj_t *hdr = lv_obj_create(probe_cont);
  lv_obj_set_width(hdr, LV_PCT(100));
  lv_obj_set_height(hdr, LV_SIZE_CONTENT);
  lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(hdr, 0, 0);
  lv_obj_set_style_pad_all(hdr, 0, 0);
  lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *step_lbl = lv_label_create(hdr);
  lv_label_set_text(step_lbl, "Step 1 of 2: Z-Offset");
  lv_obj_set_style_text_font(step_lbl, &lv_font_montserrat_16, 0);

  z_label = lv_label_create(hdr);
  lv_label_set_text(z_label, "Z -");
  lv_obj_set_style_text_font(z_label, &lv_font_montserrat_20, 0);

  instr_label = lv_label_create(probe_cont);
  lv_label_set_long_mode(instr_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(instr_label, LV_PCT(100));
  lv_label_set_text(instr_label, "Homing and probing...");
  lv_obj_set_style_text_font(instr_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(instr_label, LV_TEXT_ALIGN_CENTER, 0);

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
      lv_obj_add_event_cb(b, &RecalibrationWizardPanel::_handle_callback, LV_EVENT_CLICKED, this);
      jogs[i] = { JOG_LABELS[i], b };
    }
  };
  make_jog_row(0);
  make_jog_row(4);

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
  lv_obj_add_event_cb(abort_btn, &RecalibrationWizardPanel::_handle_callback, LV_EVENT_CLICKED, this);

  accept_btn = lv_btn_create(act);
  lv_obj_set_flex_grow(accept_btn, 1);
  lv_obj_set_height(accept_btn, 44);
  lv_obj_t *ac_lbl = lv_label_create(accept_btn);
  lv_label_set_text(ac_lbl, LV_SYMBOL_OK "  Accept");
  lv_obj_set_style_text_font(ac_lbl, &lv_font_montserrat_16, 0);
  lv_obj_center(ac_lbl);
  lv_obj_add_event_cb(accept_btn, &RecalibrationWizardPanel::_handle_callback, LV_EVENT_CLICKED, this);

  // ---------- MESH (automatic) ----------
  fill(mesh_cont);
  lv_obj_set_flex_align(mesh_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(mesh_cont, 12, 0);

  lv_obj_t *mesh_title = lv_label_create(mesh_cont);
  lv_label_set_text(mesh_title, "Step 2 of 2: Bed Mesh");
  lv_obj_set_style_text_font(mesh_title, &lv_font_montserrat_16, 0);

  mesh_status_label = lv_label_create(mesh_cont);
  lv_label_set_long_mode(mesh_status_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(mesh_status_label, LV_PCT(90));
  lv_label_set_text(mesh_status_label, "Mapping bed... this takes about a minute, no input needed.");
  lv_obj_set_style_text_font(mesh_status_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(mesh_status_label, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *spinner = lv_spinner_create(mesh_cont, 1000, 60);
  lv_obj_set_size(spinner, 50, 50);

  // ---------- DONE ----------
  fill(done_cont);
  lv_obj_set_flex_align(done_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(done_cont, 6, 0);
  lv_obj_set_style_pad_row(done_cont, 5, 0);

  lv_obj_t *done_title = lv_label_create(done_cont);
  lv_label_set_text(done_title, LV_SYMBOL_OK "  Ready to review");
  lv_obj_set_style_text_font(done_title, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(done_title, lv_palette_main(LV_PALETTE_GREEN), 0);

  done_summary_label = lv_label_create(done_cont);
  lv_label_set_long_mode(done_summary_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(done_summary_label, LV_PCT(94));
  lv_label_set_text(done_summary_label, "");
  lv_obj_set_style_text_font(done_summary_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(done_summary_label, LV_TEXT_ALIGN_CENTER, 0);

  save_btn = lv_btn_create(done_cont);
  lv_obj_set_size(save_btn, 240, 40);
  lv_obj_t *sv_lbl = lv_label_create(save_btn);
  lv_label_set_text(sv_lbl, LV_SYMBOL_SAVE "  Save & Restart");
  lv_obj_set_style_text_font(sv_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(sv_lbl);
  lv_obj_add_event_cb(save_btn, &RecalibrationWizardPanel::_handle_callback, LV_EVENT_CLICKED, this);

  sensor_refine_btn = lv_btn_create(done_cont);
  lv_obj_set_size(sensor_refine_btn, 240, 36);
  lv_obj_set_style_bg_color(sensor_refine_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  lv_obj_t *sr_lbl = lv_label_create(sensor_refine_btn);
  lv_label_set_long_mode(sr_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(sr_lbl, 220);
  lv_label_set_text(sr_lbl, "Refine with Load Sensor (optional)");
  lv_obj_set_style_text_font(sr_lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(sr_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(sr_lbl);
  lv_obj_add_event_cb(sensor_refine_btn, &RecalibrationWizardPanel::_handle_callback, LV_EVENT_CLICKED, this);

  done_back_btn = lv_btn_create(done_cont);
  lv_obj_set_size(done_back_btn, 140, 32);
  lv_obj_set_style_bg_color(done_back_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  lv_obj_t *db_lbl = lv_label_create(done_back_btn);
  lv_label_set_text(db_lbl, LV_SYMBOL_LEFT "  Discard");
  lv_obj_set_style_text_font(db_lbl, &lv_font_montserrat_12, 0);
  lv_obj_center(db_lbl);
  lv_obj_add_event_cb(done_back_btn, &RecalibrationWizardPanel::_handle_callback, LV_EVENT_CLICKED, this);

  // ---------- SENSOR_RUNNING (Z_OFFSET_CALIBRATION, single reading, automatic) ----------
  fill(sensor_cont);
  lv_obj_set_flex_align(sensor_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(sensor_cont, 12, 0);

  lv_obj_t *sensor_title = lv_label_create(sensor_cont);
  lv_label_set_text(sensor_title, "Refining with Load Sensor");
  lv_obj_set_style_text_font(sensor_title, &lv_font_montserrat_16, 0);

  sensor_status_label = lv_label_create(sensor_cont);
  lv_label_set_long_mode(sensor_status_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(sensor_status_label, LV_PCT(90));
  lv_label_set_text(sensor_status_label, "Homing, wiping, and probing...\nDo not touch the printer.");
  lv_obj_set_style_text_font(sensor_status_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(sensor_status_label, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *sensor_spinner = lv_spinner_create(sensor_cont, 1000, 60);
  lv_obj_set_size(sensor_spinner, 50, 50);

  // ---------- SENSOR_CHOICE (paper vs. sensor average, user always picks) ----------
  fill(sensor_choice_cont);
  lv_obj_set_flex_align(sensor_choice_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(sensor_choice_cont, 8, 0);
  lv_obj_set_style_pad_row(sensor_choice_cont, 6, 0);

  lv_obj_t *choice_title = lv_label_create(sensor_choice_cont);
  lv_label_set_text(choice_title, LV_SYMBOL_WARNING "  Choose a Value to Keep");
  lv_obj_set_style_text_font(choice_title, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(choice_title, lv_palette_main(LV_PALETTE_ORANGE), 0);

  sensor_choice_label = lv_label_create(sensor_choice_cont);
  lv_label_set_long_mode(sensor_choice_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(sensor_choice_label, LV_PCT(94));
  lv_label_set_text(sensor_choice_label, "");
  lv_obj_set_style_text_font(sensor_choice_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_align(sensor_choice_label, LV_TEXT_ALIGN_CENTER, 0);

  keep_paper_btn = lv_btn_create(sensor_choice_cont);
  lv_obj_set_size(keep_paper_btn, 260, 40);
  lv_obj_t *kp_lbl = lv_label_create(keep_paper_btn);
  lv_label_set_text(kp_lbl, "Keep Paper Value");
  lv_obj_set_style_text_font(kp_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(kp_lbl);
  lv_obj_add_event_cb(keep_paper_btn, &RecalibrationWizardPanel::_handle_callback, LV_EVENT_CLICKED, this);

  use_sensor_btn = lv_btn_create(sensor_choice_cont);
  lv_obj_set_size(use_sensor_btn, 260, 42);
  lv_obj_t *us_lbl = lv_label_create(use_sensor_btn);
  lv_label_set_text(us_lbl, "Use Sensor Value");
  lv_obj_set_style_text_font(us_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(us_lbl);
  lv_obj_add_event_cb(use_sensor_btn, &RecalibrationWizardPanel::_handle_callback, LV_EVENT_CLICKED, this);

  discard_all_btn = lv_btn_create(sensor_choice_cont);
  lv_obj_set_size(discard_all_btn, 260, 36);
  lv_obj_set_style_bg_color(discard_all_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  lv_obj_t *da_lbl = lv_label_create(discard_all_btn);
  lv_label_set_text(da_lbl, "Discard Both, Keep Original");
  lv_obj_set_style_text_font(da_lbl, &lv_font_montserrat_12, 0);
  lv_obj_center(da_lbl);
  lv_obj_add_event_cb(discard_all_btn, &RecalibrationWizardPanel::_handle_callback, LV_EVENT_CLICKED, this);

  show_stage(INTRO);

  ws.register_notify_update(this);
  ws.register_method_callback("notify_gcode_response", "RecalibrationWizardPanel",
    [this](json &d) { this->handle_gcode_response(d); });
}

RecalibrationWizardPanel::~RecalibrationWizardPanel() {
  disarm_sensor_timeout();
  if (panel_cont != NULL) {
    lv_obj_del(panel_cont);
    panel_cont = NULL;
  }
  ws.unregister_notify_update(this);
}

void RecalibrationWizardPanel::show_stage(Stage s) {
  spdlog::info("RecalibrationWizardPanel: show_stage {} -> {}", (int)stage, (int)s);
  stage = s;
  lv_obj_t *all[6] = { intro_cont, probe_cont, mesh_cont, done_cont, sensor_cont, sensor_choice_cont };
  for (auto *c : all) lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
  switch (s) {
    case INTRO:  lv_obj_clear_flag(intro_cont, LV_OBJ_FLAG_HIDDEN); break;
    case ZOFFSET: lv_obj_clear_flag(probe_cont, LV_OBJ_FLAG_HIDDEN); break;
    case MESH:   lv_obj_clear_flag(mesh_cont, LV_OBJ_FLAG_HIDDEN); break;
    case DONE:   lv_obj_clear_flag(done_cont, LV_OBJ_FLAG_HIDDEN); break;
    case SENSOR_RUNNING: lv_obj_clear_flag(sensor_cont, LV_OBJ_FLAG_HIDDEN); break;
    case SENSOR_CHOICE:  lv_obj_clear_flag(sensor_choice_cont, LV_OBJ_FLAG_HIDDEN); break;
  }
}

void RecalibrationWizardPanel::foreground() {
  active = false;
  probe_active = false;
  have_z = false;
  have_new_z_offset = false;
  sensor_step_active = false;
  config_backed_up = false;
  have_sensor_reading = false;
  disarm_sensor_timeout();
  show_stage(INTRO);
  lv_obj_move_foreground(panel_cont);
}

void RecalibrationWizardPanel::start_wizard() {
  if (KUtils::is_printing()) {
    KUtils::notify_toast("Can't recalibrate while printing.", 3000);
    return;
  }

  auto z = State::get_instance()->get_data(
    "/printer_state/configfile/settings/bltouch/z_offset"_json_pointer);
  old_z_offset = z.is_number() ? z.template get<double>() : 0.0;
  have_new_z_offset = false;

  active = true;
  probe_active = false;
  have_z = false;
  lv_label_set_text(z_label, "Z -");
  lv_label_set_text(instr_label, "Homing and probing...");
  show_stage(ZOFFSET);
  update_probe_ui();
  // No auto-cancel timer here (2026-07-08 decision) - this step is manual and
  // already has its own Abort button, unlike the automated sensor-refine
  // step below which does need a watchdog since nothing prompts the user.

  // Always home unconditionally, even if Klipper currently reports already
  // homed - this wizard's whole purpose is "hardware just changed," so a
  // cached homed_axes from before that change is exactly what shouldn't be
  // trusted here (2026-07-08 fix: a stale "already homed" skipped G28 on a
  // real run, which is the one case this wizard exists to protect against).
  ws.gcode_script("G28");
  ws.gcode_script("PROBE_CALIBRATE");
}

void RecalibrationWizardPanel::update_probe_ui() {
  bool en = probe_active;
  for (auto &jg : jogs) {
    if (en) lv_obj_clear_state(jg.btn, LV_STATE_DISABLED);
    else    lv_obj_add_state(jg.btn, LV_STATE_DISABLED);
  }
  if (en) lv_obj_clear_state(accept_btn, LV_STATE_DISABLED);
  else    lv_obj_add_state(accept_btn, LV_STATE_DISABLED);

  if (en) {
    if (have_z) lv_label_set_text(z_label, fmt::format("Z {:.3f}", last_z).c_str());
    else        lv_label_set_text(z_label, "Z -");
    lv_label_set_text(instr_label, "Slide paper under the nozzle. Jog down until it just drags, then Accept.");
  } else {
    lv_label_set_text(instr_label, "Homing and probing...");
  }
}

void RecalibrationWizardPanel::start_mesh() {
  show_stage(MESH);
  lv_label_set_text(mesh_status_label, "Mapping bed... this takes about a minute, no input needed.");
  ws.gcode_script("BED_MESH_CALIBRATE");
}

bool RecalibrationWizardPanel::backup_printer_cfg() {
  std::ifstream src(CONFIG_PATH, std::ios::binary);
  if (!src) {
    spdlog::warn("RecalibrationWizardPanel: could not open {} for backup", CONFIG_PATH);
    config_backed_up = false;
    return false;
  }
  std::ofstream dst(CONFIG_BACKUP_PATH, std::ios::binary | std::ios::trunc);
  if (!dst) {
    spdlog::warn("RecalibrationWizardPanel: could not open {} for backup write", CONFIG_BACKUP_PATH);
    config_backed_up = false;
    return false;
  }
  dst << src.rdbuf();
  dst.flush();
  config_backed_up = dst.good();
  return config_backed_up;
}

bool RecalibrationWizardPanel::restore_printer_cfg_backup() {
  if (!config_backed_up) return false;
  std::ifstream src(CONFIG_BACKUP_PATH, std::ios::binary);
  if (!src) {
    spdlog::warn("RecalibrationWizardPanel: could not open {} to restore", CONFIG_BACKUP_PATH);
    return false;
  }
  std::ofstream dst(CONFIG_PATH, std::ios::binary | std::ios::trunc);
  if (!dst) {
    spdlog::warn("RecalibrationWizardPanel: could not open {} to restore into", CONFIG_PATH);
    return false;
  }
  dst << src.rdbuf();
  dst.flush();
  return dst.good();
}

bool RecalibrationWizardPanel::patch_z_offset_value(double value) {
  std::ifstream in(CONFIG_PATH, std::ios::binary);
  if (!in) {
    spdlog::warn("RecalibrationWizardPanel: could not open {} to patch z_offset", CONFIG_PATH);
    return false;
  }
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();

  static const std::string marker = "#*# z_offset = ";
  auto pos = content.find(marker);
  if (pos == std::string::npos) {
    spdlog::warn("RecalibrationWizardPanel: could not find '{}' in {}", marker, CONFIG_PATH);
    return false;
  }
  auto value_start = pos + marker.size();
  auto line_end = content.find('\n', value_start);
  if (line_end == std::string::npos) line_end = content.size();
  content.replace(value_start, line_end - value_start, fmt::format("{:.3f}", value));

  std::ofstream out(CONFIG_PATH, std::ios::binary | std::ios::trunc);
  if (!out) {
    spdlog::warn("RecalibrationWizardPanel: could not open {} to write patched z_offset", CONFIG_PATH);
    return false;
  }
  out << content;
  out.flush();
  return out.good();
}

void RecalibrationWizardPanel::arm_sensor_timeout() {
  disarm_sensor_timeout();
  sensor_timeout_timer = lv_timer_create(&RecalibrationWizardPanel::sensor_timeout_cb, SENSOR_TIMEOUT_MS, this);
  lv_timer_set_repeat_count(sensor_timeout_timer, 1);
}

void RecalibrationWizardPanel::disarm_sensor_timeout() {
  if (sensor_timeout_timer != NULL) {
    lv_timer_del(sensor_timeout_timer);
    sensor_timeout_timer = NULL;
  }
}

void RecalibrationWizardPanel::sensor_timeout_cb(lv_timer_t *t) {
  auto *self = (RecalibrationWizardPanel *)t->user_data;
  self->sensor_timeout_timer = NULL;
  if (!self->sensor_step_active) return;
  self->sensor_refine_failed("no response from printer within 4 minutes");
}

void RecalibrationWizardPanel::sensor_refine_failed(const char *reason) {
  disarm_sensor_timeout();
  active = false;
  sensor_step_active = false;
  if (config_backed_up) {
    restore_printer_cfg_backup();
    ws.gcode_script("FIRMWARE_RESTART");
  }
  show_stage(INTRO);
  KUtils::notify_toast(
    fmt::format("Sensor refine failed ({}). Restored previous settings - please redo from Start.", reason).c_str(),
    6000);
}

void RecalibrationWizardPanel::update_sensor_status_text() {
  std::string temp_str = have_sensor_temps
    ? fmt::format("\nNozzle: {:.0f}C   Bed: {:.0f}C", sensor_extruder_temp, sensor_bed_temp)
    : "";
  lv_label_set_text(sensor_status_label,
    fmt::format("Homing, wiping, and probing...{}\nDo not touch the printer.", temp_str).c_str());
}

void RecalibrationWizardPanel::start_sensor_refine() {
  if (KUtils::is_printing()) {
    KUtils::notify_toast("Can't recalibrate while printing.", 3000);
    return;
  }
  if (!backup_printer_cfg()) {
    KUtils::notify_toast("Could not back up printer.cfg - skipping sensor refine for safety.", 4000);
    return;
  }

  have_sensor_reading = false;
  have_sensor_temps = false;
  sensor_step_active = true;
  active = true;
  show_stage(SENSOR_RUNNING);
  update_sensor_status_text();
  arm_sensor_timeout();

  // Always home unconditionally too (see start_wizard() - same "don't trust
  // a cached homed flag in this wizard" reasoning).
  ws.gcode_script("G28");
  ws.gcode_script("CRTENSE_NOZZLE_CLEAR");
  ws.gcode_script("Z_OFFSET_CALIBRATION");
}

void RecalibrationWizardPanel::finish_sensor_reading() {
  disarm_sensor_timeout();
  active = false;
  sensor_step_active = false;

  double diff = std::fabs(sensor_reading - new_z_offset);
  bool sensor_recommended = diff <= SENSOR_AGREEMENT_THRESHOLD_MM;

  lv_label_set_text(sensor_choice_label,
    fmt::format(
      "Paper test: {:.3f}mm   |   Sensor: {:.3f}mm   |   Diff: {:.3f}mm\n{}",
      new_z_offset, sensor_reading, diff,
      sensor_recommended
        ? "Close agreement - sensor reading (green) is usually the more precise pick."
        : "Bigger gap than normal (0.1mm) - paper test (green) is the safer pick.").c_str());

  // Recommended button: clear any local override so it falls back to the
  // theme's own live bg_color_primary (tracks theme changes with no
  // restart). The other button: explicit grey override.
  if (sensor_recommended) {
    lv_obj_set_style_bg_color(keep_paper_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
    lv_obj_remove_local_style_prop(use_sensor_btn, LV_STYLE_BG_COLOR, LV_PART_MAIN);
  } else {
    lv_obj_remove_local_style_prop(keep_paper_btn, LV_STYLE_BG_COLOR, LV_PART_MAIN);
    lv_obj_set_style_bg_color(use_sensor_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  }

  show_stage(SENSOR_CHOICE);
}

void RecalibrationWizardPanel::apply_sensor_choice(bool use_sensor) {
  double chosen = use_sensor ? sensor_reading : new_z_offset;
  if (!patch_z_offset_value(chosen)) {
    KUtils::notify_toast("Failed to write z_offset to printer.cfg - please check manually before printing.", 6000);
    lv_obj_move_background(panel_cont);
    return;
  }
  ws.gcode_script("FIRMWARE_RESTART");
  KUtils::notify_toast(fmt::format("Saving {:.3f}mm... Klipper will restart.", chosen).c_str(), 3000);
  lv_obj_move_background(panel_cont);
}

void RecalibrationWizardPanel::apply_discard_all() {
  // Reverts to whatever printer.cfg had before this whole wizard run started -
  // both the paper-test z_offset/mesh and the sensor's readings are abandoned.
  if (!restore_printer_cfg_backup()) {
    KUtils::notify_toast("No backup available to restore - printer.cfg left as the sensor last saved it.", 6000);
    lv_obj_move_background(panel_cont);
    return;
  }
  ws.gcode_script("FIRMWARE_RESTART");
  KUtils::notify_toast("Discarding this recalibration... Klipper will restart with your original settings.", 4000);
  lv_obj_move_background(panel_cont);
}

void RecalibrationWizardPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  if (!active) return;

  auto mp = j["/params/0/manual_probe"_json_pointer];
  if (!mp.is_null()) {
    if (mp.contains("is_active") && mp["is_active"].is_boolean()) {
      probe_active = mp["is_active"].template get<bool>();
    }
    if (mp.contains("z_position")) {
      auto zp = mp["z_position"];
      if (zp.is_number()) { last_z = zp.template get<double>(); have_z = true; }
      else { have_z = false; }
    }
    if (stage == ZOFFSET) update_probe_ui();
  }

  if (stage == SENSOR_RUNNING) {
    bool temps_changed = false;
    auto et = j["/params/0/extruder/temperature"_json_pointer];
    if (et.is_number()) { sensor_extruder_temp = et.template get<double>(); temps_changed = true; }
    auto bt = j["/params/0/heater_bed/temperature"_json_pointer];
    if (bt.is_number()) { sensor_bed_temp = bt.template get<double>(); temps_changed = true; }
    if (temps_changed) {
      have_sensor_temps = true;
      update_sensor_status_text();
    }
  }
}

void RecalibrationWizardPanel::handle_gcode_response(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  if (!active) return;
  if (!j.contains("params")) return;

  for (auto &el : j["params"]) {
    if (!el.is_string()) continue;
    std::string line = el.template get<std::string>();
    spdlog::info("RecalibrationWizardPanel: [stage={}] gcode response: {}", (int)stage, line);

    if (stage == ZOFFSET) {
      auto p = line.find("z_offset:");
      if (p != std::string::npos) {
        try {
          new_z_offset = std::stod(line.substr(p + std::string("z_offset:").size()));
          have_new_z_offset = true;
        } catch (const std::exception &ex) {
          spdlog::warn("RecalibrationWizardPanel: failed to parse z_offset from '{}': {}", line, ex.what());
        }
        probe_active = false;
        start_mesh();
        return;
      }
      if (line.find("Calibration aborted") != std::string::npos) {
        active = false;
        show_stage(INTRO);
        KUtils::notify_toast("Recalibration aborted.", 3000);
        return;
      }
    }

    if (stage == MESH && line.find("Mesh Bed Leveling Complete") != std::string::npos) {
      active = false;
      lv_label_set_text(done_summary_label,
        fmt::format(
          "Z-offset: {:.3f}mm -> {:.3f}mm\nBed mesh: updated. Nothing saved yet.\n"
          "Save & Restart now, OR Refine first (has its own Save - don't press both).",
          old_z_offset, new_z_offset).c_str());
      show_stage(DONE);
      return;
    }

    if (stage == SENSOR_RUNNING) {
      // Any PR_ERR_CODE (e.g. PRES_LOST_RUN_DATA - see
      // project_prtouch_mechanism_research memory) means the sensor's own
      // firmware hit its zero-retry failure path. Bail safely rather than
      // trying to continue or interpret partial data.
      if (line.find("PR_ERR_CODE") != std::string::npos) {
        sensor_refine_failed(line.c_str());
        return;
      }
      auto p = line.find("z_offset:");
      if (p != std::string::npos) {
        try {
          sensor_reading = std::stod(line.substr(p + std::string("z_offset:").size()));
          have_sensor_reading = true;
        } catch (const std::exception &ex) {
          spdlog::warn("RecalibrationWizardPanel: failed to parse sensor z_offset from '{}': {}", line, ex.what());
        }
        finish_sensor_reading();
        return;
      }
    }
  }
}

void RecalibrationWizardPanel::handle_callback(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t *btn = lv_event_get_current_target(e);
  spdlog::info("RecalibrationWizardPanel: button pressed, stage={}, btn={}"
    " (intro_back={} start={} accept={} abort={} save={} sensor_refine={} done_back={}"
    " keep_paper={} use_sensor={} discard_all={})",
    (int)stage, (void*)btn,
    btn == intro_back_btn, btn == start_btn, btn == accept_btn, btn == abort_btn,
    btn == save_btn, btn == sensor_refine_btn, btn == done_back_btn,
    btn == keep_paper_btn, btn == use_sensor_btn, btn == discard_all_btn);

  if (btn == intro_back_btn) {
    lv_obj_move_background(panel_cont);
    return;
  }

  if (btn == done_back_btn) {
    // Discard: nothing was ever saved (SAVE_CONFIG only fires from the Save
    // button), so leaving just needs a restart-free bail - re-home clears any
    // live-only offset the manual probe applied so the printer isn't left
    // trusting an unreviewed number.
    ws.gcode_script("G28");
    lv_obj_move_background(panel_cont);
    return;
  }

  if (btn == start_btn) {
    start_wizard();
    return;
  }

  if (btn == accept_btn) {
    ws.gcode_script("ACCEPT");
    return;
  }

  if (btn == abort_btn) {
    ws.gcode_script("ABORT");
    active = false;
    show_stage(INTRO);
    return;
  }

  if (btn == save_btn) {
    ws.gcode_script("SAVE_CONFIG");
    KUtils::notify_toast("Saving... Klipper will restart.", 3000);
    lv_obj_move_background(panel_cont);
    return;
  }

  if (btn == sensor_refine_btn) {
    start_sensor_refine();
    return;
  }

  if (btn == keep_paper_btn) {
    apply_sensor_choice(false);
    return;
  }

  if (btn == use_sensor_btn) {
    apply_sensor_choice(true);
    return;
  }

  if (btn == discard_all_btn) {
    apply_discard_all();
    return;
  }

  for (auto &jg : jogs) {
    if (btn == jg.btn) {
      ws.gcode_script(std::string("TESTZ Z=") + jg.label);
      return;
    }
  }
}
