#include "wifi_panel.h"
#include "utils.h"
#include "config.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <sstream>
#include <iostream>
#include <vector>
#include <utility>
#include <thread>
#include <chrono>

LV_IMG_DECLARE(back);

WifiPanel::WifiPanel(std::mutex &l)
  : lv_lock(l)
  , cont(lv_obj_create(lv_scr_act()))
  , spinner(lv_spinner_create(cont, 1000, 60))
  , top_cont(lv_obj_create(cont))
  , wifi_list_cont(lv_obj_create(top_cont))
  , wifi_right(lv_obj_create(top_cont))
  , prompt_cont(wifi_right)
  , wifi_label(lv_label_create(prompt_cont))
  , pw_row(lv_obj_create(prompt_cont))
  , password_input(lv_textarea_create(pw_row))
  , eye_btn(lv_btn_create(pw_row))
  , back_btn(cont, &back, "Back", &WifiPanel::_handle_back_btn, this)
  , kb(lv_keyboard_create(cont))
  , pm_cont(lv_obj_create(wifi_right))
  , pm_btn(lv_btn_create(pm_cont))
  , pm_label(lv_label_create(pm_btn))
  , pm_hint(lv_label_create(pm_cont))
  , static_ip_panel(l)
  , network_detail_panel(l,
      [this]() { open_static_ip_for_current(); },
      [this]() { forget_current_network(); })
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

  // The list gets more than half the row now that the right-hand panel no
  // longer needs to reserve space for a screen-dominating Low Latency pill -
  // see pm_cont below.
  lv_obj_set_flex_grow(wifi_list_cont, 3);
  lv_obj_set_height(wifi_list_cont, LV_PCT(90));
  lv_obj_set_flex_flow(wifi_list_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(wifi_list_cont, 0, 0);
  lv_obj_set_style_border_width(wifi_list_cont, 0, 0);
  lv_obj_set_scroll_dir(wifi_list_cont, LV_DIR_VER);
  lv_obj_add_flag(wifi_list_cont, LV_OBJ_FLAG_HIDDEN);

  lv_obj_set_style_border_width(wifi_right, 0, 0);
  lv_obj_set_flex_grow(wifi_right, 2);
  lv_obj_add_flag(wifi_right, LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_CLICKABLE);

  lv_obj_add_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(prompt_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(prompt_cont, 0, 0);

  lv_obj_set_width(wifi_label, LV_PCT(92));
  lv_label_set_long_mode(wifi_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(wifi_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(wifi_label, LV_ALIGN_TOP_MID, 0, 10);

  // pw_row: flex row holding the password textarea and the eye toggle button
  lv_obj_set_flex_flow(pw_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pw_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_width(pw_row, LV_PCT(92));
  lv_obj_set_height(pw_row, LV_SIZE_CONTENT);
  lv_obj_set_style_border_width(pw_row, 0, 0);
  lv_obj_set_style_bg_opa(pw_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(pw_row, 0, 0);
  lv_obj_set_style_pad_column(pw_row, 4, 0);
  lv_obj_align(pw_row, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_add_flag(pw_row, LV_OBJ_FLAG_HIDDEN);

  lv_obj_set_flex_grow(password_input, 1);
  lv_obj_set_height(password_input, LV_SIZE_CONTENT);
  lv_textarea_set_password_mode(password_input, true);
  lv_textarea_set_one_line(password_input, true);

  lv_obj_set_size(eye_btn, 36, 36);
  lv_obj_set_style_pad_all(eye_btn, 0, 0);
  {
    lv_obj_t *lbl = lv_label_create(eye_btn);
    lv_label_set_text(lbl, LV_SYMBOL_EYE_CLOSE);
    lv_obj_center(lbl);
  }
  lv_obj_add_event_cb(eye_btn, &WifiPanel::_handle_eye_btn, LV_EVENT_CLICKED, this);

  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(password_input, &WifiPanel::_handle_kb_input, LV_EVENT_FOCUSED, this);
  lv_obj_add_event_cb(password_input, &WifiPanel::_handle_kb_input, LV_EVENT_DEFOCUSED, this);
  lv_obj_add_event_cb(password_input, &WifiPanel::_handle_kb_input, LV_EVENT_READY, this);
  lv_obj_add_event_cb(password_input, &WifiPanel::_handle_kb_input, LV_EVENT_CANCEL, this);

  // allow clicks on non-clickables to hide the keyboard
  lv_obj_add_event_cb(prompt_cont, &WifiPanel::_handle_kb_input, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(wifi_label, &WifiPanel::_handle_kb_input, LV_EVENT_CLICKED, this);

  // "Low Latency" control: a compact link-style row, not the screen-dominant
  // pill this used to be - it's an advanced, set-once radio-tuning setting,
  // not part of the everyday connect flow, so it no longer competes with the
  // connection status for the right panel's primary visual weight. Same
  // underlying toggle (pm_btn, CHECKABLE) and handler as before.
  lv_obj_add_flag(pm_cont, LV_OBJ_FLAG_FLOATING);
  lv_obj_clear_flag(pm_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(pm_cont, LV_PCT(92), LV_SIZE_CONTENT);
  lv_obj_set_style_border_width(pm_cont, 0, 0);
  lv_obj_set_style_bg_opa(pm_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(pm_cont, 0, 0);
  lv_obj_set_style_pad_row(pm_cont, 2, 0);
  lv_obj_set_flex_flow(pm_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(pm_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  // Anchored to the TOP of the panel, not CENTER/BOTTOM - the floating Back
  // button is a full 110px-wide box (button_container.cpp) sitting in the
  // bottom-right corner, and both earlier placements (BOTTOM_MID, then a
  // CENTER+45 guess) still landed pm_cont's ~177px-wide box squarely across
  // Back's actual x/y footprint - confirmed by computing the real
  // coordinates, not by eyeballing a screenshot a second time. Anchoring to
  // the top, right under the (max 3-line) status text, keeps this entirely
  // above Back's top edge regardless of wifi_right's exact height.
  lv_obj_align(pm_cont, LV_ALIGN_TOP_MID, 0, 90);

  lv_obj_add_flag(pm_btn, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_size(pm_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  // Both states need the transparent/no-border override - LV_STATE_CHECKED
  // otherwise falls back to the theme's default checked-button styling
  // (a solid accent-colored pill), which is exactly the screen-dominating
  // look this control is being demoted away from.
  for (lv_state_t state : {LV_STATE_DEFAULT, LV_STATE_CHECKED}) {
    lv_obj_set_style_bg_opa(pm_btn, LV_OPA_TRANSP, LV_PART_MAIN | state);
    lv_obj_set_style_shadow_width(pm_btn, 0, LV_PART_MAIN | state);
    lv_obj_set_style_border_width(pm_btn, 0, LV_PART_MAIN | state);
  }
  lv_obj_set_style_pad_all(pm_btn, 2, 0);
  lv_label_set_text(pm_label, "Low Latency: OFF");
  lv_obj_set_style_text_font(pm_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_decor(pm_label, LV_TEXT_DECOR_UNDERLINE, 0);
  lv_obj_center(pm_label);

  lv_obj_set_width(pm_hint, LV_PCT(100));
  lv_label_set_long_mode(pm_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(pm_hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(pm_hint, "steadier WiFi - no power-save/roam/BT");
  lv_obj_set_style_text_font(pm_hint, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(pm_hint, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);

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
  rescan_budget = 4;
  last_scan_count = 0;
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
    lv_obj_add_flag(wifi_list_cont, LV_OBJ_FLAG_HIDDEN);
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

void WifiPanel::rebuild_wifi_rows() {
  wifi_rows.clear();
  lv_obj_clean(wifi_list_cont);

  std::vector<std::pair<std::string, WifiEntry>> known, other;
  for (const auto &kv : wifi_name_db) {
    if (list_networks.count(kv.first)) {
      known.push_back(kv);
    } else {
      other.push_back(kv);
    }
  }
  auto by_signal_desc = [](const std::pair<std::string, WifiEntry> &a,
                            const std::pair<std::string, WifiEntry> &b) {
    return a.second.signal > b.second.signal;
  };
  std::sort(known.begin(), known.end(), by_signal_desc);
  std::sort(other.begin(), other.end(), by_signal_desc);

  auto make_group_label = [&](const char *text) {
    lv_obj_t *l = lv_label_create(wifi_list_cont);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(l, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_obj_set_style_pad_top(l, 8, 0);
    lv_obj_set_style_pad_bottom(l, 2, 0);
    lv_obj_set_style_pad_left(l, 8, 0);
  };

  if (!known.empty()) {
    make_group_label("KNOWN NETWORKS");
    for (auto &kv : known) {
      bool connected = (kv.first == cur_network);
      auto nid_it = list_networks.find(kv.first);
      std::string nid = nid_it != list_networks.end() ? nid_it->second : "";
      wifi_rows.push_back(std::make_unique<WifiRowItem>(
        wifi_list_cont, kv.first, kv.second.signal, connected, /*saved=*/true, nid,
        [this](const std::string &ssid) { handle_row_activated(ssid); },
        [this](const std::string &ssid, const std::string &nid) { confirm_remove_network(ssid, nid); }));
    }
  }
  if (!other.empty()) {
    make_group_label("OTHER NETWORKS IN RANGE");
    for (auto &kv : other) {
      wifi_rows.push_back(std::make_unique<WifiRowItem>(
        wifi_list_cont, kv.first, kv.second.signal, /*connected=*/false, /*saved=*/false, "",
        [this](const std::string &ssid) { handle_row_activated(ssid); },
        [this](const std::string &ssid, const std::string &nid) { confirm_remove_network(ssid, nid); }));
    }
  }
}

void WifiPanel::handle_row_activated(const std::string &ssid) {
  if (!cur_network.empty() && ssid == cur_network) {
    auto it = wifi_name_db.find(ssid);
    int signal = it != wifi_name_db.end() ? it->second.signal : -100;
    bool secured = it != wifi_name_db.end() ? it->second.secured : true;
    std::string signal_text = signal >= -50 ? "Excellent" : signal >= -60 ? "Good"
                             : signal >= -70 ? "Fair" : "Weak";
    std::string security_text = secured ? "Secured (WPA/WPA2)" : "Open (unsecured)";
    auto ip = KUtils::interface_ip(KUtils::get_wifi_interface());
    network_detail_panel.foreground(ssid, signal_text, security_text, ip);
    return;
  }

  selected_network = ssid;
  if (list_networks.count(ssid)) {
    auto nid = list_networks.find(ssid)->second;
    lv_label_set_text(wifi_label, fmt::format("Connecting to {} ...", ssid).c_str());
    lv_obj_add_flag(pw_row, LV_OBJ_FLAG_HIDDEN);
    wpa_event.send_command(fmt::format("SELECT_NETWORK {}", nid));
    wpa_event.send_command("SAVE_CONFIG");
    wpa_event.send_command("REASSOCIATE");
  } else {
    entering_password = true;
    lv_label_set_text(wifi_label, fmt::format("Enter password for {}", ssid).c_str());
    lv_obj_clear_flag(pw_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pm_cont, LV_OBJ_FLAG_HIDDEN);
    lv_event_send(password_input, LV_EVENT_FOCUSED, NULL);
  }
  lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
}

void WifiPanel::open_static_ip_for_current() {
  if (cur_network.empty()) return;
  static_ip_panel.foreground(cur_network);
}

void WifiPanel::forget_current_network() {
  if (cur_network.empty()) return;
  auto it = list_networks.find(cur_network);
  if (it == list_networks.end()) return;
  confirm_remove_network(cur_network, it->second);
}

void WifiPanel::confirm_remove_network(const std::string &ssid, const std::string &nid) {
  static const char *btns[] = {"Remove", "Cancel", ""};
  std::string msg = fmt::format("Forget network \"{}\"?\nThis deletes its saved password.", ssid);
  lv_obj_t *mbox = lv_msgbox_create(NULL, NULL, msg.c_str(), btns, false);
  lv_obj_add_event_cb(mbox, [](lv_event_t *ev) {
    lv_obj_t *obj = lv_obj_get_parent(lv_event_get_target(ev));
    auto *self = static_cast<WifiPanel *>(lv_event_get_user_data(ev));
    bool confirmed = lv_msgbox_get_active_btn(obj) == 0;
    std::string nid = self->pending_remove_nid;
    self->pending_remove_nid.clear();
    lv_msgbox_close(obj);
    if (!confirmed) return;

    self->wpa_event.send_command(fmt::format("REMOVE_NETWORK {}", nid));
    self->wpa_event.send_command("SAVE_CONFIG");
    self->cur_network.clear();
    self->selected_network.clear();
    self->network_detail_panel.hide();
    lv_obj_add_flag(self->prompt_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(self->spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(self->wifi_list_cont, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(self->wifi_label, "Scanning for networks...");
    self->wpa_event.send_command("SCAN");
  }, LV_EVENT_VALUE_CHANGED, this);
  pending_remove_nid = nid;
  KUtils::style_lock_mbox(mbox, 90);
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
    // Networks actually reported by THIS scan - merged into wifi_name_db
    // below rather than replacing it outright, and anything missing gets
    // aged out gradually instead of vanishing after a single noisy scan.
    std::map<std::string, WifiEntry> seen_this_scan;

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
        WifiEntry entry;
        entry.signal = std::stoi(wifi_parts[2]);
        entry.secured = wifi_parts[3].find("WPA") != std::string::npos
                      || wifi_parts[3].find("WEP") != std::string::npos;
        seen_this_scan[wifi_parts[4]] = entry;
      }
    }

    for (const auto &kv : seen_this_scan) {
      auto &e = wifi_name_db[kv.first];
      e.signal = kv.second.signal;
      e.secured = kv.second.secured;
      e.missed = 0;
    }
    static constexpr int MAX_MISSED_SCANS = 3;
    for (auto it = wifi_name_db.begin(); it != wifi_name_db.end();) {
      if (seen_this_scan.count(it->first)) {
        ++it;
        continue;
      }
      if (++it->second.missed > MAX_MISSED_SCANS) {
        it = wifi_name_db.erase(it);
      } else {
        ++it;
      }
    }

    if (!cur_network.empty()) {
      auto ip = KUtils::interface_ip(KUtils::get_wifi_interface());
      lv_label_set_text(wifi_label, fmt::format("Connected to network {}\nIP: {}",
        cur_network, ip).c_str());
      lv_obj_add_flag(pw_row, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);
    }

    rebuild_wifi_rows();
    lv_obj_scroll_to_y(wifi_list_cont, 0, LV_ANIM_OFF);
    lv_obj_clear_flag(wifi_list_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);

    size_t current_count = wifi_name_db.size();
    bool grew = current_count > last_scan_count;
    last_scan_count = current_count;
    if (grew && rescan_budget > 0) {
      rescan_budget--;
      wpa_event.send_command("SCAN");
    }
  } else if (event.rfind("<3>CTRL-EVENT-CONNECTED", 0) == 0) {
    // The driver resets the WiFi low-latency knobs (PM/mpc/roam_off) to their
    // defaults on every link-up (reconnect or network switch), silently
    // reverting them. Re-apply the whole bundle here so the toggle stays
    // truthful. Runs on the wpa monitor thread (no LVGL).
    auto pm = Config::get_instance()->get_json("/wifi_low_latency");
    if (!pm.is_null() && pm.template get<bool>()) {
      // BCM4343 firmware resets PM to its NVRAM default after finalising
      // association, which happens slightly after CONNECTED fires.  A short
      // delay ensures our PM=0 lands after that reset.
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      KUtils::set_wifi_low_latency(true);
    }

    if (find_current_network()) {
      spdlog::trace("cur_network {}", cur_network);
      std::lock_guard<std::mutex> lock(lv_lock);

      lv_label_set_text(wifi_label,
        fmt::format("Connected to {}\nGetting IP...", cur_network).c_str());
      lv_obj_add_flag(pw_row, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(prompt_cont, LV_OBJ_FLAG_HIDDEN);

      rebuild_wifi_rows();
      lv_obj_scroll_to_y(wifi_list_cont, 0, LV_ANIM_OFF);
      lv_obj_clear_flag(wifi_list_cont, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);

      auto iface = KUtils::get_wifi_interface();
      auto net = cur_network;
      uint32_t gen = ++conn_gen;
      std::thread([this, iface, net, gen]() {
        wait_for_connectivity(iface, net, gen);
      }).detach();
    }
  }
}

void WifiPanel::handle_kb_input(lv_event_t *e)
{
  const lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(kb, password_input);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    // The keyboard covers most of the screen's width - hiding the (not
    // interactable while typing anyway) network list avoids the hard,
    // no-scroll clip through a row that a full-width keyboard otherwise
    // produces (confirmed live 2026-07-12), and hiding Back mirrors the
    // same pattern used for the octet keyboard in StaticIpPanel.
    lv_obj_add_flag(wifi_list_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(back_btn.get_container(), LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_CANCEL) {
    entering_password = false;
    lv_keyboard_set_textarea(kb, NULL);
    lv_label_set_text(wifi_label, "Please select your wifi network");
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pw_row, LV_OBJ_FLAG_HIDDEN);
    pw_visible = false;
    lv_textarea_set_password_mode(password_input, true);
    lv_label_set_text(lv_obj_get_child(eye_btn, 0), LV_SYMBOL_EYE_CLOSE);
    lv_obj_clear_flag(pm_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifi_list_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(back_btn.get_container(), LV_OBJ_FLAG_HIDDEN);
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
    lv_obj_add_flag(pw_row, LV_OBJ_FLAG_HIDDEN);
    pw_visible = false;
    lv_textarea_set_password_mode(password_input, true);
    lv_label_set_text(lv_obj_get_child(eye_btn, 0), LV_SYMBOL_EYE_CLOSE);
    lv_obj_clear_flag(pm_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifi_list_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(back_btn.get_container(), LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_CLICKED) {
    lv_obj_t *target = lv_event_get_target(e);
    if (target != kb && target != password_input && target != eye_btn && target != pw_row) {
      lv_event_send(password_input, LV_EVENT_DEFOCUSED, NULL);
    }
  }
}

void WifiPanel::wait_for_connectivity(const std::string &iface,
                                       const std::string &net,
                                       uint32_t gen) {
  std::string gw;
  // Poll up to 10s (40 × 250ms): first wait for the default route to appear
  // (signals DHCP completed), then for the gateway ARP entry to complete
  // (signals layer-3 traffic can actually flow).
  for (int i = 0; i < 40; i++) {
    if (conn_gen.load() != gen) return;
    if (gw.empty())
      gw = KUtils::gateway_ip(iface);
    if (!gw.empty() && KUtils::gateway_in_arp(gw))
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  if (conn_gen.load() != gen || entering_password) return;
  auto ip = KUtils::interface_ip(iface);
  std::lock_guard<std::mutex> lock(lv_lock);
  lv_label_set_text(wifi_label,
    fmt::format("Connected to {}\nIP: {}", net, ip).c_str());
}

void WifiPanel::handle_eye_btn(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_event_stop_bubbling(e);
  pw_visible = !pw_visible;
  lv_textarea_set_password_mode(password_input, !pw_visible);
  lv_label_set_text(lv_obj_get_child(eye_btn, 0),
                    pw_visible ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
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
