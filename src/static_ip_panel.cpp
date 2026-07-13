#include "static_ip_panel.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <arpa/inet.h>
#include <thread>

LV_IMG_DECLARE(back);

namespace {

const char *STATIC_IP_SCRIPT = "/usr/data/printer_data/config/GuppyScreen/scripts/static_ip.py";

std::string shell_quote(const std::string &s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  out += "'";
  return out;
}

bool parse_ipv4(const std::string &s, uint32_t &out) {
  struct in_addr addr;
  if (inet_pton(AF_INET, s.c_str(), &addr) != 1) return false;
  out = ntohl(addr.s_addr);
  return true;
}

// Mirrors static_ip.py's validate_entry() - keep both in sync if either changes.
std::string validate_static_entry(const std::string &ip_s,
                                   const std::string &netmask_s,
                                   const std::string &gateway_s) {
  uint32_t ip, netmask, gateway;
  if (!parse_ipv4(ip_s, ip)) return "Invalid IP address";
  if (!parse_ipv4(netmask_s, netmask)) return "Invalid netmask";
  uint32_t inv = ~netmask;
  if ((inv & (inv + 1)) != 0) return "Netmask is not a valid contiguous mask";
  if (!parse_ipv4(gateway_s, gateway)) return "Invalid gateway address";

  uint32_t network = ip & netmask;
  uint32_t broadcast = network | inv;

  if ((gateway & netmask) != network) return fmt::format("Gateway {} is not within the {} subnet", gateway_s, netmask_s);
  if (ip == gateway) return fmt::format("{} is the gateway address", ip_s);
  if (ip == network) return fmt::format("{} is the network address", ip_s);
  if (ip == broadcast) return fmt::format("{} is the broadcast address", ip_s);
  return "";
}

std::string json_str(const json &j, const char *key) {
  if (!j.contains(key) || j[key].is_null()) return "";
  return j[key].template get<std::string>();
}

std::vector<std::string> split_dots(const std::string &s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == '.') { out.push_back(cur); cur.clear(); }
    else cur += c;
  }
  out.push_back(cur);
  return out;
}

// One address = a label + 4 small octet boxes + 3 dot separators, sharing a
// digit-only keypad. Replaces a single free-text field per address - fewer
// characters to type on a shared numeric pad, and each box can be flagged
// invalid individually (see mark_octet_error) instead of only via one status
// line shared by all four fields.
void make_octet_row(lv_obj_t *parent, const char *label_text, lv_obj_t *out[4]) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(row, 1, 0);
  lv_obj_set_style_pad_column(row, 3, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *lbl = lv_label_create(row);
  lv_label_set_text(lbl, label_text);
  lv_obj_set_width(lbl, 60);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);

  for (int i = 0; i < 4; i++) {
    if (i > 0) {
      lv_obj_t *dot = lv_label_create(row);
      lv_label_set_text(dot, ".");
      lv_obj_set_style_text_font(dot, &lv_font_montserrat_12, 0);
    }
    lv_obj_t *ta = lv_textarea_create(row);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 3);
    lv_obj_set_width(ta, 34);
    lv_obj_set_height(ta, 26);
    // montserrat_12 doesn't actually fit inside a 26px-tall box - each
    // textarea was internally clipping/scrolling its own text, and which
    // one visibly showed it depended on focus history, which looked like
    // random misalignment between boxes (confirmed live 2026-07-12). Fixing
    // the font to match the box instead of regrowing the box, which would
    // reopen the panel-level scrollbar this height was chosen to avoid.
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_align(ta, LV_TEXT_ALIGN_CENTER, 0);
    out[i] = ta;
  }
}

std::string join_octets(lv_obj_t *const octets[4]) {
  std::string out;
  for (int i = 0; i < 4; i++) {
    if (i) out += '.';
    const char *t = lv_textarea_get_text(octets[i]);
    out += (t && t[0]) ? t : "";
  }
  return out;
}

