#include "wifi_panel.h"
#include "utils.h"
#include "config.h"
#include "spdlog/spdlog.h"

#include <sstream>
#include <iostream>
#include <vector>
#include <utility>
#include <algorithm>

LV_IMG_DECLARE(back);

static void draw_part_event_cb(lv_event_t *e)
{
  lv_obj_t *obj = lv_event_get_target(e);
  lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
  if (dsc->part == LV_PART_ITEMS) {
    uint32_t row = dsc->id / lv_table_get_col_cnt(obj);
    uint32_t col = dsc->id - row * lv_table_get_col_cnt(obj);

    if (col == 1 || col == 2) {
      dsc->label_dsc->align = LV_TEXT_ALIGN_CENTER;
    }
  }
}

WifiPanel::WifiPanel(std::mutex &l)
  : lv_lock(l)
  , cont(lv_obj_create(lv_scr_act()))
  , spinner(lv_spinner_create(cont, 1000, 60))
  , top_cont(lv_obj_create(cont))
  , wifi_table(lv_table_create(top_cont))
  , wifi_right(lv_obj_create(top_cont))
  , prompt_cont(wifi_right)
  , wifi_label(lv_label_create(prompt_cont))
  , password_input(lv_textarea_create(prompt_cont))
  , back_btn(cont, &back, "Back", &WifiPanel::_handle_back_btn, this)
  , kb(lv_keyboard_create(cont))
  , pm_cont(lv_obj_create(wifi_right))
  , pm_btn(lv_btn_create(pm_cont))
  , pm_label(lv_label_create(pm_btn))
  , pm_hint(lv_label_create(pm_cont))
{
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_CLICKABLE);

  lv_obj_add_flag(spinner, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_size(spinner, 80, 80);
  lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 0);

  lv_obj_add_flag(back_btn.get_container(), LV_OBJ_FLAG_FLOATING);
  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, -20);

  lv_obj_set_flex_grow(top_cont, 1);
  lv_obj_set_flex_flow(top_cont, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(top_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(top_cont, 0, 0);
  lv_obj_set_width(top_cont, LV_PCT(100));

  lv_obj_set_height(wifi_table, LV_PCT(90));
  // lv_obj_remove_style(wifi_table, NULL, LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_add_flag(wifi_table, LV_OBJ_FLAG_HIDDEN);

  auto half_screen_width = lv_disp_get_physical_hor_res(NULL) / 2;
  int small_col_width = 60;
  int padding = 20;
  int large_col_width = half_screen_width - (2 * small_col_width) - padding;

  lv_table_set_col_width(wifi_table, 0, large_col_width);
  lv_table_set_col_width(wifi_table, 1, small_col_width);
  lv_table_set_col_width(wifi_table, 2, small_col_width);

  lv_obj_add_event_cb(wifi_table, &WifiPanel::_handle_callback, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(wifi_table, &WifiPanel::_handle_callback, LV_EVENT_SIZE_CHANGED, this);
  lv_obj_add_event_cb(wifi_table, draw_part_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

  lv_obj_set_scroll_dir(wifi_table, LV_DIR_TOP | LV_DIR_BOTTOM);

  lv_obj_set_style_border_width(wifi_right, 0, 0);
  lv_obj_set_flex_grow(wifi_right, 1);
  lv_obj_add_flag(wifi_right, LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_CLICKABLE);

  lv_obj_add_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(prompt_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(prompt_cont, 0, 0);

  lv_obj_align(wifi_label, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_align(password_input, LV_ALIGN_TOP_MID, 0, 40);

  lv_obj_set_size(password_input, LV_PCT(80), LV_SIZE_CONTENT);
  lv_textarea_set_password_mode(password_input, true);
  lv_textarea_set_one_line(password_input, true);

  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(password_input, &WifiPanel::_handle_kb_input, LV_EVENT_FOCUSED, this);
  lv_obj_add_event_cb(password_input, &WifiPanel::_handle_kb_input, LV_EVENT_DEFOCUSED, this);
  lv_obj_add_event_cb(password_input, &WifiPanel::_handle_kb_input, LV_EVENT_READY, this);

  // allow clicks on non-clickables to hide the keyboard
  lv_obj_add_event_cb(prompt_cont, &WifiPanel::_handle_kb_input, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(wifi_label, &WifiPanel::_handle_kb_input, LV_EVENT_CLICKED, this);

  // "Low Latency" control: a single tappable button in the right column, under
  // the connection/IP info, plus a hint line. The button toggles its own ON/OFF
  // label; checked = Low Latency ON, unchecked = stock. When ON it drives a
  // whole bundle (see KUtils::set_wifi_low_latency): WiFi power-save / idle radio
  // sleep / background roam-scans all off, plus Bluetooth stopped to free the
  // shared 2.4GHz radio - for lower, steadier latency.
  lv_obj_clear_flag(pm_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(pm_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_border_width(pm_cont, 0, 0);
  lv_obj_set_style_bg_opa(pm_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(pm_cont, 0, 0);
  lv_obj_set_style_pad_row(pm_cont, 6, 0);
  lv_obj_set_flex_flow(pm_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(pm_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(pm_cont, LV_ALIGN_CENTER, 0, 20);

  lv_obj_add_flag(pm_btn, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_width(pm_btn, 210);
  lv_obj_set_height(pm_btn, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_ver(pm_btn, 10, 0);
  lv_label_set_text(pm_label, "Low Latency: OFF");
  lv_obj_set_style_text_font(pm_label, &lv_font_montserrat_16, 0);
  lv_obj_center(pm_label);

  lv_label_set_text(pm_hint, "on = steadier WiFi\n(no power-save / roam / BT)");
  lv_obj_set_style_text_font(pm_hint, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(pm_hint, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
  // Match the button width and left-align both lines so they sit flush under
  // the button's left edge (cleaner than centered).
  lv_obj_set_width(pm_hint, 210);
  lv_obj_set_style_text_align(pm_hint, LV_TEXT_ALIGN_LEFT, 0);

  {
    auto v = Config::get_instance()->get_json("/wifi_low_latency");
    if (!v.is_null() && v.template get<bool>()) {
      lv_obj_add_state(pm_btn, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(pm_btn, LV_STATE_CHECKED);
    }
  }
  refresh_pm_label();
  lv_obj_add_event_cb(pm_btn, &WifiPanel::_handle_pm_toggle, LV_EVENT_VALUE_CHANGED, this);

  lv_obj_move_background(cont);
  lv_obj_move_foreground(spinner);

  wpa_event.register_callback("WifiPanel",
    [this](const std::string &event) { this->handle_wpa_event(event); });

  wpa_event.start();
}

WifiPanel::~WifiPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void WifiPanel::foreground() {
  spdlog::trace("wifi panel fg");
  lv_obj_move_foreground(cont);
  lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  std::string resp = wpa_event.send_command("SCAN");
  if (resp.empty() || resp.find("FAIL") != std::string::npos) {
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(wifi_label, "Failed to start wifi scan.\nIs wpa_supplicant running?");
    lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
  }
}

void WifiPanel::handle_back_btn(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED) {
    spdlog::trace("wifi panel bg");
    lv_obj_add_flag(wifi_table, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(cont);
  }
}

void WifiPanel::refresh_pm_label() {
  bool low_latency = lv_obj_has_state(pm_btn, LV_STATE_CHECKED);
  lv_label_set_text(pm_label, low_latency ? "Low Latency: ON"
                                          : "Low Latency: OFF");
}

void WifiPanel::handle_pm_toggle(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
    return;
  }
  bool low_latency = lv_obj_has_state(pm_btn, LV_STATE_CHECKED);
  KUtils::set_wifi_low_latency(low_latency);
  Config *conf = Config::get_instance();
  conf->set<bool>("/wifi_low_latency", low_latency);
  conf->save();
  refresh_pm_label();
  spdlog::debug("wifi low-latency toggled: {}", low_latency);
}

void WifiPanel::handle_callback(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_VALUE_CHANGED) {
    uint16_t row;
    uint16_t col;

    lv_table_get_selected_cell(wifi_table, &row, &col);
    if (row > list_networks.size() || col > 1) {
      return;
    }

    selected_network = lv_table_get_cell_value(wifi_table, row, 0);
    if (col == 1 && list_networks.count(selected_network)) {
      auto nid = list_networks.find(selected_network)->second;
      wpa_event.send_command(fmt::format("REMOVE_NETWORK {}", nid));
      wpa_event.send_command("SAVE_CONFIG");
      cur_network.clear();
      selected_network.clear();
      lv_obj_add_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(wifi_table, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(wifi_label, "Scanning for networks...");
      wpa_event.send_command("SCAN");
      return;
    }

    if (cur_network.length() > 0 && cur_network == selected_network) {
      auto ip = KUtils::interface_ip(KUtils::get_wifi_interface());
      lv_label_set_text(wifi_label, fmt::format("Connected to network {}\nIP: {}",
        selected_network,
        ip).c_str());
      lv_obj_add_flag(password_input, LV_OBJ_FLAG_HIDDEN);

      if (col == 1) {
        wpa_event.send_command("DISCONNECT");
        cur_network.clear();
        selected_network.clear();
        lv_obj_add_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(wifi_table, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(wifi_label, "Scanning for networks...");
        wpa_event.send_command("SCAN");
      }

    } else if (list_networks.count(selected_network)) {
      auto nid = list_networks.find(selected_network)->second;
      lv_label_set_text(wifi_label, fmt::format("Connecting to {} ...", selected_network).c_str());
      lv_obj_add_flag(password_input, LV_OBJ_FLAG_HIDDEN);
      wpa_event.send_command(fmt::format("SELECT_NETWORK {}", nid));
      wpa_event.send_command("SAVE_CONFIG");
    } else {
      entering_password = true;
      lv_label_set_text(wifi_label, fmt::format("Enter password for {}", selected_network).c_str());
      lv_obj_clear_flag(password_input, LV_OBJ_FLAG_HIDDEN);
      lv_event_send(password_input, LV_EVENT_FOCUSED, NULL);
    }

    lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
  }
}

void WifiPanel::handle_wpa_event(const std::string &event) {
  if (entering_password) {
    return;
  }
  if (event.rfind("<3>CTRL-EVENT-SCAN-RESULTS", 0) == 0) {
    // result ready
    spdlog::trace("got scan result event");
    std::istringstream f(wpa_event.send_command("SCAN_RESULTS"));
    std::string line;
    wifi_name_db.clear();
    uint32_t index = 0;

    bool found = find_current_network();
    spdlog::trace("cur_network {}", cur_network);

    std::lock_guard<std::mutex> lock(lv_lock);
    if (!found) {
      lv_label_set_text(wifi_label, "Please select your wifi network");
    }
    while (std::getline(f, line)) {
      if (line.rfind("bss", 0) == 0) {
        continue;
      }

      auto wifi_parts = KUtils::split(line, '\t');
      spdlog::trace("wifi parts {}", fmt::join(wifi_parts, ", "));
      if (wifi_parts.size() == 5) {
        auto inserted = wifi_name_db.insert({wifi_parts[4], std::stoi(wifi_parts[2])});
        if (inserted.second) {
          lv_table_set_cell_value(wifi_table, index, 0, wifi_parts[4].c_str());
          if (cur_network == wifi_parts[4]) {
            spdlog::trace("adding symbol with ok");
            lv_table_set_cell_value(wifi_table, index, 1, LV_SYMBOL_CLOSE);
            lv_table_set_cell_value(wifi_table, index, 2, LV_SYMBOL_WIFI);
            auto ip = KUtils::interface_ip(KUtils::get_wifi_interface());
            lv_label_set_text(wifi_label, fmt::format("Connected to network {}\nIP: {}",
              cur_network,
              ip).c_str());
            lv_obj_add_flag(password_input, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
          } else if (list_networks.count(wifi_parts[4])) {
            lv_table_set_cell_value(wifi_table, index, 1, LV_SYMBOL_CLOSE);
            lv_table_set_cell_value(wifi_table, index, 2, LV_SYMBOL_WIFI);
          } else {
            spdlog::trace("adding symbol");
            lv_table_set_cell_value(wifi_table, index, 1, "");
            lv_table_set_cell_value(wifi_table, index, 2, LV_SYMBOL_WIFI);
          }

          index++;
        }
      }
    }
    lv_obj_scroll_to_y(wifi_table, 0, LV_ANIM_OFF);
    lv_obj_clear_flag(wifi_table, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  } else if (event.rfind("<3>CTRL-EVENT-CONNECTED", 0) == 0) {
    // The driver resets the WiFi low-latency knobs (PM/mpc/roam_off) to their
    // defaults on every link-up (reconnect or network switch), silently
    // reverting them. Re-apply the whole bundle here so the toggle stays
    // truthful. Runs on the wpa monitor thread (no LVGL).
    auto pm = Config::get_instance()->get_json("/wifi_low_latency");
    if (!pm.is_null() && pm.template get<bool>()) {
      KUtils::set_wifi_low_latency(true);
    }

    if (find_current_network()) {
      spdlog::trace("cur_network {}", cur_network);
      std::vector<std::pair<std::string, int>> pairs;
      for (auto it = wifi_name_db.begin(); it != wifi_name_db.end(); ++it) {
        pairs.push_back(*it);
      }

      std::sort(pairs.begin(), pairs.end(), [=](std::pair<std::string, int> &a,
        std::pair<std::string, int> &b)
        {
          return a.second > b.second;
        });

      std::lock_guard<std::mutex> lock(lv_lock);

      uint32_t index = 0;
      for (const auto &wifi : pairs) {
        lv_table_set_cell_value(wifi_table, index, 0, wifi.first.c_str());
        if (cur_network == wifi.first) {
          spdlog::trace("adding symbol with ok");
          lv_table_set_cell_value(wifi_table, index, 1, LV_SYMBOL_CLOSE);
          lv_table_set_cell_value(wifi_table, index, 2, LV_SYMBOL_WIFI);
          auto ip = KUtils::interface_ip(KUtils::get_wifi_interface());
          lv_label_set_text(wifi_label, fmt::format("Connected to network {}\nIP: {}",
            cur_network,
            ip).c_str());
          lv_obj_add_flag(password_input, LV_OBJ_FLAG_HIDDEN);
          lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
        } else if (list_networks.count(wifi.first)) {
          lv_table_set_cell_value(wifi_table, index, 1, LV_SYMBOL_CLOSE);
          lv_table_set_cell_value(wifi_table, index, 2, LV_SYMBOL_WIFI);
        } else {
          spdlog::trace("adding symbol");
          lv_table_set_cell_value(wifi_table, index, 1, "");
          lv_table_set_cell_value(wifi_table, index, 2, LV_SYMBOL_WIFI);
        }
        index++;
      }

      lv_obj_scroll_to_y(wifi_table, 0, LV_ANIM_OFF);
      lv_obj_clear_flag(wifi_table, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void WifiPanel::handle_kb_input(lv_event_t *e)
{
  const lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(kb, password_input);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_DEFOCUSED) {
    entering_password = false;
    lv_keyboard_set_textarea(kb, NULL);
    lv_label_set_text(wifi_label, "Please select your wifi network");
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(password_input, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_READY) {
    const char *password = lv_textarea_get_text(password_input);
    if (password == NULL || password[0] == 0) {
      return;
    }

    entering_password = false;
    // add network, set password, save wpa
    connect(password);
    lv_textarea_set_text(password_input, "");
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(wifi_label, fmt::format("Connecting to {} ...", selected_network).c_str());
    lv_obj_clear_state(password_input, LV_STATE_FOCUSED);
    lv_obj_add_flag(password_input, LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_CLICKED) {
    lv_obj_t *target = lv_event_get_target(e);
    if (target != kb && target != password_input) {
      lv_event_send(password_input, LV_EVENT_DEFOCUSED, NULL);
    }
  }
}

void WifiPanel::connect(const char *password) {
  std::string nid = wpa_event.send_command("ADD_NETWORK");
  spdlog::trace("add_nework {}", nid);
  if (nid.length() > 0) {
    wpa_event.send_command(fmt::format("SET_NETWORK {} ssid {:?}", nid, selected_network));
    wpa_event.send_command(fmt::format("SET_NETWORK {} psk {:?}", nid, password));
    wpa_event.send_command(fmt::format("ENABLE_NETWORK {}", nid));
    wpa_event.send_command(fmt::format("SELECT_NETWORK {}", nid));
    wpa_event.send_command("SAVE_CONFIG");
  }
}

bool WifiPanel::find_current_network() {
  list_networks.clear();
  std::string nets = wpa_event.send_command("LIST_NETWORKS");
  spdlog::trace("nets = {}", nets);
  std::istringstream f(nets);
  std::string line;
  bool found = false;
  while (std::getline(f, line)) {
    auto wifi_parts = KUtils::split(line, '\t');
    if (wifi_parts.size() == 4 && line.find("[CURRENT]") != std::string::npos) {
      cur_network = wifi_parts[1];
      list_networks.insert({wifi_parts[1], wifi_parts[0]});
      found = true;
    }

    if (wifi_parts.size() > 1) {
      list_networks.insert({wifi_parts[1], wifi_parts[0]});
    }
  }

  return found;
}
