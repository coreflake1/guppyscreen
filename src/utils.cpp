#include "hv/requests.h"
#include "hv/hurl.h"
#include "config.h"
#include "state.h"
#include "spdlog/spdlog.h"
#include "platform.h"
#include "lvgl/lvgl.h"

#include <cmath>
#include <time.h>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <experimental/filesystem>
#include <regex>

namespace fs = std::experimental::filesystem;

namespace KUtils {

  bool is_homed() {
    auto v = State::get_instance()
      ->get_data("/printer_state/toolhead/homed_axes"_json_pointer);
    if (!v.is_null()) {
      std::string homed_axes = v.template get<std::string>();
      return homed_axes.find("x") != std::string::npos
        && homed_axes.find("y") != std::string::npos
        && homed_axes.find("z") != std::string::npos;
    }
    return false;
  }

  bool is_printing() {
    auto v = State::get_instance()
      ->get_data("/printer_state/print_stats/state"_json_pointer);
    if (!v.is_null()) {
      std::string s = v.template get<std::string>();
      return s == "printing" || s == "paused";
    }
    return false;
  }

  void style_dialog_overlay(lv_obj_t *overlay) {
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
  }

  void style_dialog_box(lv_obj_t *box) {
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_bg_color(box, lv_palette_darken(LV_PALETTE_GREY, 1), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(box, 12, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  }

  void style_dialog_title(lv_obj_t *title) {
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);
  }

  void style_dialog_msgbox(lv_obj_t *mbox) {
    lv_obj_set_style_border_width(mbox, 2, 0);
    lv_obj_set_style_radius(mbox, 8, 0);
    lv_obj_set_style_bg_color(mbox, lv_palette_darken(LV_PALETTE_GREY, 1), 0);
    lv_obj_set_style_bg_opa(mbox, LV_OPA_COVER, 0);

    // lv_msgbox renders its buttons as a button-matrix whose default item style
    // is dark; restyle the items to the themed (blue) primary so they match the
    // lv_btn buttons used by the custom-box dialogs.
    lv_obj_t *btnm = lv_msgbox_get_btns(mbox);
    if (btnm != NULL) {
      lv_color_t primary = lv_theme_get_color_primary(mbox);
      lv_obj_set_style_bg_opa(btnm, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_border_width(btnm, 0, LV_PART_MAIN);
      lv_obj_set_style_bg_opa(btnm, LV_OPA_COVER, LV_PART_ITEMS);
      lv_obj_set_style_bg_color(btnm, primary, LV_PART_ITEMS);
      lv_obj_set_style_bg_color(btnm, primary, LV_PART_ITEMS | LV_STATE_CHECKED);
      lv_obj_set_style_text_color(btnm, lv_color_white(), LV_PART_ITEMS);
      lv_obj_set_style_radius(btnm, 4, LV_PART_ITEMS);
    }
  }

  // Shared styling so print-lock dialogs match the app's other msgboxes:
  // centered body text and a floating, centered button row at the bottom.
  static void style_lock_mbox(lv_obj_t *mbox, lv_coord_t btns_pct) {
    style_dialog_msgbox(mbox);

    lv_obj_t *msg = ((lv_msgbox_t *)mbox)->text;
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(msg, LV_PCT(100));
    lv_obj_center(msg);

    lv_obj_t *btnm = lv_msgbox_get_btns(mbox);
    lv_obj_add_flag(btnm, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(btnm, LV_ALIGN_BOTTOM_MID, 0, 0);

    auto hscale = (double)lv_disp_get_physical_ver_res(NULL) / 480.0;
    lv_obj_set_size(btnm, LV_PCT(btns_pct), 50 * hscale);
    lv_obj_set_size(mbox, LV_PCT(70), LV_PCT(45));
    lv_obj_center(mbox);
  }

  void notify_locked() {
    static const char *btns[] = {"OK", ""};
    lv_obj_t *mbox = lv_msgbox_create(NULL, NULL,
      "Unavailable while printing", btns, false);
    lv_obj_add_event_cb(mbox, [](lv_event_t *e) {
      lv_msgbox_close(lv_obj_get_parent(lv_event_get_target(e)));
    }, LV_EVENT_VALUE_CHANGED, NULL);
    style_lock_mbox(mbox, 50);
  }

  void notify_toast(const std::string &msg, uint32_t timeout_ms) {
    lv_obj_t *mbox = lv_msgbox_create(NULL, NULL, msg.c_str(), NULL, false);
    style_dialog_msgbox(mbox);
    lv_obj_t *txt = ((lv_msgbox_t *)mbox)->text;
    lv_obj_set_style_text_align(txt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(txt, LV_PCT(100));
    lv_obj_center(txt);
    lv_obj_set_width(mbox, LV_PCT(70));
    lv_obj_set_height(mbox, LV_SIZE_CONTENT);
    lv_obj_center(mbox);

    // Auto-close once after timeout_ms (repeat_count 1 → LVGL deletes the timer).
    lv_timer_t *t = lv_timer_create([](lv_timer_t *tm) {
      lv_obj_t *m = (lv_obj_t *)tm->user_data;
      if (m != NULL) {
        lv_msgbox_close(m);
      }
    }, timeout_ms, mbox);
    lv_timer_set_repeat_count(t, 1);
  }

  void confirm_if_printing(const std::string &msg, const std::function<void()> &cb) {
    if (!is_printing()) {
      cb();
      return;
    }

    auto *heap_cb = new std::function<void()>(cb);
    static const char *btns[] = {"Confirm", "Cancel", ""};
    lv_obj_t *mbox = lv_msgbox_create(NULL, NULL, msg.c_str(), btns, false);
    lv_obj_add_event_cb(mbox, [](lv_event_t *e) {
      lv_obj_t *obj = lv_obj_get_parent(lv_event_get_target(e));
      auto *fn = (std::function<void()> *)lv_event_get_user_data(e);
      if (lv_msgbox_get_active_btn(obj) == 0 && fn != NULL) {
        (*fn)();
      }
      delete fn;
      lv_msgbox_close(obj);
    }, LV_EVENT_VALUE_CHANGED, heap_cb);
    style_lock_mbox(mbox, 90);
  }

  bool is_running_local() {
    Config *conf = Config::get_instance();
    std::string df_host = conf->get<std::string>(conf->df() + "moonraker_host");
    return df_host == "localhost" || df_host == "127.0.0.1";
  }

  std::string get_root_path(const std::string root_name) {
    auto roots = State::get_instance()->get_data("/roots"_json_pointer);
    json filtered;
    std::copy_if(roots.begin(), roots.end(),
      std::back_inserter(filtered), [&root_name](const json &item) {
        return item.contains("name") && item["name"] == root_name;
      });

    spdlog::trace("roots {}, filtered {}", roots.dump(), filtered.dump());
    if (!filtered.empty()) {
      return filtered["/0/path"_json_pointer];
    }

    return "";
  }

  std::pair<std::string, std::pair<size_t, size_t>> get_thumbnail(const std::string &gcode_file, json &j, double scale) {
    auto &thumbs = j["/result/thumbnails"_json_pointer];
    if (!thumbs.is_null() && !thumbs.empty()) {
      auto scaled_width = scale * 300;
      spdlog::debug("using thumb at scaled width {}", scaled_width);
      uint32_t closest_index = 0;
      size_t thumb_width = 0;
      size_t thumb_height = 0;

      auto width = thumbs.at(0)["width"].is_number()
        ? thumbs.at(0)["width"].template get<int>()
        : std::stoi(thumbs.at(0)["width"].template get<std::string>());

      auto height = thumbs.at(0)["height"].is_number()
        ? thumbs.at(0)["height"].template get<int>()
        : std::stoi(thumbs.at(0)["height"].template get<std::string>());

      int closest = std::abs(scaled_width - width);
      for (int i = 0; i < thumbs.size(); i++) {
        width = thumbs.at(i)["width"].is_number()
          ? thumbs.at(i)["width"].template get<int>()
          : std::stoi(thumbs.at(i)["width"].template get<std::string>());
        height = thumbs.at(i)["height"].is_number()
          ? thumbs.at(i)["height"].template get<int>()
          : std::stoi(thumbs.at(i)["height"].template get<std::string>());

        int cur_diff = std::abs(scaled_width - width);
        if (cur_diff < closest) {
          closest = cur_diff;
          closest_index = i;
          thumb_width = width;
          thumb_height = height;
        }
      }

      auto &thumb = thumbs.at(closest_index);
      spdlog::debug("using thumb at index {}, {}", closest_index, thumbs.dump());

      std::string relative_path = thumb["relative_path"].template get<std::string>();
      size_t found = gcode_file.find_last_of("/\\");
      if (found != std::string::npos) {
        relative_path = gcode_file.substr(0, found + 1) + relative_path;
      }

      Config *conf = Config::get_instance();
      std::string df_host = conf->get<std::string>(conf->df() + "moonraker_host");
      std::string fname = relative_path.substr(relative_path.find_last_of("/\\") + 1);
      std::string fullpath = fmt::format("{}/{}", conf->get<std::string>("/thumbnail_path"), fname);

      if (is_running_local()) {
        spdlog::debug("running locally, skipping thumbnail downloads");
        auto gcode_root = get_root_path("gcodes");
        fullpath = fmt::format("{}/{}", gcode_root, relative_path);
      } else {
        std::string thumb_url = fmt::format("http://{}:{}/server/files/gcodes/{}",
          df_host,
          conf->get<uint32_t>(conf->df() + "moonraker_port"),
          HUrl::escape(relative_path));
        spdlog::debug("thumb url {}", thumb_url);
        auto size = requests::downloadFile(thumb_url.c_str(), fullpath.c_str());
        spdlog::trace("downloaded size {}", size);
      }

      return std::make_pair(fullpath, std::make_pair(thumb_width, thumb_height));
    }

    return std::make_pair("", std::make_pair(0, 0));
  }


  std::string download_file(const std::string &root,
    const std::string &fname,
    const std::string &dest) {

    auto filename = fs::path(fname).filename();
    auto dest_fullpath = fs::path(dest) / filename;

    spdlog::trace("root {}, fname {}, base filename {}, dest_fp {}", root, fname,
      filename.string(), dest_fullpath.string());
    Config *conf = Config::get_instance();
    std::string df_host = conf->get<std::string>(conf->df() + "moonraker_host");

    std::string file_url = fmt::format("http://{}:{}/server/files/{}/{}",
      df_host,
      conf->get<uint32_t>(conf->df() + "moonraker_port"),
      root,
      HUrl::escape(fname));
    // threadpool this
    spdlog::debug("file url {}", file_url);
    auto size = requests::downloadFile(file_url.c_str(), dest_fullpath.c_str());
    spdlog::trace("downloaded file size {}", size);

    return dest_fullpath.string();
  }

  std::vector<std::string> get_interfaces() {
    std::vector<std::string> ifaces;
#ifndef OS_ANDROID
    struct ifaddrs *addrs;
    getifaddrs(&addrs);
    for (struct ifaddrs *addr = addrs; addr != nullptr; addr = addr->ifa_next) {
      if (addr->ifa_addr && addr->ifa_addr->sa_family == AF_PACKET) {
        ifaces.push_back(addr->ifa_name);
      }
    }

    freeifaddrs(addrs);
#endif // OS_ANDROID
    return ifaces;
  }

  std::string interface_ip(const std::string &interface) {
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

    struct ifreq ifr {};
    strcpy(ifr.ifr_name, interface.c_str());
    ioctl(fd, SIOCGIFADDR, &ifr);
    close(fd);

    char ip[INET_ADDRSTRLEN];
    strcpy(ip, inet_ntoa(((sockaddr_in *)&ifr.ifr_addr)->sin_addr));
    return ip;
  }

  std::string get_wifi_interface() {
    std::string wpa_socket = Config::get_instance()->get<std::string>("/wpa_supplicant");
    if (fs::is_directory(fs::status(wpa_socket))) {
      for (const auto &e : fs::directory_iterator(wpa_socket)) {
        if (fs::is_socket(e.path()) && e.path().string().find("p2p") == std::string::npos) {
          return e.path().filename().string();
        }
      }
    }

    return "";
  }

  template <typename Out>
  void split(const std::string &s, char delim, Out result) {
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, delim)) {
      *result++ = item;
    }
  }

  std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, std::back_inserter(elems));
    return elems;
  }

