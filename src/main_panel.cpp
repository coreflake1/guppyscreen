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
  , macros_panel(ws, lock, macros_tab, [this]() { show_console(); })
  , console_tab(lv_tabview_add_tab(tabview, CONSOLE_SYMBOL))
  , console_panel(ws, lock, console_tab)
  , printertune_tab(lv_tabview_add_tab(tabview, TUNE_SYMBOL))
  , setting_tab(lv_tabview_add_tab(tabview, SETTING_SYMBOL))
  , setting_panel(websocket, lock, setting_tab, sm)
  , main_cont(lv_obj_create(main_tab))
  , print_status_panel(websocket, lock, main_cont)
  , print_panel(ws, lock, print_status_panel, sm)
  , printertune_panel(ws, lock, printertune_tab, print_status_panel.get_finetune_panel(), print_status_panel.get_zoffset_panel())
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

  // always reset the Macros tab to its Favorites view whenever it's opened
  lv_obj_add_event_cb(tabview, &MainPanel::_handle_tab_changed, LV_EVENT_VALUE_CHANGED, this);

  lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tabview);
  // catch re-taps of the already-active tab (no VALUE_CHANGED is raised then)
  lv_obj_add_event_cb(tab_btns, &MainPanel::_handle_tab_btn_clicked, LV_EVENT_CLICKED, this);
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
    // Allowed while paused (toolhead is parked) - only block mid-print.
    if (KUtils::is_printing() && !KUtils::is_paused()) {
      KUtils::notify_locked();
      return;
    }
    homing_panel.foreground();
  }
}

void MainPanel::handle_extrude_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    spdlog::trace("clicked extruder");
    // Allowed while paused so the user can purge/load filament (runout, manual
    // colour change) - only block mid-print.
    if (KUtils::is_printing() && !KUtils::is_paused()) {
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

void MainPanel::handle_tab_changed(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
    uint32_t act = lv_tabview_get_tab_act(tabview);
    // macros_tab (index 1) resets to Favorites; console_tab (index 2) to Terminal
    if (act == 1) {
      macros_panel.reset_to_favorites();
    } else if (act == 2) {
      console_panel.reset_to_terminal();
    }
  }
}

void MainPanel::show_console() {
  // console tab is index 2; switch to it and show the terminal so a macro
  // launched from the Macros screen is visible executing
  lv_tabview_set_act(tabview, 2, LV_ANIM_OFF);
  console_panel.reset_to_terminal();
}

