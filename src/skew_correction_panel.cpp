#include "skew_correction_panel.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <string>
#include <cstdlib>

// XY-plane measurement labels (A-C, B-D are the diagonals; A-D the reference).
static const char *FIELD_LABELS[3] = {"A-C", "B-D", "A-D"};

bool SkewCorrectionPanel::is_enabled() {
  // Detect via the RAW config sections, not settings: [skew_correction] is a
  // bare section whose module reads no config options, so Klipper never lists
  // it under configfile.settings (unlike axis_twist_compensation, which has
  // options). It IS always present under configfile.config when in printer.cfg.
  auto cfg = State::get_instance()->get_data(
    "/printer_state/configfile/config/skew_correction"_json_pointer);
  return !cfg.is_null();
}

SkewCorrectionPanel::SkewCorrectionPanel(KWebSocketClient &websocket_client)
  : ws(websocket_client)
  , panel_cont(lv_obj_create(lv_scr_act()))
{
  lv_obj_move_background(panel_cont);
  lv_obj_set_size(panel_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(panel_cont, 8, 0);
  lv_obj_set_flex_flow(panel_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(panel_cont, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(panel_cont, 6, 0);

  lv_obj_t *title = lv_label_create(panel_cont);
  lv_label_set_text(title, "Skew Correction");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

  lv_obj_t *body = lv_label_create(panel_cont);
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(body, LV_PCT(96));
  lv_label_set_text(body,
    "Print a skew calibration object, measure the three lengths (mm) with "
    "calipers, and enter them. A-C and B-D are the diagonals; A-D the edge.");
  lv_obj_set_style_text_font(body, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);

  for (int i = 0; i < 3; i++) {
    lv_obj_t *row = lv_obj_create(panel_cont);
    lv_obj_set_size(row, LV_PCT(96), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 2, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, FIELD_LABELS[i]);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);

    ta[i] = lv_textarea_create(row);
    lv_textarea_set_one_line(ta[i], true);
    lv_textarea_set_placeholder_text(ta[i], "mm");
    lv_obj_set_width(ta[i], LV_PCT(72));
    lv_obj_add_event_cb(ta[i], &SkewCorrectionPanel::_handle_ta, LV_EVENT_ALL, this);
  }

  lv_obj_t *btnrow = lv_obj_create(panel_cont);
  lv_obj_set_size(btnrow, LV_PCT(96), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(btnrow, 2, 0);
  lv_obj_set_style_border_width(btnrow, 0, 0);
  lv_obj_clear_flag(btnrow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(btnrow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btnrow, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  back_btn = lv_btn_create(btnrow);
  lv_obj_set_size(back_btn, 140, 44);
  lv_obj_set_style_bg_color(back_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  lv_obj_t *bl = lv_label_create(back_btn);
  lv_label_set_text(bl, LV_SYMBOL_LEFT "  Back");
  lv_obj_center(bl);
  lv_obj_add_event_cb(back_btn, &SkewCorrectionPanel::_handle_callback, LV_EVENT_CLICKED, this);

  apply_btn = lv_btn_create(btnrow);
  lv_obj_set_size(apply_btn, 190, 44);
  lv_obj_t *al = lv_label_create(apply_btn);
  lv_label_set_text(al, LV_SYMBOL_OK "  Apply & Save");
  lv_obj_center(al);
  lv_obj_add_event_cb(apply_btn, &SkewCorrectionPanel::_handle_callback, LV_EVENT_CLICKED, this);

  // Own decimal numeric keyboard (the shared Numpad is integer-only). Digits +
  // "." + backspace + OK. Overlays the bottom; hidden until a field is tapped.
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
  lv_obj_add_event_cb(kb, &SkewCorrectionPanel::_handle_kb, LV_EVENT_ALL, this);
}

SkewCorrectionPanel::~SkewCorrectionPanel() {
  if (panel_cont != NULL) {
    lv_obj_del(panel_cont);
    panel_cont = NULL;
  }
}

void SkewCorrectionPanel::foreground() {
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(panel_cont);
}

void SkewCorrectionPanel::handle_ta(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(kb, lv_event_get_target(e));
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(kb);
  }
}

void SkewCorrectionPanel::handle_kb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
}

void SkewCorrectionPanel::handle_callback(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  lv_obj_t *btn = lv_event_get_current_target(event);
  if (btn == back_btn) {
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(panel_cont);
  } else if (btn == apply_btn) {
    apply_and_save();
  }
}

void SkewCorrectionPanel::apply_and_save() {
  std::string s[3];
  for (int i = 0; i < 3; i++) {
    const char *txt = lv_textarea_get_text(ta[i]);
    s[i] = txt ? std::string(txt) : std::string();
    if (s[i].empty() || std::atof(s[i].c_str()) <= 0.0) {
      KUtils::notify_toast("Enter all three measurements (mm) first.", 4000);
      return;
    }
  }
  // SET_SKEW XY=<ac>,<bd>,<ad> then persist (restarts Klipper).
  ws.gcode_script(fmt::format("SET_SKEW XY={},{},{}\nSAVE_CONFIG",
                              s[0], s[1], s[2]));
  KUtils::notify_toast("Skew applied. Saving and restarting Klipper.", 4000);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(panel_cont);
}
