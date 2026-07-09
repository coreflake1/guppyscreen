#ifndef __PRINTERTUNE_PANEL_H__
#define __PRINTERTUNE_PANEL_H__

#include "finetune_panel.h"
#include "zoffset_panel.h"
#include "limits_panel.h"
#include "bedmesh_panel.h"
#include "inputshaper_panel.h"
#include "tmc_tune_panel.h"
#include "tmc_status_panel.h"
#include "power_panel.h"
#include "firmware_retraction_panel.h"
#include "axis_twist_panel.h"
#include "skew_correction_panel.h"
#include "calibration_menu_panel.h"
#include "button_container.h"
#include "lvgl/lvgl.h"

#include <mutex>
class PrinterTunePanel {
 public:
  PrinterTunePanel(KWebSocketClient &c, std::mutex &l, lv_obj_t *parent, FineTunePanel &, ZOffsetPanel &);
  ~PrinterTunePanel();

  lv_obj_t *get_container();
  BedMeshPanel &get_bedmesh_panel();
  PowerPanel &get_power_panel();
  void init(json &j);
  void handle_callback(lv_event_t *event);

  static void _handle_callback(lv_event_t *event) {
    PrinterTunePanel *panel = (PrinterTunePanel*)event->user_data;
    panel->handle_callback(event);
  };

 private:
  lv_obj_t *cont;
  BedMeshPanel bedmesh_panel;
  FineTunePanel &finetune_panel;
  ZOffsetPanel &zoffset_panel;
  LimitsPanel limits_panel;
  InputShaperPanel inputshaper_panel;
  TmcTunePanel tmc_tune_panel;
  TmcStatusPanel tmc_status_panel;
  PowerPanel power_panel;
  FirmwareRetractionPanel firmware_retraction_panel;
  AxisTwistPanel axis_twist_panel;
  SkewCorrectionPanel skew_correction_panel;
  // Constructed last: takes references to several of the panels above, which
  // it now owns the Tune-tab entry point for (Input Shaper, Axis Twist, Skew,
  // TMC Autotune, plus Bed Mesh - Bed Mesh doubles as its own top-level tile
  // below since viewing it is routine even though calibrating it isn't).
  CalibrationMenuPanel calibration_menu_panel;
  ButtonContainer bedmesh_btn;
  ButtonContainer finetune_btn;
  ButtonContainer limits_btn;
  ButtonContainer tmc_status_btn;
  ButtonContainer power_devices_btn;
  ButtonContainer retraction_btn;
  ButtonContainer zoffset_btn;
  ButtonContainer calibration_btn;

};

#endif // __PRINTERTUNE_PANEL_H__
