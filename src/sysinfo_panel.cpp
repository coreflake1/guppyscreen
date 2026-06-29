#include "sysinfo_panel.h"
#include "utils.h"
#include "config.h"
#include "theme.h"
#include "touch_beep.h"
#include "spdlog/spdlog.h"
#include "guppyscreen.h"

#include <algorithm>
#include <iterator>
#include <map>

#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

LV_IMG_DECLARE(back);
LV_IMG_DECLARE(cancel);

#ifdef GUPPYSCREEN_VERSION
#define GS_VERSION GUPPYSCREEN_VERSION
#else
#define GS_VERSION "dev-snapshot"
#endif

std::vector<std::string> SysInfoPanel::log_levels = {
  "trace",
  "debug",
  "info"
};

std::vector<std::string> SysInfoPanel::themes = {
  "blue",
  "red",
  "green",
  "purple",
  "pink",
  "yellow"
};

struct SleepOption {
  const char *label;
  int32_t seconds;
};

static const SleepOption sleep_options[] = {
  {"Never", -1},
  {"30 Seconds", 30},
  {"1 Minute", 60},
  {"5 Minutes", 300},
  {"10 Minutes", 600},
  {"30 Minutes", 1800},
  {"1 Hour", 3600}
};

// Brightness presets as percentages of the device's max_brightness. 10% is
// the floor (chosen to keep the screen readable enough to navigate back to
// this menu and recover from a too-dark setting).
struct BrightnessOption {
  const char *label;
  int percent;
};

static const BrightnessOption brightness_options[] = {
  {"Low (10%)",    10},
  {"Dim (25%)",    25},
  {"Medium (50%)", 50},
  {"Bright (75%)", 75},
  {"Max (100%)",  100},
};

