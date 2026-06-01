#ifndef __FIRMWARE_RETRACTION_PANEL_H__
#define __FIRMWARE_RETRACTION_PANEL_H__

#include "lvgl/lvgl.h"
#include "selector.h"
#include "websocket_client.h"
#include "notify_consumer.h"

#include <mutex>

// Live-tuning panel for Klipper's [firmware_retraction], reached from the Tune
// tab. One labelled row per SET_RETRACTION value (name + current value + -/+),
// a length-step and speed-step selector, and a Reset-all. Gated on the
// firmware_retraction object existing; shows an empty-state when it's absent.
class FirmwareRetractionPanel : public NotifyConsumer {
 public:
  FirmwareRetractionPanel(KWebSocketClient &, std::mutex &);
  ~FirmwareRetractionPanel();
  void foreground();
  void consume(json &j);

  void handle_callback(lv_event_t *event);
  static void _handle_callback(lv_event_t *e) { ((FirmwareRetractionPanel*)e->user_data)->handle_callback(e); }

 private:
  // one tunable retraction field
  struct Field {
    const char *name;     // display name
    const char *param;    // SET_RETRACTION parameter
    const char *key;      // firmware_retraction status / config key
    const char *unit;     // "mm" / "mm/s"
    bool is_speed;        // integer + speed step (vs. 2-decimal + length step)
    lv_obj_t *minus;
    lv_obj_t *plus;
    lv_obj_t *value;
  };

  void build_row(Field &f);
  void apply_step(Field &f, bool up);
  void send_field(const Field &f, double value);
  double cur_value(const Field &f);
  double config_default(const Field &f);
  void refresh_values();              // from State
  void update_from(json &fr);         // from a status object

  KWebSocketClient &ws;
  lv_obj_t *panel_cont;
  lv_obj_t *body;
  lv_obj_t *list_area;
  lv_obj_t *step_row;
  lv_obj_t *bottom_row;
  lv_obj_t *empty_cont;
  lv_obj_t *reset_all_btn;
  lv_obj_t *back_btn;
  lv_obj_t *empty_back_btn;
  Selector length_step_selector;
  Selector speed_step_selector;

  Field fields[4];
};

#endif  // __FIRMWARE_RETRACTION_PANEL_H__
