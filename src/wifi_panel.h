#ifndef __WIFI_PANEL_H__
#define __WIFI_PANEL_H__

#include "wpa_event.h"
#include "button_container.h"
#include "static_ip_panel.h"
#include "network_detail_panel.h"
#include "wifi_row_item.h"
#include "lvgl/lvgl.h"
#include <mutex>

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

class WifiPanel {
public:
  WifiPanel(std::mutex &l);

  ~WifiPanel();

  void foreground();
  void handle_back_btn(lv_event_t *event);
  void handle_pm_toggle(lv_event_t *event);
  void refresh_pm_label();
  void handle_wpa_event(const std::string &events);
  void confirm_remove_network(const std::string &ssid, const std::string &nid);
  void handle_kb_input(lv_event_t *e);
  void handle_eye_btn(lv_event_t *e);
  void wait_for_connectivity(const std::string &iface, const std::string &net, uint32_t gen);
  void connect(const char *);
  bool find_current_network();

  // WifiRowItem calls this when its row is tapped: connects/reconnects for
  // an ordinary row, or opens NetworkDetailPanel if the row is the network
  // we're already connected to (where "connect" wouldn't mean anything).
  void handle_row_activated(const std::string &ssid);

  // NetworkDetailPanel's two buttons call back into here - it owns the
  // WpaEvent connection and the StaticIpPanel instance those actions need.
  void open_static_ip_for_current();
  void forget_current_network();

  static void _handle_back_btn(lv_event_t *event) {
    WifiPanel *panel = (WifiPanel *)event->user_data;
    panel->handle_back_btn(event);
  };

  static void _handle_pm_toggle(lv_event_t *event) {
    WifiPanel *panel = (WifiPanel *)event->user_data;
    panel->handle_pm_toggle(event);
  };

  static void _handle_kb_input(lv_event_t *e) {
    WifiPanel *panel = (WifiPanel *)e->user_data;
    panel->handle_kb_input(e);
  };

  static void _handle_eye_btn(lv_event_t *e) {
    WifiPanel *panel = (WifiPanel *)e->user_data;
    panel->handle_eye_btn(e);
  };

private:
  void rebuild_wifi_rows();

  std::mutex &lv_lock;
  WpaEvent wpa_event;
  lv_obj_t *cont;
  lv_obj_t *spinner;
  lv_obj_t *top_cont;
  lv_obj_t *wifi_list_cont;
  lv_obj_t *wifi_right;
  lv_obj_t *prompt_cont;
  lv_obj_t *wifi_label;
  lv_obj_t *pw_row;
  lv_obj_t *password_input;
  lv_obj_t *eye_btn;
  ButtonContainer back_btn;
  lv_obj_t *kb;
  lv_obj_t *pm_cont;
  lv_obj_t *pm_btn;
  lv_obj_t *pm_label;
  lv_obj_t *pm_hint;
  std::string selected_network;
  std::string cur_network;
  std::string pending_remove_nid;
  std::map<std::string, std::string> list_networks;

  // Everything the list needs to know about one SSID seen in a scan.
  // "missed" is how many consecutive scans it's been absent from - lets
  // rebuild_wifi_rows age a network out gradually instead of dropping it the
  // instant a single noisy scan misses it.
  struct WifiEntry {
    int signal = -100;
    bool secured = false;
    int missed = 0;
  };
  std::map<std::string, WifiEntry> wifi_name_db;
  std::vector<std::unique_ptr<WifiRowItem>> wifi_rows;

  bool entering_password = false;
  bool pw_visible = false;
  int rescan_budget = 0;
  size_t last_scan_count = 0;
  std::atomic<uint32_t> conn_gen{0};
  StaticIpPanel static_ip_panel;
  NetworkDetailPanel network_detail_panel;

};

#endif // __WIFI_PANEL_H__
