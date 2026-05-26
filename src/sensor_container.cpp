#include "sensor_container.h"
#include "spdlog/spdlog.h"

#include <string>

SensorContainer::SensorContainer(KWebSocketClient &c,
  lv_obj_t *parent,
  const void *img,
  const char *text,
  lv_color_t color,
  bool can_edit,
  bool show_target,
  Numpad &np,
  std::string name,
  lv_obj_t *chart_chart,
  lv_chart_series_t *chart_series,
  SensorType type)
  : ws(c)
  , sensor_cont(lv_obj_create(parent))
  , sensor_img(lv_img_create(sensor_cont))
  , sensor_label(lv_label_create(sensor_cont))
  , value_label(lv_label_create(sensor_cont))
  , value(0)
  , divider_label(lv_label_create(sensor_cont))
  , target_label(lv_label_create(sensor_cont))
  , target(-1)
  , numpad(np)
  , id(name)
  , chart(chart_chart)
  , series(chart_series)
  , target_series(NULL)
  , last_updated_ts(std::time(nullptr))
  , type(type)
{
  lv_obj_clear_flag(sensor_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_color(sensor_cont, color, LV_PART_MAIN);
  lv_obj_set_style_border_side(sensor_cont, LV_BORDER_SIDE_LEFT, LV_PART_MAIN);
  lv_obj_set_style_border_width(sensor_cont, 5, LV_PART_MAIN);

  // auto cont_width = (double)lv_disp_get_physical_hor_res(NULL) * 0.4125;
  // cont_width = cont_width > 330 ? 330 : cont_width;
  // auto cont_height = (double)lv_disp_get_physical_ver_res(NULL) * 0.125;
  // cont_height = cont_height > 60 ? 60 : cont_height;

  auto width_scale = (double)lv_disp_get_physical_hor_res(NULL) / 800.0;
  auto height_scale = (double)lv_disp_get_physical_ver_res(NULL) / 480.0;
  /* Tighter row: 48 (was 60) so three sensors + chart fit comfortably at 480x272. */
  lv_obj_set_size(sensor_cont, 330 * width_scale, 48 * height_scale);
  lv_obj_set_style_pad_all(sensor_cont, 0, 0);

  /* Smaller, screen-aware font — on 480x272 the previous theme-default m12 felt oversized. */
  const lv_font_t *row_font = (width_scale < 0.8) ? &lv_font_montserrat_10 : &lv_font_montserrat_14;

  lv_img_set_src(sensor_img, img);
  lv_obj_align(sensor_img, LV_ALIGN_LEFT_MID, 0, 0);

  lv_label_set_text(sensor_label, text);
  lv_obj_align_to(sensor_label, sensor_img, LV_ALIGN_OUT_RIGHT_MID, -7 * width_scale, 0);
  lv_obj_set_style_text_font(sensor_label, row_font, 0);
  // Clip long display names (e.g. "MCU Temperature") so they don't bleed into the value/target boxes.
  lv_obj_set_width(sensor_label, 130 * width_scale);
  lv_label_set_long_mode(sensor_label, LV_LABEL_LONG_DOT);

  // Right-anchored block: [value] [/] [target]. Sized so 3-digit values ("210") fit inside
  // the bordered target box without wrapping. Targets/values use LV_LABEL_LONG_CLIP to
  // suppress wrap even if a future value somehow exceeds the box.
  lv_label_set_text(value_label, "0");
  lv_obj_set_width(value_label, 50 * width_scale);
  lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, -90 * width_scale, 0);
  lv_label_set_long_mode(value_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_pad_all(value_label, 2 * width_scale, 0);
  lv_obj_set_style_text_font(value_label, row_font, 0);

  lv_label_set_text(divider_label, "/");
  lv_obj_set_width(divider_label, 12 * width_scale);
  lv_obj_align(divider_label, LV_ALIGN_RIGHT_MID, -72 * width_scale, 0);
  lv_obj_set_style_pad_all(divider_label, 0, 0);
  lv_obj_set_style_text_font(divider_label, row_font, 0);

  if (show_target || can_edit) {
    lv_label_set_text(target_label, "0");
    lv_obj_set_width(target_label, 60 * width_scale);
    lv_obj_align(target_label, LV_ALIGN_RIGHT_MID, -5 * width_scale, 0);
    lv_obj_set_style_pad_all(target_label, 2 * width_scale, 0);
    lv_obj_set_style_text_font(target_label, row_font, 0);
    lv_label_set_long_mode(target_label, LV_LABEL_LONG_CLIP);
  } else {
    lv_obj_add_flag(target_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(divider_label, LV_OBJ_FLAG_HIDDEN);
  }

  if (can_edit) {
    lv_obj_set_style_border_width(target_label, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(target_label, 6, LV_PART_MAIN);
    lv_obj_set_style_border_color(target_label, lv_palette_darken(LV_PALETTE_GREY, 1), LV_PART_MAIN);

    spdlog::debug("sensor cb registered name {}, cont {}, this {}, np {}",
      id, fmt::ptr(sensor_cont), fmt::ptr(this), fmt::ptr(&np));
    lv_obj_add_event_cb(sensor_cont, &SensorContainer::_handle_edit, LV_EVENT_CLICKED, this);
  }
}

SensorContainer::SensorContainer(KWebSocketClient &c,
  lv_obj_t *parent,
  const void *img,
  uint16_t img_scale,
  const char *text,
  lv_color_t color,
  bool can_edit,
  bool show_target,
  Numpad &np,
  std::string name,
  lv_obj_t *chart,
  lv_chart_series_t *chart_series,
  SensorType type)
  : SensorContainer(c, parent, img, text, color, can_edit, show_target, np, name, chart, chart_series, type)
{
  lv_img_set_zoom(sensor_img, img_scale);
}

SensorContainer::~SensorContainer() {
  if (sensor_cont != NULL) {
    spdlog::debug("deleting sensor {}", id);
    lv_obj_del(sensor_cont);
    sensor_cont = NULL;
  }

  if (series != NULL && chart != NULL) {
    lv_chart_remove_series(chart, series);
    series = NULL;
  }

  if (target_series != NULL && chart != NULL) {
    lv_chart_remove_series(chart, target_series);
    target_series = NULL;
  }
}

lv_obj_t *SensorContainer::get_sensor() {
  return sensor_cont;
}

void SensorContainer::update_target(int new_target) {
  if (new_target >= 0) {
    target = new_target;
    lv_label_set_text(target_label, fmt::format("{}", new_target).c_str());
    /* Paint the entire target series at the new target value — gives a flat
     * horizontal line at that temp so the user can see actual vs set at a glance. */
    if (target_series != NULL && chart != NULL) {
      lv_chart_set_all_value(chart, target_series, new_target);
    }
  }
}

void SensorContainer::set_target_series(lv_chart_series_t *ts) {
  target_series = ts;
  if (ts != NULL && chart != NULL && target >= 0) {
    lv_chart_set_all_value(chart, ts, target);
  }
}

void SensorContainer::update_value(int new_value) {
  if (value != new_value) {
    value = new_value;
    lv_label_set_text(value_label, fmt::format("{}", new_value).c_str());
  }
}

void SensorContainer::update_series(int v) {
  if (series != NULL && chart != NULL) {
#ifdef SIMULATOR
    /* No throttling in simulator mode — driven entirely by the mock timer. */
    lv_chart_set_next_value(chart, series, v);
#else
    auto delta = std::time(nullptr) - last_updated_ts;
    if (delta > 1) {
      lv_chart_set_next_value(chart, series, v);
      last_updated_ts = std::time(nullptr);
    }
#endif
  }
}

void SensorContainer::handle_edit(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    spdlog::trace("sensor callback this {}, {}, {}", id, fmt::ptr(this), fmt::ptr(&numpad));
    numpad.set_callback([this](double v) {
      std::string cmd;
      switch (type) {
      case SensorType::Heater:
        cmd = fmt::format("SET_HEATER_TEMPERATURE HEATER={} TARGET={}", id, v);
        break;
      case SensorType::TempFan:
        cmd = fmt::format("SET_TEMPERATURE_FAN_TARGET TEMPERATURE_FAN={} TARGET={}", id, v);
        break;
      default:
        return;
      }
      ws.gcode_script(cmd);
      });
    /* Pre-fill the numpad with the current target so the user can adjust
     * from the existing value instead of retyping from scratch. */
    numpad.foreground_reset(target);
  }
}
