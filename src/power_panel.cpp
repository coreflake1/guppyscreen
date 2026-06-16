#include "power_panel.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <map>
#include <fstream>

LV_IMG_DECLARE(back);

PowerPanel::PowerPanel(KWebSocketClient &websocket_client, std::mutex &l)
  : ws(websocket_client)
  , lv_lock(l)
  , cont(lv_obj_create(lv_scr_act()))
  , back_btn(cont, &back, "Back", &PowerPanel::_handle_callback, this)
{
  lv_obj_move_background(cont);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  // Power-loss recovery sits at the top, above the power-device toggles.
  build_recovery_section();

  lv_obj_add_flag(back_btn.get_container(), LV_OBJ_FLAG_FLOATING);
  lv_obj_align(back_btn.get_container(), LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

void PowerPanel::build_recovery_section() {
  recovery_cont = lv_obj_create(cont);
  lv_obj_set_size(recovery_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(recovery_cont, 4, 0);
  lv_obj_set_flex_flow(recovery_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(recovery_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_clear_flag(recovery_cont, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *heading = lv_label_create(recovery_cont);
  lv_label_set_text(heading, LV_SYMBOL_CHARGE " Power Loss Recovery");

  recovery_status = lv_label_create(recovery_cont);
  lv_obj_set_width(recovery_status, LV_PCT(100));
  lv_label_set_long_mode(recovery_status, LV_LABEL_LONG_WRAP);
  lv_label_set_text(recovery_status, "");

  lv_obj_t *btn_row = lv_obj_create(recovery_cont);
  lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(btn_row, 0, 0);
  lv_obj_set_style_pad_column(btn_row, 8, 0);
  lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

  recovery_resume_btn = lv_btn_create(btn_row);
  lv_obj_t *rl = lv_label_create(recovery_resume_btn);
  lv_label_set_text(rl, LV_SYMBOL_PLAY " Resume Print");
  lv_obj_center(rl);
  lv_obj_add_event_cb(recovery_resume_btn, &PowerPanel::_handle_callback, LV_EVENT_CLICKED, this);

  recovery_dismiss_btn = lv_btn_create(btn_row);
  lv_obj_t *dl = lv_label_create(recovery_dismiss_btn);
  lv_label_set_text(dl, LV_SYMBOL_CLOSE " Dismiss");
  lv_obj_center(dl);
  lv_obj_add_event_cb(recovery_dismiss_btn, &PowerPanel::_handle_callback, LV_EVENT_CLICKED, this);

  // Hidden until refresh_recovery() finds a recoverable print.
  lv_obj_add_flag(recovery_resume_btn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(recovery_dismiss_btn, LV_OBJ_FLAG_HIDDEN);
}

std::string PowerPanel::recoverable_print(std::string &display) {
  display = "";
  // The saved-state file lives on the printer's filesystem; only readable when the UI
  // runs locally on the machine (not the desktop simulator / remote).
  if (!KUtils::is_running_local()) {
    return "";
  }
  std::ifstream f("/usr/data/creality/userdata/config/print_file_name.json");
  if (!f.good()) {
    return "";
  }
  try {
    json j = json::parse(f, nullptr, false);
    if (j.is_discarded() || !j.contains("file_path")) {
      return "";
    }
    std::string fp = j["file_path"].template get<std::string>();
    if (fp.empty()) {
      return "";
    }
    // SDCARD_PRINT_FILE wants the path relative to the gcodes dir.
    std::string rel = fp;
    auto pos = fp.find("/gcodes/");
    if (pos != std::string::npos) {
      rel = fp.substr(pos + 8); // strlen("/gcodes/")
    }
    auto slash = rel.find_last_of('/');
    display = (slash == std::string::npos) ? rel : rel.substr(slash + 1);
    return rel;
  } catch (...) {
    return "";
  }
}

void PowerPanel::refresh_recovery() {
  if (recovery_cont == nullptr) {
    return;
  }
  std::string display;
  recovery_relpath = recoverable_print(display);

  bool show_actions = false;
  if (KUtils::is_printing()) {
    lv_label_set_text(recovery_status,
      "A print is running. If power is lost, reopen this screen to resume.");
  } else if (recovery_relpath.empty()) {
    lv_label_set_text(recovery_status, "No interrupted print to recover.");
  } else {
    lv_label_set_text(recovery_status,
      ("Last print: " + display + "\nResume from where power was lost?").c_str());
    show_actions = true;
  }

  if (show_actions) {
    lv_obj_clear_flag(recovery_resume_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(recovery_dismiss_btn, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(recovery_resume_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(recovery_dismiss_btn, LV_OBJ_FLAG_HIDDEN);
  }
}

PowerPanel::~PowerPanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }

  devices.clear();
}

void PowerPanel::create_device(json &j) {
  std::string name = j["device"].template get<std::string>();

  lv_obj_t *power_device_toggle;
  auto entry = devices.find(name);
  if (entry == devices.end()) {
    lv_obj_t *power_device_toggle_cont = lv_obj_create(cont);
    lv_obj_set_size(power_device_toggle_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(power_device_toggle_cont, 0, 0);

    lv_obj_t *l = lv_label_create(power_device_toggle_cont);
    lv_label_set_text(l, name.c_str());
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);

    power_device_toggle = lv_switch_create(power_device_toggle_cont); 
    lv_obj_align(power_device_toggle, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_add_event_cb(power_device_toggle, &PowerPanel::_handle_callback,
	        LV_EVENT_VALUE_CHANGED, this);

    devices.insert({name, power_device_toggle});
  } else {
    power_device_toggle = entry->second;
  }
  
  std::string status = j["status"].template get<std::string>();
  spdlog::debug("Fetched initial status for power device {}: {}", name, status);
  
  if (status == "on") {
    lv_obj_add_state(power_device_toggle, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(power_device_toggle, LV_STATE_CHECKED);
  }
}

void PowerPanel::create_devices(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);

  if (j.contains("result")) {
    json result = j["result"];
    if (result.contains("devices")) {
      json devices = result["devices"];
      for (auto &device : devices.items()) {
        create_device(device.value());
      }
    }
  }
}

void PowerPanel::handle_device_callback(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);

  if (j.contains("result")) {
    json result = j["result"];
    for (auto &device : result.items()) {
      auto entry = devices.find(device.key());
      if (entry != devices.end()) {
        std::string new_status = device.value();

        spdlog::debug("Fetched new status for power device {}: {}", entry->first, new_status);

        if (new_status == "on") {
          lv_obj_add_state(entry->second, LV_STATE_CHECKED);
        } else {
          lv_obj_clear_state(entry->second, LV_STATE_CHECKED);
        }
      }
    }
  }
}

void PowerPanel::foreground() {
  lv_obj_move_foreground(cont);

  // Re-check for a recoverable print each time the screen is opened.
  refresh_recovery();

  json params;
  for (auto &device : devices) {
    params[device.first] = 0; // value is ignored by moonraker
  }
  ws.send_jsonrpc("machine.device_power.status", params, [this](json& j) {
    this->handle_device_callback(j);
  });
}

void PowerPanel::handle_callback(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(e);
    if (btn == back_btn.get_container()) {
      lv_obj_move_background(cont);
    } else if (btn == recovery_resume_btn) {
      if (!recovery_relpath.empty()) {
        // Creality's virtual_sdcard restores the saved breakpoint (reheat + position)
        // when ISCONTINUEPRINT=1; if no valid breakpoint exists it safely starts the
        // file normally rather than crashing.
        ws.gcode_script("SDCARD_PRINT_FILE FILENAME=\"" + recovery_relpath + "\" ISCONTINUEPRINT=1");
        KUtils::notify_toast("Resuming print after power loss...", 2500);
        lv_label_set_text(recovery_status, "Resuming...");
        lv_obj_add_flag(recovery_resume_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(recovery_dismiss_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_background(cont);
      }
    } else if (btn == recovery_dismiss_btn) {
      lv_label_set_text(recovery_status, "No interrupted print to recover.");
      lv_obj_add_flag(recovery_resume_btn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(recovery_dismiss_btn, LV_OBJ_FLAG_HIDDEN);
    }
  } else if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
    lv_obj_t *obj = lv_event_get_target(e);
    for (auto &device : devices) {
      if (obj == device.second) {
        bool turnOn = lv_obj_has_state(device.second, LV_STATE_CHECKED);
        std::string status = turnOn ? "on" : "off";

        spdlog::debug("Turning power device {} {}", device.first, status);

        json params;
        params["device"] = device.first;
        params["action"] = status;
        ws.send_jsonrpc("machine.device_power.post_device", params, [this](json& j) {
          this->handle_device_callback(j);
        });
	      break;
      }
    }
  }
}
