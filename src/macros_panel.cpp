#include "macros_panel.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

// reuse the app's material-design icons (same set the homing panel uses)
LV_IMG_DECLARE(arrow_up);
LV_IMG_DECLARE(arrow_down);

static const char *VIEW_MAP[] = {"Favorites", "All Macros", ""};

MacrosPanel::MacrosPanel(KWebSocketClient &c, std::mutex &l, lv_obj_t *parent,
			 std::function<void()> on_macro_run)
  : ws(c)
  , lv_lock(l)
  , cont(lv_obj_create(parent))
  , top_controls(lv_obj_create(cont))
  , view_toggle(lv_btnmatrix_create(top_controls))
  , body(lv_obj_create(cont))
  , top_cont(lv_obj_create(body))
  , empty_label(lv_label_create(top_cont))
  , nav_cont(lv_obj_create(body))
  , up_btn(lv_btn_create(nav_cont))
  , ok_btn(lv_btn_create(nav_cont))
  , down_btn(lv_btn_create(nav_cont))
  , kb(lv_keyboard_create(cont))
  , view(FAVORITES)
  , highlight_index(-1)
  , expanded_item(nullptr)
  , on_macro_run(on_macro_run)
{
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(cont, 0, 0);

  // header: Favorites / All Macros segmented toggle
  lv_obj_set_size(top_controls, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(top_controls, 0, 0);
  lv_obj_clear_flag(top_controls, LV_OBJ_FLAG_SCROLLABLE);

  lv_btnmatrix_set_map(view_toggle, VIEW_MAP);
  lv_btnmatrix_set_btn_ctrl_all(view_toggle, LV_BTNMATRIX_CTRL_CHECKABLE);
  lv_btnmatrix_set_one_checked(view_toggle, true);
  lv_btnmatrix_set_btn_ctrl(view_toggle, FAVORITES, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_set_size(view_toggle, LV_PCT(100), 40);
  lv_obj_set_style_text_font(view_toggle, &lv_font_montserrat_14, LV_STATE_DEFAULT);
  // let the two buttons fill the bar (no border, minimal gap), so the touch
  // targets are wide; checked button gets the accent, the rest stays neutral
  lv_obj_set_style_border_width(view_toggle, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(view_toggle, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_pad_all(view_toggle, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(view_toggle, 3, LV_PART_MAIN);
  lv_obj_set_style_bg_color(view_toggle, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_ITEMS);
  lv_obj_add_event_cb(view_toggle, &MacrosPanel::_handle_view_changed, LV_EVENT_VALUE_CHANGED, this);

  // body: macro list (grows) + right-side nav column
  lv_obj_set_flex_grow(body, 1);
  lv_obj_set_width(body, LV_PCT(100));
  lv_obj_set_style_pad_all(body, 0, 0);
  lv_obj_set_style_pad_column(body, 0, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

  // scrollable macro list (drag-scroll kept, scrollbar hidden)
  lv_obj_set_flex_grow(top_cont, 1);
  lv_obj_set_height(top_cont, LV_PCT(100));
  lv_obj_set_style_pad_all(top_cont, 0, 0);
  // inset rows from the right so the Play buttons don't sit flush against the
  // up/ok/down column (avoids accidental presses)
  lv_obj_set_style_pad_right(top_cont, 10, 0);
  lv_obj_set_flex_flow(top_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(top_cont, 0, 0);
  lv_obj_set_scrollbar_mode(top_cont, LV_SCROLLBAR_MODE_OFF);
  // clamp scrolling to the content (no elastic over-scroll past the first/last
  // macro, which would leave whitespace at the top)
  lv_obj_clear_flag(top_cont, LV_OBJ_FLAG_SCROLL_ELASTIC);

  // floated (ignored by the list's flex layout) so it can be centred
  lv_label_set_text(empty_label,
		    "No favorites yet.\nOpen All Macros and tap the star to add.");
  lv_obj_add_flag(empty_label, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_width(empty_label, LV_PCT(90));
  lv_obj_set_style_text_align(empty_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(empty_label, &lv_font_montserrat_16, 0);
  lv_obj_center(empty_label);
  lv_obj_add_flag(empty_label, LV_OBJ_FLAG_HIDDEN);

  // right-side nav column: up / ok / down
  lv_obj_set_size(nav_cont, 48, LV_PCT(100));
  lv_obj_set_style_pad_all(nav_cont, 2, 0);
  lv_obj_set_style_pad_row(nav_cont, 4, 0);
  lv_obj_set_style_border_width(nav_cont, 0, 0);
  lv_obj_set_flex_flow(nav_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(nav_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(nav_cont, LV_OBJ_FLAG_SCROLLABLE);

  // neutral grey buttons (keep the screen from being all-blue); up/down reuse
  // the material-design arrow icons, OK keeps the check glyph
  auto style_nav_btn = [](lv_obj_t *btn) {
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_bg_color(btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  };

  style_nav_btn(up_btn);
  lv_obj_t *up_img = lv_img_create(up_btn);
  lv_img_set_src(up_img, &arrow_up);
  lv_obj_center(up_img);
  lv_obj_add_event_cb(up_btn, &MacrosPanel::_handle_nav, LV_EVENT_CLICKED, this);

  style_nav_btn(ok_btn);
  lv_obj_t *ok_lbl = lv_label_create(ok_btn);
  lv_label_set_text(ok_lbl, LV_SYMBOL_OK);
  lv_obj_set_style_text_font(ok_lbl, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_obj_center(ok_lbl);
  lv_obj_add_event_cb(ok_btn, &MacrosPanel::_handle_nav, LV_EVENT_CLICKED, this);

  style_nav_btn(down_btn);
  lv_obj_t *down_img = lv_img_create(down_btn);
  lv_img_set_src(down_img, &arrow_down);
  lv_obj_center(down_img);
  lv_obj_add_event_cb(down_btn, &MacrosPanel::_handle_nav, LV_EVENT_CLICKED, this);

  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_font(kb, &lv_font_montserrat_10, LV_STATE_DEFAULT);
}

MacrosPanel::~MacrosPanel()
{
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void MacrosPanel::populate() {
  macro_items.clear();
  visible_items.clear();
  highlight_index = -1;
  expanded_item = nullptr;

  auto config_json = State::get_instance()
    ->get_data("/printer_state/configfile/config"_json_pointer);

  // TODO: this is a race condition
  auto macro_settings = State::get_instance()->get_data("/guppysettings/macros/settings"_json_pointer);

  if (!config_json.is_null()) {
    auto macros = KUtils::parse_macros(config_json);

    for (auto const & [k, v] : macros) {
      auto fav_json = macro_settings[json::json_pointer(fmt::format("/{}/favorite", k))];
      bool fav = !fav_json.is_null() ? fav_json.template get<bool>() : false;
      macro_items.push_back(std::make_shared<MacroItem>(
	ws, top_cont, k, v, kb, fav,
	[this]() { apply_view(); },
	[this](MacroItem *m) { highlight_item(m); toggle_expand(m); },
	[this]() { if (on_macro_run) on_macro_run(); }));
    }
  }

  apply_view();
}

void MacrosPanel::reset_to_favorites() {
  view = FAVORITES;
  lv_btnmatrix_set_btn_ctrl(view_toggle, FAVORITES, LV_BTNMATRIX_CTRL_CHECKED);
  apply_view();
}

void MacrosPanel::highlight_item(MacroItem *m) {
  for (size_t i = 0; i < visible_items.size(); i++) {
    if (visible_items[i] == m) {
      set_highlight((int)i);
      return;
    }
  }
}

// single-expand: expanding one macro collapses any other that was open
void MacrosPanel::toggle_expand(MacroItem *m) {
  if (m->is_expanded()) {
    m->set_expanded(false);
    if (expanded_item == m) {
      expanded_item = nullptr;
    }
  } else {
    if (expanded_item != nullptr && expanded_item != m) {
      expanded_item->set_expanded(false);
    }
    m->set_expanded(true);
    expanded_item = m;
  }
}

void MacrosPanel::collapse_expanded() {
  if (expanded_item != nullptr) {
    expanded_item->set_expanded(false);
    expanded_item = nullptr;
  }
}

void MacrosPanel::apply_view() {
  // switching views (or re-entering the tab) collapses any open macro
  collapse_expanded();
  for (const auto &m : macro_items) {
    m->set_visible(view == ALL || m->is_favorite());
  }
  rebuild_visible();

  // reset scroll to the top so a switched/re-entered view starts at the first
  // row (and the centered empty-state label isn't scrolled out of sight)
  lv_obj_scroll_to_y(top_cont, 0, LV_ANIM_OFF);

  bool empty = (view == FAVORITES) && visible_items.empty();
  if (empty) {
    lv_obj_clear_flag(empty_label, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(empty_label, LV_OBJ_FLAG_HIDDEN);
  }

  // resolve flex widths now so each name label's ellipsis (LONG_DOT) is
  // computed before the first paint, instead of wrapping to two lines until
  // the row is first interacted with
  lv_obj_update_layout(cont);

  // keep a sensible highlight after the visible set changes
  if (visible_items.empty()) {
    highlight_index = -1;
  } else {
    set_highlight(0);
  }
}

void MacrosPanel::rebuild_visible() {
  visible_items.clear();
  for (const auto &m : macro_items) {
    if (m->is_visible()) {
      visible_items.push_back(m.get());
    }
  }
}

void MacrosPanel::set_highlight(int idx) {
  // clear every item first so a stale highlight can't survive a view switch
  // (an item highlighted in one view may sit at a different index, or be
  // hidden, in the other view)
  for (const auto &m : macro_items) {
    m->set_highlight(false);
  }
  highlight_index = idx;
  if (highlight_index >= 0 && highlight_index < (int)visible_items.size()) {
    MacroItem *m = visible_items[highlight_index];
    m->set_highlight(true);
    lv_obj_scroll_to_view(m->get_cont(), LV_ANIM_ON);
  }
}

void MacrosPanel::move_highlight(int delta) {
  if (visible_items.empty()) {
    return;
  }
  int idx = highlight_index < 0 ? 0 : highlight_index + delta;
  if (idx < 0) {
    idx = 0;
  } else if (idx >= (int)visible_items.size()) {
    idx = (int)visible_items.size() - 1;
  }
  // moving the highlight away collapses whatever was expanded
  if (idx != highlight_index) {
    collapse_expanded();
  }
  set_highlight(idx);
}

void MacrosPanel::handle_view_changed(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
    return;
  }
  uint32_t id = lv_btnmatrix_get_selected_btn(view_toggle);
  view = (id == ALL) ? ALL : FAVORITES;
  apply_view();
}

void MacrosPanel::handle_nav(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  lv_obj_t *target = lv_event_get_target(e);
  if (target == up_btn) {
    move_highlight(-1);
  } else if (target == down_btn) {
    move_highlight(1);
  } else if (target == ok_btn) {
    if (highlight_index >= 0 && highlight_index < (int)visible_items.size()) {
      MacroItem *m = visible_items[highlight_index];
      toggle_expand(m);
      lv_obj_scroll_to_view(m->get_cont(), LV_ANIM_ON);
    }
  }
}