void set_octets(lv_obj_t *const octets[4], const std::string &dotted) {
  auto parts = split_dots(dotted);
  for (int i = 0; i < 4; i++) {
    lv_textarea_set_text(octets[i], (size_t)i < parts.size() ? parts[i].c_str() : "");
  }
}

void clear_octet_error(lv_obj_t *ta) {
  lv_obj_set_style_border_color(ta, lv_theme_get_color_primary(ta), LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(ta, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
}

void mark_octet_error(lv_obj_t *ta) {
  lv_obj_set_style_border_width(ta, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(ta, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
}

// Returns true if every box in the group holds a valid 0-255 integer;
// invalid boxes are marked red in place via mark_octet_error.
bool check_octet_group(lv_obj_t *const group[4]) {
  bool ok = true;
  for (int i = 0; i < 4; i++) {
    const char *t = lv_textarea_get_text(group[i]);
    int v = -1;
    if (t && t[0]) {
      try { v = std::stoi(t); } catch (...) { v = -1; }
    }
    if (v < 0 || v > 255) {
      mark_octet_error(group[i]);
      ok = false;
    }
  }
  return ok;
}

} // namespace

StaticIpPanel::StaticIpPanel(std::mutex &l)
  : lv_lock(l)
  , cont(lv_obj_create(lv_scr_act()))
  , title_label(lv_label_create(cont))
  , mode_cont(lv_obj_create(cont))
  , dhcp_mode_btn(lv_btn_create(mode_cont))
  , manual_mode_btn(lv_btn_create(mode_cont))
  , revert_btn(lv_btn_create(cont))
  , status_label(lv_label_create(cont))
  , body_cont(lv_obj_create(cont))
  , save_btn(lv_btn_create(cont))
  , spinner(lv_spinner_create(cont, 1000, 60))
  , kb(lv_keyboard_create(cont))
  , back_btn(cont, &back, "Back", &StaticIpPanel::_handle_back_btn, this)
{
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(cont, 8, 0);
  // Extra bottom breathing room specifically - body_cont's flex_grow(1)
  // otherwise consumes all slack and Save ends up sitting flush against the
  // screen's bottom edge (confirmed live 2026-07-12).
  lv_obj_set_style_pad_bottom(cont, 16, 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(cont, 4, 0);

  lv_label_set_text(title_label, "Static IP");
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);

  lv_obj_set_size(mode_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_clear_flag(mode_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(mode_cont, 0, 0);
  lv_obj_set_style_bg_opa(mode_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(mode_cont, 0, 0);
  lv_obj_set_style_pad_column(mode_cont, 8, 0);
  lv_obj_set_flex_flow(mode_cont, LV_FLEX_FLOW_ROW);

  lv_obj_set_flex_grow(dhcp_mode_btn, 1);
  lv_obj_set_height(dhcp_mode_btn, 36);
  {
    lv_obj_t *l = lv_label_create(dhcp_mode_btn);
    lv_label_set_text(l, "DHCP");
    lv_obj_center(l);
  }
  lv_obj_add_event_cb(dhcp_mode_btn, &StaticIpPanel::_handle_mode_toggle, LV_EVENT_CLICKED, this);

  lv_obj_set_flex_grow(manual_mode_btn, 1);
  lv_obj_set_height(manual_mode_btn, 36);
  {
    lv_obj_t *l = lv_label_create(manual_mode_btn);
    lv_label_set_text(l, "Manual");
    lv_obj_center(l);
  }
  lv_obj_add_event_cb(manual_mode_btn, &StaticIpPanel::_handle_mode_toggle, LV_EVENT_CLICKED, this);

  // Only shown while a static config is actually active for this SSID -
  // reverting it is now its own explicit action, never a side effect of
  // switching to the DHCP tab just to look at it (that tab is a pure view
  // switch, see handle_mode_toggle).
  lv_obj_set_size(revert_btn, LV_PCT(100), 30);
  lv_obj_set_style_bg_opa(revert_btn, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(revert_btn, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(revert_btn, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_MAIN);
  {
    lv_obj_t *l = lv_label_create(revert_btn);
    lv_label_set_text(l, "Revert to DHCP");
    lv_obj_set_style_text_color(l, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_center(l);
  }
  lv_obj_add_event_cb(revert_btn, &StaticIpPanel::_handle_revert_btn, LV_EVENT_CLICKED, this);
  lv_obj_add_flag(revert_btn, LV_OBJ_FLAG_HIDDEN);

  lv_obj_set_width(body_cont, LV_PCT(100));
  lv_obj_set_flex_grow(body_cont, 1);
  lv_obj_clear_flag(body_cont, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_set_scroll_dir(body_cont, LV_DIR_VER);
  // The 4 field rows fit above the fold in the common case - AUTO showed a
  // scrollbar sliver anyway (confirmed live 2026-07-12) because the reserved
  // bottom margin below Save (added for the flush-to-edge fix) trims a few
  // px off body_cont's own height, just enough to trip the auto-threshold
  // even though nothing is actually clipped. Scrolling still works via
  // touch-drag as a safety net for a future longer error message - only the
  // visual indicator is off.
  lv_obj_set_scrollbar_mode(body_cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_border_width(body_cont, 0, 0);
  lv_obj_set_style_bg_opa(body_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(body_cont, 0, 0);
  lv_obj_set_style_pad_row(body_cont, 4, 0);
  lv_obj_set_flex_flow(body_cont, LV_FLEX_FLOW_COLUMN);

  lv_obj_t *fields_cont = lv_obj_create(body_cont);
  lv_obj_set_size(fields_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_clear_flag(fields_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(fields_cont, 0, 0);
  lv_obj_set_style_bg_opa(fields_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(fields_cont, 0, 0);
  lv_obj_set_style_pad_row(fields_cont, 2, 0);
  lv_obj_set_flex_flow(fields_cont, LV_FLEX_FLOW_COLUMN);

  make_octet_row(fields_cont, "IP", ip_octets);
  make_octet_row(fields_cont, "Netmask", netmask_octets);
  make_octet_row(fields_cont, "Gateway", gateway_octets);
  make_octet_row(fields_cont, "DNS", dns_octets);

  for (lv_obj_t *const *group : {ip_octets, netmask_octets, gateway_octets, dns_octets}) {
    for (int i = 0; i < 4; i++) {
      lv_obj_add_event_cb(group[i], &StaticIpPanel::_handle_kb_input, LV_EVENT_FOCUSED, this);
      lv_obj_add_event_cb(group[i], &StaticIpPanel::_handle_kb_input, LV_EVENT_DEFOCUSED, this);
      lv_obj_add_event_cb(group[i], &StaticIpPanel::_handle_kb_input, LV_EVENT_READY, this);
      lv_obj_add_event_cb(group[i], &StaticIpPanel::_handle_kb_input, LV_EVENT_CANCEL, this);
    }
  }

  lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(status_label, LV_PCT(100));
  lv_label_set_text(status_label, "");
  lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);

  lv_obj_set_size(save_btn, 140, 36);
  {
    lv_obj_t *l = lv_label_create(save_btn);
    lv_label_set_text(l, LV_SYMBOL_OK "  Save");
    lv_obj_center(l);
  }
  lv_obj_add_event_cb(save_btn, &StaticIpPanel::_handle_save_btn, LV_EVENT_CLICKED, this);

  lv_obj_add_flag(spinner, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(spinner, 50, 50);
  lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 0);

  // Digits + backspace + OK only - no "." key. Each box only ever holds one
  // octet, so there's nothing for a dot key to do here.
  static const char *kb_map[] = {"1", "2", "3", "\n",
                                 "4", "5", "6", "\n",
                                 "7", "8", "9", "\n",
                                 LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, NULL};
  static const lv_btnmatrix_ctrl_t kb_ctrl[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_USER_1, kb_map, kb_ctrl);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_IGNORE_LAYOUT);
  // A full-screen-width keyboard (the original size, copied from the
  // free-text field it used to edit) looked wildly oversized for typing 1-3
  // digits into a 34px octet box - confirmed live 2026-07-12. Sized to match
  // what it actually edits instead.
  lv_obj_set_size(kb, 220, 150);
  // Anchored to the right side, not the bottom - the field rows are a
  // ~220px-wide column starting at the screen's left edge, leaving a wide
  // unused strip to their right. A bottom-anchored keyboard sat directly
  // under whichever row was focused, hiding it while typing (confirmed live
  // 2026-07-12, worst for Gateway/DNS since they're lower in the column) -
  // anchoring beside the fields instead of below them means the focused box
  // stays visible regardless of which row it is. Offset down from vertical
  // center to clear the title/mode/status header, which spans the full
  // width (including this right-hand strip).
  lv_obj_align(kb, LV_ALIGN_RIGHT_MID, -8, 35);
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

  lv_obj_add_flag(back_btn.get_container(), LV_OBJ_FLAG_FLOATING);
  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, -20);

  lv_obj_move_background(cont);
}

StaticIpPanel::~StaticIpPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

void StaticIpPanel::foreground(const std::string &new_ssid) {
  ssid = new_ssid;
  lv_label_set_text(title_label, fmt::format("Static IP - {}", ssid).c_str());
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(cont);
  refresh_status();
}

void StaticIpPanel::handle_back_btn(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(cont);
}

void StaticIpPanel::set_manual_mode(bool manual) {
  manual_mode = manual;
  // Selected button: clear any local override so it falls back to the
  // theme's own live bg_color_primary (tracks theme changes with no
  // restart). Unselected button: explicit grey override.
  if (manual) {
    lv_obj_remove_local_style_prop(manual_mode_btn, LV_STYLE_BG_COLOR, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dhcp_mode_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  } else {
    lv_obj_remove_local_style_prop(dhcp_mode_btn, LV_STYLE_BG_COLOR, LV_PART_MAIN);
    lv_obj_set_style_bg_color(manual_mode_btn, lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);
  }

  for (lv_obj_t *const *group : {ip_octets, netmask_octets, gateway_octets, dns_octets}) {
    for (int i = 0; i < 4; i++) {
      if (manual) {
        lv_obj_clear_state(group[i], LV_STATE_DISABLED);
      } else {
        lv_obj_add_state(group[i], LV_STATE_DISABLED);
      }
    }
  }

  if (manual) {
    lv_obj_clear_flag(save_btn, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(save_btn, LV_OBJ_FLAG_HIDDEN);
  }
}

void StaticIpPanel::update_revert_visibility() {
  if (has_static_config) {
    lv_obj_clear_flag(revert_btn, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(revert_btn, LV_OBJ_FLAG_HIDDEN);
  }
}

void StaticIpPanel::set_busy(bool b, const std::string &msg) {
  busy = b;
  if (b) {
    lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(status_label, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_label_set_text(status_label, msg.c_str());
  } else {
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
  }
}

void StaticIpPanel::run_backend_async(const std::string &args, const std::string &busy_msg,
                                       const std::function<void(int, const std::string &)> &on_done) {
  // No lock here (unlike the detached thread below) - this always runs
  // synchronously from a button/dialog click callback, which LVGL invokes
  // from inside lv_timer_handler() while the main loop already holds
  // lv_lock (see GuppyScreen::loop()). Re-locking here on the same thread
  // self-deadlocks a plain std::mutex instantly - confirmed live 2026-07-12,
  // froze the whole screen (touch input kept working at the hardware level,
  // but the main thread never returned to release its own lock). Matches
  // the same (correct) pattern already used in refresh_status().
  set_busy(true, busy_msg);
  std::string cmd = fmt::format("python3 {} {}", STATIC_IP_SCRIPT, args);
  std::thread([this, cmd, on_done]() {
    auto result = KUtils::exec_capture(cmd);
    std::lock_guard<std::mutex> lock(lv_lock);
    set_busy(false, "");
    on_done(result.first, result.second);
  }).detach();
}

void StaticIpPanel::refresh_status() {
  set_busy(true, "Loading current network state...");
  std::string cmd = fmt::format("python3 {} status", STATIC_IP_SCRIPT);
  std::string ssid_copy = ssid;
  std::thread([this, cmd, ssid_copy]() {
    auto result = KUtils::exec_capture(cmd);
    int rc = result.first;
    std::string output = result.second;
    std::lock_guard<std::mutex> lock(lv_lock);
    set_busy(false, "");

    if (rc != 0) {
      // Fail safe: without a known state, default to DHCP-mode appearance -
      // fields disabled, Save hidden - rather than leaving both mode buttons
      // and the Save button in their as-constructed (both-active) state.
      has_static_config = false;
      set_manual_mode(false);
      update_revert_visibility();
      lv_obj_set_style_text_color(status_label, lv_palette_main(LV_PALETTE_RED), 0);
      lv_label_set_text(status_label, fmt::format("Couldn't read network state:\n{}", output).c_str());
      return;
    }

    try {
      json j = json::parse(output);
      json live = j.value("live", json::object());

      set_octets(ip_octets, json_str(live, "ip"));
      set_octets(netmask_octets, json_str(live, "netmask"));
      set_octets(gateway_octets, json_str(live, "gateway"));

      json dns_arr = live.value("dns", json::array());
      std::string dns_val = (dns_arr.is_array() && !dns_arr.empty()) ? dns_arr[0].template get<std::string>() : "";
      set_octets(dns_octets, dns_val);

      json configured = j.value("configured", json::object());
      has_static_config = configured.contains(ssid_copy);
      set_manual_mode(has_static_config);
      update_revert_visibility();

      lv_obj_set_style_text_color(status_label, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
      lv_label_set_text(status_label,
        fmt::format("Currently {} on {}", has_static_config ? "static" : "DHCP",
                    json_str(live, "ip")).c_str());
    } catch (const std::exception &ex) {
      lv_obj_set_style_text_color(status_label, lv_palette_main(LV_PALETTE_RED), 0);
      lv_label_set_text(status_label, fmt::format("Couldn't parse network state: {}", ex.what()).c_str());
    }
  }).detach();
}

void StaticIpPanel::handle_mode_toggle(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || busy) return;
  lv_obj_t *target = lv_event_get_target(e);
  // Pure view switch, nothing else - reverting a live static config is
  // handle_revert_btn's job now, never a side effect of tapping a tab.
  set_manual_mode(target == manual_mode_btn);
}

void StaticIpPanel::handle_revert_btn(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || busy) return;

  static const char *btns[] = {"Revert to DHCP", "Cancel", ""};
  std::string msg = fmt::format("Switch \"{}\" back to DHCP?\nThis drops the static IP.", ssid);
  lv_obj_t *mbox = lv_msgbox_create(NULL, NULL, msg.c_str(), btns, false);
  lv_obj_add_event_cb(mbox, [](lv_event_t *ev) {
    lv_obj_t *obj = lv_obj_get_parent(lv_event_get_target(ev));
    auto *self = static_cast<StaticIpPanel *>(lv_event_get_user_data(ev));
    bool confirmed = lv_msgbox_get_active_btn(obj) == 0;
    lv_msgbox_close(obj);
    if (!confirmed) return;

    self->run_backend_async(fmt::format("remove {}", shell_quote(self->ssid)),
                             "Reverting to DHCP...",
                             [self](int rc, const std::string &output) {
      if (rc == 0) {
        self->has_static_config = false;
        self->set_manual_mode(false);
        self->update_revert_visibility();
        self->refresh_status();
      } else {
        lv_obj_set_style_text_color(self->status_label, lv_palette_main(LV_PALETTE_RED), 0);
        lv_label_set_text(self->status_label, fmt::format("Revert failed:\n{}", output).c_str());
      }
    });
  }, LV_EVENT_VALUE_CHANGED, this);
  KUtils::style_lock_mbox(mbox, 90);
}

void StaticIpPanel::handle_save_btn(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || busy) return;

  for (lv_obj_t *const *group : {ip_octets, netmask_octets, gateway_octets, dns_octets}) {
    for (int i = 0; i < 4; i++) clear_octet_error(group[i]);
  }

  bool ip_ok = check_octet_group(ip_octets);
  bool netmask_ok = check_octet_group(netmask_octets);
  bool gateway_ok = check_octet_group(gateway_octets);
  bool dns_ok = check_octet_group(dns_octets);

  if (!ip_ok || !netmask_ok || !gateway_ok || !dns_ok) {
    lv_obj_set_style_text_color(status_label, lv_palette_main(LV_PALETTE_RED), 0);
    lv_label_set_text(status_label, "Enter a value 0-255 in every highlighted box.");
    return;
  }

  std::string ip = join_octets(ip_octets);
  std::string netmask = join_octets(netmask_octets);
  std::string gateway = join_octets(gateway_octets);
  std::string dns = join_octets(dns_octets);

  std::string err = validate_static_entry(ip, netmask, gateway);
  if (!err.empty()) {
    lv_obj_set_style_text_color(status_label, lv_palette_main(LV_PALETTE_RED), 0);
    lv_label_set_text(status_label, err.c_str());
    return;
  }

  std::string args = fmt::format("set {} {} {} {} {}", shell_quote(ssid), ip, netmask, gateway, dns);
  run_backend_async(args, "Applying static IP...", [this](int rc, const std::string &output) {
    if (rc == 0) {
      lv_obj_set_style_text_color(status_label, lv_palette_main(LV_PALETTE_GREEN), 0);
      lv_label_set_text(status_label, "Static IP applied.");
      has_static_config = true;
      update_revert_visibility();
    } else {
      lv_obj_set_style_text_color(status_label, lv_palette_main(LV_PALETTE_RED), 0);
      lv_label_set_text(status_label, fmt::format("Failed to apply:\n{}", output).c_str());
    }
  });
}

void StaticIpPanel::handle_kb_input(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *target = lv_event_get_target(e);

  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(kb, target);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    // Land on the end of whatever's already there (e.g. tapping "137" to
    // change just the last digit), not wherever the tap happened to land -
    // confirmed live 2026-07-12 this wasn't happening by default.
    lv_textarea_set_cursor_pos(target, LV_TEXTAREA_CURSOR_LAST);
    // Hide Back rather than trying to keep the keyboard clear of its
    // footprint - confirmed live 2026-07-12 that even the resized keyboard
    // still reaches the bottom-right corner. Whatever dismisses the
    // keyboard (below) always brings Back back.
    lv_obj_add_flag(back_btn.get_container(), LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_CANCEL) {
    // Unlike READY below, nothing here cleared LV_STATE_FOCUSED - a box
    // tapped into and then tapped away from (rather than dismissed via the
    // keyboard's own OK) kept the theme's focused-border styling
    // permanently, making its effective size subtly different from a
    // never-touched box in the same row. Confirmed live 2026-07-12: edited
    // octet boxes visibly misaligned against their neighbors afterward.
    lv_obj_clear_state(target, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(kb, NULL);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(back_btn.get_container(), LV_OBJ_FLAG_HIDDEN);
  } else if (code == LV_EVENT_READY) {
    lv_obj_clear_state(target, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(kb, NULL);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(back_btn.get_container(), LV_OBJ_FLAG_HIDDEN);
  }
}
