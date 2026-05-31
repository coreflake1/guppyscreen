#include "mini_print_status.h"
#include "spdlog/spdlog.h"

MiniPrintStatus::MiniPrintStatus(lv_obj_t *parent,
				 lv_event_cb_t cb,
				 void* user_data)
  : cont(lv_obj_create(parent))
  , progress_bar(lv_arc_create(cont))
  , thumb(lv_img_create(cont))
  , status_label(lv_label_create(cont))
  , chip(lv_obj_create(parent))
  , status("n/a")
  , eta("...")
{
  lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_color_t cur_bg = lv_obj_get_style_bg_color(cont, 0);
  lv_color_t mixed = lv_color_mix(lv_palette_main(LV_PALETTE_GREY),
				  cur_bg, LV_OPA_10);
  
  lv_obj_set_style_bg_color(cont, mixed, 0);  
  lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  auto scale = (double)lv_disp_get_physical_hor_res(NULL) / 800.0;

  
  lv_obj_set_size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_top(cont, 0, 0);
  lv_obj_set_style_pad_bottom(cont, 0, 0);
  
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
  
  lv_obj_set_style_border_width(cont, 2, 0);
  lv_obj_set_style_radius(cont, 4, 0);
  
  lv_obj_add_flag(cont, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(cont, LV_ALIGN_TOP_RIGHT, 0, -14 * scale);
  lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(cont, cb, LV_EVENT_CLICKED, user_data);

  lv_label_set_text(status_label, fmt::format("ETA: {}\n{}", eta, status).c_str());
  // Homing/Extrude are blocked while printing, so this overlay can afford a
  // modest bump up from the tiny default — but keep it balanced with the
  // thumbnail preview alongside it.
  lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);

  lv_arc_set_rotation(progress_bar, 270);
  // Small progress ring so the thumbnail preview can be the overlay's hero.
  lv_obj_set_size(progress_bar, 28, 28);
  lv_obj_set_style_arc_width(progress_bar, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_width(progress_bar, 4, LV_PART_INDICATOR);
  lv_arc_set_bg_angles(progress_bar, 0, 360);
  lv_obj_remove_style(progress_bar, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(progress_bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_center(progress_bar);

  lv_img_set_size_mode(thumb, LV_IMG_SIZE_MODE_REAL);
  // Hidden until a real thumbnail arrives (update_img); avoids the red
  // empty-image placeholder block.
  lv_obj_add_flag(thumb, LV_OBJ_FLAG_HIDDEN);

  // Tiny "Paused" pill shown in place of the full overlay while paused. It's a
  // separate floating object the home panel positions in a corner; tapping it
  // opens the full status panel (same callback as the overlay).
  lv_obj_add_flag(chip, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(chip, LV_OBJ_FLAG_FLOATING);
  lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_hor(chip, 8, 0);
  lv_obj_set_style_pad_ver(chip, 3, 0);
  lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(chip, 0, 0);
  // Match the themed (blue) primary used by the other buttons rather than
  // standing out — nothing else in the UI is amber.
  lv_obj_set_style_bg_color(chip, lv_theme_get_color_primary(chip), 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
  lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(chip, cb, LV_EVENT_CLICKED, user_data);

  lv_obj_t *chip_label = lv_label_create(chip);
  lv_label_set_text(chip_label, LV_SYMBOL_PAUSE " Paused");
  lv_obj_set_style_text_font(chip_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(chip_label, lv_color_white(), 0);
  lv_obj_center(chip_label);
}

MiniPrintStatus::~MiniPrintStatus() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
  if (chip != NULL) {
    lv_obj_del(chip);
    chip = NULL;
  }
}


void MiniPrintStatus::show() {
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(cont);
}

void MiniPrintStatus::hide() {
  lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(cont);
}

lv_obj_t *MiniPrintStatus::get_container() {
  return cont;
}

void MiniPrintStatus::show_chip() {
  lv_obj_clear_flag(chip, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(chip);
}

void MiniPrintStatus::hide_chip() {
  lv_obj_add_flag(chip, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *MiniPrintStatus::get_chip() {
  return chip;
}

void MiniPrintStatus::update_eta(std::string &eta_str) {
  eta = eta_str;
  lv_label_set_text(status_label, fmt::format("ETA: {}\n{}", eta, status).c_str());
}

void MiniPrintStatus::update_status(std::string &status_str) {
  status = status_str;
  lv_label_set_text(status_label, fmt::format("ETA: {}\n{}", eta, status).c_str());
}

void MiniPrintStatus::update_progress(int p) {
  lv_arc_set_value(progress_bar, p);
}

void MiniPrintStatus::update_img(const std::string &img_path, size_t twidth) {
  auto screen_width = lv_disp_get_physical_hor_res(NULL);
  // ~12% of screen width — the preview is the overlay's hero now that the
  // progress ring is small.
  uint32_t normalized_thumb_scale = ((0.12 * (double)screen_width) / (double)twidth) * 256;
  lv_img_set_zoom(thumb, normalized_thumb_scale);
  lv_obj_clear_flag(thumb, LV_OBJ_FLAG_HIDDEN);  // a real preview arrived — show it
  lv_img_set_src(thumb, img_path.c_str());
}

void MiniPrintStatus::reset() {
  lv_arc_set_value(progress_bar, 0);

  // Drop the preview and hide it until the next job's thumbnail loads (avoids
  // the old red empty-image placeholder block).
  lv_img_set_src(thumb, NULL);
  lv_obj_add_flag(thumb, LV_OBJ_FLAG_HIDDEN);

  eta = "...";
  status = "n/a";  
}

