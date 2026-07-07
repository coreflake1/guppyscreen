#include "zoffset_panel.h"
#include "state.h"
#include "spdlog/spdlog.h"
#include "utils.h"

ZOffsetPanel::ZOffsetPanel(KWebSocketClient &websocket_client, std::mutex &l)
  : NotifyConsumer(l)
  , ws(websocket_client)
  , panel_cont(lv_obj_create(lv_scr_act()))
  , value_label(lv_label_create(panel_cont))
  , up_btn(lv_btn_create(panel_cont))
  , down_btn(lv_btn_create(panel_cont))
  , reset_btn(lv_btn_create(panel_cont))
  , auto_save_hint(lv_label_create(panel_cont))
  , back_btn(lv_btn_create(panel_cont))
  , step_selector(panel_cont, "Step (mm)", {"0.001", "0.005", "0.01", "0.025", "0.05", ""}, 2,
      &ZOffsetPanel::_handle_callback, this)
{
  lv_obj_move_background(panel_cont);
  lv_obj_set_size(panel_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(panel_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(panel_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(panel_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(panel_cont, 6, 0);
  lv_obj_set_style_pad_row(panel_cont, 6, 0);

  // big live readout
  lv_label_set_text(value_label, "0.000 mm");
  lv_obj_set_style_text_font(value_label, &lv_font_montserrat_20, 0);

  // raise / lower buttons: full-width, clear physical direction
  auto style_dir_btn = [](lv_obj_t *b, const char *txt) {
    lv_obj_set_width(b, LV_PCT(94));
    lv_obj_set_height(b, 42);
    lv_obj_set_style_bg_color(b, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl);
  };
  style_dir_btn(up_btn, LV_SYMBOL_UP "  Raise  (farther from bed)");
  style_dir_btn(down_btn, LV_SYMBOL_DOWN "  Lower  (closer to bed)");
  lv_obj_add_event_cb(up_btn, &ZOffsetPanel::_handle_callback, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(down_btn, &ZOffsetPanel::_handle_callback, LV_EVENT_CLICKED, this);

  lv_obj_set_width(step_selector.get_container(), LV_PCT(94));

  // the Helper Script's Save Z-Offset macros persist every adjustment to
  // variables.cfg automatically, so there is no Save button - just say so.
  lv_label_set_text(auto_save_hint, LV_SYMBOL_OK "  Adjustments are saved automatically");
  lv_obj_set_style_text_font(auto_save_hint, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(auto_save_hint, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);

  // bottom row: Reset | Back
  lv_obj_t *bottom = lv_obj_create(panel_cont);
  lv_obj_set_width(bottom, LV_PCT(94));
  lv_obj_set_height(bottom, LV_SIZE_CONTENT);
  lv_obj_clear_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(bottom, 0, 0);
  lv_obj_set_style_pad_column(bottom, 6, 0);
  lv_obj_set_style_border_width(bottom, 0, 0);
  lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_ROW);

  // the two buttons were created as children of panel_cont so the members stay
  // simple; move them into the bottom row now and size them evenly.
  lv_obj_set_parent(reset_btn, bottom);
  lv_obj_set_parent(back_btn, bottom);

  auto style_action_btn = [](lv_obj_t *b, const char *txt, lv_color_t col) {
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_height(b, 40);
    lv_obj_set_style_bg_color(b, col, LV_PART_MAIN);
    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, txt);
    lv_obj_center(lbl);
  };
  style_action_btn(reset_btn, LV_SYMBOL_REFRESH "  Reset", lv_palette_darken(LV_PALETTE_GREY, 2));
  style_action_btn(back_btn, LV_SYMBOL_LEFT "  Back", lv_palette_darken(LV_PALETTE_GREY, 2));
  lv_obj_add_event_cb(reset_btn, &ZOffsetPanel::_handle_callback, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(back_btn, &ZOffsetPanel::_handle_callback, LV_EVENT_CLICKED, this);

  ws.register_notify_update(this);
}

ZOffsetPanel::~ZOffsetPanel() {
  if (panel_cont != NULL) {
    lv_obj_del(panel_cont);
    panel_cont = NULL;
  }
  ws.unregister_notify_update(this);
}

double ZOffsetPanel::cur_offset() {
  auto v = State::get_instance()->get_data(
    "/printer_state/gcode_move/homing_origin/2"_json_pointer);
  return v.is_number() ? v.template get<double>() : 0.0;
}

void ZOffsetPanel::update_value(double z) {
  lv_label_set_text(value_label, fmt::format("{:.3f} mm", z).c_str());
}

void ZOffsetPanel::apply_step(bool raise) {
  // SET_GCODE_OFFSET ... MOVE=1 needs a homed axis: unhomed, Klipper still sets
  // the offset (the readout would change) but the move fails and the Helper
  // Script's save macro aborts before SAVE_VARIABLE - so it'd be applied live,
  // not moved, and never saved. Guard before sending so the three never drift.
  if (!KUtils::is_homed()) {
    KUtils::show_homing_prompt(ws);
    return;
  }

  const char *step = lv_btnmatrix_get_btn_text(step_selector.get_selector(),
    step_selector.get_selected_idx());

  // SET_GCODE_OFFSET operates in Klipper's normalized gcode frame, where
  // negative Z_ADJUST always means closer to the bed - independent of the
  // printer's physical stepper wiring. invert_z_direction only corrects the
  // on-screen jog arrows for manual G0 moves (see homing_panel.cpp) and must
  // not be applied here.
  const char *sign = raise ? "+" : "-";
  ws.gcode_script(fmt::format("SET_GCODE_OFFSET Z_ADJUST={}{} MOVE=1", sign, step));
}

void ZOffsetPanel::foreground() {
  update_value(cur_offset());
  lv_obj_move_foreground(panel_cont);
}

void ZOffsetPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  auto v = j["/params/0/gcode_move/homing_origin/2"_json_pointer];
  if (v.is_number()) update_value(v.template get<double>());
}

void ZOffsetPanel::handle_callback(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t *sel = lv_event_get_target(e);
    if (sel == step_selector.get_selector()) {
      step_selector.set_selected_idx(lv_btnmatrix_get_selected_btn(sel));
    }
    return;
  }

  if (code != LV_EVENT_CLICKED) return;
  lv_obj_t *btn = lv_event_get_current_target(e);

  if (btn == up_btn) {
    apply_step(true);
  } else if (btn == down_btn) {
    apply_step(false);
  } else if (btn == reset_btn) {
    if (!KUtils::is_homed()) {
      KUtils::show_homing_prompt(ws);
      return;
    }
    ws.gcode_script("SET_GCODE_OFFSET Z=0 MOVE=1");
  } else if (btn == back_btn) {
    lv_obj_move_background(panel_cont);
  }
}
