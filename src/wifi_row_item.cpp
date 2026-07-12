#include "wifi_row_item.h"

WifiRowItem::WifiRowItem(lv_obj_t *parent,
                         const std::string &ssid_,
                         int signal_dbm,
                         bool connected,
                         bool saved,
                         const std::string &nid_,
                         std::function<void(const std::string &)> on_activate_,
                         std::function<void(const std::string &, const std::string &)> on_forget_)
  : ssid(ssid_)
  , nid(nid_)
  , cont(lv_obj_create(parent))
  , on_activate(std::move(on_activate_))
  , on_forget(std::move(on_forget_))
{
  lv_obj_set_size(cont, LV_PCT(100), connected ? 42 : 34);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(cont, 1, 0);
  lv_obj_set_style_border_side(cont, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(cont, lv_palette_darken(LV_PALETTE_GREY, 3), 0);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(cont, 0, 0);
  lv_obj_set_style_pad_hor(cont, 8, 0);
  lv_obj_set_style_pad_ver(cont, 3, 0);
  lv_obj_set_style_pad_column(cont, 8, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  build_signal_bars(cont, signal_dbm);

  lv_obj_t *text_col = lv_obj_create(cont);
  // lv_obj_create() widgets are clickable by default, which was silently
  // swallowing taps on the name/subtitle area instead of letting them fall
  // through to cont's own row-click handler below - confirmed live
  // 2026-07-12: only the tiny chevron (a plain label, non-clickable by
  // default) was actually opening Network Details.
  lv_obj_clear_flag(text_col, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_flex_grow(text_col, 1);
  lv_obj_set_height(text_col, LV_SIZE_CONTENT);
  lv_obj_clear_flag(text_col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(text_col, 0, 0);
  lv_obj_set_style_bg_opa(text_col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(text_col, 0, 0);
  lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *name_lbl = lv_label_create(text_col);
  lv_label_set_text(name_lbl, ssid.c_str());
  lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(name_lbl, LV_PCT(100));
  lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_14, 0);

  if (connected) {
    lv_obj_t *sub = lv_label_create(text_col);
    lv_label_set_text(sub, "Connected");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(sub, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
  }

  if (connected) {
    // The only secondary affordance on the connected row - opens
    // NetworkDetailPanel. The row body does the same thing (see
    // handle_row_click), the chevron is purely a visual hint that this row
    // leads somewhere else.
    lv_obj_t *chev = lv_label_create(cont);
    lv_label_set_text(chev, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(chev, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
  } else if (saved) {
    // Forget affordance for a known-but-not-connected network. Its own
    // clickable box (40x30, spaced from the row body by pad_column above) so
    // it can never be confused with the row's own connect action - unlike
    // the two flush 60px table columns this replaces.
    lv_obj_t *forget_hit = lv_obj_create(cont);
    lv_obj_set_size(forget_hit, 40, 30);
    lv_obj_clear_flag(forget_hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(forget_hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_width(forget_hit, 0, 0);
    lv_obj_set_style_bg_opa(forget_hit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(forget_hit, 0, 0);
    lv_obj_t *x = lv_label_create(forget_hit);
    lv_label_set_text(x, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(x, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_obj_center(x);
    lv_obj_add_event_cb(forget_hit, &WifiRowItem::_handle_forget_click, LV_EVENT_CLICKED, this);
  }

  lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(cont, &WifiRowItem::_handle_row_click, LV_EVENT_CLICKED, this);
}

WifiRowItem::~WifiRowItem() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void WifiRowItem::build_signal_bars(lv_obj_t *parent, int dbm) {
  lv_obj_t *bars = lv_obj_create(parent);
  lv_obj_clear_flag(bars, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(bars, 16, 13);
  lv_obj_clear_flag(bars, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(bars, 0, 0);
  lv_obj_set_style_bg_opa(bars, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(bars, 0, 0);
  lv_obj_set_style_pad_column(bars, 2, 0);
  lv_obj_set_flex_flow(bars, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

  // Bucket thresholds match common RSSI conventions (>=-50 excellent down to
  // <-70 weak) - reused as text in NetworkDetailPanel's Signal row too.
  int level = dbm >= -50 ? 4 : dbm >= -60 ? 3 : dbm >= -70 ? 2 : 1;
  static const lv_coord_t heights[4] = {4, 7, 10, 13};
  for (int i = 0; i < 4; i++) {
    lv_obj_t *bar = lv_obj_create(bars);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(bar, 3, heights[i]);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_bg_color(bar,
      i < level ? lv_palette_lighten(LV_PALETTE_GREY, 3) : lv_palette_darken(LV_PALETTE_GREY, 2), 0);
  }
}

void WifiRowItem::handle_row_click(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (on_activate) on_activate(ssid);
}

void WifiRowItem::handle_forget_click(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (on_forget) on_forget(ssid, nid);
}
