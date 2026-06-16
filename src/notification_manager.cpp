#include "notification_manager.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

static const int    TAB_W      = 60;     // left tab bar width
static const size_t MAX_TOASTS = 3;
static const uint32_t INFO_MS  = 4000;   // info auto-dismiss

NotificationManager::NotificationManager(KWebSocketClient &websocket_client, std::mutex &lock)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , cont(lv_obj_create(lv_layer_top()))
  , baselined(false)
{
  // toast stack: top of screen, right of the tab bar, stacking downward.
  lv_coord_t w = lv_disp_get_hor_res(NULL) - TAB_W - 8;
  lv_obj_set_size(cont, w, LV_SIZE_CONTENT);
  lv_obj_align(cont, LV_ALIGN_TOP_LEFT, TAB_W + 4, 4);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_style_pad_row(cont, 4, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_CLICKABLE);  // empty area passes clicks through

  ws.register_notify_update(this);
  ws.register_method_callback("notify_gcode_response", "NotificationManager",
                              [this](json &d) { this->handle_gcode_response(d); });
  // re-baseline after a (re)connect so we don't replay state as fresh events
  ws.register_method_callback("notify_klippy_ready", "NotificationManager",
                              [this](json &) { this->baselined = false; });

#ifdef SIMULATOR
  // no printer in the sim - periodically re-show a demo of each severity so the
  // overlay can be inspected. (The info toast auto-dismisses; this re-adds it.)
  lv_timer_create([](lv_timer_t *t) {
    ((NotificationManager *)t->user_data)->sim_demo();
  }, 6000, this);
#endif
}

#ifdef SIMULATOR
void NotificationManager::sim_demo() {
  // called from an lv_timer cb -> lv_lock is already held by lv_timer_handler
  push("Heater extruder: temperature outside range", ERROR);
  push("Bed leveling complete", INFO);  // M117-style status message
  static bool homing_shown = false;
  if (!homing_shown) {  // show the homing modal once so it can be inspected
    homing_shown = true;
    show_homing_prompt();
  }
}
#endif

NotificationManager::~NotificationManager() {
  ws.unregister_notify_update(this);
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

// ---- event intake ----

void NotificationManager::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  if (!baselined) {
    baseline();
    baselined = true;
    return;  // never replay the initial snapshot as toasts
  }
  if (j.contains("params") && j["params"].is_array() && !j["params"].empty()) {
    process(j["params"][0]);
  }
}

void NotificationManager::handle_gcode_response(json &j) {
  if (!j.contains("params")) {
    return;
  }
  std::lock_guard<std::mutex> lock(lv_lock);
  for (auto &el : j["params"]) {
    if (!el.is_string()) {
      continue;
    }
    std::string line = el.template get<std::string>();
    // "!! " = Klipper error; "// action:" belongs to PromptPanel; skip the rest
    if (line.rfind("!! ", 0) == 0) {
      std::string err = line.substr(3);
      // a homing-required error becomes an actionable modal instead of a toast
      if (err.find("Must home axis first") != std::string::npos) {
        show_homing_prompt();
      } else {
        push(err, ERROR);
      }
    }
  }
}

// ---- baseline + diff ----

void NotificationManager::baseline() {
  State *s = State::get_instance();
  auto ps = s->get_data("/printer_state"_json_pointer);

  last_webhooks_state = ps.contains("webhooks") && ps["webhooks"]["state"].is_string()
    ? ps["webhooks"]["state"].template get<std::string>() : "ready";
  last_print_state = ps.contains("print_stats") && ps["print_stats"]["state"].is_string()
    ? ps["print_stats"]["state"].template get<std::string>() : "standby";
  last_display_msg = ps.contains("display_status") && ps["display_status"]["message"].is_string()
    ? ps["display_status"]["message"].template get<std::string>() : "";

  spdlog::debug("notif baseline: webhooks={} print={}", last_webhooks_state, last_print_state);
}

