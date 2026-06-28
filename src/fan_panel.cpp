#include "fan_panel.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cmath>

LV_IMG_DECLARE(cancel);
LV_IMG_DECLARE(fan_on);
LV_IMG_DECLARE(back);

FanPanel::FanPanel(KWebSocketClient &websocket_client, std::mutex &lock)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , fanpanel_cont(lv_obj_create(lv_scr_act()))
  , fans_cont(lv_obj_create(fanpanel_cont))
  , back_btn(fanpanel_cont, &back, "Back", &FanPanel::_handle_callback, this)
{
  lv_obj_set_style_pad_all(fanpanel_cont, 0, 0);

  lv_obj_clear_flag(fanpanel_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(fanpanel_cont, LV_PCT(100), LV_PCT(100));

  lv_obj_center(fans_cont);
  lv_obj_set_size(fans_cont, lv_pct(80), lv_pct(100));
  lv_obj_set_flex_flow(fans_cont, LV_FLEX_FLOW_COLUMN);
#ifdef GUPPY_SMALL_SCREEN
  lv_obj_set_style_pad_bottom(fans_cont, 60, 0);
#endif

  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, -20);
  ws.register_notify_update(this);
}

FanPanel::~FanPanel() {
  if (fanpanel_cont != NULL) {
    lv_obj_del(fanpanel_cont);
    fanpanel_cont = NULL;
  }

  fans.clear();

  ws.unregister_notify_update(this);
}

void FanPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  for (auto &f : fans) {
    // hack for output_pin fans
    auto fan_value = j[json::json_pointer(fmt::format("/params/0/{}/value", f.first))];
    if (!fan_value.is_null()) {
      int v = static_cast<int>(fan_value.template get<double>() * 100);
      f.second->update_value(v);
    }

    fan_value = j[json::json_pointer(fmt::format("/params/0/{}/speed", f.first))];
    if (!fan_value.is_null()) {
      int v = static_cast<int>(fan_value.template get<double>() * 100);
      f.second->update_value(v);
    }
  }
  update_readonly();
}

void FanPanel::create_fans(json &f) {
  std::lock_guard<std::mutex> lock(lv_lock);
  fans.clear();
  // drop any read-only rows from a previous build (e.g. on reconnect)
  for (auto &kv : ro_labels) {
    lv_obj_t *row = lv_obj_get_parent(kv.second);
    if (row != NULL) {
      lv_obj_del(row);
    }
  }
  ro_labels.clear();

  for (auto &fan : f.items()) {
    std::string key = fan.key();
    spdlog::trace("create fan {}, {}", f.dump(), fan.value().dump());
    std::string display_name = fan.value()["display_name"].template get<std::string>();

    lv_event_cb_t fan_cb = &FanPanel::_handle_fan_update;
    if (key == "fan") {
      fan_cb = &FanPanel::_handle_fan_update_part_fan;
    } else if (key.rfind("output_pin ", 0) != 0) {
      // generic_fan, controller_fan, etc.
      fan_cb = &FanPanel::_handle_fan_update_generic;
    }
    auto fptr = std::make_shared<SliderContainer>(fans_cont, display_name.c_str(), &cancel, "Off",
      &fan_on, "Max", fan_cb, this);
    fans.insert({key, fptr});
    // lv_obj_set_grid_cell(fptr->get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, rowidx++, 1);
  }

  create_readonly_fans();

#ifdef GUPPY_SMALL_SCREEN
  const size_t scroll_threshold = 2;
#else
  const size_t scroll_threshold = 3;
#endif
  if (fans.size() + ro_labels.size() > scroll_threshold) {
    lv_obj_add_flag(fans_cont, LV_OBJ_FLAG_SCROLLABLE);
  } else {
    lv_obj_clear_flag(fans_cont, LV_OBJ_FLAG_SCROLLABLE);
  }

  lv_obj_move_foreground(back_btn.get_container());
}

// Read-only fans: heater_fan / controller_fan (auto-managed by Klipper) and any
// fan-named output_pin that isn't already an editable slider. Shown as a simple
// name + value row (no controls).
void FanPanel::create_readonly_fans() {
  State *s = State::get_instance();
  std::vector<std::string> candidates = s->get_fans();
  for (auto &op : s->get_output_pins()) {
    std::string lower = op;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.find("fan") != std::string::npos) {
      candidates.push_back(op);
    }
  }

  for (auto &key : candidates) {
    if (fans.count(key) || ro_labels.count(key)) {
      continue;  // already shown as an editable slider, or already added
    }
    lv_obj_t *row = lv_obj_create(fans_cont);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_hor(row, 10, 0);
    lv_obj_set_style_pad_ver(row, 10, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, KUtils::fan_display_name(key).c_str());
    lv_obj_set_style_text_color(name, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);

    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, "-");
    ro_labels[key] = val;
  }

  update_readonly();
}

bool FanPanel::is_binary_fan(const std::string &key) {
  if (key.rfind("heater_fan ", 0) == 0 || key.rfind("controller_fan ", 0) == 0) {
    return true;  // auto-managed, effectively on/off
  }
  if (key.rfind("output_pin ", 0) == 0) {
    std::string lower = key;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    auto pwm = State::get_instance()->get_data(
      json::json_pointer("/printer_state/configfile/settings/" + lower + "/pwm"));
    // output_pin defaults to non-PWM (binary) unless pwm: True is configured
    return pwm.is_boolean() ? !pwm.template get<bool>() : true;
  }
  return false;  // fan_generic etc. -> show a percentage
}

