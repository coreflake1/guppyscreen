#include "printertune_panel.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

LV_IMG_DECLARE(bedmesh_img);
LV_IMG_DECLARE(fine_tune_img);
LV_IMG_DECLARE(limit_img);
LV_IMG_DECLARE(chart_img);
LV_IMG_DECLARE(retract_img);
LV_IMG_DECLARE(home_z);
LV_IMG_DECLARE(calibration_img);

#ifndef ZBOLT
LV_IMG_DECLARE(power_devices_img);
#else
LV_IMG_DECLARE(print);
#endif

PrinterTunePanel::PrinterTunePanel(KWebSocketClient &c, std::mutex &l, lv_obj_t *parent, FineTunePanel &finetune, ZOffsetPanel &zoffset)
  : cont(lv_obj_create(parent))
  , bedmesh_panel(c, l)
  , finetune_panel(finetune)
  , zoffset_panel(zoffset)
  , limits_panel(c, l)
  , inputshaper_panel(c, l)
  , tmc_tune_panel(c)
  , tmc_status_panel(c, l)
  , power_panel(c, l)
  , firmware_retraction_panel(c, l)
  , axis_twist_panel(c, l)
  , skew_correction_panel(c)
  , calibration_menu_panel(c, l, bedmesh_panel, inputshaper_panel, axis_twist_panel, skew_correction_panel, tmc_tune_panel)
  , bedmesh_btn(cont, &bedmesh_img, "Bed Mesh", &PrinterTunePanel::_handle_callback, this)
  , finetune_btn(cont, &fine_tune_img, "Fine Tune", &PrinterTunePanel::_handle_callback, this)
  , limits_btn(cont, &limit_img, "Limits", &PrinterTunePanel::_handle_callback, this)
  , tmc_status_btn(cont, &chart_img, "TMC Metrics", &PrinterTunePanel::_handle_callback, this)
#ifndef ZBOLT
  , power_devices_btn(cont, &power_devices_img, "Power Settings", &PrinterTunePanel::_handle_callback, this)
#else
  , power_devices_btn(cont, &print, "Power Settings", &PrinterTunePanel::_handle_callback, this)
#endif
  , retraction_btn(cont, &retract_img, "Retraction", &PrinterTunePanel::_handle_callback, this)
  , zoffset_btn(cont, &home_z, "Z Offset", &PrinterTunePanel::_handle_callback, this)
  , calibration_btn(cont, &calibration_img, "Calibration", &PrinterTunePanel::_handle_callback, this)
{
  lv_obj_move_background(cont);

  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(5), LV_GRID_FR(5),
    LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
      LV_GRID_TEMPLATE_LAST};

  lv_obj_set_grid_dsc_array(cont, grid_main_col_dsc, grid_main_row_dsc);

  // row 1 - live / per-print tuning
  lv_obj_set_grid_cell(finetune_btn.get_container(), LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(zoffset_btn.get_container(), LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(retraction_btn.get_container(), LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(limits_btn.get_container(), LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_STRETCH, 1, 1);

  // row 2 - routine checks + rarely-touched system settings + the calibration hub
  lv_obj_set_grid_cell(bedmesh_btn.get_container(), LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
  lv_obj_set_grid_cell(power_devices_btn.get_container(), LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
  lv_obj_set_grid_cell(tmc_status_btn.get_container(), LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
  lv_obj_set_grid_cell(calibration_btn.get_container(), LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
}

PrinterTunePanel::~PrinterTunePanel() {
  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }
}

lv_obj_t *PrinterTunePanel::get_container() {
  return cont;
}

BedMeshPanel &PrinterTunePanel::get_bedmesh_panel() {
  return bedmesh_panel;
}

PowerPanel &PrinterTunePanel::get_power_panel() {
  return power_panel;
}

void PrinterTunePanel::init(json &j) {
  limits_panel.init(j);

  tmc_status_panel.init(j);

  // TMC Autotune's motor_database.cfg detection now lives with the row itself,
  // inside the Calibration hub.
  calibration_menu_panel.init(j);
}

void PrinterTunePanel::handle_callback(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(event);

    if (btn == finetune_btn.get_container()) {
      spdlog::trace("tune finetune pressed");
      finetune_panel.foreground();
    } else if (btn == bedmesh_btn.get_container()) {
      spdlog::trace("tune bedmesh pressed");
      // Viewing the mesh mid-print is fine; the mutating actions (Calibrate /
      // Clear / Save) are blocked inside the panel instead.
      bedmesh_panel.foreground();
    } else if (btn == limits_btn.get_container()) {
      spdlog::trace("limits pressed");
      KUtils::confirm_if_printing("Printer is printing.\nAdjust limits anyway?",
        [this]() { limits_panel.foreground(); });
    } else if (btn == tmc_status_btn.get_container()) {
      spdlog::trace("tmc metrics pressed");
      tmc_status_panel.foreground();
    } else if (btn == power_devices_btn.get_container()) {
      spdlog::trace("power devices pressed");
      KUtils::confirm_if_printing("Printer is printing.\nChange power devices anyway?",
        [this]() { power_panel.foreground(); });
    } else if (btn == retraction_btn.get_container()) {
      spdlog::trace("retraction pressed");
      // Check 1: is [firmware_retraction] configured in Klipper at all? When it
      // is, Klipper exposes a firmware_retraction status object.
      auto fr = State::get_instance()->get_data(
        "/printer_state/firmware_retraction"_json_pointer);
      if (fr.is_null()) {
        KUtils::notify_toast(
          "Firmware retraction isn't enabled.\n"
          "Add [firmware_retraction] to printer.cfg.",
          4000);
        return;
      }
      // Check 2: only meaningful mid-print. Idle, we let the user tune freely;
      // printing, we make sure the running file is actually sliced for firmware
      // retraction (uses G10/G11) or the live values won't do anything.
      if (KUtils::is_printing()
          && KUtils::print_uses_firmware_retraction() == 0) {
        KUtils::notify_toast(
          "This print isn't sliced for firmware retraction.\n"
          "These settings won't apply.",
          4000);
        return;
      }
      firmware_retraction_panel.foreground();
    } else if (btn == zoffset_btn.get_container()) {
      spdlog::trace("z offset pressed");
      zoffset_panel.foreground();
    } else if (btn == calibration_btn.get_container()) {
      spdlog::trace("calibration menu pressed");
      // The wizard inside probes the bed and ends in a SAVE_CONFIG restart;
      // never mid-print.
      if (KUtils::is_printing()) { KUtils::notify_locked(); return; }
      calibration_menu_panel.foreground();
    }
  }
}
