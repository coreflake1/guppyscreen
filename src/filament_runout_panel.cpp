#include "filament_runout_panel.h"
#include "state.h"
#include "config.h"
#include "utils.h"
#include "spdlog/spdlog.h"

FilamentRunoutPanel::FilamentRunoutPanel(KWebSocketClient &websocket_client, std::mutex &lock)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , mbox(NULL)
  , baselined(false)
  , last_detected(true)
{
  ws.register_notify_update(this);

#ifdef SIMULATOR
  // no printer in the sim: pop the dialog once so it can be inspected
  lv_timer_t *t = lv_timer_create([](lv_timer_t *tm) {
    ((FilamentRunoutPanel *)tm->user_data)->sim_show();
  }, 3000, this);
  lv_timer_set_repeat_count(t, 1);
#endif
}

FilamentRunoutPanel::~FilamentRunoutPanel() {
  ws.unregister_notify_update(this);
  close();
}

void FilamentRunoutPanel::show() {
  if (mbox != NULL) {
    return;  // already open
  }
  static const char *btns[] = {"Load", "Continue", "Cancel", ""};
  mbox = lv_msgbox_create(NULL, "Filament runout",
    "Load new filament, then Continue.", btns, false);
  KUtils::style_lock_mbox(mbox, 95);  // centered card + blue buttons, like other dialogs
  lv_obj_add_event_cb(mbox, &FilamentRunoutPanel::_mbox_cb, LV_EVENT_VALUE_CHANGED, this);
}

void FilamentRunoutPanel::close() {
  if (mbox != NULL) {
    lv_msgbox_close(mbox);
    mbox = NULL;
  }
}

bool FilamentRunoutPanel::printing_or_paused() const {
  auto v = State::get_instance()->get_data("/printer_state/print_stats/state"_json_pointer);
  if (!v.is_string()) {
    return false;
  }
  std::string s = v.template get<std::string>();
  return s == "printing" || s == "paused";
}

bool FilamentRunoutPanel::filament_present() const {
  if (fil_key.empty()) {
    return true;
  }
  auto v = State::get_instance()->get_data(
    json::json_pointer("/printer_state/" + fil_key + "/filament_detected"));
  return v.is_boolean() ? v.template get<bool>() : true;
}

std::string FilamentRunoutPanel::load_macro_gcode() const {
  std::string macro = "LOAD_FILAMENT";
  Config *conf = Config::get_instance();
  auto v = conf->get_json(conf->df() + "default_macros/load_filament");
  if (v.is_string()) {
    macro = v.template get<std::string>();
  }
  if (macro == "_GUPPY_LOAD_MATERIAL") {
    auto t = State::get_instance()->get_data("/printer_state/extruder/target"_json_pointer);
    int temp = (t.is_number() && t.template get<double>() > 0) ? (int)t.template get<double>() : 220;
    return fmt::format("{} EXTRUDER_TEMP={} EXTRUDE_LEN=50", macro, temp);
  }
  return macro;
}

void FilamentRunoutPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);

  if (!baselined) {
    // discover the sensor + its current reading from full state, fire nothing
    auto ps = State::get_instance()->get_data("/printer_state"_json_pointer);
    if (ps.is_object()) {
      for (auto &el : ps.items()) {
        if (el.key().rfind("filament_switch_sensor ", 0) == 0 ||
            el.key().rfind("filament_motion_sensor ", 0) == 0) {
          fil_key = el.key();
          if (el.value()["filament_detected"].is_boolean()) {
            last_detected = el.value()["filament_detected"].template get<bool>();
          }
          break;
        }
      }
    }
    baselined = true;
    return;
  }

  if (fil_key.empty() || !j.contains("params") || !j["params"].is_array() || j["params"].empty()) {
    return;
  }
  auto &st = j["params"][0];
  if (!st.is_object() || !st.contains(fil_key)) {
    return;
  }
  auto &fo = st[fil_key];
  bool enabled = fo["enabled"].is_boolean() ? fo["enabled"].template get<bool>() : true;
  if (!fo["filament_detected"].is_boolean()) {
    return;
  }
  bool det = fo["filament_detected"].template get<bool>();
  if (det == last_detected) {
    return;
  }
  last_detected = det;
  // runout: only matters mid-print and while the sensor is enabled
  if (!det && enabled && printing_or_paused()) {
    show();
  }
}

void FilamentRunoutPanel::handle_mbox(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || mbox == NULL) {
    return;
  }
  uint16_t id = lv_msgbox_get_active_btn(mbox);
  if (id == 0) {            // Load filament — keep the dialog open
    ws.gcode_script(load_macro_gcode());
  } else if (id == 1) {     // Continue — re-check the sensor, then resume
    if (filament_present()) {
      ws.gcode_script("RESUME");
      close();
    } else {
      KUtils::notify_toast("No filament detected yet.");
    }
  } else if (id == 2) {     // Cancel print
    ws.gcode_script("CANCEL_PRINT");
    close();
  }
}
