#include "network_detail_panel.h"
#include "spdlog/spdlog.h"

LV_IMG_DECLARE(back);

namespace {

lv_obj_t *make_info_row(lv_obj_t *parent, const char *label_text, lv_obj_t **val_out) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(row, 2, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, label_text);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);

  lv_obj_t *val = lv_label_create(row);
  lv_obj_set_style_text_font(val, &lv_font_montserrat_12, 0);
  *val_out = val;
  return row;
}

} // namespace

NetworkDetailPanel::NetworkDetailPanel(std::mutex &l,
                                       std::function<void()> on_configure_static_ip_,
                                       std::function<void()> on_forget_)
  : lv_lock(l)
  , cont(lv_obj_create(lv_scr_act()))
  , title_label(lv_label_create(cont))
  , info_card(lv_obj_create(cont))
  , signal_val(nullptr)
  , security_val(nullptr)
  , ip_val(nullptr)
  , configure_btn(lv_btn_create(cont))
  , forget_btn(lv_btn_create(cont))
  , back_btn(cont, &back, "Back", &NetworkDetailPanel::_handle_back_btn, this)
  , on_configure_static_ip(std::move(on_configure_static_ip_))
  , on_forget(std::move(on_forget_))
{
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(cont, 10, 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(cont, 8, 0);

  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, 0);

  lv_obj_set_size(info_card, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_clear_flag(info_card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(info_card, 8, 0);
  lv_obj_set_style_pad_row(info_card, 2, 0);
  lv_obj_set_style_radius(info_card, 4, 0);
  lv_obj_set_style_bg_color(info_card, lv_palette_darken(LV_PALETTE_GREY, 4), 0);
  lv_obj_set_flex_flow(info_card, LV_FLEX_FLOW_COLUMN);

  make_info_row(info_card, "Signal", &signal_val);
  make_info_row(info_card, "Security", &security_val);
  make_info_row(info_card, "IP address", &ip_val);

  // Fixed width, not 100% - the floating Back button is a 110px-wide box
  // pinned to the bottom-right corner (button_container.cpp), and a
  // full-width button here lands squarely under it (confirmed live
  // 2026-07-12). Narrowing keeps this clear of Back's footprint regardless
  // of exact vertical position, rather than fine-tuning y-offsets to dodge it.
  lv_obj_set_size(configure_btn, 320, 42);
  {
    lv_obj_t *l = lv_label_create(configure_btn);
    lv_label_set_text(l, "Configure Static IP");
    lv_obj_center(l);
  }
  lv_obj_add_event_cb(configure_btn, &NetworkDetailPanel::_handle_configure_btn, LV_EVENT_CLICKED, this);

  lv_obj_set_size(forget_btn, 320, 42);
  lv_obj_set_style_bg_opa(forget_btn, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(forget_btn, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(forget_btn, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
  {
    lv_obj_t *l = lv_label_create(forget_btn);
    lv_label_set_text(l, "Forget This Network");
    lv_obj_set_style_text_color(l, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_center(l);
  }
  lv_obj_add_event_cb(forget_btn, &NetworkDetailPanel::_handle_forget_btn, LV_EVENT_CLICKED, this);

  lv_obj_add_flag(back_btn.get_container(), LV_OBJ_FLAG_FLOATING);
  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, -20);

  lv_obj_move_background(cont);
}

NetworkDetailPanel::~NetworkDetailPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void NetworkDetailPanel::foreground(const std::string &ssid,
                                    const std::string &signal_text,
                                    const std::string &security_text,
                                    const std::string &ip_text) {
  lv_label_set_text(title_label, ssid.c_str());
  lv_label_set_text(signal_val, signal_text.c_str());
  lv_label_set_text(security_val, security_text.c_str());
  lv_label_set_text(ip_val, ip_text.c_str());
  lv_obj_move_foreground(cont);
}

void NetworkDetailPanel::hide() {
  lv_obj_move_background(cont);
}

void NetworkDetailPanel::handle_back_btn(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  hide();
}

void NetworkDetailPanel::handle_configure_btn(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (on_configure_static_ip) on_configure_static_ip();
}

void NetworkDetailPanel::handle_forget_btn(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (on_forget) on_forget();
}