void MainPanel::handle_tab_btn_clicked(lv_event_t *event) {
  // Fires on every tab-button click, including re-tapping the already-active
  // tab (which does NOT raise VALUE_CHANGED). Mirror handle_tab_changed so the
  // Console tab always drops back to its terminal, and Macros back to Favorites.
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  uint32_t act = lv_tabview_get_tab_act(tabview);
  if (act == 1) {
    macros_panel.reset_to_favorites();
  } else if (act == 2) {
    console_panel.reset_to_terminal();
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

  // The print overlay (shown only while printing, when Homing/Extrude are
  // locked) lives in the button cluster, left-aligned with the Homing box.
  lv_obj_t *mini = print_status_panel.get_mini_container();
  lv_obj_set_parent(mini, btn_wrapper);
  lv_obj_align(mini, LV_ALIGN_TOP_LEFT, 0, 0);

  // The "Paused" chip (shown only while paused, when the buttons are live)
  // parks in the top-right corner. It's small enough to leave the buttons
  // underneath it tappable while staying clearly visible.
  lv_obj_t *paused_chip = print_status_panel.get_mini_chip();
  lv_obj_set_parent(paused_chip, main_cont);
  lv_obj_align(paused_chip, LV_ALIGN_TOP_RIGHT, -4, 4);

  /* Disable scrolling on the sensor container - the 5px left border on each
   * sensor row was tripping the horizontal scrollbar even though everything
   * fits visually. */
  lv_obj_clear_flag(temp_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(temp_cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_all(temp_cont, 0, 0);
  /* Zero row gap so sensor rows stack flush - default flex pad_row eats vertical space. */
  lv_obj_set_style_pad_row(temp_cont, 0, 0);

  lv_obj_set_flex_flow(temp_cont, LV_FLEX_FLOW_COLUMN);
  /* Row 0 is CONTENT-sized - it shrinks to fit only the sensor rows. */
  lv_obj_set_grid_cell(temp_cont, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1);

  lv_obj_align(temp_chart, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_width(temp_chart, LV_PCT(45));
  lv_obj_set_style_size(temp_chart, 0, LV_PART_INDICATOR);

  lv_chart_set_range(temp_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 300);
  /* Chart in row 1, stretches to fill remaining space below the sensor rows. */
  lv_obj_set_grid_cell(temp_chart, LV_GRID_ALIGN_END, 0, 2, LV_GRID_ALIGN_STRETCH, 1, 1);
  /* 7 major ticks = labels at 0,50,100,...,300 - 50° steps for easier reading. */
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
   * shape - but bypass the Moonraker-dependent extruder/heater/sensor lookups. */
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

  /* Seed fans so the Fans screen shows an editable slider (output_pin fan0)
   * plus read-only rows: a binary heater_fan (On/Off) and a PWM output_pin
   * (percentage). */
  {
    json objs;
    objs["result"]["objects"] = json::array({
      "output_pin fan0", "output_pin MainBoardFan", "heater_fan nozzle_fan"});
    State::get_instance()->set_data("printer_objs", objs, "/result");

    json ps;
    ps["params"][0]["output_pin fan0"]["value"] = 0.5;
    ps["params"][0]["output_pin MainBoardFan"]["value"] = 0.4;
    ps["params"][0]["heater_fan nozzle_fan"]["speed"] = 1.0;
    auto &set = ps["params"][0]["configfile"]["settings"];
    set["output_pin fan0"]["pwm"] = true;
    // MainBoardFan has no pwm key on KE (digital-only pin) -> shows On/Off
    State::get_instance()->set_data("printer_state", ps, "/params/0");

    json editable;
    editable["output_pin fan0"] = {{"id", "output_pin fan0"}, {"display_name", "Part Cooling Fan"}};
    create_fans(editable);
  }

  /* Seed a handful of gcode_macros into printer_state so the Macros panel
   * has realistic data to render in the sim: a mix of param/no-param macros,
   * a couple of `_`-private ones (which parse_macros filters out), and enough
   * entries to require scrolling. One is pre-favorited via the guppyscreen DB
   * namespace so the Favorites view isn't empty. */
  {
    auto mock_macro = [](const std::string &gcode) {
      json m;
      m["gcode"] = gcode;
      return m;
    };

    json cfg;
    cfg["gcode_macro LOAD_FILAMENT"] =
      mock_macro("{% set t = params.TEMP|default(220) %}\nM109 S{t}\nG1 E50 F300");
    cfg["gcode_macro UNLOAD_FILAMENT"] =
      mock_macro("{% set t = params.TEMP|default(220) %}\nM109 S{t}\nG1 E-50 F300");
    cfg["gcode_macro HEAT_SOAK"] =
      mock_macro("{% set t = params.TEMP|default(60) %}{% set d = params.DURATION|default(10) %}\nM140 S{t}");
    cfg["gcode_macro START_PRINT"] =
      mock_macro("{% set bed = params.BED|default(60) %}{% set hot = params.EXTRUDER|default(200) %}\nM140 S{bed}");
    cfg["gcode_macro END_PRINT"]    = mock_macro("M104 S0\nM140 S0\nG28 X Y");
    cfg["gcode_macro PURGE"]        = mock_macro("G1 E20 F200");
    cfg["gcode_macro CLEAN_NOZZLE"] = mock_macro("G28\nG1 X10 Y10 F3000");
    cfg["gcode_macro PARK"]         = mock_macro("G1 X0 Y0 F6000");
    cfg["gcode_macro BEEP"]         = mock_macro("M300 S1000 P200");
    cfg["gcode_macro LED_ON"]       = mock_macro("SET_PIN PIN=caselight VALUE=1");
    cfg["gcode_macro LED_OFF"]      = mock_macro("SET_PIN PIN=caselight VALUE=0");
    cfg["gcode_macro PROBE_CALIBRATE"] =
      mock_macro("{% set sp = params.SPEED|default(5) %}\nPROBE_CALIBRATE");
    cfg["gcode_macro CALIBRATE_PRESSURE_ADVANCE_LONG_NAME_TEST"] =
      mock_macro("TUNING_TOWER COMMAND=SET_PRESSURE_ADVANCE");
    cfg["gcode_macro _PRIVATE_HELPER"] = mock_macro("G4 P100");
    cfg["gcode_macro _CG28"]           = mock_macro("G28");

    json ps;
    ps["params"][0]["configfile"]["config"] = cfg;
    State::get_instance()->set_data("printer_state", ps, "/params/0");

    json fav;
    fav["params"][0]["macros"]["settings"]["START_PRINT"]["favorite"] = true;
    State::get_instance()->set_data("guppysettings", fav, "/params/0");

    macros_panel.populate();
  }

  /* Seed a firmware_retraction object so the Retraction panel renders its live
   * controls in the sim. (On a real printer without [firmware_retraction] the
   * object is absent and the panel shows its empty-state instead.) */
  {
    json fr;
    auto &o = fr["params"][0]["firmware_retraction"];
    o["retract_length"] = 0.5;
    o["retract_speed"] = 40;
    o["unretract_extra_length"] = 0.0;
    o["unretract_speed"] = 30;
    auto &d = fr["params"][0]["configfile"]["settings"]["firmware_retraction"];
    d["retract_length"] = 0.5;
    d["retract_speed"] = 40;
    d["unretract_extra_length"] = 0.0;
    d["unretract_speed"] = 30;
    State::get_instance()->set_data("printer_state", fr, "/params/0");
  }

  /* Seed gcode_move so the FineTune / Z-Offset panels and the print-status
   * Z-offset cell render a live value in the sim (no Moonraker to provide it). */
  {
    json gm;
    auto &o = gm["params"][0]["gcode_move"];
    o["homing_origin"] = json::array({0.0, 0.0, -0.040});
    o["speed_factor"] = 1.0;
    o["extrude_factor"] = 1.0;
    State::get_instance()->set_data("printer_state", gm, "/params/0");
  }

  /* Seed the console: recent history, favorites, a gcode.help list spanning the
   * quick-filter chip groups, and some output lines. */
  {
    json hist;
    hist["params"][0]["commandHistory"] = json::array({
      "SAVE_CONFIG", "SET_PRESSURE_ADVANCE ADVANCE=0.04", "QUERY_PROBE",
      "M117 hello", "BED_MESH_CALIBRATE", "G28"});
    State::get_instance()->set_data("console", hist, "/params/0");

    json cfav;
    cfav["params"][0]["console"]["favorites"] = json::array({"G28", "BED_MESH_CALIBRATE"});
    State::get_instance()->set_data("guppysettings", cfav, "/params/0");

    static const char *CMDS[] = {
      "G28","G29","M204","M205","M106","M107","M117","M600",
      "SET_VELOCITY_LIMIT","FORCE_MOVE","SET_KINEMATIC_POSITION","SET_GCODE_OFFSET",
      "SET_HEATER_TEMPERATURE","TEMPERATURE_WAIT","PID_CALIBRATE",
      "BED_MESH_CALIBRATE","BED_MESH_CLEAR","BED_MESH_MAP","BED_MESH_PROFILE","BED_MESH_OFFSET",
      "PROBE","PROBE_ACCURACY","PROBE_CALIBRATE","QUERY_PROBE","Z_OFFSET_APPLY_PROBE",
      "INPUTSHAPER","TEST_RESONANCES","ACCELEROMETER_QUERY","SET_PRESSURE_ADVANCE","SHAPER_CALIBRATE",
      "SET_TMC_CURRENT","DUMP_TMC","INIT_TMC","SET_TMC_FIELD",
      "SET_PIN","SET_FILAMENT_SENSOR","SET_DISPLAY_TEXT","SET_IDLE_TIMEOUT",
      "QUERY_ENDSTOPS","QUERY_ADC","QUERY_FILAMENT_SENSOR",
      "CAM_BRIGHTNESS","CAM_CONTRAST","CAM_HUE","CAM_SATURATION",
      "SAVE_CONFIG","SAVE_GCODE_STATE","SAVE_VARIABLE",
      "RESPOND","STATUS","EXCLUDE_OBJECT","RESTART","FIRMWARE_RESTART",
      "_HOME_Z","_CG28", NULL};
    json help;
    for (int i = 0; CMDS[i] != NULL; i++) {
      help["result"][CMDS[i]] = "mock help";
    }
    console_panel.handle_macros(help);
    json resp;
    resp["params"] = json::array({"Klipper state: Ready", "// probe: open"});
    console_panel.handle_macro_response(resp);
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
   * Foregrounded last so it covers the others - press Back to step back through
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
