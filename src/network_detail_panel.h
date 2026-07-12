#ifndef __NETWORK_DETAIL_PANEL_H__
#define __NETWORK_DETAIL_PANEL_H__

#include "button_container.h"
#include "lvgl/lvgl.h"

#include <functional>
#include <mutex>
#include <string>

// Reached by tapping the currently-connected row in WifiPanel. Replaces the
// old pencil-icon-in-a-table-cell pattern (a hidden, undiscoverable feature
// whose icon meaning silently changed only for the connected row) with two
// plainly-labelled buttons. Read-only: it never talks to wpa_supplicant or
// the static-ip backend itself, only reports taps back to WifiPanel via the
// callbacks handed to the constructor - WifiPanel already owns the
// WpaEvent/StaticIpPanel plumbing those actions need.
class NetworkDetailPanel {
public:
  NetworkDetailPanel(std::mutex &l,
                      std::function<void()> on_configure_static_ip,
                      std::function<void()> on_forget);
  ~NetworkDetailPanel();

  void foreground(const std::string &ssid,
                   const std::string &signal_text,
                   const std::string &security_text,
                   const std::string &ip_text);
  void hide();

  void handle_back_btn(lv_event_t *e);
  void handle_configure_btn(lv_event_t *e);
  void handle_forget_btn(lv_event_t *e);

  static void _handle_back_btn(lv_event_t *e) { ((NetworkDetailPanel *)e->user_data)->handle_back_btn(e); }
  static void _handle_configure_btn(lv_event_t *e) { ((NetworkDetailPanel *)e->user_data)->handle_configure_btn(e); }
  static void _handle_forget_btn(lv_event_t *e) { ((NetworkDetailPanel *)e->user_data)->handle_forget_btn(e); }

private:
  std::mutex &lv_lock;
  lv_obj_t *cont;
  lv_obj_t *title_label;
  lv_obj_t *info_card;
  lv_obj_t *signal_val;
  lv_obj_t *security_val;
  lv_obj_t *ip_val;
  lv_obj_t *configure_btn;
  lv_obj_t *forget_btn;
  ButtonContainer back_btn;

  std::function<void()> on_configure_static_ip;
  std::function<void()> on_forget;
};

#endif // __NETWORK_DETAIL_PANEL_H__
