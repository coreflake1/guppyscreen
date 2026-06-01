#ifndef __NOTIFICATION_MANAGER_H__
#define __NOTIFICATION_MANAGER_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"

#include <mutex>
#include <list>
#include <string>

// Session-lived manager that turns Klipper/Moonraker events into on-screen
// toasts. Subscribes to notify_status_update (webhooks, print_stats, filament
// sensor, display_status) and notify_gcode_response ("!!" errors). Renders a
// stacking toast overlay on lv_layer_top() so it shows above every panel.
class NotificationManager : public NotifyConsumer {
 public:
  NotificationManager(KWebSocketClient &ws, std::mutex &lock);
  ~NotificationManager();

  void consume(json &j);             // notify_status_update
  void handle_gcode_response(json &j);

  // toast lifecycle (called from LVGL thread)
  void remove_card(lv_obj_t *card, bool from_timer);

#ifdef SIMULATOR
  void sim_demo();  // show one toast of each severity (no printer in the sim)
#endif

  static void _tap_cb(lv_event_t *e);
  static void _timer_cb(lv_timer_t *t);
  static void _homing_mbox_cb(lv_event_t *e);

 private:
  enum Severity { INFO, WARNING, ERROR };
  struct Toast { std::string text; lv_obj_t *card; lv_timer_t *timer; };

  void baseline();                   // snapshot watched fields from State (no toasts)
  void process(json &status);        // diff a status object, fire toasts
  void push(const std::string &text, Severity sev);
  void show_homing_prompt();         // modal on "Must home axis first"
  bool is_printing() const { return last_print_state == "printing"; }

  KWebSocketClient &ws;
  lv_obj_t *cont;                    // toast stack on lv_layer_top()
  std::list<Toast> toasts;           // front = oldest
  lv_obj_t *homing_mbox;             // open homing prompt, or NULL

  bool baselined;
  std::string last_webhooks_state;
  std::string last_print_state;
  std::string last_display_msg;
};

#endif  // __NOTIFICATION_MANAGER_H__
