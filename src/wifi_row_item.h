#ifndef __WIFI_ROW_ITEM_H__
#define __WIFI_ROW_ITEM_H__

#include "lvgl/lvgl.h"

#include <functional>
#include <string>

// One row in WifiPanel's network list. Exactly one tap target per row - the
// row body activates the network (connect, or open details if it's already
// the connected one); a separately-spaced trailing icon, present only on
// known-but-not-connected rows, forgets the saved network. A prior design
// crammed a status icon and an edit icon into two adjacent narrow table
// columns instead - a mistap between them deleted a saved network's password
// live on-device, which is the failure mode this row layout exists to avoid.
class WifiRowItem {
public:
  WifiRowItem(lv_obj_t *parent,
              const std::string &ssid,
              int signal_dbm,
              bool connected,
              bool saved,
              const std::string &nid,
              std::function<void(const std::string &ssid)> on_activate,
              std::function<void(const std::string &ssid, const std::string &nid)> on_forget);
  ~WifiRowItem();

  lv_obj_t *get_container() { return cont; }

  void handle_row_click(lv_event_t *e);
  void handle_forget_click(lv_event_t *e);

  static void _handle_row_click(lv_event_t *e) {
    ((WifiRowItem *)e->user_data)->handle_row_click(e);
  }
  static void _handle_forget_click(lv_event_t *e) {
    ((WifiRowItem *)e->user_data)->handle_forget_click(e);
  }

private:
  void build_signal_bars(lv_obj_t *parent, int dbm);

  std::string ssid;
  std::string nid;
  lv_obj_t *cont;
  std::function<void(const std::string &)> on_activate;
  std::function<void(const std::string &, const std::string &)> on_forget;
};

#endif // __WIFI_ROW_ITEM_H__
