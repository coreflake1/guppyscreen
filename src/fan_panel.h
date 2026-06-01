#ifndef __FAN_PANEL_H__
#define __FAN_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"
#include "slider_container.h"
#include "button_container.h"

#include <map>
#include <memory>
#include <mutex>

class FanPanel : public NotifyConsumer {
 public:
  FanPanel(KWebSocketClient &ws, std::mutex &lock);
  ~FanPanel();

  void consume(json &j);
  
  lv_obj_t *get_container();
  void create_fans(json &f);
  void foreground();
  void handle_callback(lv_event_t *event);
  void handle_fan_update(lv_event_t *event);
  void handle_fan_update_part_fan(lv_event_t *event);
  void handle_fan_update_generic(lv_event_t *event);

  static void _handle_callback(lv_event_t *event) {
    FanPanel *panel = (FanPanel*)event->user_data;
    panel->handle_callback(event);
  };

  static void _handle_fan_update(lv_event_t *event) {
    FanPanel *panel = (FanPanel*)event->user_data;
    panel->handle_fan_update(event);
  };

  static void _handle_fan_update_part_fan(lv_event_t *event) {
    FanPanel *panel = (FanPanel*)event->user_data;
    panel->handle_fan_update_part_fan(event);
  };
  
  static void _handle_fan_update_generic(lv_event_t *event) {
    FanPanel *panel = (FanPanel*)event->user_data;
    panel->handle_fan_update_generic(event);
  };

 private:
  void create_readonly_fans();                  // read-only fans (heater_fan, controller_fan, fan output pins)
  void update_readonly();
  bool is_binary_fan(const std::string &key);
  std::string fmt_ro_value(const std::string &key);

  KWebSocketClient &ws;
  lv_obj_t *fanpanel_cont;
  lv_obj_t *fans_cont;
  std::map<std::string, std::shared_ptr<SliderContainer>> fans;
  std::map<std::string, lv_obj_t*> ro_labels;   // read-only fan id -> value label
  ButtonContainer back_btn;
};

#endif // __FAN_PANEL_H__
