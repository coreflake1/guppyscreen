#ifndef __CALIBRATION_MENU_PANEL_H__
#define __CALIBRATION_MENU_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "recalibration_wizard_panel.h"
#include "esteps_calibration_panel.h"
#include "bedmesh_panel.h"
#include "inputshaper_panel.h"
#include "axis_twist_panel.h"
#include "skew_correction_panel.h"
#include "tmc_tune_panel.h"

#include <mutex>
#include <vector>

// Entry point for every calibration flow, reached from the Tune tab's single
// "Calibration" button. Mirrors wiki/Calibration-Explained.md's own
// recommended order as a numbered, scrollable list - Axis Twist through TMC
// Autotune - so the on-screen menu and the docs never drift apart. Axis Twist
// comes before Auto Calibration deliberately, not just cosmetically: Klipper
// applies its correction to every probe call from then on (patched into
// probe.py), so a bed mesh captured before it is twist-biased and has to be
// redone - see the wiki page and axis_twist_compensation.py's probe.py patch.
//
// Below the numbered list, an unordered "Other tools" section holds routines
// that don't fit a fixed sequence - today just Bed Mesh, which Auto
// Calibration already runs internally but is also useful standalone (e.g.
// re-mesh only, after a small change, without redoing the whole wizard).
//
// Bed Mesh / Input Shaper / Axis Twist / Skew / TMC Autotune panels are owned
// by PrinterTunePanel (Bed Mesh doubles as a Tune-tab top-level tile too,
// since viewing it is routine even though calibrating it isn't) and passed in
// by reference here, the same pattern PrinterTunePanel itself already uses
// for FineTunePanel/ZOffsetPanel.
class CalibrationMenuPanel {
 public:
  CalibrationMenuPanel(KWebSocketClient &, std::mutex &,
                        BedMeshPanel &, InputShaperPanel &,
                        AxisTwistPanel &, SkewCorrectionPanel &,
                        TmcTunePanel &);
  ~CalibrationMenuPanel();

  // Detects motor_database.cfg the same way PrinterTunePanel::init does, to
  // enable/init the TMC Autotune row.
  void init(json &j);

  void foreground();
  void handle_callback(lv_event_t *event);
  static void _handle_callback(lv_event_t *e) { ((CalibrationMenuPanel*)e->user_data)->handle_callback(e); }

  // right-side Up/OK/Down/Back nav column, same idea as macros_panel.cpp's
  // nav_cont: move a highlight through the rows and activate/leave with OK/Back,
  // without needing to land a precise tap on a specific row.
  void handle_nav(lv_event_t *event);
  static void _handle_nav(lv_event_t *e) { ((CalibrationMenuPanel*)e->user_data)->handle_nav(e); }

 private:
  void move_highlight(int delta);
  void set_highlight(int idx);
  void activate_row(lv_obj_t *row);

  KWebSocketClient &ws;

  lv_obj_t *panel_cont;
  lv_obj_t *list_cont;
  lv_obj_t *nav_cont;
  lv_obj_t *up_btn;
  lv_obj_t *ok_btn;
  lv_obj_t *down_btn;
  lv_obj_t *back_btn;

  // numbered, in recommended order
  lv_obj_t *axis_twist_row;
  lv_obj_t *wizard_row;
  lv_obj_t *inputshaper_row;
  lv_obj_t *esteps_row;
  lv_obj_t *skew_row;
  lv_obj_t *tmc_tune_row;
  // unordered utility section
  lv_obj_t *bedmesh_row;

  // every row above, in on-screen order, for Up/Down to walk through
  std::vector<lv_obj_t*> rows;
  int highlight_index;

  RecalibrationWizardPanel recalibration_wizard_panel;
  EstepsCalibrationPanel esteps_calibration_panel;
  BedMeshPanel &bedmesh_panel;
  InputShaperPanel &inputshaper_panel;
  AxisTwistPanel &axis_twist_panel;
  SkewCorrectionPanel &skew_correction_panel;
  TmcTunePanel &tmc_tune_panel;

  bool tmc_tune_available;
};

#endif  // __CALIBRATION_MENU_PANEL_H__
