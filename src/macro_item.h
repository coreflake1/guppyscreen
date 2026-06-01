#ifndef __MACRO_ITEM_H__
#define __MACRO_ITEM_H__

#include "websocket_client.h"
#include "lvgl/lvgl.h"

#include <string>
#include <vector>
#include <map>
#include <utility>
#include <functional>

class MacroItem {
 public:
  MacroItem(KWebSocketClient &c,
	    lv_obj_t *parent,
	    std::string macro_name,
	    const std::map<std::string, std::string> &m_params,
	    lv_obj_t *keyboard,
	    bool favorite,
	    std::function<void()> on_favorite_changed,
	    std::function<void(MacroItem*)> on_activated,
	    std::function<void()> on_play);

  ~MacroItem();

  void handle_kb_input(lv_event_t *e);
  void handle_send_macro(lv_event_t *e);
  void handle_favorite(lv_event_t *e);
  void handle_expand(lv_event_t *e);

  bool is_favorite() const { return favorite; }
  const std::string &name() const { return mname; }
  lv_obj_t *get_cont() { return cont; }

  // view filtering (Favorites vs All)
  void set_visible(bool v);
  bool is_visible() const { return visible; }

  // keyboard-nav highlight
  void set_highlight(bool h);

  // collapse/expand of the parameter rows
  void set_expanded(bool e);
  void toggle_expand() { set_expanded(!expanded); }
  bool is_expanded() const { return expanded; }
  bool has_params() const { return !params.empty(); }

  static void _handle_kb_input(lv_event_t *e) {
    MacroItem *panel = (MacroItem*)e->user_data;
    panel->handle_kb_input(e);
  };

  static void _handle_send_macro(lv_event_t *e) {
    MacroItem *panel = (MacroItem*)e->user_data;
    panel->handle_send_macro(e);
  };

  static void _handle_favorite(lv_event_t *e) {
    MacroItem *panel = (MacroItem*)e->user_data;
    panel->handle_favorite(e);
  };

  static void _handle_expand(lv_event_t *e) {
    MacroItem *panel = (MacroItem*)e->user_data;
    panel->handle_expand(e);
  };

 private:
  void update_favorite_icon();

  KWebSocketClient &ws;
  std::string mname;
  lv_obj_t *cont;
  lv_obj_t *top_cont;
  lv_obj_t *fav_img;
  lv_obj_t *macro_label;
  lv_obj_t *params_cont;
  lv_obj_t *kb;
  bool favorite;
  bool visible;
  bool expanded;
  bool highlighted;
  std::function<void()> on_favorite_changed;
  std::function<void(MacroItem*)> on_activated;
  std::function<void()> on_play;
  std::vector<std::pair<lv_obj_t*, lv_obj_t*>> params;

};

#endif // __MACRO_ITEM_H__
