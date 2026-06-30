#ifndef __CONSOLE_PANEL_H__
#define __CONSOLE_PANEL_H__

#include "websocket_client.h"
#include "command_item.h"
#include "lvgl/lvgl.h"

#include <mutex>
#include <list>
#include <vector>
#include <memory>
#include <string>

class ConsolePanel {
 public:
  ConsolePanel(KWebSocketClient &ws, std::mutex &lock, lv_obj_t *parent);
  ~ConsolePanel();

  lv_obj_t *get_container();
  void foreground();
  void reset_to_terminal();

  // data
  void handle_macros(json &d);          // gcode.help + history + favorites
  void handle_macro_response(json &d);  // notify_gcode_response -> output

  // terminal
  void handle_input_kb(lv_event_t *e);
  void handle_send(lv_event_t *e);
  void handle_clear(lv_event_t *e);

  // commands browser
  void handle_open_commands(lv_event_t *e); // terminal -> command browser
  void handle_category(lv_event_t *e);      // a category row tapped -> drill in
  void handle_back(lv_event_t *e);          // back -> category menu
  void handle_filter_kb(lv_event_t *e);
  void handle_nav(lv_event_t *e);

  static void _handle_input_kb(lv_event_t *e)    { ((ConsolePanel*)e->user_data)->handle_input_kb(e); }
  static void _handle_send(lv_event_t *e)        { ((ConsolePanel*)e->user_data)->handle_send(e); }
  static void _handle_clear(lv_event_t *e)       { ((ConsolePanel*)e->user_data)->handle_clear(e); }
  static void _handle_open_commands(lv_event_t *e){ ((ConsolePanel*)e->user_data)->handle_open_commands(e); }
  static void _handle_category(lv_event_t *e)    { ((ConsolePanel*)e->user_data)->handle_category(e); }
  static void _handle_back(lv_event_t *e)        { ((ConsolePanel*)e->user_data)->handle_back(e); }
  static void _handle_filter_kb(lv_event_t *e)   { ((ConsolePanel*)e->user_data)->handle_filter_kb(e); }
  static void _handle_nav(lv_event_t *e)         { ((ConsolePanel*)e->user_data)->handle_nav(e); }

 private:
  enum Mode { TERMINAL = 0, COMMANDS = 1 };
  enum Level { CATEGORY = 0, COMMAND_LIST = 1 };

  // a named bucket of commands (Favorites / Recent / keyword group / Other / All)
  struct CmdCategory {
    std::string label;
    std::vector<std::string> cmds;
  };

  void show_mode(Mode m);
  void show_categories();               // commands view, level 1
  void enter_category(int idx);         // commands view, level 2
  void build_keyword_cats();            // partition all_commands once (cached)
  void rebuild_category_rows();         // (re)draw the level-1 menu
  void rebuild_command_list();          // populate the pool for the active category

  int  nav_count() const;               // navigable rows on the current level
  lv_obj_t *nav_obj(int idx) const;
  void set_highlight(int idx);
  void move_highlight(int delta);

  void select_command(CommandItem *item);
  void toggle_favorite(CommandItem *item);
  bool is_favorite(const std::string &cmd) const;
  void send_command(const std::string &cmd);
  void append_output(const std::string &line);
  void trim_output_lines();
  void push_history(const std::string &cmd);
  void save_favorites();

  KWebSocketClient &ws;
  std::mutex &lv_lock;

  lv_obj_t *console_cont;

  // terminal view
  lv_obj_t *term_view;
  lv_obj_t *output;
  lv_obj_t *input_bar;
  lv_obj_t *input;

  // commands view
  lv_obj_t *cmd_view;
  lv_obj_t *subhdr;        // back row (level 2 only)
  lv_obj_t *back_btn;
  lv_obj_t *title_label;
  lv_obj_t *cmd_body;
  lv_obj_t *cat_cont;      // level-1 category rows
  lv_obj_t *list_cont;     // level-2 pooled command rows
  lv_obj_t *empty_label;
  lv_obj_t *footer_label;
  lv_obj_t *nav_cont;
  lv_obj_t *up_btn;
  lv_obj_t *ok_btn;
  lv_obj_t *down_btn;
  lv_obj_t *filter;

  lv_obj_t *kb;

  Mode mode;
  Level level;
  int highlight_index;
  int active_cat;          // index into display_cats while at level 2

  std::vector<std::string> all_commands;   // gcode.help (no '_' private), sorted
  std::list<std::string> history;          // recent first
  std::vector<std::string> favorites;      // pinned commands

  std::vector<CmdCategory> keyword_cats;   // cached partition of all_commands (incl "Other")
  std::vector<CmdCategory> display_cats;   // what the level-1 menu currently shows

  std::vector<std::shared_ptr<CommandItem>> pool;
  std::vector<CommandItem*> visible_items;
};

#endif // __CONSOLE_PANEL_H__
