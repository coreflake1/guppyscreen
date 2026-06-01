#include "console_panel.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cctype>
#include <cstring>

LV_FONT_DECLARE(dejavusans_mono_14);
LV_IMG_DECLARE(arrow_up);
LV_IMG_DECLARE(arrow_down);

static const int POOL_SIZE = 60;

// Keyword-driven command categories for the drill-down menu. Order = priority:
// each command lands in the FIRST group whose any keyword is a substring, so the
// counts partition cleanly. Whatever matches nothing falls to "Other"; "All" is
// always present as the full searchable fallback, so nothing is ever lost even if
// this map is imperfect. Firmware-adaptive: empty groups are hidden.
struct CatDef { const char *label; const char *keys; };  // keys: '|'-separated
static const CatDef CAT_DEFS[] = {
  {"Bed Mesh / Leveling", "MESH|Z_TILT|QUAD_GANTRY|SCREWS_TILT|BED_SCREWS|BED_TILT|LEVEL"},
  {"Probe / Calibration", "PROBE|CALIBRATE|PID|SHAPER|RESONANCE|ACCELEROMETER|PRESSURE_ADVANCE|Z_OFFSET|GCODE_OFFSET|ENDSTOP_PHASE"},
  {"Motion",              "G28|HOME|MOVE|KINEMATIC|VELOCITY|M204|M205|STEPPER_BUZZ|M84|M18"},
  {"Temperature / Fans",  "TEMP|HEATER|M104|M109|M140|M190|M106|M107|FAN"},
  {"Extruder / Filament", "EXTRUD|FILAMENT|M600|LOAD|UNLOAD|RETRACT|M83|M82"},
  {"TMC / Drivers",       "TMC"},
  {"Print Job",           "PRINT|SDCARD|PAUSE|RESUME|CANCEL|EXCLUDE_OBJECT|M24|M25|M73"},
  {"Camera",              "CAM"},
  {"Query / Debug",       "QUERY|GET_POSITION|M114|DUMP|DEBUG|RESPOND|M118|STATUS|HELP"},
  {"System",              "RESTART|SAVE_CONFIG|SAVE_VARIABLE|SAVE_GCODE|SHUTDOWN|REBOOT|M112|IDLE_TIMEOUT|SET_PIN|DISPLAY|M115|BEEP|UPDATE"},
};

static std::string to_upper(const std::string &s) {
  std::string u;
  std::transform(s.begin(), s.end(), std::back_inserter(u),
		 [](unsigned char c){ return std::toupper(c); });
  return u;
}

// does cmd contain any of the '|'-separated keywords in defs?
static bool matches_keys(const std::string &cmd, const char *keys) {
  const char *p = keys;
  while (*p) {
    const char *bar = strchr(p, '|');
    std::string k = bar ? std::string(p, bar - p) : std::string(p);
    if (!k.empty() && cmd.find(k) != std::string::npos) {
      return true;
    }
    if (!bar) {
      break;
    }
    p = bar + 1;
  }
  return false;
}

