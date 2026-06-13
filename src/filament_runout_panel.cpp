#include "filament_runout_panel.h"
#include "state.h"
#include "config.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <algorithm>

// Chunked load: feed RUNOUT_LOAD_TOTAL_MM in RUNOUT_LOAD_CHUNK_MM steps so the
// feed can be stopped between chunks (the stock LOAD_MATERIAL macro is one
// uninterruptible G1 E150 F180 move). RUNOUT_LOAD_FEED is mm/min; 300 = 5 mm/s.
static const int RUNOUT_LOAD_TOTAL_MM = 150;
static const int RUNOUT_LOAD_CHUNK_MM = 10;
static const int RUNOUT_LOAD_FEED = 300;

// Button maps for the runout dialog. The first button toggles Load <-> Stop
// depending on whether a chunked feed is in progress (lv_btnmatrix_set_map
// stores the pointer, not a copy, so these must have static lifetime).
static const char *RUNOUT_BTNS_IDLE[]    = {"Load", "Continue", "Cancel", ""};
static const char *RUNOUT_BTNS_LOADING[] = {"Stop", "Continue", "Cancel", ""};

FilamentRunoutPanel::FilamentRunoutPanel(KWebSocketClient &websocket_client, std::mutex &lock)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , mbox(NULL)
  , baselined(false)
  , last_detected(true)
{
  ws.register_notify_update(this);

#ifdef SIMULATOR
  // no printer in the sim: pop the dialog once so it can be inspected
  lv_timer_t *t = lv_timer_create([](lv_timer_t *tm) {
    ((FilamentRunoutPanel *)tm->user_data)->sim_show();
  }, 3000, this);
  lv_timer_set_repeat_count(t, 1);
#endif
}

FilamentRunoutPanel::~FilamentRunoutPanel() {
  ws.unregister_notify_update(this);
  close();
}

void FilamentRunoutPanel::show() {
  if (mbox != NULL) {
    return;  // already open
  }
  mbox = lv_msgbox_create(NULL, "Filament runout",
    "Load new filament, then Continue.", RUNOUT_BTNS_IDLE, false);
  KUtils::style_lock_mbox(mbox, 95);  // centered card + blue buttons, like other dialogs
  lv_obj_add_event_cb(mbox, &FilamentRunoutPanel::_mbox_cb, LV_EVENT_VALUE_CHANGED, this);
}

void FilamentRunoutPanel::close() {
  // Halt any in-flight chunked feed: clearing load_active makes the next chunk
  // callback bail instead of feeding into a closed dialog.
  load_stop = true;
  load_active = false;
  if (mbox != NULL) {
    lv_msgbox_close(mbox);
    mbox = NULL;
  }
}

bool FilamentRunoutPanel::printing_or_paused() const {
  auto v = State::get_instance()->get_data("/printer_state/print_stats/state"_json_pointer);
  if (!v.is_string()) {
    return false;
  }
  std::string s = v.template get<std::string>();
  return s == "printing" || s == "paused";
}

bool FilamentRunoutPanel::filament_present() const {
  if (fil_key.empty()) {
    return true;
  }
  auto v = State::get_instance()->get_data(
    json::json_pointer("/printer_state/" + fil_key + "/filament_detected"));
  return v.is_boolean() ? v.template get<bool>() : true;
}

void FilamentRunoutPanel::set_mbox_text(const char *txt) {
  if (mbox == NULL) {
    return;
  }
  lv_obj_t *lbl = lv_msgbox_get_text(mbox);
  if (lbl != NULL) {
    lv_label_set_text(lbl, txt);
  }
}

void FilamentRunoutPanel::set_load_btn(bool loading) {
  if (mbox == NULL) {
    return;
  }
  lv_obj_t *btnm = lv_msgbox_get_btns(mbox);
  if (btnm != NULL) {
    lv_btnmatrix_set_map(btnm, loading ? RUNOUT_BTNS_LOADING : RUNOUT_BTNS_IDLE);
  }
}