  std::string get_obj_name(const std::string &id) {
    size_t pos = id.find_last_of(' ');
    return id.substr(pos + 1);
  }

  std::string to_title(std::string s) {
    bool last = true;
    for (char &c : s) {
      c = last ? std::toupper(c) : std::tolower(c);
      if (c == '_') {
        c = ' ';
      }

      last = std::isspace(c);
    }
    return s;
  }


  std::string eta_string(int64_t s) {
    time_t seconds(s);
    tm p;
    gmtime_r(&seconds, &p);

    std::ostringstream os;

    if (p.tm_yday > 0)
      os << p.tm_yday << "d ";

    if (p.tm_hour > 0)
      os << p.tm_hour << "h ";

    if (p.tm_min > 0)
      os << p.tm_min << "m ";

    os << p.tm_sec << "s";

    return os.str();
  }

  size_t bytes_to_mb(size_t s) {
    return s / 1024 / 1024;
  }

  // First /sys/class/backlight/<dev>/ found, or "" if no device. Cached
  // after first scan so we don't hit the FS on every brightness write.
  static const std::string& backlight_dir() {
    static std::string cached;
    static bool searched = false;
    if (!searched) {
      searched = true;
      try {
        if (fs::is_directory("/sys/class/backlight")) {
          for (auto &p : fs::directory_iterator("/sys/class/backlight")) {
            cached = p.path().string();
            break;
          }
        }
      } catch (const std::exception &e) {
        spdlog::debug("backlight scan failed: {}", e.what());
      }
      if (cached.empty()) {
        spdlog::info("no /sys/class/backlight device — brightness control disabled");
      } else {
        spdlog::info("backlight device: {}", cached);
      }
    }
    return cached;
  }

