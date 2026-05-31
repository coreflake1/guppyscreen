#include "macro_item.h"
#include "utils.h"
#include "spdlog/spdlog.h"

LV_IMG_DECLARE(img_star);

MacroItem::MacroItem(KWebSocketClient &c,
		     lv_obj_t *parent,
		     std::string macro_name,
		     const std::map<std::string, std::string> &m_params,
		     lv_obj_t *keyboard,
		     bool fav,
		     std::function<void()> on_fav_changed,
		     std::function<void(MacroItem*)> on_act)
  : ws(c)
  , mname(macro_name)
  , cont(lv_obj_create(parent))
  , top_cont(lv_obj_create(cont))
  , fav_img(lv_img_create(top_cont))
  , macro_label(lv_label_create(top_cont))
  , params_cont(NULL)
  , kb(keyboard)
  , favorite(fav)
  , visible(true)
  , expanded(false)
  , highlighted(false)
  , on_favorite_changed(on_fav_changed)
  , on_activated(on_act)
{
  lv_obj_set_size(cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(cont, 4, 0);
  lv_obj_set_style_pad_row(cont, 0, 0);
  lv_obj_set_style_border_side(cont, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
  lv_obj_set_style_border_width(cont, 2, 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

  // top row: [ * ] [ macro name (grows) ] [ play ]
  lv_obj_set_size(top_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(top_cont, 0, 0);
  lv_obj_set_style_pad_column(top_cont, 4, 0);
  lv_obj_set_flex_flow(top_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(top_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(top_cont, LV_OBJ_FLAG_SCROLLABLE);

  // favorite star toggle (a clickable star image, kept first so it sits left
  // of the name); recolored amber when favorited, grey when not
  lv_img_set_src(fav_img, &img_star);
  lv_obj_add_flag(fav_img, LV_OBJ_FLAG_CLICKABLE);
  update_favorite_icon();
  lv_obj_add_event_cb(fav_img, &MacroItem::_handle_favorite, LV_EVENT_CLICKED, this);

  // macro name (tap to expand/collapse params). Long names get an ellipsis;
  // the highlighted row scrolls (see set_highlight) so the full name is
  // readable for just the selected item.
  lv_obj_set_flex_grow(macro_label, 1);
  lv_label_set_long_mode(macro_label, LV_LABEL_LONG_DOT);
  lv_label_set_text(macro_label, mname.c_str());
  lv_obj_add_flag(macro_label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(macro_label, &MacroItem::_handle_expand, LV_EVENT_CLICKED, this);

  // run button
  lv_obj_t *run_btn = lv_btn_create(top_cont);
  lv_obj_set_style_text_font(run_btn, &lv_font_montserrat_10, LV_STATE_DEFAULT);
  lv_obj_set_width(run_btn, 62);
  lv_obj_t *run_btn_label = lv_label_create(run_btn);
  lv_label_set_text(run_btn_label, LV_SYMBOL_PLAY);
  lv_obj_center(run_btn_label);
  lv_obj_add_event_cb(run_btn, &MacroItem::_handle_send_macro, LV_EVENT_CLICKED, this);

  if (!m_params.empty()) {
    params_cont = lv_obj_create(cont);
    lv_obj_set_size(params_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(params_cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(params_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(params_cont, 0, 0);
    lv_obj_set_style_border_width(params_cont, 0, 0);
    lv_obj_clear_flag(params_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *param_name;
    lv_obj_t *param_value;

    for (auto const & [k, v] : m_params) {
      param_name = lv_label_create(params_cont);
      lv_label_set_text(param_name, k.c_str());

      param_value = lv_textarea_create(params_cont);
      lv_textarea_set_one_line(param_value, true);
      lv_textarea_set_text(param_value, v.c_str());
      lv_obj_clear_flag(param_value, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_add_event_cb(param_value, &MacroItem::_handle_kb_input, LV_EVENT_ALL, this);

      lv_obj_set_width(param_name, LV_PCT(30));
      lv_obj_set_width(param_value, LV_PCT(45));

      params.push_back({param_name, param_value});
    }

    // collapsed by default
    lv_obj_add_flag(params_cont, LV_OBJ_FLAG_HIDDEN);
  }
}

MacroItem::~MacroItem() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void MacroItem::update_favorite_icon() {
  lv_obj_set_style_img_recolor_opa(fav_img, LV_OPA_COVER, LV_PART_MAIN);
  if (favorite) {
    lv_obj_set_style_img_recolor(fav_img, lv_palette_main(LV_PALETTE_AMBER), LV_PART_MAIN);
  } else {
    lv_obj_set_style_img_recolor(fav_img, lv_palette_lighten(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  }
}

void MacroItem::set_visible(bool v) {
  visible = v;
  if (v) {
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
  }
}

void MacroItem::set_highlight(bool h) {
  highlighted = h;
  if (h) {
    lv_obj_set_style_bg_color(cont, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cont, LV_OPA_30, LV_PART_MAIN);
    // scroll the selected row's name so a long macro name is fully readable
    lv_label_set_long_mode(macro_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  } else {
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_label_set_long_mode(macro_label, LV_LABEL_LONG_DOT);
  }
}

void MacroItem::set_expanded(bool e) {
  expanded = e;
  if (params_cont == NULL) {
    return;
  }
  if (e) {
    lv_obj_clear_flag(params_cont, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(params_cont, LV_OBJ_FLAG_HIDDEN);
  }
}

void MacroItem::handle_expand(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    // tapping a row moves the highlight onto it and toggles its expansion;
    // the panel handles both (so it can enforce single-expand)
    if (on_activated) {
      on_activated(this);
    }
  }
}

void MacroItem::handle_favorite(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  spdlog::trace("macro item favorite toggle: {}", mname);

  favorite = !favorite;
  update_favorite_icon();

  std::string key = fmt::format("macros.settings.{}", mname);
  json h = {
    {"namespace", "guppyscreen"},
    {"key", key},
    {"value", {
	{ "favorite", favorite }
      }
    }
  };
  ws.send_jsonrpc("server.database.post_item", h);

  if (on_favorite_changed) {
    on_favorite_changed();
  }
}

void MacroItem::handle_kb_input(lv_event_t *e)
{
  const lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *obj = lv_event_get_target(e);

  if(code == LV_EVENT_FOCUSED) {
    spdlog::trace("macro item focused");
    lv_keyboard_set_textarea(kb, obj);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(lv_obj_get_parent(kb));

    // position the field being edited just above the keyboard (small gap),
    // by scrolling the macro list (this item's parent) by the exact overlap
    lv_obj_t *list = lv_obj_get_parent(cont);
    lv_area_t ta_coords, kb_coords;
    lv_obj_get_coords(obj, &ta_coords);
    lv_obj_get_coords(kb, &kb_coords);
    lv_coord_t gap = 10;
    lv_coord_t delta = (ta_coords.y2 + gap) - kb_coords.y1;
    if (delta != 0) {
      lv_obj_scroll_by(list, 0, -delta, LV_ANIM_OFF);
    }
  }

  if(code == LV_EVENT_DEFOCUSED) {
    spdlog::trace("macro item defocused");
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }

  // both the keyboard's OK (checkmark) and close (X) buttons dismiss it
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    spdlog::trace("macro item keyboard done");
    lv_keyboard_set_textarea(kb, NULL);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_state(obj, LV_STATE_FOCUSED);
  }
}

void MacroItem::handle_send_macro(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED) {
    std::vector<std::string> kv;
    kv.push_back(mname);
    for (const auto &p: params) {
      const char *v = lv_textarea_get_text(p.second);
      if (v != NULL && strlen(v) > 0) {
	const char *k = lv_label_get_text(p.first);
	kv.push_back(fmt::format("{}={}", k, v));
      }
    }

    std::string command = fmt::format("{}", fmt::join(kv, " "));
    spdlog::trace("sending macro: {}", command);
    KUtils::confirm_if_printing("Printer is printing.\nRun this macro anyway?",
      [this, command]() { ws.gcode_script(command); });
  }
}
