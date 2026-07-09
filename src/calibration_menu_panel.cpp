#include "calibration_menu_panel.h"
#include "utils.h"
#include "state.h"

#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

LV_IMG_DECLARE(arrow_up);
LV_IMG_DECLARE(arrow_down);
LV_IMG_DECLARE(home_z);
LV_IMG_DECLARE(extrude_img);
LV_IMG_DECLARE(layers_img);
LV_IMG_DECLARE(inputshaper_img);
LV_IMG_DECLARE(skew_img);
LV_IMG_DECLARE(motor_img);
LV_IMG_DECLARE(bedmesh_img);

// Icons are shown at native size (46x46, same material_46 assets the Tune-tab
// tiles use) - no lv_img_set_zoom/LV_IMG_SIZE_MODE_REAL this time. That
// combination was tried first to shrink rows further, but produced a visible
// double-image ghosting artifact across every icon it touched in the sim -
// looked like an LVGL zoom/real-size-mode interaction bug, not per-icon
// artwork, so it's not worth the risk for a size-only win. Icon is nullptr
// for entries that don't need one (none currently, kept for flexibility).
//
// One row per calibration flow: optional numbered badge + icon + label + a
// small chevron chip, styled like macro_item.cpp's rows (thin bottom-border
// separator, transparent background, no per-row background bar) instead of a
// filled button - reads as a plain list, and the chip borrows macro_item's
// compact "Play" button look so the tap affordance still stands out. Number
// is nullptr for the unordered utility section.
static lv_obj_t *make_row(lv_obj_t *parent, const void *icon, const char *label,
                           lv_event_cb_t cb, void *user_data, const char *number = nullptr) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_radius(row, 0, 0);
  lv_obj_set_style_pad_all(row, 6, 0);
  lv_obj_set_style_pad_column(row, 8, 0);
  lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_border_color(row, lv_palette_darken(LV_PALETTE_GREY, 3), 0);
  lv_obj_set_style_bg_color(row, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

  if (icon != nullptr) {
    lv_obj_t *img = lv_img_create(row);
    lv_img_set_src(img, icon);
  }

  if (number != nullptr) {
    // Between the 46px icon and the 14px row label in visual weight.
    lv_obj_t *badge = lv_obj_create(row);
    lv_obj_set_size(badge, 26, 26);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *num_lbl = lv_label_create(badge);
    lv_label_set_text(num_lbl, number);
    lv_obj_set_style_text_font(num_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(num_lbl, lv_color_white(), 0);
    lv_obj_center(num_lbl);
  }

  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, label);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl, lv_palette_darken(LV_PALETTE_GREY, 1), LV_STATE_DISABLED);
  lv_obj_set_flex_grow(lbl, 1);

  // compact chevron "chip" - same visual weight as macro_item.cpp's Play
  // button, but purely decorative here (the whole row is one tap target).
  lv_obj_t *chip = lv_obj_create(row);
  lv_obj_set_size(chip, 22, 22);
  lv_obj_set_style_radius(chip, 4, 0);
  lv_obj_set_style_bg_color(chip, lv_palette_darken(LV_PALETTE_GREY, 3), 0);
  lv_obj_set_style_border_width(chip, 0, 0);
  lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(chip, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *chev = lv_label_create(chip);
  lv_label_set_text(chev, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_font(chev, &lv_font_montserrat_10, 0);
  lv_obj_center(chev);

  lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, user_data);
  return row;
}

static lv_obj_t *make_section_label(lv_obj_t *parent, const char *text) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(lbl, lv_palette_main(LV_PALETTE_GREY), 0);
  return lbl;
}