  static int read_int_file(const std::string &path, int fallback) {
    std::ifstream f(path);
    int v = fallback;
    if (f) { f >> v; }
    return v;
  }

  int backlight_max() {
    auto &d = backlight_dir();
#ifdef SIMULATOR
    // No /sys/class/backlight on x86 desktop; pretend we're a KE so the
    // sysinfo slider renders with the same range it'd show on-device.
    if (d.empty()) return 300;
#endif
    if (d.empty()) return 0;
    return read_int_file(d + "/max_brightness", 0);
  }

  // Used by SIMULATOR and as a fallback when sysfs reads fail.
  static int cached_brightness = -1;

  int backlight_get() {
    auto &d = backlight_dir();
    if (d.empty()) {
      return cached_brightness;
    }
    int v = read_int_file(d + "/brightness", -1);
    if (v >= 0) cached_brightness = v;
    return v;
  }

  void backlight_set(int v) {
    if (v < 0) v = 0;
    int mx = backlight_max();
    if (mx > 0 && v > mx) v = mx;

    cached_brightness = v;
    auto &d = backlight_dir();
    if (d.empty()) return;
    std::ofstream f(d + "/brightness");
    if (f) {
      f << v;
    } else {
      spdlog::warn("backlight_set: failed to write {}/brightness", d);
    }
  }

