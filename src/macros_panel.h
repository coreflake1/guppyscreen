#ifndef __MACROS_PANEL_H__
#define __MACROS_PANEL_H__

#include "websocket_client.h"
#include "macro_item.h"
#include "lvgl/lvgl.h"

#include <vector>
#include <memory>
#include <mutex>

class MacrosPanel {
 public:
  MacrosPanel(KWebSocketClient &c, std::mutex &l, lv_obj_t *parent);
  ~MacrosPanel();

  void populate();
  void reset_to_favorites();
  void handle_view_changed(lv_event_t *e);
  void handle_nav(lv_event_t *e);

  static void _handle_view_changed(lv_event_t *e) {
    MacrosPanel *panel = (MacrosPanel*)e->user_data;
    panel->handle_view_changed(e);
  };

  static void _handle_nav(lv_event_t *e) {
    MacrosPanel *panel = (MacrosPanel*)e->user_data;
    panel->handle_nav(e);
  };

 private:
  enum View { FAVORITES = 0, ALL = 1 };

  void apply_view();
  void rebuild_visible();
  void move_highlight(int delta);
  void set_highlight(int idx);
  void highlight_item(MacroItem *m);

  KWebSocketClient &ws;
  std::mutex &lv_lock;
  lv_obj_t *cont;
  lv_obj_t *top_controls;
  lv_obj_t *view_toggle;
  lv_obj_t *body;
  lv_obj_t *top_cont;
  lv_obj_t *empty_label;
  lv_obj_t *nav_cont;
  lv_obj_t *up_btn;
  lv_obj_t *ok_btn;
  lv_obj_t *down_btn;
  lv_obj_t *kb;
  View view;
  int highlight_index;
  std::vector<std::shared_ptr<MacroItem>> macro_items;
  std::vector<MacroItem*> visible_items;

};

#endif // __MACROS_PANEL_H__