SysInfoPanel::SysInfoPanel()
  : cont(lv_obj_create(lv_scr_act()))
  , left_cont(lv_obj_create(cont))
  , right_cont(lv_obj_create(cont))
  , network_label(lv_label_create(right_cont))

  // display sleep
  , disp_sleep_cont(lv_obj_create(left_cont))
  , display_sleep_dd(lv_dropdown_create(disp_sleep_cont))

  // display brightness
  , brightness_cont(lv_obj_create(left_cont))
  , brightness_dd(lv_dropdown_create(brightness_cont))

  // log level
  , ll_cont(lv_obj_create(left_cont))
  , loglevel_dd(lv_dropdown_create(ll_cont))
  , loglevel(1)

  // estop prompt
  , estop_toggle_cont(lv_obj_create(left_cont))
  , prompt_estop_toggle(lv_switch_create(estop_toggle_cont))

  // Z axis icons
  , z_icon_toggle_cont(lv_obj_create(left_cont))
  , z_icon_toggle(lv_switch_create(z_icon_toggle_cont))

  , y_icon_toggle_cont(lv_obj_create(left_cont))
  , y_icon_toggle(lv_switch_create(y_icon_toggle_cont))

  // theme + default temp live in the right column to balance the panel
  , theme_cont(lv_obj_create(right_cont))
  , theme_dd(lv_dropdown_create(theme_cont))
  , theme(0)

  , def_temp_cont(lv_obj_create(right_cont))
  , def_temp_dd(lv_dropdown_create(def_temp_cont))

  , touch_beep_cont(lv_obj_create(right_cont))
  , touch_beep_toggle(lv_switch_create(touch_beep_cont))

  , reset_options_btn(cont, &cancel, "Reset\nOptions", &SysInfoPanel::_handle_callback, this)
  , back_btn(cont, &back, "Back", &SysInfoPanel::_handle_callback, this)
{
  lv_obj_move_background(cont);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);

  lv_obj_clear_flag(left_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(left_cont, LV_PCT(50), LV_PCT(100));
  lv_obj_set_flex_flow(left_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(left_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_clear_flag(right_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(right_cont, LV_PCT(50), LV_PCT(100));
  lv_obj_set_flex_flow(right_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(right_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  Config *conf = Config::get_instance();
  lv_obj_t *l = lv_label_create(disp_sleep_cont);
  lv_obj_set_size(disp_sleep_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(disp_sleep_cont, 0, 0);
  lv_label_set_text(l, "Display Sleep");
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_align(display_sleep_dd, LV_ALIGN_RIGHT_MID, 0, 0);

  std::string dd_options;
  for (const auto &opt : sleep_options) {
    dd_options += opt.label;
    dd_options += '\n';
  }
  dd_options.pop_back();

  lv_dropdown_set_options(display_sleep_dd, dd_options.c_str());

  auto v = conf->get_json("/display_sleep_sec");
  if (!v.is_null()) {
    auto sleep_sec = v.template get<int32_t>();
    for (size_t i = 0; i < std::size(sleep_options); ++i) {
      if (sleep_options[i].seconds == sleep_sec) {
        lv_dropdown_set_selected(display_sleep_dd, i);
        break;
      }
    }
  }

  lv_obj_add_event_cb(display_sleep_dd, &SysInfoPanel::_handle_callback,
    LV_EVENT_VALUE_CHANGED, this);

  // Brightness preset dropdown - same row layout as Display Sleep above.
  // Hidden if the device has no controllable backlight.
  lv_obj_set_size(brightness_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(brightness_cont, 0, 0);
  int b_max = KUtils::backlight_max();
  if (b_max > 0) {
    lv_obj_t *bl_label = lv_label_create(brightness_cont);
    lv_label_set_text(bl_label, "Brightness");
    lv_obj_align(bl_label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_align(brightness_dd, LV_ALIGN_RIGHT_MID, 0, 0);

    std::string b_dd_options;
    for (const auto &opt : brightness_options) {
      b_dd_options += opt.label;
      b_dd_options += '\n';
    }
    b_dd_options.pop_back();
    lv_dropdown_set_options(brightness_dd, b_dd_options.c_str());

    // Pick the saved value's closest preset (or Max if nothing saved).
    int saved_pct = 100;
    auto v_b = conf->get_json("/display_brightness");
    if (!v_b.is_null()) {
      saved_pct = (v_b.template get<int>() * 100) / b_max;
    }
    size_t best_idx = std::size(brightness_options) - 1;
    int best_diff = std::abs(saved_pct - brightness_options[best_idx].percent);
    for (size_t i = 0; i < std::size(brightness_options); ++i) {
      int d = std::abs(saved_pct - brightness_options[i].percent);
      if (d < best_diff) {
        best_diff = d;
        best_idx = i;
      }
    }
    lv_dropdown_set_selected(brightness_dd, best_idx);

    lv_obj_add_event_cb(brightness_dd, &SysInfoPanel::_handle_callback,
      LV_EVENT_VALUE_CHANGED, this);
  } else {
    lv_obj_add_flag(brightness_cont, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_set_size(ll_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(ll_cont, 0, 0);
  l = lv_label_create(ll_cont);
  lv_label_set_text(l, "Log Level");
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_align(loglevel_dd, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_dropdown_set_options(loglevel_dd, fmt::format("{}", fmt::join(log_levels, "\n")).c_str());

  auto df = conf->get_json("/default_printer");
  json j_null;
  v = !df.empty() ? conf->get_json(conf->df() + "log_level") : j_null;
  if (!v.is_null()) {
    auto it = std::find(log_levels.begin(), log_levels.end(), v.template get<std::string>());
    if (it != std::end(log_levels)) {
      loglevel = std::distance(log_levels.begin(), it);
      lv_dropdown_set_selected(loglevel_dd, loglevel);
    }
  } else {
    lv_dropdown_set_selected(loglevel_dd, loglevel);
  }

  lv_obj_add_event_cb(loglevel_dd, &SysInfoPanel::_handle_callback,
    LV_EVENT_VALUE_CHANGED, this);

  lv_obj_set_size(estop_toggle_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(estop_toggle_cont, 0, 0);

  l = lv_label_create(estop_toggle_cont);
  lv_label_set_text(l, "Prompt Emergency Stop");
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_align(prompt_estop_toggle, LV_ALIGN_RIGHT_MID, 0, 0);

  v = conf->get_json("/prompt_emergency_stop");
  if (!v.is_null()) {
    if (v.template get<bool>()) {
      lv_obj_add_state(prompt_estop_toggle, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(prompt_estop_toggle, LV_STATE_CHECKED);
    }
  } else {
    lv_obj_add_state(prompt_estop_toggle, LV_STATE_CHECKED);
  }

  lv_obj_add_event_cb(prompt_estop_toggle, &SysInfoPanel::_handle_callback,
    LV_EVENT_VALUE_CHANGED, this);

  /* Z icon selection */
  lv_obj_set_size(z_icon_toggle_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(z_icon_toggle_cont, 0, 0);

  l = lv_label_create(z_icon_toggle_cont);
  lv_label_set_text(l, "Invert Z Direction");
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_align(z_icon_toggle, LV_ALIGN_RIGHT_MID, 0, 0);

  v = conf->get_json("/invert_z_direction");
  if (!v.is_null()) {
    if (v.template get<bool>()) {
      lv_obj_add_state(z_icon_toggle, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(z_icon_toggle, LV_STATE_CHECKED);
    }
  } else {
    // Default is cleared
    lv_obj_clear_state(z_icon_toggle, LV_STATE_CHECKED);
  }

  lv_obj_add_event_cb(z_icon_toggle, &SysInfoPanel::_handle_callback,
    LV_EVENT_VALUE_CHANGED, this);

  /* Y direction (bed-slinger): the up-arrow/Y+ jog moves the bed, which on a
   * bed-slinger travels opposite to the toolhead frame, so the arrows can feel
   * reversed. This flips the homing-panel Y jog like Invert Z does for Z. */
  lv_obj_set_size(y_icon_toggle_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(y_icon_toggle_cont, 0, 0);

  l = lv_label_create(y_icon_toggle_cont);
  lv_label_set_text(l, "Invert Y Direction");
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_align(y_icon_toggle, LV_ALIGN_RIGHT_MID, 0, 0);

  v = conf->get_json("/invert_y_direction");
  if (!v.is_null()) {
    if (v.template get<bool>()) {
      lv_obj_add_state(y_icon_toggle, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(y_icon_toggle, LV_STATE_CHECKED);
    }
  } else {
    // Default is cleared
    lv_obj_clear_state(y_icon_toggle, LV_STATE_CHECKED);
  }

  lv_obj_add_event_cb(y_icon_toggle, &SysInfoPanel::_handle_callback,
    LV_EVENT_VALUE_CHANGED, this);

  // theme dropdown
  lv_obj_set_size(theme_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(theme_cont, 0, 0);
  l = lv_label_create(theme_cont);
  lv_label_set_text(l, "Theme Color");
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_align(theme_dd, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_dropdown_set_options(theme_dd, fmt::format("{}", fmt::join(themes, "\n")).c_str());

  v = conf->get_json("/theme");
  if (!v.is_null()) {
    auto it = std::find(themes.begin(), themes.end(), v.template get<std::string>());
    if (it != std::end(themes)) {
      theme = std::distance(themes.begin(), it);
      lv_dropdown_set_selected(theme_dd, theme);
    }
  } else {
    lv_dropdown_set_selected(theme_dd, theme);
  }
  lv_obj_add_event_cb(theme_dd, &SysInfoPanel::_handle_callback,
    LV_EVENT_VALUE_CHANGED, this);

  // default extruder temp
  lv_obj_set_size(def_temp_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(def_temp_cont, 0, 0);
  l = lv_label_create(def_temp_cont);
  lv_label_set_text(l, "Def. Temp");
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_align(def_temp_dd, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_dropdown_set_options(def_temp_dd, "180\n200\n220\n240\n260\n280\n300");
  auto def_ext_temp = conf->get_json("/default_extruder_temp");
  if (!def_ext_temp.is_null()) {
    std::string val = std::to_string(def_ext_temp.template get<int>());
    int32_t idx = lv_dropdown_get_option_index(def_temp_dd, val.c_str());
    if (idx != -1) {
      lv_dropdown_set_selected(def_temp_dd, idx);
    }
  }
  lv_obj_add_event_cb(def_temp_dd, &SysInfoPanel::_handle_callback, LV_EVENT_VALUE_CHANGED, this);

  // Touch Beep: opt-in audible click feedback on the hardware buzzer. Off by
  // default; mirrors the stock Creality KE click sound.
  lv_obj_set_size(touch_beep_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(touch_beep_cont, 0, 0);
  l = lv_label_create(touch_beep_cont);
  lv_label_set_text(l, "Touch Beep");
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_align(touch_beep_toggle, LV_ALIGN_RIGHT_MID, 0, 0);

  v = conf->get_json("/touch_beep");
  if (!v.is_null() && v.template get<bool>()) {
    lv_obj_add_state(touch_beep_toggle, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(touch_beep_toggle, LV_STATE_CHECKED);
  }

  lv_obj_add_event_cb(touch_beep_toggle, &SysInfoPanel::_handle_callback,
    LV_EVENT_VALUE_CHANGED, this);

  // Reset Options lives alone in the top-right corner, away from other controls.
  lv_obj_add_flag(reset_options_btn.get_container(), LV_OBJ_FLAG_FLOATING);
  lv_obj_align(reset_options_btn.get_container(), LV_ALIGN_TOP_RIGHT, 0, 0);

  lv_obj_add_flag(back_btn.get_container(), LV_OBJ_FLAG_FLOATING);
  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, 0);

  // The network/version text is left-aligned and the Reset Options button is
  // narrow and pinned to the right, so they don't overlap. Reserve only the
  // button's width on the right so a very long interface line can't slide under
  // it - full vertical height stays available so the version never clips.
  lv_obj_update_layout(cont);
  lv_obj_set_width(network_label,
    lv_obj_get_width(right_cont)
      - lv_obj_get_width(reset_options_btn.get_container()) - 4);
  lv_label_set_long_mode(network_label, LV_LABEL_LONG_WRAP);

  // The Theme/Def-Temp rows now share the right column with the network text.
  // A long network list (e.g. many interfaces) can push these rows down into
  // the Back button's band, so reserve clearance for the WIDER of the two
  // floating corner buttons (Reset Options top-right, Back bottom-right) plus a
  // comfortable margin, otherwise a right-aligned dropdown tucks under a button.
  lv_coord_t corner_btn_w = std::max(
      lv_obj_get_width(reset_options_btn.get_container()),
      lv_obj_get_width(back_btn.get_container()));
  lv_coord_t right_row_w = lv_obj_get_width(right_cont) - corner_btn_w - 12;
  lv_obj_set_width(theme_cont, right_row_w);
  lv_obj_set_width(def_temp_cont, right_row_w);
  lv_obj_set_width(touch_beep_cont, right_row_w);
}

SysInfoPanel::~SysInfoPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void SysInfoPanel::foreground() {
  lv_obj_move_foreground(cont);

  auto ifaces = KUtils::get_interfaces();
  std::vector<std::string> network_detail;
  network_detail.push_back("Network");
  for (auto &iface : ifaces) {
    auto ip = KUtils::interface_ip(iface);
    network_detail.push_back(fmt::format("\t{}: {}", iface, ip));
  }
  lv_label_set_text(network_label, fmt::format("{}\n\nGuppyScreen\n\tVersion: " GS_VERSION,
    fmt::join(network_detail, "\n")).c_str());
}

void SysInfoPanel::handle_callback(lv_event_t *e)
{
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(e);

    if (btn == back_btn.get_container()) {
      lv_obj_move_background(cont);
    } else if (btn == reset_options_btn.get_container()) {
      show_reset_options();
    }
  } else if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t *obj = lv_event_get_target(e);
    Config *conf = Config::get_instance();
    if (obj == loglevel_dd) {
      auto idx = lv_dropdown_get_selected(loglevel_dd);
      if (idx != loglevel) {
        if (loglevel < log_levels.size()) {
          loglevel = idx;
          auto ll = spdlog::level::from_str(log_levels[loglevel]);

          spdlog::set_level(ll);
          spdlog::flush_on(ll);
          spdlog::debug("setting log_level to {}", log_levels[loglevel]);
          conf->set<std::string>(conf->df() + "log_level", log_levels[loglevel]);
          conf->save();
        }
      }
    } else if (obj == prompt_estop_toggle) {
      bool should_prompt = lv_obj_has_state(prompt_estop_toggle, LV_STATE_CHECKED);
      conf->set<bool>("/prompt_emergency_stop", should_prompt);
      conf->save();
    } else if (obj == display_sleep_dd) {
      char buf[64];
      lv_dropdown_get_selected_str(display_sleep_dd, buf, sizeof(buf));
      std::string selected_label = buf;

      for (const auto &opt : sleep_options) {
        if (opt.label == selected_label) {
          conf->set<int32_t>("/display_sleep_sec", opt.seconds);
          conf->save();
          break;
        }
      }
    } else if (obj == z_icon_toggle) {
      bool inverted = lv_obj_has_state(z_icon_toggle, LV_STATE_CHECKED);
      conf->set<bool>("/invert_z_direction", inverted);
      conf->save();
    } else if (obj == y_icon_toggle) {
      bool inverted = lv_obj_has_state(y_icon_toggle, LV_STATE_CHECKED);
      conf->set<bool>("/invert_y_direction", inverted);
      conf->save();
    } else if (obj == touch_beep_toggle) {
      bool enabled = lv_obj_has_state(touch_beep_toggle, LV_STATE_CHECKED);
      conf->set<bool>("/touch_beep", enabled);
      conf->save();
      TouchBeep::set_enabled(enabled);
      if (enabled) {
        TouchBeep::beep(); // immediate confirmation that it works
      }
    } else if (obj == theme_dd) {
      auto idx = lv_dropdown_get_selected(theme_dd);
      if (idx != theme) {
        theme = idx;
        auto selected_theme = themes[theme];
        conf->set<std::string>("/theme", selected_theme);
        conf->save();
        auto theme_config = fs::canonical(conf->get_path()).parent_path() / "themes" / (selected_theme + ".json");
        ThemeConfig::get_instance()->init(theme_config);
        GuppyScreen::refresh_theme();
      }
    } else if (obj == def_temp_dd) {
      char buf[64];
      lv_dropdown_get_selected_str(def_temp_dd, buf, sizeof(buf));
      conf->set<int>("/default_extruder_temp", std::stoi(buf));
      conf->save();
    } else if (obj == brightness_dd) {
      // Resolve preset → absolute brightness, apply, and persist.
      auto idx = lv_dropdown_get_selected(brightness_dd);
      if (idx < std::size(brightness_options)) {
        int b_max = KUtils::backlight_max();
        int value = (b_max * brightness_options[idx].percent) / 100;
        if (value < 1) value = 1;  // never write 0 from settings (sleep path does that)
        KUtils::backlight_set(value);
        conf->set<int>("/display_brightness", value);
        conf->save();
      }
    }
  }
}

void SysInfoPanel::show_reset_options() {
  lv_obj_t *overlay = lv_obj_create(lv_scr_act());
  lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(overlay, 0, 0);
  lv_obj_set_style_pad_all(overlay, 0, 0);

  lv_obj_t *box = lv_obj_create(overlay);
  KUtils::style_dialog_box(box);
  lv_obj_set_size(box, LV_PCT(92), LV_SIZE_CONTENT);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(box, 8, 0);
  lv_obj_set_style_pad_all(box, 10, 0);
  lv_obj_center(box);

  lv_obj_t *title = lv_label_create(box);
  KUtils::style_dialog_title(title);
  lv_label_set_text(title, "Reset Options");
  lv_obj_set_width(title, LV_PCT(100));

  auto make_opt_btn = [&](const char *lbl_text, const char *desc_text) -> lv_obj_t * {
    lv_obj_t *btn = lv_btn_create(box);
    lv_obj_set_size(btn, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(btn, lv_palette_darken(LV_PALETTE_GREY, 2), 0);
    lv_obj_set_style_pad_all(btn, 8, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(btn, 3, 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, lbl_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_width(lbl, LV_PCT(100));

    lv_obj_t *desc = lv_label_create(btn);
    lv_label_set_text(desc, desc_text);
    lv_obj_set_style_text_font(desc, &lv_font_montserrat_10, 0);
    lv_obj_set_width(desc, LV_PCT(100));
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);

    return btn;
  };

  lv_obj_t *btn1 = make_opt_btn(
    "Reset GuppyScreen settings",
    "Deletes display config and sensor layout.\nGuppyScreen restarts with defaults."
  );

  lv_obj_t *btn2 = make_opt_btn(
    "Factory Reset Printer",
    "Wipes OpenKE, Klipper config, gcodes and\ncalibration. Reboots to stock Creality firmware."
  );

  lv_obj_t *btn3 = make_opt_btn(
    "Reset Touch Calibration",
    "Clears saved calibration coefficients.\nGuppyScreen restarts and asks you to recalibrate."
  );

  lv_obj_t *close_btn = lv_btn_create(box);
  lv_obj_set_size(close_btn, LV_PCT(50), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(close_btn, lv_palette_darken(LV_PALETTE_GREY, 1), 0);
  lv_obj_t *close_lbl = lv_label_create(close_btn);
  lv_label_set_text(close_lbl, "Close");
  lv_obj_center(close_lbl);

  lv_obj_add_event_cb(close_btn, [](lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
      // close_btn -> box -> overlay
      lv_obj_del_async(lv_obj_get_parent(lv_obj_get_parent(lv_event_get_current_target(e))));
    }
  }, LV_EVENT_CLICKED, NULL);

  lv_obj_add_event_cb(btn1, [](lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ((SysInfoPanel *)e->user_data)->show_reset_confirm(
      "Reset GuppyScreen settings?",
      "Deletes guppyconfig.json and restarts.\nKlipper keeps running.\n\nThis cannot be undone.",
      []() {
        Config *conf = Config::get_instance();
        fs::remove(conf->get_path());
        spdlog::warn("GuppyScreen config reset.");
        _exit(0);
      }
    );
  }, LV_EVENT_CLICKED, this);

  lv_obj_add_event_cb(btn2, [](lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ((SysInfoPanel *)e->user_data)->show_reset_confirm(
      "Factory Reset Printer?",
      "Wipes OpenKE, Klipper config, gcodes\nand calibration. Reboots to stock.\n\nThis cannot be undone.",
      []() {
        spdlog::warn("Factory reset printer: executing S58factoryreset reset");
        system("/etc/init.d/S58factoryreset reset");
      }
    );
  }, LV_EVENT_CLICKED, this);

  lv_obj_add_event_cb(btn3, [](lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ((SysInfoPanel *)e->user_data)->show_reset_confirm(
      "Reset Touch Calibration?",
      "Clears saved calibration. GuppyScreen\nwill restart and ask you to recalibrate.",
      []() {
        spdlog::warn("Touch calibration reset requested.");
        Config *conf = Config::get_instance();
        conf->set<bool>("/touch_calibrated", true);
        conf->set<json>("/touch_calibration_coeff", json());
        conf->save();
        _exit(0);
      }
    );
  }, LV_EVENT_CLICKED, this);
}

void SysInfoPanel::show_reset_confirm(const char *title, const char *detail,
                                       const std::function<void()> &cb) {
  static const char *btns[] = {"Cancel", "Confirm", ""};

  lv_obj_t *mbox = lv_msgbox_create(NULL, NULL,
    fmt::format("{}\n\n{}", title, detail).c_str(), btns, false);
  KUtils::style_dialog_msgbox(mbox);

  lv_obj_t *msg = ((lv_msgbox_t *)mbox)->text;
  lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(msg, LV_PCT(100));
  lv_obj_center(msg);

  lv_obj_t *btnm = lv_msgbox_get_btns(mbox);
  lv_btnmatrix_set_btn_ctrl(btnm, 0, LV_BTNMATRIX_CTRL_CHECKED);
  lv_btnmatrix_set_btn_ctrl(btnm, 1, LV_BTNMATRIX_CTRL_CHECKED);
  lv_obj_add_flag(btnm, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(btnm, LV_ALIGN_BOTTOM_MID, 0, 0);

  auto hscale = (double)lv_disp_get_physical_ver_res(NULL) / 480.0;
  lv_obj_set_size(btnm, LV_PCT(90), 50 * hscale);
  lv_obj_set_size(mbox, LV_PCT(75), LV_PCT(55));

  lv_obj_add_event_cb(btnm, [](lv_event_t *e) {
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part == LV_PART_ITEMS && dsc->id == 1) {
      dsc->rect_dsc->bg_color = lv_color_hex(0xC62828);
      dsc->rect_dsc->bg_opa   = LV_OPA_COVER;
      dsc->label_dsc->color   = lv_color_white();
    }
  }, LV_EVENT_DRAW_PART_BEGIN, NULL);

  auto *pcb = new std::function<void()>(cb);
  lv_obj_add_event_cb(mbox, [](lv_event_t *e) {
    lv_obj_t *obj = lv_obj_get_parent(lv_event_get_target(e));
    auto *pcb = (std::function<void()> *)e->user_data;
    if (lv_msgbox_get_active_btn(obj) == 1) {
      (*pcb)();
    }
    delete pcb;
    lv_msgbox_close(obj);
  }, LV_EVENT_VALUE_CHANGED, pcb);

  lv_obj_center(mbox);
}
