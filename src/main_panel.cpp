#include "main_panel.h"
#include "state.h"
#include "utils.h"
#include "lvgl/lvgl.h"
#include "spdlog/spdlog.h"

#include <string>

LV_IMG_DECLARE(filament_img);
LV_IMG_DECLARE(light_img);
LV_IMG_DECLARE(move);
LV_IMG_DECLARE(print);
LV_IMG_DECLARE(extruder);
LV_IMG_DECLARE(bed);
LV_IMG_DECLARE(fan);
LV_IMG_DECLARE(heater);

LV_FONT_DECLARE(materialdesign_font_40);
#define MACROS_SYMBOL "\xF3\xB1\xB2\x83"
#define CONSOLE_SYMBOL "\xF3\xB0\x86\x8D"
#define TUNE_SYMBOL "\xF3\xB1\x95\x82"
#define HOME_SYMBOL "\xF3\xB0\x8B\x9C"
#define SETTING_SYMBOL "\xF3\xB0\x92\x93"

MainPanel::MainPanel(KWebSocketClient &websocket,
  std::mutex &lock,
  SpoolmanPanel &sm)
  : NotifyConsumer(lock)
  , ws(websocket)
  , homing_panel(ws, lock)
  , fan_panel(ws, lock)
  , led_panel(ws, lock)
  , tabview(lv_tabview_create(lv_scr_act(), LV_DIR_LEFT, 60))
  , main_tab(lv_tabview_add_tab(tabview, HOME_SYMBOL))
  , macros_tab(lv_tabview_add_tab(tabview, MACROS_SYMBOL))
  , macros_panel(ws, lock, macros_tab)
  , console_tab(lv_tabview_add_tab(tabview, CONSOLE_SYMBOL))
  , console_panel(ws, lock, console_tab)
  , printertune_tab(lv_tabview_add_tab(tabview, TUNE_SYMBOL))
  , setting_tab(lv_tabview_add_tab(tabview, SETTING_SYMBOL))
  , setting_panel(websocket, lock, setting_tab, sm)
  , main_cont(lv_obj_create(main_tab))
  , print_status_panel(websocket, lock, main_cont)
  , print_panel(ws, lock, print_status_panel, sm)
  , printertune_panel(ws, lock, printertune_tab, print_status_panel.get_finetune_panel())
  , numpad(Numpad(main_cont))
  , extruder_panel(ws, lock, numpad, sm)
  , prompt_panel(websocket, lock, main_cont)
  , spoolman_panel(sm)
  , temp_cont(lv_obj_create(main_cont))
  , temp_chart(lv_chart_create(main_cont))
  , homing_btn(main_cont, &move, "Homing", &MainPanel::_handle_homing_cb, this)
  , extrude_btn(main_cont, &filament_img, "Extrude", &MainPanel::_handle_extrude_cb, this)
  , action_btn(main_cont, &fan, "Fans", &MainPanel::_handle_fanpanel_cb, this)
  , led_btn(main_cont, &light_img, "LED", &MainPanel::_handle_ledpanel_cb, this)
  , print_btn(main_cont, &print, "Print", &MainPanel::_handle_print_cb, this)
{
  lv_style_init(&style);
  lv_style_set_img_recolor_opa(&style, LV_OPA_30);
  lv_style_set_img_recolor(&style, lv_color_black());
  lv_style_set_border_width(&style, 0);
  lv_style_set_bg_color(&style, lv_palette_darken(LV_PALETTE_GREY, 4));

  ws.register_notify_update(this);
}

MainPanel::~MainPanel() {
  if (tabview != NULL) {
    lv_obj_del(tabview);
    tabview = NULL;
  }

  sensors.clear();
}

void MainPanel::subscribe() {
  spdlog::trace("main panel subscribing");
  ws.send_jsonrpc("printer.gcode.help", [this](json &d) { console_panel.handle_macros(d); });
  print_panel.subscribe();
}

PrinterTunePanel &MainPanel::get_tune_panel() {
  return printertune_panel;
}

void MainPanel::init(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  for (const auto &el : sensors) {
    auto target_value = j[json::json_pointer(fmt::format("/result/status/{}/target", el.first))];
    if (!target_value.is_null()) {
      int target = target_value.template get<int>();
      el.second->update_target(target);
    }

    auto temp_value = j[json::json_pointer(fmt::format("/result/status/{}/temperature", el.first))];
    if (!temp_value.is_null()) {
      int value = temp_value.template get<int>();
      el.second->update_series(value);
      el.second->update_value(value);
    }
  }

  macros_panel.populate();

  auto fans = State::get_instance()->get_display_fans();
  print_status_panel.init(fans);
  printertune_panel.init(j);
}

void MainPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  for (const auto &el : sensors) {
    auto target_value = j[json::json_pointer(fmt::format("/params/0/{}/target", el.first))];
    if (!target_value.is_null()) {
      int target = target_value.template get<int>();
      el.second->update_target(target);
    }

    auto temp_value = j[json::json_pointer(fmt::format("/params/0/{}/temperature", el.first))];
    if (!temp_value.is_null()) {
      int value = temp_value.template get<int>();
      el.second->update_series(value);
      el.second->update_value(value);
    }
  }
}

static void scroll_begin_event(lv_event_t *e)
{
  /*Disable the scroll animations. Triggered when a tab button is clicked */
  if (lv_event_get_code(e) == LV_EVENT_SCROLL_BEGIN) {
    lv_anim_t *a = (lv_anim_t *)lv_event_get_param(e);
    if (a)  a->time = 0;
  }
}

void MainPanel::create_panel() {
  lv_obj_clear_flag(lv_tabview_get_content(tabview), LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(lv_tabview_get_content(tabview), scroll_begin_event, LV_EVENT_SCROLL_BEGIN, NULL);

  lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tabview);
  lv_obj_set_style_bg_color(tab_btns, lv_palette_main(LV_PALETTE_GREY), LV_STATE_CHECKED | LV_PART_ITEMS);
  lv_obj_set_style_outline_width(tab_btns, 0, LV_PART_ITEMS | LV_STATE_FOCUS_KEY | LV_STATE_FOCUS_KEY);
  lv_obj_set_style_border_side(tab_btns, 0, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_font(tab_btns, &materialdesign_font_40, LV_STATE_DEFAULT);

  // lv_obj_set_style_text_font(lv_scr_act(), LV_FONT_DEFAULT, 0);

  lv_obj_set_style_pad_all(main_tab, 0, 0);
  lv_obj_set_style_pad_all(macros_tab, 0, 0);
  lv_obj_set_style_pad_all(console_tab, 0, 0);
  lv_obj_set_style_pad_all(printertune_tab, 0, 0);
  lv_obj_set_style_pad_all(setting_tab, 0, 0);

  create_main(main_tab);

}

void MainPanel::handle_homing_cb(lv_event_t *event) {
  spdlog::trace("clicked homing1");
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    spdlog::trace("clicked homing");
    if (KUtils::is_printing()) {
      KUtils::notify_locked();
      return;
    }
    homing_panel.foreground();
  }
}

void MainPanel::handle_extrude_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    spdlog::trace("clicked extruder");
    if (KUtils::is_printing()) {
      KUtils::notify_locked();
      return;
    }
    extruder_panel.foreground();
  }
}

void MainPanel::handle_fanpanel_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    spdlog::trace("clicked fan panel");
    fan_panel.foreground();
  }
}

void MainPanel::handle_ledpanel_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    spdlog::trace("clicked led panel");
    led_panel.foreground();
  }
}

void MainPanel::handle_print_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    spdlog::trace("clicked print");
    print_panel.foreground();
  }
}

