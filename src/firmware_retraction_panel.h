#ifndef __FIRMWARE_RETRACTION_PANEL_H__
#define __FIRMWARE_RETRACTION_PANEL_H__

#include "lvgl/lvgl.h"
#include "button_container.h"
#include "selector.h"
#include "image_label.h"
#include "websocket_client.h"
#include "notify_consumer.h"

#include <mutex>

// Live-tuning panel for Klipper's [firmware_retraction], modelled on FineTune.
// Adjusts the four SET_RETRACTION values. Gated on the firmware_retraction
// printer object existing; shows an empty-state when it's absent.
class FirmwareRetractionPanel : public NotifyConsumer {
 public:
  FirmwareRetractionPanel(KWebSocketClient &, std::mutex &);
  ~FirmwareRetractionPanel();
  void foreground();
  void consume(json &j);

  void handle_callback(lv_event_t *event);  // selectors + back
  void handle_retract_length(lv_event_t *event);
  void handle_retract_speed(lv_event_t *event);
  void handle_unretract_extra(lv_event_t *event);
  void handle_unretract_speed(lv_event_t *event);

  static void _handle_callback(lv_event_t *e)        { ((FirmwareRetractionPanel*)e->user_data)->handle_callback(e); }
  static void _handle_retract_length(lv_event_t *e)  { ((FirmwareRetractionPanel*)e->user_data)->handle_retract_length(e); }
  static void _handle_retract_speed(lv_event_t *e)   { ((FirmwareRetractionPanel*)e->user_data)->handle_retract_speed(e); }
  static void _handle_unretract_extra(lv_event_t *e) { ((FirmwareRetractionPanel*)e->user_data)->handle_unretract_extra(e); }
  static void _handle_unretract_speed(lv_event_t *e) { ((FirmwareRetractionPanel*)e->user_data)->handle_unretract_speed(e); }

 private:
  // current value (from state) for a retraction field, or NaN if unavailable
  double cur_value(const char *field);
  double config_default(const char *field);
  void update_values_from(json &src, const char *base_ptr);

  KWebSocketClient &ws;
  lv_obj_t *panel_cont;
  lv_obj_t *body;          // all controls (shown when the object exists)
  lv_obj_t *empty_cont;    // empty-state (shown when the object is absent)
  lv_obj_t *values_cont;

  ButtonContainer rl_reset_btn, rl_up_btn, rl_down_btn;
  ButtonContainer rs_reset_btn, rs_up_btn, rs_down_btn;
  ButtonContainer ue_reset_btn, ue_up_btn, ue_down_btn;
  ButtonContainer us_reset_btn, us_up_btn, us_down_btn;
  ButtonContainer back_btn;
  ButtonContainer empty_back_btn;
  Selector length_step_selector;  // mm steps (lengths)
  Selector speed_step_selector;   // mm/s steps (speeds)
  ImageLabel retract_length_lbl;
  ImageLabel retract_speed_lbl;
  ImageLabel unretract_extra_lbl;
  ImageLabel unretract_speed_lbl;
};

#endif  // __FIRMWARE_RETRACTION_PANEL_H__