void FilamentRunoutPanel::begin_load() {
  load_active = true;
  load_stop = false;
  load_remaining_mm = RUNOUT_LOAD_TOTAL_MM;
  set_load_btn(true);   // Load -> Stop while feeding
  set_mbox_text("Loading filament...\nPress Stop to halt.");
  send_load_chunk();
}

void FilamentRunoutPanel::send_load_chunk() {
  if (!load_active) {
    return;  // dialog closed or load already finished
  }
  if (load_stop || load_remaining_mm <= 0) {
    finish_load();
    return;
  }
  int chunk = std::min(RUNOUT_LOAD_CHUNK_MM, load_remaining_mm);
  load_remaining_mm -= chunk;
  // The next chunk is only issued from this response callback, so setting
  // load_stop halts the feed within one chunk. (Assumes the hotend is at print
  // temp — true for a paused runout; a cold-extrude error still fires the
  // callback, so the loop drains harmlessly rather than hanging.)
  ws.gcode_script(fmt::format("M83\nG1 E{} F{}", chunk, RUNOUT_LOAD_FEED),
    [this](json &) {
      std::lock_guard<std::mutex> lock(lv_lock);
      send_load_chunk();
    });
}

void FilamentRunoutPanel::finish_load() {
  load_active = false;
  load_stop = false;
  load_remaining_mm = 0;
  set_load_btn(false);  // Stop -> Load; press again to feed more
  set_mbox_text("Load new filament, then Continue.");
}

void FilamentRunoutPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);

  if (!baselined) {
    // discover the sensor + its current reading from full state, fire nothing
    auto ps = State::get_instance()->get_data("/printer_state"_json_pointer);
    if (ps.is_object()) {
      for (auto &el : ps.items()) {
        if (el.key().rfind("filament_switch_sensor ", 0) == 0 ||
            el.key().rfind("filament_motion_sensor ", 0) == 0) {
          fil_key = el.key();
          if (el.value()["filament_detected"].is_boolean()) {
            last_detected = el.value()["filament_detected"].template get<bool>();
          }
          break;
        }
      }
    }
    baselined = true;
    return;
  }

  if (fil_key.empty() || !j.contains("params") || !j["params"].is_array() || j["params"].empty()) {
    return;
  }
  auto &st = j["params"][0];
  if (!st.is_object() || !st.contains(fil_key)) {
    return;
  }
  auto &fo = st[fil_key];
  bool enabled = fo["enabled"].is_boolean() ? fo["enabled"].template get<bool>() : true;
  if (!fo["filament_detected"].is_boolean()) {
    return;
  }
  bool det = fo["filament_detected"].template get<bool>();
  if (det == last_detected) {
    return;
  }
  last_detected = det;
  // runout: only matters mid-print and while the sensor is enabled
  if (!det && enabled && printing_or_paused()) {
    show();
  }
}

void FilamentRunoutPanel::handle_mbox(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || mbox == NULL) {
    return;
  }
  uint16_t id = lv_msgbox_get_active_btn(mbox);
  if (id == 0) {            // First button: "Load" when idle, "Stop" while feeding
    if (load_active) {
      load_stop = true;     // Stop pressed: halt after the in-flight chunk
      set_mbox_text("Stopping load...");
    } else {
      begin_load();
    }
  } else if (id == 1) {     // Continue — re-check the sensor, then resume
    if (load_active) {
      // Don't resume mid-feed; stop first, then the user taps Continue again.
      load_stop = true;
      KUtils::notify_toast("Stopping load; tap Continue again.");
      return;
    }
    if (filament_present()) {
      ws.gcode_script("RESUME");
      close();
    } else {
      KUtils::notify_toast("No filament detected yet.");
    }
  } else if (id == 2) {     // Cancel print
    load_stop = true;       // CANCEL_PRINT halts motion; stop our feed loop too
    ws.gcode_script("CANCEL_PRINT");
    close();
  }
}
