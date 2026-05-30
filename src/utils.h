#ifndef __K_UTILS_H__
#define __K_UTILS_H__

#include "hv/json.hpp"
#include "lvgl/lvgl.h"
#include <vector>
#include <map>
#include <functional>
#include <algorithm>
#include <utility>

using json = nlohmann::json;

namespace KUtils {
  bool is_homed();
  // True while a print job is active (printing or paused). Panels that move axes,
  // home, or run calibration must not act in this state.
  bool is_printing();
  // Modal "Unavailable while printing" notice (single OK button).
  void notify_locked();
  // Transient, auto-dismissing message (no buttons). Used for non-blocking
  // feedback like "Heating, press again when ready".
  void notify_toast(const std::string &msg, uint32_t timeout_ms = 1800);
  // Runs cb immediately when idle; while printing, shows a Confirm/Cancel modal
  // and only runs cb if the user confirms the override.
  void confirm_if_printing(const std::string &msg, const std::function<void()> &cb);
  bool is_running_local();
  std::string get_root_path(const std::string root_name);

  // ---- Shared dialog look ----------------------------------------------------
  // One definition of the app's modal look so every popup matches and can't
  // drift. Custom dialogs (their own overlay + box) use the first three; the
  // last styles an LVGL lv_msgbox to the same card look. Sizes/alignment stay
  // per-dialog; these set only the shared visuals (dim backdrop, rounded grey
  // bordered card, montserrat_14 title).
  void style_dialog_overlay(lv_obj_t *overlay);  // full-screen dim backdrop
  void style_dialog_box(lv_obj_t *box);          // the centered card
  void style_dialog_title(lv_obj_t *title);      // top-aligned header label
  void style_dialog_msgbox(lv_obj_t *mbox);      // card look for an lv_msgbox
  // Full modal look for an lv_msgbox: card + centered body + floating, centered
  // button row (btns_pct = button-row width). Matches the print-lock dialogs.
  void style_lock_mbox(lv_obj_t *mbox, lv_coord_t btns_pct);

  // path, width
  std::pair<std::string, std::pair<size_t, size_t>> get_thumbnail(const std::string &gcode_file, json &j, double scale);

  std::string download_file(const std::string &root,
    const std::string &fname,
    const std::string &dest);

  std::vector<std::string> get_interfaces();
  std::string interface_ip(const std::string &interface);
  std::string get_wifi_interface();

  template <typename Out>
  void split(const std::string &s, char delim, Out result);

  std::vector<std::string> split(const std::string &s, char delim);

  std::string get_obj_name(const std::string &id);
  std::string to_title(std::string s);
  std::string eta_string(int64_t s);
  size_t bytes_to_mb(size_t s);

  // Backlight helpers. Scan /sys/class/backlight/ on first call; pick the first
  // device. backlight_max() returns 0 if no device is found, in which case the
  // callers should hide the brightness UI and skip writes. backlight_set() is
  // a no-op when no device is available (or in SIMULATOR builds where it just
  // caches the value for the sysinfo slider to read back).
  int backlight_max();
  int backlight_get();
  void backlight_set(int value);

  template<typename T, typename U> void sort_map_values(std::map<T, U> v,
    std::vector<U> &out_vect,
    std::function<bool(U &, U &)> sorter) {
    for (auto &el : v) {
      out_vect.push_back(el.second);
    }

    std::sort(out_vect.begin(), out_vect.end(), sorter);
  };

  std::map<std::string, std::map<std::string, std::string>> parse_macros(json &m);

};

#endif // __K_UTILS_H__