  std::map<std::string, std::map<std::string, std::string>> parse_macros(json &m) {
    std::map<std::string, std::map<std::string, std::string>> macros;

    std::regex param_regex(R"(params\.(\w+)(.*))", std::regex_constants::icase);
    std::regex default_value_regex(R"(\|\s*default\s*\(\s*((["'])(?:\\.|[^\x02])*\2|-?[0-9][^,)]*))",
      std::regex_constants::icase);
    for (auto &el : m.items()) {
      std::string key = el.key();
      if (key.rfind("gcode_macro ", 0) == 0) {
        auto &gcode = el.value()["/gcode"_json_pointer];
        if (!gcode.is_null()) {
          auto macro_split = split(el.key(), ' ');
          if (macro_split.size() > 1 && macro_split[1].rfind("_", 0) != 0) {
            std::string macro_name = macro_split[1];

            const auto &gcode_str = gcode.template get<std::string>();
            auto param_begin =
              std::sregex_iterator(gcode_str.begin(), gcode_str.end(), param_regex);
            auto param_end = std::sregex_iterator();

            std::map<std::string, std::string> macro_params;
            for (std::sregex_iterator i = param_begin; i != param_end; ++i) {
              std::smatch match = *i;
              std::string param_name = match.str(1);
              std::string rest = match.str(2);
              std::smatch matches;
              std::string default_value = "";

              spdlog::trace("macro: {}, param; {}, rest: {}", macro_name, param_name, rest);

              if (std::regex_search(rest, matches, default_value_regex)) {
                default_value = matches.str(1);
              }

              macro_params.insert({param_name, default_value});
            }
            macros.insert({macro_name, macro_params});
          }
        }
      }
    }

    return macros;
  }
}  // namespace KUtils
