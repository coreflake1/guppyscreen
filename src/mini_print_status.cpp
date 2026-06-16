#include "mini_print_status.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#ifdef SIMULATOR
LV_IMG_DECLARE(print);  // stand-in preview for the sim (no gcode thumbnail there)
#endif

// remaining seconds -> hh:mm:ss, dropping the hours field when it's zero
static std::string fmt_hms(int64_t s) {
  if (s < 0) {
    s = 0;
  }
  int h = (int)(s / 3600);
  int m = (int)((s % 3600) / 60);
  int sec = (int)(s % 60);
  if (h > 0) {
    return fmt::format("{}:{:02d}:{:02d}", h, m, sec);
  }
  return fmt::format("{:02d}:{:02d}", m, sec);
}

MiniPrintStatus::MiniPrintStatus(lv_obj_t *parent,
				 lv_event_cb_t cb,
				 void* user_data)
  : cont(lv_obj_create(parent))
  , thumb(lv_img_create(cont))
  , info_cont(lv_obj_create(cont))
  , time_label(lv_label_create(info_cont))
  , pct_label(lv_label_create(info_cont))
  , chip(lv_obj_create(parent))
  , status("n/a")
  , eta("--:--")
  , progress(0)
{
  lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
  lv_color_t cur_bg = lv_obj_get_style_bg_color(cont, 0);
  lv_color_t mixed = lv_color_mix(lv_palette_main(LV_PALETTE_GREY), cur_bg, LV_OPA_10);

  lv_obj_set_style_bg_color(cont, mixed, 0);
  lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  auto scale = (double)lv_disp_get_physical_hor_res(NULL) / 800.0;

  lv_obj_set_size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(cont, 4, 0);
  lv_obj_set_style_pad_column(cont, 6, 0);
  lv_obj_set_style_border_width(cont, 2, 0);
  lv_obj_set_style_radius(cont, 4, 0);

  lv_obj_add_flag(cont, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(cont, LV_ALIGN_TOP_RIGHT, 0, -14 * scale);
  lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(cont, cb, LV_EVENT_CLICKED, user_data);

  // Hidden until a real thumbnail arrives (update_img); avoids the red
  // empty-image placeholder block.
  lv_img_set_size_mode(thumb, LV_IMG_SIZE_MODE_REAL);
  lv_obj_add_flag(thumb, LV_OBJ_FLAG_HIDDEN);

  // info column: remaining time (hh:mm:ss) prominent, progress % under it.
  // Fixed width so the overlay's right edge stays static as the ETA/percent digit widths
  // change each second (it was SIZE_CONTENT, so the whole box jittered). Width is measured
  // from the widest string we can ever show - "ETA  88:88:88" at the time font - so it
  // can't clip and tracks the font instead of a magic number. The spare room becomes
  // trailing whitespace via the left-aligned (START) labels below, which pins the text's
  // left edge ("ETA"/status stay put). The digits themselves still shift a pixel or two
  // as they count - Montserrat's figures aren't equal-width ('1' is ~95u vs '0' ~171u) -
  // which is accepted; only a monospaced font could remove it, and we keep Montserrat.
  lv_point_t info_w;
  lv_txt_get_size(&info_w, "ETA  88:88:88", &lv_font_montserrat_16, 0, 0, LV_COORD_MAX, 0);
  lv_obj_set_size(info_cont, info_w.x, LV_SIZE_CONTENT);
  lv_obj_clear_flag(info_cont, LV_OBJ_FLAG_SCROLLABLE);
  // Make the whole overlay one tap target. A plain container is clickable by default and
  // was swallowing presses over the text, so only the (non-clickable) preview image
  // opened the status screen. Clearing it lets text presses fall through to `cont`.
  lv_obj_clear_flag(info_cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_pad_all(info_cont, 0, 0);
  lv_obj_set_style_pad_row(info_cont, 2, 0);
  lv_obj_set_style_border_width(info_cont, 0, 0);
  lv_obj_set_style_bg_opa(info_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(info_cont, LV_FLEX_FLOW_COLUMN);
  // cross-axis START = left-align, so the time/percent text grows rightward into the
  // trailing whitespace and its left edge stays put (no per-second wiggle).
  lv_obj_set_flex_align(info_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

  lv_obj_set_style_text_font(time_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_font(pct_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(pct_label, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
  render();

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

void MiniPrintStatus::render() {
  lv_label_set_text(time_label, fmt::format("ETA  {}", eta).c_str());
  lv_label_set_text(pct_label, fmt::format("{}  {}%", KUtils::to_title(status), progress).c_str());
}

#ifdef SIMULATOR
void MiniPrintStatus::sim_thumb() {
  lv_img_set_src(thumb, &print);
  lv_img_set_zoom(thumb, 170);
  lv_obj_clear_flag(thumb, LV_OBJ_FLAG_HIDDEN);
}
#endif

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

void MiniPrintStatus::update_eta(int64_t eta_secs) {
  eta = fmt_hms(eta_secs);
  render();
}

void MiniPrintStatus::update_status(std::string &status_str) {
  status = status_str;
}

void MiniPrintStatus::update_progress(int p) {
  progress = p;
  render();
}

void MiniPrintStatus::update_img(const std::string &img_path, size_t twidth) {
  auto screen_width = lv_disp_get_physical_hor_res(NULL);
  // ~11% of screen width - compact preview alongside the time/percent
  uint32_t normalized_thumb_scale = ((0.11 * (double)screen_width) / (double)twidth) * 256;
  lv_img_set_zoom(thumb, normalized_thumb_scale);
  lv_obj_clear_flag(thumb, LV_OBJ_FLAG_HIDDEN);  // a real preview arrived - show it
  lv_img_set_src(thumb, img_path.c_str());
}

void MiniPrintStatus::reset() {
  // Drop the preview and hide it until the next job's thumbnail loads (avoids
  // the old red empty-image placeholder block).
  lv_img_set_src(thumb, NULL);
  lv_obj_add_flag(thumb, LV_OBJ_FLAG_HIDDEN);

  eta = "--:--";
  status = "n/a";
  progress = 0;
  render();
}
