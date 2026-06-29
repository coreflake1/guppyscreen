#ifndef __WIFI_PANEL_H__
#define __WIFI_PANEL_H__

#include "wpa_event.h"
#include "button_container.h"
#include "lvgl/lvgl.h"
#include <mutex>

#include <atomic>
#include <map>
#include <set>
#include <string>

class WifiPanel {
public:
  WifiPanel(std::mutex &l);

  ~WifiPanel();

  void foreground();
  void handle_back_btn(lv_event_t *event);
  void handle_callback(lv_event_t *event);
  void handle_pm_toggle(lv_event_t *event);
  void refresh_pm_label();
  void handle_wpa_event(const std::string &events);
  void handle_kb_input(lv_event_t *e);
  void handle_eye_btn(lv_event_t *e);
  void wait_for_connectivity(const std::string &iface, const std::string &net, uint32_t gen);
  void connect(const char *);
  bool find_current_network();

  static void _handle_back_btn(lv_event_t *event) {
    WifiPanel *panel = (WifiPanel *)event->user_data;
    panel->handle_back_btn(event);
  };

  static void _handle_callback(lv_event_t *event) {
    WifiPanel *panel = (WifiPanel *)event->user_data;
    panel->handle_callback(event);
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
  std::mutex &lv_lock;
  WpaEvent wpa_event;
  lv_obj_t *cont;
  lv_obj_t *spinner;
  lv_obj_t *top_cont;
  lv_obj_t *wifi_table;
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
  std::map<std::string, std::string> list_networks;
  std::map<std::string, int> wifi_name_db;
  bool entering_password = false;
  bool pw_visible = false;
  std::atomic<uint32_t> conn_gen{0};

};

#endif // __WIFI_PANEL_H__
