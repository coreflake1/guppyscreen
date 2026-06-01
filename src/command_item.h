#ifndef __COMMAND_ITEM_H__
#define __COMMAND_ITEM_H__

#include "lvgl/lvgl.h"

#include <string>
#include <functional>

// A single reusable row in the console command browser: a favorite star + the
// command text. Rows are pooled by ConsolePanel (created once, repopulated on
// tab/filter change) so the 170-entry gcode list never spawns 170 live widgets.
class CommandItem {
 public:
  CommandItem(lv_obj_t *parent,
	      std::function<void(CommandItem*)> on_select,
	      std::function<void(CommandItem*)> on_favorite);
  ~CommandItem();

  void set_command(const std::string &cmd);
  const std::string &command() const { return cmd; }

  void set_favorite(bool fav);
  bool is_favorite() const { return favorite; }

  void set_highlight(bool h);

  void set_visible(bool v);
  bool is_visible() const { return visible; }

  lv_obj_t *get_cont() { return cont; }

  void handle_select(lv_event_t *e);
  void handle_favorite(lv_event_t *e);

  static void _handle_select(lv_event_t *e) {
    CommandItem *item = (CommandItem*)e->user_data;
    item->handle_select(e);
  };
  static void _handle_favorite(lv_event_t *e) {
    CommandItem *item = (CommandItem*)e->user_data;
    item->handle_favorite(e);
  };

 private:
  void update_favorite_icon();

  lv_obj_t *cont;
  lv_obj_t *fav_img;
  lv_obj_t *label;
  std::string cmd;
  bool favorite;
  bool visible;
  std::function<void(CommandItem*)> on_select;
  std::function<void(CommandItem*)> on_favorite;
};

#endif // __COMMAND_ITEM_H__