CalibrationMenuPanel::CalibrationMenuPanel(KWebSocketClient &websocket_client, std::mutex &l,
                                            BedMeshPanel &bedmesh, InputShaperPanel &inputshaper,
                                            AxisTwistPanel &axis_twist, SkewCorrectionPanel &skew,
                                            TmcTunePanel &tmc_tune)
  : ws(websocket_client)
  , panel_cont(lv_obj_create(lv_scr_act()))
  , highlight_index(-1)
  , recalibration_wizard_panel(websocket_client, l)
  , bedmesh_panel(bedmesh)
  , inputshaper_panel(inputshaper)
  , axis_twist_panel(axis_twist)
  , skew_correction_panel(skew)
  , tmc_tune_panel(tmc_tune)
  , tmc_tune_available(false)
{
  lv_obj_move_background(panel_cont);
  lv_obj_set_size(panel_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(panel_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(panel_cont, 8, 0);
  lv_obj_set_style_border_width(panel_cont, 0, 0);
  // Grid, not flex, for the left-column/nav-column split: the nav column
  // needs to span the panel's *entire* height, level with "Calibration"
  // itself, not just the space below the title - so title+list share a left
  // column instead of the title sitting above a body row. LV_GRID_ALIGN_
  // STRETCH sizes both top-level cells to the panel's full height directly,
  // the same technique printertune_panel.cpp's own Tune-tab grid relies on.
  static lv_coord_t panel_col_dsc[] = {LV_GRID_FR(1), 48, LV_GRID_TEMPLATE_LAST};
  static lv_coord_t panel_row_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(panel_cont, panel_col_dsc, panel_row_dsc);

  lv_obj_t *left_col = lv_obj_create(panel_cont);
  lv_obj_set_grid_cell(left_col, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_style_pad_all(left_col, 0, 0);
  lv_obj_set_style_pad_row(left_col, 4, 0);
  lv_obj_set_style_border_width(left_col, 0, 0);
  lv_obj_set_flex_flow(left_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(left_col, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(left_col);
  lv_label_set_text(title, "Calibration");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

  list_cont = lv_obj_create(left_col);
  lv_obj_set_width(list_cont, LV_PCT(100));
  lv_obj_set_flex_grow(list_cont, 1);
  lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list_cont, 2, 0);
  lv_obj_set_style_border_width(list_cont, 0, 0);
  lv_obj_set_style_pad_all(list_cont, 0, 0);
  lv_obj_set_style_pad_right(list_cont, 6, 0);
  // Six numbered rows + a divider + a utility row never fit a 480x272 screen
  // at once - scroll rather than cram, matching console_panel's list convention.
  lv_obj_add_flag(list_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(list_cont, LV_SCROLLBAR_MODE_AUTO);

  make_section_label(list_cont, "IN ORDER");
  axis_twist_row  = make_row(list_cont, &layers_img,      "Axis Twist",              &CalibrationMenuPanel::_handle_callback, this, "1");
  wizard_row      = make_row(list_cont, &home_z,          "Auto Calibration",        &CalibrationMenuPanel::_handle_callback, this, "2");
  inputshaper_row = make_row(list_cont, &inputshaper_img, "Input Shaper",            &CalibrationMenuPanel::_handle_callback, this, "3");
  esteps_row      = make_row(list_cont, &extrude_img,     "E-Steps Calibration",     &CalibrationMenuPanel::_handle_callback, this, "4");
  skew_row        = make_row(list_cont, &skew_img,        "Skew Correction",         &CalibrationMenuPanel::_handle_callback, this, "5");
  tmc_tune_row    = make_row(list_cont, &motor_img,       "TMC Autotune (optional)", &CalibrationMenuPanel::_handle_callback, this, "6");

  make_section_label(list_cont, "OTHER");
  bedmesh_row = make_row(list_cont, &bedmesh_img, "Bed Mesh", &CalibrationMenuPanel::_handle_callback, this);

  rows = {axis_twist_row, wizard_row, inputshaper_row, esteps_row, skew_row, tmc_tune_row, bedmesh_row};

  // Right-side nav column - same construction as macros_panel.cpp's nav_cont
  // (Up/OK/Down), with a 4th Back button appended below them.
  nav_cont = lv_obj_create(panel_cont);
  lv_obj_set_grid_cell(nav_cont, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_style_pad_all(nav_cont, 2, 0);
  lv_obj_set_style_border_width(nav_cont, 0, 0);
  lv_obj_clear_flag(nav_cont, LV_OBJ_FLAG_SCROLLABLE);

  // Grid again, not flex_grow, for the 4 buttons themselves: flex_grow split
  // them unevenly (three normal, Back squeezed small) even with matching
  // style on all four - a 4-row FR(1) grid divides evenly, guaranteed, no
  // grow-distribution ambiguity to debug further.
  static lv_coord_t nav_row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t nav_col_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(nav_cont, nav_col_dsc, nav_row_dsc);
  lv_obj_set_style_pad_row(nav_cont, 4, 0);
  lv_obj_set_style_pad_column(nav_cont, 4, 0);

  auto style_nav_btn = [](lv_obj_t *btn, int row) {
    lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, row, 1);
    lv_obj_set_style_bg_color(btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  };

  up_btn = lv_btn_create(nav_cont);
  style_nav_btn(up_btn, 0);
  lv_obj_t *up_img = lv_img_create(up_btn);
  lv_img_set_src(up_img, &arrow_up);
  lv_obj_center(up_img);
  lv_obj_add_event_cb(up_btn, &CalibrationMenuPanel::_handle_nav, LV_EVENT_CLICKED, this);

  ok_btn = lv_btn_create(nav_cont);
  style_nav_btn(ok_btn, 1);
  lv_obj_t *ok_lbl = lv_label_create(ok_btn);
  lv_label_set_text(ok_lbl, LV_SYMBOL_OK);
  lv_obj_set_style_text_font(ok_lbl, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_obj_center(ok_lbl);
  lv_obj_add_event_cb(ok_btn, &CalibrationMenuPanel::_handle_nav, LV_EVENT_CLICKED, this);

  down_btn = lv_btn_create(nav_cont);
  style_nav_btn(down_btn, 2);
  lv_obj_t *down_img = lv_img_create(down_btn);
  lv_img_set_src(down_img, &arrow_down);
  lv_obj_center(down_img);
  lv_obj_add_event_cb(down_btn, &CalibrationMenuPanel::_handle_nav, LV_EVENT_CLICKED, this);

  back_btn = lv_btn_create(nav_cont);
  style_nav_btn(back_btn, 3);
  lv_obj_t *back_lbl = lv_label_create(back_btn);
  lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_center(back_lbl);
  lv_obj_add_event_cb(back_btn, &CalibrationMenuPanel::_handle_callback, LV_EVENT_CLICKED, this);
}

CalibrationMenuPanel::~CalibrationMenuPanel() {
  if (panel_cont != NULL) {
    lv_obj_del(panel_cont);
    panel_cont = NULL;
  }
}

void CalibrationMenuPanel::init(json &j) {
  State *s = State::get_instance();
  auto kp = s->get_data("/printer_info/klipper_path"_json_pointer);
  if (!kp.is_null()) {
    auto p = fs::path(kp.template get<std::string>()) / "klippy/extras/motor_database.cfg";
    if (fs::exists(p)) {
      tmc_tune_available = true;
      tmc_tune_panel.init(j, p);
    } else {
      tmc_tune_available = false;
    }
  }

  if (!tmc_tune_available) {
    lv_obj_add_state(tmc_tune_row, LV_STATE_DISABLED);
  }
}

void CalibrationMenuPanel::foreground() {
  lv_obj_move_foreground(panel_cont);
  set_highlight(0);
}

void CalibrationMenuPanel::set_highlight(int idx) {
  if (idx < 0 || idx >= (int)rows.size()) {
    return;
  }
  if (highlight_index >= 0 && highlight_index < (int)rows.size()) {
    lv_obj_set_style_bg_opa(rows[highlight_index], LV_OPA_TRANSP, 0);
  }
  highlight_index = idx;
  lv_obj_set_style_bg_opa(rows[highlight_index], LV_OPA_30, 0);
  lv_obj_scroll_to_view(rows[highlight_index], LV_ANIM_ON);
}

void CalibrationMenuPanel::move_highlight(int delta) {
  if (rows.empty()) {
    return;
  }
  int idx = highlight_index < 0 ? 0 : highlight_index + delta;
  if (idx < 0) {
    idx = 0;
  } else if (idx >= (int)rows.size()) {
    idx = (int)rows.size() - 1;
  }
  set_highlight(idx);
}

void CalibrationMenuPanel::handle_nav(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t *target = lv_event_get_target(e);

  if (target == up_btn) {
    move_highlight(-1);
  } else if (target == down_btn) {
    move_highlight(1);
  } else if (target == ok_btn) {
    if (highlight_index >= 0 && highlight_index < (int)rows.size()) {
      activate_row(rows[highlight_index]);
    }
  }
}

void CalibrationMenuPanel::handle_callback(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t *btn = lv_event_get_current_target(e);

  if (btn == back_btn) {
    lv_obj_move_background(panel_cont);
    return;
  }

  activate_row(btn);
}

void CalibrationMenuPanel::activate_row(lv_obj_t *row) {
  if (row == axis_twist_row) {
    // Detect via the config section: the v0.12.0 module has no get_status, so
    // it never shows up as a live printer object - only configfile.settings.
    if (!AxisTwistPanel::is_enabled()) {
      KUtils::notify_toast(
        "Axis Twist Compensation isn't enabled.\n"
        "Add [axis_twist_compensation] to printer.cfg.",
        4000);
      return;
    }
    // Calibration probes the bed; never mid-print.
    if (KUtils::is_printing()) { KUtils::notify_locked(); return; }
    axis_twist_panel.foreground();
    return;
  }

  if (row == wizard_row) {
    // The wizard inside probes the bed and ends in a SAVE_CONFIG restart;
    // never mid-print.
    if (KUtils::is_printing()) { KUtils::notify_locked(); return; }
    recalibration_wizard_panel.foreground();
    return;
  }

  if (row == inputshaper_row) {
    if (KUtils::is_printing()) { KUtils::notify_locked(); return; }
    inputshaper_panel.foreground();
    return;
  }

  if (row == esteps_row) {
    // CALIBRATE_ESTEPS is a self-contained gcode macro (Klipper action:prompt
    // protocol) - prompt_panel.cpp already renders its guided flow app-wide,
    // no dedicated panel needed here.
    if (KUtils::is_printing()) { KUtils::notify_locked(); return; }
    ws.gcode_script("CALIBRATE_ESTEPS");
    return;
  }

  if (row == skew_row) {
    // Detect via the config section (no get_status), like Axis Twist.
    if (!SkewCorrectionPanel::is_enabled()) {
      KUtils::notify_toast(
        "Skew correction isn't enabled.\n"
        "Add [skew_correction] to printer.cfg.",
        4000);
      return;
    }
    // Apply & Save runs SAVE_CONFIG (restarts Klipper); never mid-print.
    if (KUtils::is_printing()) { KUtils::notify_locked(); return; }
    skew_correction_panel.foreground();
    return;
  }

  if (row == tmc_tune_row) {
    if (!tmc_tune_available) return;  // row is LV_STATE_DISABLED, shouldn't fire, belt-and-suspenders
    if (KUtils::is_printing()) { KUtils::notify_locked(); return; }
    tmc_tune_panel.foreground();
    return;
  }

  if (row == bedmesh_row) {
    // Viewing the mesh mid-print is fine; the mutating actions (Calibrate /
    // Clear / Save) are blocked inside the panel itself.
    bedmesh_panel.foreground();
    return;
  }
}