std::string FanPanel::fmt_ro_value(const std::string &key) {
  State *s = State::get_instance();
  double v = 0.0;
  bool have = false;
  auto sp = s->get_data(json::json_pointer("/printer_state/" + key + "/speed"));
  if (sp.is_number()) {
    v = sp.template get<double>();
    have = true;
  } else {
    auto val = s->get_data(json::json_pointer("/printer_state/" + key + "/value"));
    if (val.is_number()) {
      v = val.template get<double>();
      have = true;
    }
  }
  if (!have) {
    return "-";
  }
  if (is_binary_fan(key)) {
    return v > 0.0 ? "On" : "Off";
  }
  std::string out = fmt::format("{}%", (int)std::lround(v * 100.0));
  auto rpm = s->get_data(json::json_pointer("/printer_state/" + key + "/rpm"));
  if (rpm.is_number()) {
    out += fmt::format("  {} RPM", (int)std::lround(rpm.template get<double>()));
  }
  return out;
}

void FanPanel::update_readonly() {
  for (auto &kv : ro_labels) {
    lv_label_set_text(kv.second, fmt_ro_value(kv.first).c_str());
  }
}

void FanPanel::foreground() {
  for (auto &f : fans) {
    // hack for output_pin fans
    auto fan_value = State::get_instance()
      ->get_data(json::json_pointer(fmt::format("/printer_state/{}/value", f.first)));
    if (!fan_value.is_null()) {
      int v = static_cast<int>(fan_value.template get<double>() * 100);
      f.second->update_value(v);
    }

    fan_value = State::get_instance()
      ->get_data(json::json_pointer(fmt::format("/printer_state/{}/speed", f.first)));
    if (!fan_value.is_null()) {
      int v = static_cast<int>(fan_value.template get<double>() * 100);
      f.second->update_value(v);
    }
  }

  update_readonly();
  lv_obj_move_foreground(back_btn.get_container());
  lv_obj_move_foreground(fanpanel_cont);
}

void FanPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);
  if (btn == back_btn.get_container()) {
    lv_obj_move_background(fanpanel_cont);
  } else {
    spdlog::debug("Unknown action button pressed");
  }
}

void FanPanel::handle_fan_update(lv_event_t *event) {
  lv_obj_t *obj = lv_event_get_target(event);

  if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
    double pct = 255 * (double)lv_slider_get_value(obj) / 100.0;

    spdlog::trace("updating fan speed to {}", pct);
    for (auto &f : fans) {
      if (obj == f.second->get_slider()) {
        std::string fan_name = KUtils::get_obj_name(f.first);
        spdlog::trace("update fan {}", fan_name);
        ws.gcode_script(fmt::format(fmt::format("SET_PIN PIN={} VALUE={}", fan_name, pct)));
        break;
      }
    }
  } else if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    obj = lv_event_get_current_target(event);
    for (auto &f : fans) {
      if (obj == f.second->get_off()) {
        std::string fan_name = KUtils::get_obj_name(f.first);
        spdlog::trace("turning off fan {}", fan_name);
        ws.gcode_script(fmt::format("SET_PIN PIN={} VALUE=0", fan_name));
        f.second->update_value(0);
        break;
      } else if (obj == f.second->get_max()) {
        std::string fan_name = KUtils::get_obj_name(f.first);
        spdlog::trace("turning fan to max {}", fan_name);
        ws.gcode_script(fmt::format("SET_PIN PIN={} VALUE=255", fan_name));
        f.second->update_value(100);
        break;
      }
    }
  }
}

void FanPanel::handle_fan_update_part_fan(lv_event_t *event) {
  lv_obj_t *obj = lv_event_get_target(event);

  if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
    double pct = 255 * (double)lv_slider_get_value(obj) / 100.0;

    spdlog::trace("updating part fan speed to {}", pct);
    for (auto &f : fans) {
      if (obj == f.second->get_slider()) {
        ws.gcode_script(fmt::format(fmt::format("M106 S{}", pct)));
        break;
      }
    }

  } else if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    obj = lv_event_get_current_target(event);

    for (auto &f : fans) {
      if (obj == f.second->get_off()) {
        ws.gcode_script("M106 S0");
        f.second->update_value(0);
        break;
      } else if (obj == f.second->get_max()) {
        ws.gcode_script("M106 S255");
        f.second->update_value(100);
        break;
      }
    }
  }
}

void FanPanel::handle_fan_update_generic(lv_event_t *event) {
  lv_obj_t *obj = lv_event_get_target(event);

  if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
    double pct = (double)lv_slider_get_value(obj) / 100.0;

    spdlog::trace("updating fan speed to {}", pct);
    for (auto &f : fans) {
      if (obj == f.second->get_slider()) {
        std::string fan_name = KUtils::get_obj_name(f.first);
        spdlog::trace("update fan {}", fan_name);
        ws.gcode_script(fmt::format(fmt::format("SET_FAN_SPEED FAN={} SPEED={}", fan_name, pct)));
        break;
      }
    }
  } else if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    obj = lv_event_get_current_target(event);

    for (auto &f : fans) {
      if (obj == f.second->get_off()) {
        std::string fan_name = KUtils::get_obj_name(f.first);
        spdlog::trace("turning off fan {}", fan_name);
        ws.gcode_script(fmt::format("SET_FAN_SPEED FAN={} SPEED=0", fan_name));
        f.second->update_value(0);
        break;
      } else if (obj == f.second->get_max()) {
        std::string fan_name = KUtils::get_obj_name(f.first);
        spdlog::trace("turning fan to max {}", fan_name);
        ws.gcode_script(fmt::format("SET_FAN_SPEED FAN={} SPEED=1", fan_name));
        f.second->update_value(100);
        break;
      }
    }
  }
}
