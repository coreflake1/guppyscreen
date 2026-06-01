#include "command_item.h"

LV_IMG_DECLARE(img_star);

CommandItem::CommandItem(lv_obj_t *parent,
			 std::function<void(CommandItem*)> on_sel,
			 std::function<void(CommandItem*)> on_fav)
  : cont(lv_obj_create(parent))
  , fav_img(lv_img_create(cont))
  , label(lv_label_create(cont))
  , favorite(false)
  , visible(true)
  , on_select(on_sel)
  , on_favorite(on_fav)
{
  lv_obj_set_size(cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(cont, 6, 0);
  lv_obj_set_style_pad_column(cont, 6, 0);
  lv_obj_set_style_border_side(cont, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
  lv_obj_set_style_border_width(cont, 1, 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(cont, &CommandItem::_handle_select, LV_EVENT_CLICKED, this);

  // favorite star (recolored amber/grey, like the macros panel)
  lv_img_set_src(fav_img, &img_star);
  lv_obj_add_flag(fav_img, LV_OBJ_FLAG_CLICKABLE);
  update_favorite_icon();
  lv_obj_add_event_cb(fav_img, &CommandItem::_handle_favorite, LV_EVENT_CLICKED, this);

  lv_obj_set_flex_grow(label, 1);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_label_set_text(label, "");
}

CommandItem::~CommandItem() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void CommandItem::set_command(const std::string &c) {
  cmd = c;
  lv_label_set_text(label, cmd.c_str());
}

void CommandItem::update_favorite_icon() {
  lv_obj_set_style_img_recolor_opa(fav_img, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_img_recolor(fav_img,
    favorite ? lv_palette_main(LV_PALETTE_AMBER)
	     : lv_palette_lighten(LV_PALETTE_GREY, 2), LV_PART_MAIN);
}

void CommandItem::set_favorite(bool fav) {
  favorite = fav;
  update_favorite_icon();
}

void CommandItem::set_highlight(bool h) {
  if (h) {
    lv_obj_set_style_bg_color(cont, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cont, LV_OPA_30, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  } else {
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  }
}

void CommandItem::set_visible(bool v) {
  visible = v;
  if (v) {
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
  }
}

void CommandItem::handle_select(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED && on_select) {
    on_select(this);
  }
}

void CommandItem::handle_favorite(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED && on_favorite) {
    on_favorite(this);
  }
}