void NotificationManager::process(json &st) {
  if (!st.is_object()) {
    return;
  }

  // Klipper error / shutdown
  if (st.contains("webhooks") && st["webhooks"]["state"].is_string()) {
    std::string ns = st["webhooks"]["state"].template get<std::string>();
    if (ns != last_webhooks_state) {
      if (ns == "shutdown" || ns == "error") {
        std::string msg = st["webhooks"]["state_message"].is_string()
          ? st["webhooks"]["state_message"].template get<std::string>() : "";
        push(msg.empty() ? ("Klipper " + ns) : msg, ERROR);
      }
      last_webhooks_state = ns;
    }
  }

  // keep print state fresh for M117 gating (completion is shown on the
  // print-status panel; runout is handled by the runout modal)
  if (st.contains("print_stats") && st["print_stats"]["state"].is_string()) {
    last_print_state = st["print_stats"]["state"].template get<std::string>();
  }

  // M117 / status messages - only when not printing (slicers spam M117 progress)
  if (st.contains("display_status") && st["display_status"].contains("message")) {
    auto &m = st["display_status"]["message"];
    std::string msg = m.is_string() ? m.template get<std::string>() : "";
    if (msg != last_display_msg) {
      if (!msg.empty() && !is_printing()) {
        push(msg, INFO);
      }
      last_display_msg = msg;
    }
  }
}

// ---- toast rendering ----

void NotificationManager::push(const std::string &text, Severity sev) {
  // coalesce: identical text already shown -> refresh its timer, don't stack
  for (auto &t : toasts) {
    if (t.text == text) {
      if (t.timer != NULL) {
        lv_timer_reset(t.timer);
      }
      return;
    }
  }

  if (toasts.size() >= MAX_TOASTS) {
    remove_card(toasts.front().card, false);  // drop oldest
  }

  lv_color_t bg;
  const char *icon;
  switch (sev) {
    case ERROR:   bg = lv_palette_darken(LV_PALETTE_RED, 2);    icon = LV_SYMBOL_WARNING; break;
    case WARNING: bg = lv_palette_darken(LV_PALETTE_ORANGE, 1); icon = LV_SYMBOL_WARNING; break;
    default:      bg = lv_palette_darken(LV_PALETTE_BLUE, 2);   icon = LV_SYMBOL_BELL;    break;
  }

  lv_obj_t *card = lv_obj_create(cont);
  lv_obj_set_width(card, LV_PCT(100));
  lv_obj_set_height(card, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(card, 8, 0);
  lv_obj_set_style_pad_column(card, 8, 0);
  lv_obj_set_style_radius(card, 6, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_bg_color(card, bg, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_user_data(card, this);
  lv_obj_add_event_cb(card, &NotificationManager::_tap_cb, LV_EVENT_CLICKED, this);
  lv_obj_move_to_index(card, 0);  // newest on top

  lv_obj_t *ic = lv_label_create(card);
  lv_label_set_text(ic, icon);
  lv_obj_set_style_text_font(ic, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(ic, lv_color_white(), 0);

  lv_obj_t *lbl = lv_label_create(card);
  lv_obj_set_flex_grow(lbl, 1);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
  lv_label_set_text(lbl, text.c_str());
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);

  // info auto-dismisses; warning/error stay until tapped (or condition clears)
  lv_timer_t *timer = NULL;
  if (sev == INFO) {
    timer = lv_timer_create(&NotificationManager::_timer_cb, INFO_MS, card);
    lv_timer_set_repeat_count(timer, 1);
  }

  toasts.push_back({text, card, timer});
}

void NotificationManager::remove_card(lv_obj_t *card, bool from_timer) {
  for (auto it = toasts.begin(); it != toasts.end(); ++it) {
    if (it->card == card) {
      // a one-shot timer auto-deletes after its callback returns; only delete it
      // ourselves on a manual/early dismiss
      if (!from_timer && it->timer != NULL) {
        lv_timer_del(it->timer);
      }
      lv_obj_del(it->card);
      toasts.erase(it);
      return;
    }
  }
}

void NotificationManager::_tap_cb(lv_event_t *e) {
  auto *self = (NotificationManager *)lv_event_get_user_data(e);
  self->remove_card(lv_event_get_current_target(e), false);
}

void NotificationManager::_timer_cb(lv_timer_t *t) {
  lv_obj_t *card = (lv_obj_t *)t->user_data;
  auto *self = (NotificationManager *)lv_obj_get_user_data(card);
  self->remove_card(card, true);
}

// ---- homing-required modal ----

void NotificationManager::show_homing_prompt() {
  KUtils::show_homing_prompt(ws);
}