ConsolePanel::ConsolePanel(KWebSocketClient &websocket_client, std::mutex &lock, lv_obj_t *parent)
  : ws(websocket_client)
  , lv_lock(lock)
  , console_cont(lv_obj_create(parent))
  , term_view(lv_obj_create(console_cont))
  , output(lv_textarea_create(term_view))
  , input_bar(lv_obj_create(term_view))
  , input(lv_textarea_create(input_bar))
  , cmd_view(lv_obj_create(console_cont))
  , subhdr(lv_obj_create(cmd_view))
  , back_btn(lv_btn_create(subhdr))
  , title_label(lv_label_create(subhdr))
  , cmd_body(lv_obj_create(cmd_view))
  , cat_cont(lv_obj_create(cmd_body))
  , list_cont(lv_obj_create(cmd_body))
  , empty_label(lv_label_create(list_cont))
  , footer_label(lv_label_create(cmd_view))
  , nav_cont(lv_obj_create(cmd_body))
  , up_btn(lv_btn_create(nav_cont))
  , ok_btn(lv_btn_create(nav_cont))
  , down_btn(lv_btn_create(nav_cont))
  , filter(lv_textarea_create(cmd_view))
  , kb(lv_keyboard_create(console_cont))
  , mode(TERMINAL)
  , level(CATEGORY)
  , highlight_index(-1)
  , active_cat(-1)
{
  lv_obj_set_size(console_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(console_cont, 0, 0);
  lv_obj_set_flex_flow(console_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(console_cont, 0, 0);
  lv_obj_set_style_text_font(console_cont, &dejavusans_mono_14, LV_STATE_DEFAULT);

  // ================= TERMINAL VIEW =================
  lv_obj_set_flex_grow(term_view, 1);
  lv_obj_set_width(term_view, LV_PCT(100));
  lv_obj_set_style_pad_all(term_view, 0, 0);
  lv_obj_set_style_pad_row(term_view, 0, 0);
  lv_obj_set_style_border_width(term_view, 0, 0);
  lv_obj_set_flex_flow(term_view, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(term_view, LV_OBJ_FLAG_SCROLLABLE);

  // output log (read-only-ish, full width)
  lv_obj_set_flex_grow(output, 1);
  lv_obj_set_width(output, LV_PCT(100));
  lv_obj_set_style_border_width(output, 0, 0);
  lv_textarea_set_cursor_click_pos(output, false);
  lv_obj_clear_flag(output, LV_OBJ_FLAG_CLICK_FOCUSABLE);
  lv_obj_set_scrollbar_mode(output, LV_SCROLLBAR_MODE_AUTO);

  // input bar: [ {} ] [ input ........ ] [x] [> Send]
  lv_obj_set_size(input_bar, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(input_bar, 5, 0);
  lv_obj_set_style_pad_column(input_bar, 6, 0);
  lv_obj_set_style_border_width(input_bar, 0, 0);
  lv_obj_set_flex_flow(input_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(input_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(input_bar, LV_OBJ_FLAG_SCROLLABLE);

  // slightly larger touch targets across the input bar
  const lv_coord_t BAR_BTN_W = 54;
  const lv_coord_t BAR_H = 46;

  // open the command browser (left of the input)
  lv_obj_t *cmds_btn = lv_btn_create(input_bar);
  lv_obj_set_size(cmds_btn, BAR_BTN_W, BAR_H);
  lv_obj_set_style_bg_color(cmds_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  lv_obj_t *cmds_lbl = lv_label_create(cmds_btn);
  lv_obj_set_style_text_font(cmds_lbl, &lv_font_montserrat_20, 0);
  lv_label_set_text(cmds_lbl, LV_SYMBOL_LIST);
  lv_obj_center(cmds_lbl);
  lv_obj_add_event_cb(cmds_btn, &ConsolePanel::_handle_open_commands, LV_EVENT_CLICKED, this);
  lv_obj_move_to_index(cmds_btn, 0);  // leftmost

  // input grows to fill; set height AFTER set_one_line (which forces content height)
  lv_obj_set_flex_grow(input, 1);
  lv_textarea_set_one_line(input, true);
  lv_obj_set_style_text_font(input, &lv_font_montserrat_16, LV_PART_MAIN);  // scale text to the taller field (20 overflowed with long commands)
  lv_obj_set_height(input, BAR_H);
  lv_textarea_set_placeholder_text(input, "type gcode...");
  lv_obj_add_event_cb(input, &ConsolePanel::_handle_input_kb, LV_EVENT_ALL, this);

  lv_obj_t *clear_btn = lv_btn_create(input_bar);
  lv_obj_set_size(clear_btn, BAR_BTN_W, BAR_H);
  lv_obj_set_style_bg_color(clear_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  lv_obj_t *clear_lbl = lv_label_create(clear_btn);
  lv_obj_set_style_text_font(clear_lbl, &lv_font_montserrat_20, 0);
  lv_label_set_text(clear_lbl, LV_SYMBOL_CLOSE);
  lv_obj_center(clear_lbl);
  lv_obj_add_event_cb(clear_btn, &ConsolePanel::_handle_clear, LV_EVENT_CLICKED, this);

  lv_obj_t *send_btn = lv_btn_create(input_bar);
  lv_obj_set_size(send_btn, BAR_BTN_W, BAR_H);
  lv_obj_t *send_lbl = lv_label_create(send_btn);
  lv_obj_set_style_text_font(send_lbl, &lv_font_montserrat_20, 0);
  lv_label_set_text(send_lbl, LV_SYMBOL_NEW_LINE);
  lv_obj_center(send_lbl);
  lv_obj_add_event_cb(send_btn, &ConsolePanel::_handle_send, LV_EVENT_CLICKED, this);

  // ================= COMMANDS VIEW =================
  lv_obj_set_flex_grow(cmd_view, 1);
  lv_obj_set_width(cmd_view, LV_PCT(100));
  lv_obj_set_style_pad_all(cmd_view, 0, 0);
  lv_obj_set_style_pad_row(cmd_view, 0, 0);
  lv_obj_set_style_border_width(cmd_view, 0, 0);
  lv_obj_set_flex_flow(cmd_view, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(cmd_view, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(cmd_view, LV_OBJ_FLAG_HIDDEN);

  // ---- sub-header: [ < Back ]  <title> ---- (always shown in the browser;
  // the back button is hidden at the category level)
  lv_obj_set_size(subhdr, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(subhdr, 4, 0);
  lv_obj_set_style_pad_column(subhdr, 8, 0);
  lv_obj_set_style_border_width(subhdr, 0, 0);
  lv_obj_set_flex_flow(subhdr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(subhdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(subhdr, LV_OBJ_FLAG_SCROLLABLE);

  // a clearly-tappable "< Back" pill
  lv_obj_set_height(back_btn, 34);
  lv_obj_set_style_bg_color(back_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  lv_obj_set_style_pad_hor(back_btn, 14, LV_PART_MAIN);
  lv_obj_t *back_lbl = lv_label_create(back_btn);
  lv_label_set_text(back_lbl, LV_SYMBOL_LEFT "  Back");
  lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_14, 0);
  lv_obj_center(back_lbl);
  lv_obj_add_event_cb(back_btn, &ConsolePanel::_handle_back, LV_EVENT_CLICKED, this);

  lv_obj_set_flex_grow(title_label, 1);
  lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, 0);
  lv_label_set_text(title_label, "");

  // ---- body: (category list | command list) + nav column ----
  lv_obj_set_flex_grow(cmd_body, 1);
  lv_obj_set_width(cmd_body, LV_PCT(100));
  lv_obj_set_style_pad_all(cmd_body, 0, 0);
  lv_obj_set_style_pad_column(cmd_body, 0, 0);
  lv_obj_set_style_border_width(cmd_body, 0, 0);
  lv_obj_set_flex_flow(cmd_body, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(cmd_body, LV_OBJ_FLAG_SCROLLABLE);

  // level-1 category container
  lv_obj_set_flex_grow(cat_cont, 1);
  lv_obj_set_height(cat_cont, LV_PCT(100));
  lv_obj_set_style_pad_all(cat_cont, 0, 0);
  lv_obj_set_style_pad_right(cat_cont, 8, 0);
  lv_obj_set_style_border_width(cat_cont, 0, 0);
  lv_obj_set_flex_flow(cat_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(cat_cont, 0, 0);
  lv_obj_set_scrollbar_mode(cat_cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(cat_cont, LV_OBJ_FLAG_SCROLL_ELASTIC);

  // level-2 command list (pooled rows)
  lv_obj_set_flex_grow(list_cont, 1);
  lv_obj_set_height(list_cont, LV_PCT(100));
  lv_obj_set_style_pad_all(list_cont, 0, 0);
  lv_obj_set_style_pad_right(list_cont, 8, 0);
  lv_obj_set_style_border_width(list_cont, 0, 0);
  lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list_cont, 0, 0);
  lv_obj_set_scrollbar_mode(list_cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(list_cont, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_add_flag(list_cont, LV_OBJ_FLAG_HIDDEN);

  lv_label_set_text(empty_label, "");
  lv_obj_add_flag(empty_label, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_width(empty_label, LV_PCT(90));
  lv_obj_set_style_text_align(empty_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(empty_label, &lv_font_montserrat_16, 0);
  lv_obj_center(empty_label);
  lv_obj_add_flag(empty_label, LV_OBJ_FLAG_HIDDEN);

  // nav column: up / ok / down
  lv_obj_set_size(nav_cont, 48, LV_PCT(100));
  lv_obj_set_style_pad_all(nav_cont, 2, 0);
  lv_obj_set_style_pad_row(nav_cont, 4, 0);
  lv_obj_set_style_border_width(nav_cont, 0, 0);
  lv_obj_set_flex_flow(nav_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(nav_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(nav_cont, LV_OBJ_FLAG_SCROLLABLE);

  auto style_nav = [](lv_obj_t *b) {
    lv_obj_set_width(b, LV_PCT(100));
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_bg_color(b, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  };
  style_nav(up_btn);
  lv_obj_t *up_img = lv_img_create(up_btn);
  lv_img_set_src(up_img, &arrow_up);
  lv_obj_center(up_img);
  lv_obj_add_event_cb(up_btn, &ConsolePanel::_handle_nav, LV_EVENT_CLICKED, this);

  style_nav(ok_btn);
  lv_obj_t *ok_lbl = lv_label_create(ok_btn);
  lv_label_set_text(ok_lbl, LV_SYMBOL_OK);
  lv_obj_set_style_text_font(ok_lbl, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_obj_center(ok_lbl);
  lv_obj_add_event_cb(ok_btn, &ConsolePanel::_handle_nav, LV_EVENT_CLICKED, this);

  style_nav(down_btn);
  lv_obj_t *down_img = lv_img_create(down_btn);
  lv_img_set_src(down_img, &arrow_down);
  lv_obj_center(down_img);
  lv_obj_add_event_cb(down_btn, &ConsolePanel::_handle_nav, LV_EVENT_CLICKED, this);

  // footer (only shown when the list is truncated)
  lv_obj_set_width(footer_label, LV_PCT(100));
  lv_obj_set_style_text_font(footer_label, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_align(footer_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(footer_label, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_add_flag(footer_label, LV_OBJ_FLAG_HIDDEN);

  // filter box (level 2 only)
  lv_obj_set_size(filter, LV_PCT(100), LV_SIZE_CONTENT);
  lv_textarea_set_one_line(filter, true);
  lv_textarea_set_placeholder_text(filter, "filter...");
  lv_obj_add_flag(filter, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(filter, &ConsolePanel::_handle_filter_kb, LV_EVENT_ALL, this);

  // shared keyboard
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_font(kb, &lv_font_montserrat_10, LV_STATE_DEFAULT);

  // pool of reusable command rows
  for (int i = 0; i < POOL_SIZE; i++) {
    auto item = std::make_shared<CommandItem>(
      list_cont,
      [this](CommandItem *c) { select_command(c); },
      [this](CommandItem *c) { toggle_favorite(c); });
    item->set_visible(false);
    pool.push_back(item);
  }

  ws.register_method_callback("notify_gcode_response",
			      "ConsolePanel",
			      [this](json& d) { this->handle_macro_response(d); });
}

ConsolePanel::~ConsolePanel() {
  if (console_cont != NULL) {
    lv_obj_del(console_cont);
    console_cont = NULL;
  }
}

lv_obj_t *ConsolePanel::get_container() {
  return console_cont;
}

void ConsolePanel::foreground() {
  reset_to_terminal();
}

void ConsolePanel::reset_to_terminal() {
  show_mode(TERMINAL);
}

// -------- data --------

void ConsolePanel::handle_macros(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);

  // recent command history (shared with fluidd)
  auto db_history = State::get_instance()->get_data("/console/commandHistory"_json_pointer);
  if (!db_history.is_null()) {
    history = db_history.template get<std::list<std::string>>();
  }

  // pinned favorites (guppyscreen namespace)
  auto db_fav = State::get_instance()->get_data("/guppysettings/console/favorites"_json_pointer);
  if (!db_fav.is_null() && db_fav.is_array()) {
    favorites = db_fav.template get<std::vector<std::string>>();
  }

  // all gcode commands (skip '_' private helpers), sorted
  if (j.contains("result")) {
    all_commands.clear();
    for (const auto &el : j["result"].items()) {
      const std::string &k = el.key();
      if (!k.empty() && k[0] != '_') {
	all_commands.push_back(k);
      }
    }
    std::sort(all_commands.begin(), all_commands.end());
  }

  build_keyword_cats();
}

void ConsolePanel::handle_macro_response(json &j) {
  if (j.contains("params")) {
    std::lock_guard<std::mutex> lock(lv_lock);
    for (auto &l : j["params"]) {
      append_output(l.template get<std::string>());
    }
  }
}

void ConsolePanel::append_output(const std::string &line) {
  lv_textarea_add_text(output, line.c_str());
  lv_textarea_add_text(output, "\n");
  lv_textarea_set_cursor_pos(output, LV_TEXTAREA_CURSOR_LAST);  // auto-scroll to bottom
}

// -------- terminal --------

void ConsolePanel::send_command(const std::string &cmd) {
  if (cmd.empty()) {
    return;
  }
  KUtils::confirm_if_printing("Printer is printing.\nSend this command anyway?",
    [this, cmd]() {
      append_output("> " + cmd);
      ws.gcode_script(cmd);
    });
  push_history(cmd);
}

void ConsolePanel::push_history(const std::string &cmd) {
  if (!history.empty() && history.front() == cmd) {
    return;  // don't duplicate the most recent
  }
  if (history.size() >= 20) {
    history.pop_back();
  }
  history.push_front(cmd);

  json h = {
    {"namespace", "fluidd"},
    {"key", "console.commandHistory"},
    {"value", history}
  };
  ws.send_jsonrpc("server.database.post_item", h);
}

void ConsolePanel::handle_send(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  const char *cmd = lv_textarea_get_text(input);
  if (cmd == NULL || cmd[0] == 0) {
    return;
  }
  send_command(std::string(cmd));
  lv_textarea_set_text(input, "");
}

void ConsolePanel::handle_clear(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_textarea_set_text(input, "");
  }
}

void ConsolePanel::handle_input_kb(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(kb, input);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
  if (code == LV_EVENT_DEFOCUSED) {
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    // the keyboard's OK/X only dismiss it (and keep the typed text); the command
    // is sent only via the dedicated Send button
    lv_keyboard_set_textarea(kb, NULL);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_state(input, LV_STATE_FOCUSED);
  }
}

// -------- commands browser --------

void ConsolePanel::show_mode(Mode m) {
  mode = m;
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  if (m == TERMINAL) {
    lv_obj_clear_flag(term_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cmd_view, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(term_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cmd_view, LV_OBJ_FLAG_HIDDEN);
    show_categories();  // always enter the commands view at the category menu
  }
}

void ConsolePanel::handle_open_commands(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    show_mode(COMMANDS);
  }
}

// ---- level 1: category menu ----

void ConsolePanel::build_keyword_cats() {
  // partition all_commands into keyword groups (first match wins), leftovers -> Other.
  keyword_cats.clear();
  std::vector<CmdCategory> groups;
  const int n = (int)(sizeof(CAT_DEFS) / sizeof(CAT_DEFS[0]));
  for (int i = 0; i < n; i++) {
    groups.push_back({CAT_DEFS[i].label, {}});
  }
  CmdCategory other{"Other", {}};

  for (const auto &cmd : all_commands) {
    int hit = -1;
    for (int i = 0; i < n; i++) {
      if (matches_keys(cmd, CAT_DEFS[i].keys)) {
	hit = i;
	break;
      }
    }
    if (hit >= 0) {
      groups[hit].cmds.push_back(cmd);
    } else {
      other.cmds.push_back(cmd);
    }
  }

  for (auto &g : groups) {
    if (!g.cmds.empty()) {
      keyword_cats.push_back(std::move(g));
    }
  }
  if (!other.cmds.empty()) {
    keyword_cats.push_back(std::move(other));
  }
}

void ConsolePanel::show_categories() {
  level = CATEGORY;
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  // back at the top level exits to the terminal
  lv_obj_clear_flag(subhdr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(title_label, "Commands");
  lv_obj_add_flag(filter, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(footer_label, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(list_cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(cat_cont, LV_OBJ_FLAG_HIDDEN);

  // hide all pooled rows (they belong to level 2)
  for (auto &it : pool) {
    it->set_visible(false);
  }

  // assemble the displayed categories: Favorites, Recent, keyword groups, All
  display_cats.clear();
  display_cats.push_back({"Favorites", favorites});
  display_cats.push_back({"Recent", std::vector<std::string>(history.begin(), history.end())});
  for (const auto &g : keyword_cats) {
    display_cats.push_back(g);
  }
  display_cats.push_back({"All commands", all_commands});

  rebuild_category_rows();
  lv_obj_scroll_to_y(cat_cont, 0, LV_ANIM_OFF);
  highlight_index = -1;
  if (!display_cats.empty()) {
    set_highlight(0);
  }
}

void ConsolePanel::rebuild_category_rows() {
  lv_obj_clean(cat_cont);
  for (const auto &c : display_cats) {
    lv_obj_t *row = lv_obj_create(cat_cont);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(row, 8, 0);
    lv_obj_set_style_pad_ver(row, 9, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, &ConsolePanel::_handle_category, LV_EVENT_CLICKED, this);

    lv_obj_t *name = lv_label_create(row);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_label_set_text_fmt(name, "%s (%d)", c.label.c_str(), (int)c.cmds.size());

    lv_obj_t *chev = lv_label_create(row);
    lv_label_set_text(chev, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(chev, lv_palette_main(LV_PALETTE_GREY), 0);
  }
}

void ConsolePanel::handle_category(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  lv_obj_t *row = lv_event_get_target(e);
  enter_category((int)lv_obj_get_index(row));
}

// ---- level 2: a category's command list ----

void ConsolePanel::enter_category(int idx) {
  if (idx < 0 || idx >= (int)display_cats.size()) {
    return;
  }
  active_cat = idx;
  level = COMMAND_LIST;

  lv_label_set_text(title_label, display_cats[idx].label.c_str());
  lv_textarea_set_text(filter, "");

  lv_obj_clear_flag(subhdr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(filter, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(list_cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(cat_cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

  rebuild_command_list();
}

void ConsolePanel::handle_back(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  // back walks up one level: command list -> categories -> terminal
  if (level == COMMAND_LIST) {
    show_categories();
  } else {
    show_mode(TERMINAL);
  }
}

bool ConsolePanel::is_favorite(const std::string &cmd) const {
  return std::find(favorites.begin(), favorites.end(), cmd) != favorites.end();
}

void ConsolePanel::rebuild_command_list() {
  if (active_cat < 0 || active_cat >= (int)display_cats.size()) {
    return;
  }
  const std::vector<std::string> &src = display_cats[active_cat].cmds;

  // apply filter (case-insensitive substring)
  std::string f = to_upper(lv_textarea_get_text(filter));
  std::vector<std::string> matches;
  for (const auto &s : src) {
    if (f.empty() || to_upper(s).find(f) != std::string::npos) {
      matches.push_back(s);
    }
  }

  int shown = std::min((int)matches.size(), POOL_SIZE);
  visible_items.clear();
  for (int i = 0; i < POOL_SIZE; i++) {
    if (i < shown) {
      pool[i]->set_command(matches[i]);
      pool[i]->set_favorite(is_favorite(matches[i]));
      pool[i]->set_highlight(false);
      pool[i]->set_visible(true);
      visible_items.push_back(pool[i].get());
    } else {
      pool[i]->set_visible(false);
    }
  }

  // footer when truncated
  if ((int)matches.size() > shown) {
    lv_label_set_text_fmt(footer_label, "showing %d of %d  -  filter to narrow",
			  shown, (int)matches.size());
    lv_obj_clear_flag(footer_label, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(footer_label, LV_OBJ_FLAG_HIDDEN);
  }

  // empty state
  if (matches.empty()) {
    const std::string &lbl = display_cats[active_cat].label;
    const char *msg = "No commands.";
    if (!f.empty()) {
      msg = "No commands match the filter.";
    } else if (lbl == "Favorites") {
      msg = "No favorites yet.\nStar commands in any list.";
    } else if (lbl == "Recent") {
      msg = "No recent commands yet.";
    }
    lv_label_set_text(empty_label, msg);
    lv_obj_clear_flag(empty_label, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(empty_label, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_scroll_to_y(list_cont, 0, LV_ANIM_OFF);
  highlight_index = -1;
  if (!visible_items.empty()) {
    set_highlight(0);
  }
}

// ---- shared navigation (works on whichever level is active) ----

int ConsolePanel::nav_count() const {
  if (level == CATEGORY) {
    return (int)lv_obj_get_child_cnt(cat_cont);
  }
  return (int)visible_items.size();
}

lv_obj_t *ConsolePanel::nav_obj(int idx) const {
  if (idx < 0 || idx >= nav_count()) {
    return NULL;
  }
  if (level == CATEGORY) {
    return lv_obj_get_child(cat_cont, idx);
  }
  return visible_items[idx]->get_cont();
}

void ConsolePanel::set_highlight(int idx) {
  // clear previous highlight on the active level
  if (level == CATEGORY) {
    uint32_t cnt = lv_obj_get_child_cnt(cat_cont);
    for (uint32_t i = 0; i < cnt; i++) {
      lv_obj_set_style_bg_opa(lv_obj_get_child(cat_cont, i), LV_OPA_TRANSP, LV_PART_MAIN);
    }
  } else {
    for (auto &it : pool) {
      it->set_highlight(false);
    }
  }

  highlight_index = idx;
  if (idx < 0 || idx >= nav_count()) {
    return;
  }

  lv_obj_t *obj = nav_obj(idx);
  if (level == CATEGORY) {
    lv_obj_set_style_bg_color(obj, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_30, LV_PART_MAIN);
  } else {
    visible_items[idx]->set_highlight(true);
  }
  lv_obj_scroll_to_view(obj, LV_ANIM_ON);
}

void ConsolePanel::move_highlight(int delta) {
  int cnt = nav_count();
  if (cnt == 0) {
    return;
  }
  int idx = highlight_index < 0 ? 0 : highlight_index + delta;
  if (idx < 0) {
    idx = 0;
  } else if (idx >= cnt) {
    idx = cnt - 1;
  }
  set_highlight(idx);
}

void ConsolePanel::handle_nav(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
    return;
  }
  lv_obj_t *target = lv_event_get_target(e);
  if (target == up_btn) {
    move_highlight(-1);
  } else if (target == down_btn) {
    move_highlight(1);
  } else if (target == ok_btn) {
    if (highlight_index < 0 || highlight_index >= nav_count()) {
      return;
    }
    if (level == CATEGORY) {
      enter_category(highlight_index);
    } else {
      select_command(visible_items[highlight_index]);
    }
  }
}

void ConsolePanel::select_command(CommandItem *item) {
  // insert the command into the terminal input for editing, then switch to it
  lv_textarea_set_text(input, item->command().c_str());
  reset_to_terminal();
  lv_keyboard_set_textarea(kb, input);
  lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_textarea_set_cursor_pos(input, LV_TEXTAREA_CURSOR_LAST);
}

void ConsolePanel::toggle_favorite(CommandItem *item) {
  const std::string &cmd = item->command();
  auto it = std::find(favorites.begin(), favorites.end(), cmd);
  bool nowfav;
  if (it != favorites.end()) {
    favorites.erase(it);
    nowfav = false;
  } else {
    favorites.push_back(cmd);
    nowfav = true;
  }
  item->set_favorite(nowfav);
  save_favorites();

  // on the Favorites list the row should disappear immediately
  if (active_cat >= 0 && active_cat < (int)display_cats.size() &&
      display_cats[active_cat].label == "Favorites") {
    display_cats[active_cat].cmds = favorites;
    rebuild_command_list();
  }
}

void ConsolePanel::save_favorites() {
  json h = {
    {"namespace", "guppyscreen"},
    {"key", "console.favorites"},
    {"value", favorites}
  };
  ws.send_jsonrpc("server.database.post_item", h);
}

void ConsolePanel::handle_filter_kb(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(kb, filter);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
  if (code == LV_EVENT_DEFOCUSED) {
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
  if (code == LV_EVENT_VALUE_CHANGED) {
    rebuild_command_list();
  }
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    lv_keyboard_set_textarea(kb, NULL);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_state(filter, LV_STATE_FOCUSED);
  }
}