void MainPanel::create_main(lv_obj_t *parent)
{
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_ROW_WRAP);

  /* Row 0: sensors (CONTENT-sized). Row 1: chart fills the rest. The
   * previous buffer row left a visible whitespace gap between sensors and chart. */
  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_TEMPLATE_LAST};

  lv_obj_clear_flag(main_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_height(main_cont, LV_PCT(100));

  lv_obj_set_flex_grow(main_cont, 1);
  lv_obj_set_grid_dsc_array(main_cont, grid_main_col_dsc, grid_main_row_dsc);

  // Wrap all buttons in a container spanning cols 2-3 across all 3 rows so
  // button heights are uniform and independent of the CONTENT-sized sensor row.
  lv_obj_t *btn_wrapper = lv_obj_create(main_cont);
  lv_obj_clear_flag(btn_wrapper, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(btn_wrapper, 0, 0);
  lv_obj_set_style_border_width(btn_wrapper, 0, 0);
  lv_obj_set_style_bg_opa(btn_wrapper, LV_OPA_TRANSP, 0);
  lv_obj_set_grid_cell(btn_wrapper, LV_GRID_ALIGN_STRETCH, 2, 2, LV_GRID_ALIGN_STRETCH, 0, 2);

  static lv_coord_t btn_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t btn_row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(btn_wrapper, btn_col_dsc, btn_row_dsc);

  lv_obj_set_parent(homing_btn.get_container(), btn_wrapper);
  lv_obj_set_parent(extrude_btn.get_container(), btn_wrapper);
  lv_obj_set_parent(action_btn.get_container(), btn_wrapper);
  lv_obj_set_parent(led_btn.get_container(), btn_wrapper);
  lv_obj_set_parent(print_btn.get_container(), btn_wrapper);

  lv_obj_set_grid_cell(homing_btn.get_container(), LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_grid_cell(extrude_btn.get_container(), LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_grid_cell(action_btn.get_container(), LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(led_btn.get_container(), LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(print_btn.get_container(), LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 2, 1);

  /* Disable scrolling on the sensor container — the 5px left border on each
   * sensor row was tripping the horizontal scrollbar even though everything
   * fits visually. */
  lv_obj_clear_flag(temp_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(temp_cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_all(temp_cont, 0, 0);
  /* Zero row gap so sensor rows stack flush — default flex pad_row eats vertical space. */
  lv_obj_set_style_pad_row(temp_cont, 0, 0);

  lv_obj_set_flex_flow(temp_cont, LV_FLEX_FLOW_COLUMN);
  /* Row 0 is CONTENT-sized — it shrinks to fit only the sensor rows. */
  lv_obj_set_grid_cell(temp_cont, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1);

  lv_obj_align(temp_chart, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_width(temp_chart, LV_PCT(45));
  lv_obj_set_style_size(temp_chart, 0, LV_PART_INDICATOR);

  lv_chart_set_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 300);
  /* Chart in row 1, stretches to fill remaining space below the sensor rows. */
  lv_obj_set_grid_cell(temp_chart, LV_GRID_ALIGN_END, 0, 2, LV_GRID_ALIGN_STRETCH, 1, 1);
  /* 7 major ticks = labels at 0,50,100,...,300 — 50° steps for easier reading. */
  lv_chart_set_axis_tick(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 7, 5, true, 25);
  lv_obj_set_style_text_font(temp_chart, &lv_font_montserrat_8, LV_PART_TICKS);

  /* 5 interior horizontal div lines align with the 50° tick interval (50,100,150,200,250). */
  lv_chart_set_div_line_count(temp_chart, 5, 8);
  /* point_count + zoom_x previously 5000 each, which compressed all data into
   * the rightmost slice of the chart. With zoom 256 (1x) and a smaller point
   * buffer, the pre-populated samples spread across the full chart width. */
  lv_chart_set_point_count(temp_chart, 300);
  lv_chart_set_zoom_x(temp_chart, 256);
  /* Scroll is set in create_sensors() after sensors are added and layout is stable. */
}

void MainPanel::create_sensors(json &temp_sensors) {
  std::lock_guard<std::mutex> lock(lv_lock);
  sensors.clear();
  for (auto &sensor : temp_sensors.items()) {
    std::string key = sensor.key();
    std::string sensor_name = key;
    bool controllable = sensor.value()["controllable"].template get<bool>();

    lv_color_t color_code = lv_palette_main(LV_PALETTE_ORANGE);
    if (!sensor.value()["color"].is_number()) {
      std::string color = sensor.value()["color"].template get<std::string>();
      if (color == "red") {
        color_code = lv_palette_main(LV_PALETTE_RED);
      } else if (color == "purple") {
        color_code = lv_palette_main(LV_PALETTE_PURPLE);
      } else if (color == "blue") {
        color_code = lv_palette_main(LV_PALETTE_BLUE);
      }
    } else {
      color_code = lv_palette_main((lv_palette_t)sensor.value()["color"].template get<int>());
    }

    std::string display_name = sensor.value()["display_name"].template get<std::string>();

    SensorType type = SensorType::Heater;
    auto space_idx = key.find(' ');
    if (space_idx != std::string::npos) {
      std::string sensor_type = key.substr(0, space_idx);
      sensor_name = key.substr(space_idx + 1);
      if (sensor_type == "temperature_fan") {
        type = SensorType::TempFan;
      } else if (sensor_type == "temperature_sensor") {
        type = SensorType::Sensor;
      }
    }

    const void *sensor_img = &heater;
    if (key == "extruder") {
      sensor_img = &extruder;
    } else if (key == "heater_bed") {
      sensor_img = &bed;
    }

    lv_chart_series_t *temp_series =
      lv_chart_add_series(temp_chart, color_code, LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(temp_chart, temp_series, 0);

    auto sc = std::make_shared<SensorContainer>(ws, temp_cont, sensor_img, 150,
      display_name.c_str(), color_code, controllable, false, numpad, sensor_name,
      temp_chart, temp_series, type);

    /* Controllable sensors (extruder, bed) get a second series rendered as a
     * faint horizontal line at the target temperature so the user can see
     * actual vs set at a glance. */
    if (controllable) {
      lv_color_t target_color = lv_color_mix(color_code, lv_color_white(), 128);
      lv_chart_series_t *target_series =
        lv_chart_add_series(temp_chart, target_color, LV_CHART_AXIS_PRIMARY_Y);
      lv_chart_set_all_value(temp_chart, target_series, 0);
      sc->set_target_series(target_series);
    }

    sensors.insert({key, sc});
  }

  /* Layout is now stable (sensors added to temp_cont, CONTENT row resolved).
   * Scroll chart to show the newest data (right end of the ring buffer). */
  /* Force main_cont grid to recalculate: row 0 was LV_GRID_CONTENT but temp_cont
   * was empty when create_main() ran, so its height was 0. Now sensors are present. */
  lv_obj_update_layout(main_cont);
  lv_obj_scroll_to_x(temp_chart, LV_COORD_MAX, LV_ANIM_OFF);
}

void MainPanel::create_fans(json &fans) {
  fan_panel.create_fans(fans);
}

void MainPanel::create_leds(json &leds) {
  led_panel.init(leds);
}

void MainPanel::enable_spoolman() {
  spoolman_panel.init();
  setting_panel.enable_spoolman();
  extruder_panel.enable_spoolman();
  print_panel.enable_spoolman();
}

#ifdef SIMULATOR
#include "config.h"

void MainPanel::sim_setup_mock_data() {
  Config *conf = Config::get_instance();
  json &user_sensors = conf->get_json(conf->df() + "monitored_sensors");
  if (user_sensors.is_null()) {
    spdlog::warn("sim_setup_mock_data: no monitored_sensors in config");
    return;
  }

  /* Build a display_sensors json keyed by id, matching State::get_display_sensors
   * shape — but bypass the Moonraker-dependent extruder/heater/sensor lookups. */
  json display_sensors;
  for (auto &s : user_sensors) {
    std::string id = s["id"].template get<std::string>();
    display_sensors[id] = s;
  }
  create_sensors(display_sensors);

  /* Set initial displayed values. */
  for (auto &kv : sensors) {
    const std::string &key = kv.first;
    auto &s = kv.second;
    if (key.find("extruder") != std::string::npos) {
      s->update_value(205); s->update_target(210);
    } else if (key.find("heater_bed") != std::string::npos) {
      s->update_value(62); s->update_target(65);
    } else {
      s->update_value(38);
    }
  }

  /* Pre-populate the chart with ~200 historical points so the graph
   * looks alive immediately rather than starting empty. */
  for (int i = 0; i < 200; i++) {
    for (auto &kv : sensors) {
      const std::string &key = kv.first;
      auto &s = kv.second;
      int v;
      if (key.find("extruder") != std::string::npos) {
        v = 200 + (i % 12) - 6;
      } else if (key.find("heater_bed") != std::string::npos) {
        v = 60 + (i % 6) - 3;
      } else {
        v = 35 + (i % 8) - 4;
      }
      s->update_series(v);
    }
  }

  /* Drive the print status panel so its new widgets (filename label,
   * dynamic ETA, spinner-on-extruder etc.) render with sensible values. */
  print_status_panel.sim_setup_mock_data();

  /* Then bring the extruder panel up in its busy state so #48's spinner is
   * visible at startup. Foregrounded *after* the print status panel so the
   * extruder is what we see; press Back from there to inspect each panel. */
  extruder_panel.sim_show_busy();

  /* Enable the Spoolman entry points and populate the panel with fake spools
   * so the Spoolman UI is testable without a live Moonraker + Spoolman backend.
   * Foregrounded last so it covers the others — press Back to step back through
   * the stack (spoolman → extruder → home). */
  setting_panel.enable_spoolman();
  extruder_panel.enable_spoolman();
  print_panel.enable_spoolman();
  spoolman_panel.sim_setup_mock_data();
  spoolman_panel.foreground();

  /* Top-most: the System settings panel, so the new brightness slider is the
   * first thing visible in the sim. Press Back to step back through the stack:
   * sysinfo → spoolman → extruder → home. */
  setting_panel.get_sysinfo_panel().foreground();

  /* Faster timer (300ms) gives a denser live chart since SIMULATOR mode
   * bypasses the 1s throttle in SensorContainer::update_series. */
  lv_timer_t *t = lv_timer_create([](lv_timer_t *timer) {
    auto *self = static_cast<MainPanel *>(timer->user_data);
    static int n = 0;
    n++;
    for (auto &kv : self->sensors) {
      const std::string &key = kv.first;
      auto &s = kv.second;
      int v;
      if (key.find("extruder") != std::string::npos) {
        v = 205 + (n % 12) - 6;
      } else if (key.find("heater_bed") != std::string::npos) {
        v = 62 + (n % 6) - 3;
      } else {
        v = 38 + (n % 8) - 4;
      }
      s->update_value(v);
      s->update_series(v);
    }
  }, 300, this);
  (void)t;
}
#endif
