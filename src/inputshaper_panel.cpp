#include "inputshaper_panel.h"
#include "state.h"
#include "utils.h"
#include "config.h"
#include "spdlog/spdlog.h"

#include <algorithm>

LV_IMG_DECLARE(resume);
LV_IMG_DECLARE(sd_img);
LV_IMG_DECLARE(emergency);
LV_IMG_DECLARE(back);

LV_FONT_DECLARE(dejavusans_mono_14);

#define X_DATA "/tmp/resonances_x_x.csv"
#define X_PNG "resonances_x.png"
#define Y_DATA "/tmp/resonances_y_y.csv"
#define Y_PNG "resonances_y.png"

std::vector<std::string> InputShaperPanel::shapers = {
  "zv",
  "mzv",
  "ei",
  "2hump_ei",
  "3hump_ei"
};

InputShaperPanel::InputShaperPanel(KWebSocketClient &c, std::mutex &l)
  : ws(c)
  , lv_lock(l)
  , cont(lv_obj_create(lv_scr_act()))

  // xgraph
  , xgraph_cont(lv_obj_create(cont))
  , xgraph(lv_img_create(xgraph_cont))
  , xoutput(lv_label_create(cont))
  , xspinner(lv_spinner_create(cont, 1000, 60))

  // ygraph
  , ygraph_cont(lv_obj_create(cont))
  , ygraph(lv_img_create(ygraph_cont))
  , youtput(lv_label_create(cont))
  , yspinner(lv_spinner_create(cont, 1000, 60))

  // x controls
  , xcontrol(lv_obj_create(cont))
  , xaxis_label(lv_label_create(xcontrol))
  , x_switch(lv_switch_create(xcontrol))
  , xslider_cont(lv_obj_create(xcontrol))
  , xslider(lv_slider_create(xslider_cont))
  , xlabel(lv_label_create(xslider_cont))
  , xshaper_dd(lv_dropdown_create(xcontrol))

  // y controls
  , ycontrol(lv_obj_create(cont))
  , yaxis_label(lv_label_create(ycontrol))
  , y_switch(lv_switch_create(ycontrol))
  , yslider_cont(lv_obj_create(ycontrol))
  , yslider(lv_slider_create(yslider_cont))
  , ylabel(lv_label_create(yslider_cont))
  , yshaper_dd(lv_dropdown_create(ycontrol))

  , button_cont(lv_obj_create(cont))
  , switch_cont(lv_obj_create(button_cont))
  , graph_switch_label(lv_label_create(switch_cont))
  , graph_switch(lv_switch_create(switch_cont))
  , calibrate_btn(button_cont, &resume, "Calibrate", &InputShaperPanel::_handle_callback, this)
  , save_btn(button_cont, &sd_img, "Save", &InputShaperPanel::_handle_callback, this)
  , emergency_btn(button_cont, &emergency, "Stop", &InputShaperPanel::_handle_callback, this,
    "Do you want to emergency stop?",
    [&c]() {
      spdlog::debug("emergency stop pressed");
      c.send_jsonrpc("printer.emergency_stop");
    })
  , back_btn(cont, &back, "Back", &InputShaperPanel::_handle_callback, this)
  , ximage_fullsized(false)
  , yimage_fullsized(false)
  , x_pending(false)
  , y_pending(false)
  , y_after_move(false)
  , next_axis_is_x(false)
  , cal_watchdog(NULL)
{
  lv_obj_move_background(cont);

  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(cont, 0, 0);

  lv_obj_t *graph_label = lv_label_create(xgraph_cont);
  lv_label_set_text(graph_label, "X Frequency Response");
  lv_obj_align(graph_label, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_set_style_pad_all(xgraph_cont, 0, 0);
  lv_obj_add_flag(xgraph_cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(xgraph_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(xgraph_cont, &InputShaperPanel::_handle_image_clicked,
    LV_EVENT_CLICKED, this);

  graph_label = lv_label_create(ygraph_cont);
  lv_label_set_text(graph_label, "Y Frequency Response");
  lv_obj_align(graph_label, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_set_style_pad_all(ygraph_cont, 0, 0);
  lv_obj_add_flag(ygraph_cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(ygraph_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(ygraph_cont, &InputShaperPanel::_handle_image_clicked,
    LV_EVENT_CLICKED, this);

  // graphs
  lv_img_set_zoom(xgraph, 95);
  lv_obj_center(xgraph);
  // lv_img_set_src(xgraph, "A:/usr/data/printer_data/thumbnails/resonances_x.png");

  lv_img_set_zoom(ygraph, 95);
  lv_obj_center(ygraph);
  // lv_img_set_src(ygraph, "A:/usr/data/printer_data/thumbnails/resonances_y.png");

  lv_obj_set_size(ygraph_cont, LV_PCT(40), LV_PCT(45));
  lv_obj_clear_flag(ygraph_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(xgraph_cont, LV_PCT(40), LV_PCT(45));
  lv_obj_clear_flag(xgraph_cont, LV_OBJ_FLAG_SCROLLABLE);

  // text output
  lv_obj_set_size(xoutput, LV_PCT(38), LV_PCT(45));
  lv_label_set_text(xoutput, "");
  lv_obj_set_style_text_font(xoutput, &lv_font_montserrat_10, LV_STATE_DEFAULT);

  lv_obj_set_size(youtput, LV_PCT(38), LV_PCT(45));
  lv_label_set_text(youtput, "");
  lv_obj_set_style_text_font(youtput, &lv_font_montserrat_10, LV_STATE_DEFAULT);

  // spinners
  lv_obj_add_flag(xspinner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(xspinner, 80, 80);

  lv_obj_add_flag(yspinner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(yspinner, 80, 80);

  // buttons
  lv_obj_set_size(button_cont, LV_PCT(20), LV_PCT(100));
  lv_obj_clear_flag(button_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(button_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(button_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_flex_grow(calibrate_btn.get_container(), 1);
  lv_obj_set_flex_grow(save_btn.get_container(), 1);
  lv_obj_set_flex_grow(emergency_btn.get_container(), 1);

  lv_obj_set_size(switch_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_row(switch_cont, 0, 0);
  lv_obj_set_flex_flow(switch_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(switch_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_set_style_text_align(graph_switch_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(graph_switch_label, "Graph");
  lv_obj_set_width(graph_switch_label, LV_PCT(100));
  lv_obj_clear_state(graph_switch, LV_STATE_CHECKED);
  lv_obj_clear_flag(switch_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(switch_cont, 0, 0);

  auto scale = (double)lv_disp_get_physical_hor_res(NULL) / 800.0;

  // controls
  lv_obj_set_flex_flow(xcontrol, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(xcontrol, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(xcontrol, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(xcontrol, LV_PCT(62), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_row(xcontrol, 0, 0);
  lv_obj_set_style_pad_all(xcontrol, 0, 0);

  lv_obj_set_width(xaxis_label, LV_PCT(100));
  lv_label_set_text(xaxis_label, "X Axis");
  lv_obj_add_state(x_switch, LV_STATE_CHECKED);

  lv_obj_clear_flag(xslider_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(xslider_cont, LV_PCT(53), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(xslider_cont, 0, 0);

  lv_obj_center(xslider);
  lv_obj_set_width(xslider, LV_PCT(85));
  lv_slider_set_range(xslider, 0, 1400);

  lv_obj_add_event_cb(xslider, &InputShaperPanel::_handle_update_slider,
    LV_EVENT_VALUE_CHANGED, this);


  lv_obj_align_to(xlabel, xslider, LV_ALIGN_OUT_BOTTOM_MID, 0, 35 * scale);
  lv_label_set_text(xlabel, "0 Hz");

  lv_dropdown_set_options(xshaper_dd, fmt::format("{}", fmt::join(shapers, "\n")).c_str());

  lv_obj_set_flex_flow(ycontrol, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(ycontrol, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(ycontrol, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(ycontrol, LV_PCT(62), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(ycontrol, 0, 0);
  lv_obj_set_style_pad_row(ycontrol, 0, 0);

  lv_obj_set_width(yaxis_label, LV_PCT(100));
  lv_label_set_text(yaxis_label, "Y Axis");
  lv_obj_add_state(y_switch, LV_STATE_CHECKED);

  lv_obj_clear_flag(yslider_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(yslider_cont, LV_PCT(53), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(yslider_cont, 0, 0);

  lv_obj_center(yslider);
  lv_obj_set_width(yslider, LV_PCT(85));
  lv_slider_set_range(yslider, 0, 1400);

  lv_obj_add_event_cb(yslider, &InputShaperPanel::_handle_update_slider,
    LV_EVENT_VALUE_CHANGED, this);

  lv_obj_align_to(ylabel, yslider, LV_ALIGN_OUT_BOTTOM_MID, 0, 35 * scale);
  lv_label_set_text(ylabel, "0 Hz");

  lv_dropdown_set_options(yshaper_dd, fmt::format("{}", fmt::join(shapers, "\n")).c_str());

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(2), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(3), LV_GRID_FR(7), LV_GRID_FR(7),
    LV_GRID_TEMPLATE_LAST};

  lv_obj_set_grid_dsc_array(cont, grid_main_col_dsc, grid_main_row_dsc);

  // row 1, col 2/3
  lv_obj_set_grid_cell(xspinner, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(yspinner, LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(xgraph_cont, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(ygraph_cont, LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(xoutput, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
  lv_obj_set_grid_cell(youtput, LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);

  // row 2, col 2 span 2
  lv_obj_set_grid_cell(xcontrol, LV_GRID_ALIGN_START, 1, 2, LV_GRID_ALIGN_START, 1, 1);

  // row 3, col 2 span 2
  lv_obj_set_grid_cell(ycontrol, LV_GRID_ALIGN_START, 1, 2, LV_GRID_ALIGN_START, 2, 1);

  // row 1, col 1
  lv_obj_set_grid_cell(button_cont, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 3);

  lv_obj_add_flag(back_btn.get_container(), LV_OBJ_FLAG_FLOATING);
  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, -20);

  // TODO: show only register when issuing macros inputshaper cares about, then unregister after.
  // ws.register_gcode_resp([this](json& d) { this->handle_macro_response(d); });
  ws.register_method_callback("notify_gcode_response",
    "InputShaperPanel",
    [this](json &d) { this->handle_macro_response(d); });
}

InputShaperPanel::~InputShaperPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void InputShaperPanel::foreground() {
  auto inputshaper = State::get_instance()
    ->get_data("/printer_state/configfile/config/input_shaper"_json_pointer);
  spdlog::trace("input shapper {}", inputshaper.dump());

  if (!inputshaper.is_null()) {
    auto v = inputshaper["/shaper_freq_x"_json_pointer];
    if (!v.is_null()) {
      double hz = std::stod(v.template get<std::string>());
      lv_slider_set_value(xslider, hz * 10, LV_ANIM_OFF);
      lv_label_set_text(xlabel, fmt::format("{} Hz", hz).c_str());
    }

    v = inputshaper["/shaper_type_x"_json_pointer];
    if (!v.is_null()) {
      auto shaper = v.template get<std::string>();
      auto idx = find_shaper_index(shapers, shaper);
      lv_dropdown_set_selected(xshaper_dd, idx);
    }

    v = inputshaper["/shaper_freq_y"_json_pointer];
    if (!v.is_null()) {
      double hz = std::stod(v.template get<std::string>());
      lv_slider_set_value(yslider, hz * 10, LV_ANIM_OFF);
      lv_label_set_text(ylabel, fmt::format("{} Hz", hz).c_str());
    }

    v = inputshaper["/shaper_type_y"_json_pointer];
    if (!v.is_null()) {
      auto shaper = v.template get<std::string>();
      auto idx = find_shaper_index(shapers, shaper);
      lv_dropdown_set_selected(yshaper_dd, idx);
    }
  }

  lv_obj_move_foreground(cont);
}


void InputShaperPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);
  if (btn == calibrate_btn.get_container()) {
    bool x_requested = lv_obj_has_state(x_switch, LV_STATE_CHECKED);
    bool y_requested = lv_obj_has_state(y_switch, LV_STATE_CHECKED);

    if (!x_requested && !y_requested) {
      KUtils::notify_toast("Pick X and/or Y first, then Calibrate.", 2500);
      return;
    }

    // Axes run ONE AT A TIME so the accelerometer can be moved between them.
    // Confirm placement before each axis; both -> Y first, then prompt for X.
    y_after_move = (x_requested && y_requested);
    // X is shown on top in the GUI, so run X first when selected; Y second.
    show_placement_prompt(/*is_x=*/x_requested, /*moving=*/false);

  } else if (btn == save_btn.get_container()) {
    double xhz = (double)lv_slider_get_value(xslider) / 10.0;
    double yhz = (double)lv_slider_get_value(yslider) / 10.0;

    char xbuf[10];
    lv_dropdown_get_selected_str(xshaper_dd, xbuf, sizeof(xbuf));

    char ybuf[10];
    lv_dropdown_get_selected_str(yshaper_dd, ybuf, sizeof(ybuf));

    ws.gcode_script(fmt::format("SAVE_INPUT_SHAPER SHAPER_FREQ_X={} SHAPER_TYPE_X={} SHAPER_FREQ_Y={} SHAPER_TYPE_Y={}\nSAVE_CONFIG",
      xhz, xbuf, yhz, ybuf));

    KUtils::notify_toast(
      fmt::format("Saved — X: {} @ {:.1f} Hz, Y: {} @ {:.1f} Hz. Klipper is restarting to apply it "
        "(if it shows 'shutdown', just restart it again).", xbuf, xhz, ybuf, yhz),
      6000);

  } else if (btn == back_btn.get_container()) {
    lv_obj_move_background(cont);
  } else if (btn == emergency_btn.get_container()) {
    ws.send_jsonrpc("printer.emergency_stop");
  }
}

void InputShaperPanel::handle_macro_response(json &j) {
  spdlog::trace("inputshapping macro response: {}", j.dump());
  auto &v = j["/params/0"_json_pointer];
  if (!v.is_null()) {
    std::string resp = v.template get<std::string>();
    std::lock_guard<std::mutex> lock(lv_lock);
    bool graph_requested = lv_obj_has_state(graph_switch, LV_STATE_CHECKED);
    if (resp.rfind("// {\"shapers\":", 0) == 0) {
      auto res = json::parse(resp.substr(3));
      auto &axis_log = res["/logfile"_json_pointer];
      auto &axis_png = res["/png"_json_pointer];

      if (!axis_log.is_null()) {
        std::string fn = axis_log.template get<std::string>();
        if (X_DATA == fn) {
          if (graph_requested && !axis_png.is_null()) {
            spdlog::trace("found generated x freq png");
            auto config_root = KUtils::get_root_path("config");
            auto png_path = fmt::format("{}/{}", config_root.length() > 0 ? config_root : "/tmp", X_PNG);

            png_path =
              fmt::format("A:{}", KUtils::is_running_local()
                ? png_path
                : KUtils::download_file("config", X_PNG, Config::get_instance()->get_thumbnail_path()));

            spdlog::trace("x freq png path {}", png_path);

            lv_label_set_text(xoutput, "");
            lv_img_set_src(xgraph, png_path.c_str());
            lv_obj_clear_flag(xgraph_cont, LV_OBJ_FLAG_HIDDEN);
            set_shaper_detail(res, NULL, xslider, xlabel, xshaper_dd);
            lv_obj_move_foreground(xgraph_cont);
          } else {
            set_shaper_detail(res, xoutput, xslider, xlabel, xshaper_dd);
            lv_obj_move_foreground(xoutput);
          }

          lv_obj_add_flag(xspinner, LV_OBJ_FLAG_HIDDEN);
          lv_obj_move_background(xspinner);
          {
            auto &b = res["/best"_json_pointer];
            if (!b.is_null()) {
              auto bn = b.template get<std::string>();
              double bf = res["/shapers"][bn]["freq"].template get<double>();
              KUtils::notify_toast(fmt::format("X done — recommended {} @ {:.1f} Hz. Tap Save to apply.", bn, bf), 4000);
            }
          }
          x_pending = false;   // X is finished regardless of what's next
          if (y_after_move) {
            // both axes: pause the watchdog and prompt to move the sensor for Y
            if (cal_watchdog != NULL) {
              lv_timer_del(cal_watchdog);
              cal_watchdog = NULL;
            }
            show_placement_prompt(/*is_x=*/false, /*moving=*/true);
          } else if (!x_pending && !y_pending) {
            end_calibration_ui();
          }

        } else if (Y_DATA == fn) {
          if (graph_requested && !axis_png.is_null()) {
            spdlog::trace("found generated y freq png");
            auto config_root = KUtils::get_root_path("config");
            auto png_path = fmt::format("{}/{}", config_root.length() > 0 ? config_root : "/tmp", Y_PNG);

            png_path =
              fmt::format("A:{}", KUtils::is_running_local()
                ? png_path
                : KUtils::download_file("config", Y_PNG, Config::get_instance()->get_thumbnail_path()));

            spdlog::trace("y freq png path {}", png_path);

            lv_label_set_text(youtput, "");
            lv_img_set_src(ygraph, png_path.c_str());
            lv_obj_clear_flag(ygraph_cont, LV_OBJ_FLAG_HIDDEN);
            set_shaper_detail(res, NULL, yslider, ylabel, yshaper_dd);
            lv_obj_move_foreground(ygraph_cont);
          } else {
            set_shaper_detail(res, youtput, yslider, ylabel, yshaper_dd);
            lv_obj_move_foreground(youtput);
          }

          lv_obj_add_flag(yspinner, LV_OBJ_FLAG_HIDDEN);
          lv_obj_move_background(yspinner);
          {
            auto &b = res["/best"_json_pointer];
            if (!b.is_null()) {
              auto bn = b.template get<std::string>();
              double bf = res["/shapers"][bn]["freq"].template get<double>();
              KUtils::notify_toast(fmt::format("Y done — recommended {} @ {:.1f} Hz. Tap Save to apply.", bn, bf), 4000);
            }
          }
          y_pending = false;   // Y is the last axis when both are run
          if (!x_pending && !y_pending) {
            end_calibration_ui();
          }
        }
      }

    } else if ("// Resonances data written to " Y_DATA " file" == resp) {
      KUtils::notify_toast(graph_requested ? "Analysing Y + rendering graph (slower)…" : "Analysing Y…", 3000);
      auto config_root = KUtils::get_root_path("config");
      auto screen_width = (double)lv_disp_get_physical_hor_res(NULL) / 100.0;
      auto screen_height = (double)lv_disp_get_physical_ver_res(NULL) / 100.0;
      auto png_path = fmt::format("{}/{}", config_root.length() > 0 ? config_root : "/tmp", Y_PNG);
      std::string arg = graph_requested
        ? fmt::format("{} -o {} -w {} -l {}", Y_DATA, png_path, screen_width, screen_height)
        : Y_DATA;

      ws.gcode_script(fmt::format("RUN_SHELL_COMMAND CMD=guppy_input_shaper PARAMS={:?}", arg));

    } else if ("// Resonances data written to " X_DATA " file" == resp) {
      KUtils::notify_toast(graph_requested ? "Analysing X + rendering graph (slower)…" : "Analysing X…", 3000);
      auto config_root = KUtils::get_root_path("config");
      auto screen_width = (double)lv_disp_get_physical_hor_res(NULL) / 100.0;
      auto screen_height = (double)lv_disp_get_physical_ver_res(NULL) / 100.0;
      auto png_path = fmt::format("{}/{}", config_root.length() > 0 ? config_root : "/tmp", X_PNG);
      std::string arg = graph_requested
        ? fmt::format("{} -o {} -w {} -l {}", X_DATA, png_path, screen_width, screen_height)
        : X_DATA;

      ws.gcode_script(fmt::format("RUN_SHELL_COMMAND CMD=guppy_input_shaper PARAMS={:?}", arg));
    }
  }
}

void InputShaperPanel::handle_image_clicked(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED) {
    lv_obj_t *clicked = lv_event_get_target(e);

    if (clicked == xgraph_cont) {
      if (yimage_fullsized) {
        lv_obj_invalidate(ygraph);
        lv_img_set_zoom(ygraph, 150);
        yimage_fullsized = false;
        lv_obj_set_size(ygraph_cont, LV_PCT(40), LV_PCT(45));
        lv_obj_clear_flag(ygraph_cont, LV_OBJ_FLAG_FLOATING);

        // lv_obj_set_size(ygraph_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
      }

      lv_obj_invalidate(xgraph);

      if (ximage_fullsized) {
        lv_img_set_zoom(xgraph, 95);
        lv_obj_set_size(xgraph_cont, LV_PCT(40), LV_PCT(45));
        lv_obj_clear_flag(xgraph_cont, LV_OBJ_FLAG_FLOATING);

        // lv_obj_set_size(xgraph_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

      } else {
        lv_img_set_zoom(xgraph, LV_IMG_ZOOM_NONE);
        lv_obj_add_flag(xgraph_cont, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(xgraph_cont, LV_PCT(100), LV_PCT(100));
      }
      lv_obj_move_foreground(xgraph_cont);
      ximage_fullsized = !ximage_fullsized;

    } else if (clicked == ygraph_cont) {
      if (ximage_fullsized) {
        lv_obj_invalidate(xgraph);
        lv_img_set_zoom(xgraph, 150);
        ximage_fullsized = false;
        // lv_obj_set_size(xgraph_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_size(xgraph_cont, LV_PCT(40), LV_PCT(45));
        lv_obj_clear_flag(xgraph_cont, LV_OBJ_FLAG_FLOATING);

      }

      lv_obj_invalidate(ygraph);

      if (yimage_fullsized) {
        lv_img_set_zoom(ygraph, 95);
        // lv_obj_set_size(ygraph_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_size(ygraph_cont, LV_PCT(40), LV_PCT(45));
        lv_obj_clear_flag(ygraph_cont, LV_OBJ_FLAG_FLOATING);

      } else {
        lv_img_set_zoom(ygraph, LV_IMG_ZOOM_NONE);
        lv_obj_set_size(ygraph_cont, LV_PCT(100), LV_PCT(100));

        // floating hacks (free child from grid) around the grid layout alignment
        lv_obj_add_flag(ygraph_cont, LV_OBJ_FLAG_FLOATING);

      }
      lv_obj_move_foreground(ygraph_cont);
      yimage_fullsized = !yimage_fullsized;
    }
  }
}

void InputShaperPanel::handle_update_slider(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t *obj = lv_event_get_target(e);
    double hz = (double)lv_slider_get_value(obj);
    lv_slider_set_value(obj, hz, LV_ANIM_OFF);
    if (obj == xslider) {
      lv_label_set_text(xlabel, fmt::format("{} hz", hz / 10.0).c_str());
    } else if (obj == yslider) {
      lv_label_set_text(ylabel, fmt::format("{} hz", hz / 10.0).c_str());
    }
  }
}

uint32_t InputShaperPanel::find_shaper_index(const std::vector<std::string> &s,
  const std::string &shaper) {
  return std::distance(s.cbegin(), std::find(s.cbegin(), s.cend(), shaper));
}

void InputShaperPanel::set_shaper_detail(json &res,
  lv_obj_t *label,
  lv_obj_t *slider,
  lv_obj_t *slider_label,
  lv_obj_t *dd) {
  auto &shapers_resp = res["/shapers"_json_pointer];
  if (!shapers_resp.is_null()) {
    std::vector<std::string> shaper_details;
    auto &best_shaper = res["/best"_json_pointer];
    if (!best_shaper.is_null()) {
      auto bs_name = best_shaper.template get<std::string>();
      double f = shapers_resp[bs_name]["freq"]
        .template get<double>();
      shaper_details.push_back(fmt::format("Best: {} @ {:.1f} Hz\n", bs_name, f));

      uint32_t idx = find_shaper_index(shapers, bs_name);
      lv_dropdown_set_selected(dd, idx);

      lv_slider_set_value(slider, f * 10, LV_ANIM_OFF);
      lv_label_set_text(slider_label, fmt::format("{:.1f} Hz", f).c_str());
    }

    if (label != NULL) {
      // compact, proportional-font-friendly list (the full table is on the graph)
      for (auto &el : shapers_resp.items()) {
        shaper_details.push_back(
          fmt::format("{}  {:.1f} Hz  {:.1f}% vib",
            el.key(),
            el.value()["freq"].template get<double>(),
            el.value()["vib"].template get<double>()));
      }

      lv_label_set_text(label, fmt::format("{}", fmt::join(shaper_details, "\n")).c_str());
    }
  }
}

void InputShaperPanel::end_calibration_ui() {
  calibrate_btn.enable();
  save_btn.enable();
  // unlock the Graph switch
  lv_obj_clear_state(graph_switch, LV_STATE_DISABLED);
  lv_obj_add_flag(graph_switch, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(xspinner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(yspinner, LV_OBJ_FLAG_HIDDEN);
  if (cal_watchdog != NULL) {
    lv_timer_del(cal_watchdog);
    cal_watchdog = NULL;
  }
}

void InputShaperPanel::handle_watchdog() {
  // Runs on the LVGL timer thread (lv_timer_handler already holds lv_lock), and
  // repeat_count is 1 so LVGL frees the timer after this returns — drop our handle
  // and DON'T lv_timer_del it here.
  cal_watchdog = NULL;
  if (x_pending || y_pending) {
    x_pending = false;
    y_pending = false;
    y_after_move = false;
    calibrate_btn.enable();
    save_btn.enable();
    lv_obj_add_flag(xspinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(yspinner, LV_OBJ_FLAG_HIDDEN);
    KUtils::notify_toast(
      "Calibration timed out — nothing was changed. Check the printer and try again.", 6000);
  }
}

void InputShaperPanel::start_axis(bool is_x) {
  lv_obj_t *graph = is_x ? xgraph : ygraph;
  lv_obj_t *graph_cont = is_x ? xgraph_cont : ygraph_cont;
  lv_obj_t *output = is_x ? xoutput : youtput;
  lv_obj_t *spinner = is_x ? xspinner : yspinner;

  ws.gcode_script(fmt::format("TEST_RESONANCES AXIS={} NAME={}\nM400",
    is_x ? "X" : "Y", is_x ? "x" : "y"));

  // free src + hack to color in empty space (same as the original per-axis path)
  lv_img_set_src(graph, NULL);
  ((lv_img_t *)graph)->src_type = LV_IMG_SRC_SYMBOL;

  lv_label_set_text(output, "");
  lv_obj_add_flag(graph_cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(spinner);
}

void InputShaperPanel::begin_axis(bool is_x) {
  calibrate_btn.disable();
  save_btn.disable();

  bool homing = !KUtils::is_homed();
  bool graph = lv_obj_has_state(graph_switch, LV_STATE_CHECKED);
  // lock the Graph mode for the whole run so it can't be toggled mid-test
  lv_obj_add_state(graph_switch, LV_STATE_DISABLED);
  lv_obj_clear_flag(graph_switch, LV_OBJ_FLAG_CLICKABLE);
  KUtils::notify_toast(
    fmt::format("Calibrating {} — it'll move & shake for ~1 min. Leave it be.{}{}",
      is_x ? "X" : "Y",
      homing ? " (homing first)" : "",
      graph ? " Graph is on, so it's slower." : ""),
    5000);

  if (is_x) {
    x_pending = true;
  } else {
    y_pending = true;
  }

  if (cal_watchdog != NULL) {
    lv_timer_del(cal_watchdog);
    cal_watchdog = NULL;
  }
  // Timeout depends on the (now-locked) Graph mode: graph rendering on the KE is
  // slow, so allow 6 min with it on; text-only finishes fast, so 3 min. Either
  // way it recovers from a genuinely stuck run (no endless spinner).
  cal_watchdog = lv_timer_create(&InputShaperPanel::_watchdog_cb,
    graph ? 360000 : 180000, this);
  lv_timer_set_repeat_count(cal_watchdog, 1);

  if (homing) {
    ws.gcode_script("G28");
  }
  start_axis(is_x);
}

void InputShaperPanel::show_placement_prompt(bool is_x, bool moving) {
  next_axis_is_x = is_x;
  static const char *btns[] = {"Continue", "Cancel", ""};
  const char *title = moving ? "Move the accelerometer" : "Position the accelerometer";
  std::string where = is_x ? "the TOOLHEAD (for X)" : "the BED (for Y)";
  std::string body = moving
    ? fmt::format("First axis done.\n\nNow mount the accelerometer on {}, then tap Continue.", where)
    : fmt::format("Mount the accelerometer on {}, then tap Continue.", where);
  lv_obj_t *mbox = lv_msgbox_create(NULL, title, body.c_str(), btns, false);
  KUtils::style_lock_mbox(mbox, 90);
  lv_obj_add_event_cb(mbox, &InputShaperPanel::_placement_prompt_cb, LV_EVENT_VALUE_CHANGED, this);
}

void InputShaperPanel::_placement_prompt_cb(lv_event_t *e) {
  lv_obj_t *mbox = lv_event_get_current_target(e);
  InputShaperPanel *self = (InputShaperPanel *)lv_event_get_user_data(e);
  uint32_t btn = lv_msgbox_get_active_btn(mbox);
  lv_msgbox_close(mbox);

  if (btn == 0) {                         // Continue
    bool is_x = self->next_axis_is_x;
    if (!is_x) {
      self->y_after_move = false;         // Y is now running, nothing deferred
    }
    self->begin_axis(is_x);
  } else {                                // Cancel
    self->y_after_move = false;
    self->end_calibration_ui();           // idempotent; keeps any axis already saved
  }
}
