#ifndef __MINI_PRINT_STATUS__
#define __MINI_PRINT_STATUS__

#include "lvgl/lvgl.h"
#include <string>

class MiniPrintStatus {
 public:
  MiniPrintStatus(lv_obj_t *parent,
		  lv_event_cb_t cb,
		  void* user_data);

  ~MiniPrintStatus();

  void show();
  void hide();
  lv_obj_t *get_container();

  // A tiny "Paused" pill shown (instead of the full overlay) while the print is
  // paused — keeps a tap-target back into the full status panel without
  // covering the Homing/Extrude buttons.
  void show_chip();
  void hide_chip();
  lv_obj_t *get_chip();

  void update_eta(std::string &eta_str);
  void update_status(std::string &status_str);
  void update_progress(int p);
  void update_img(const std::string &img_path, size_t twidth);
  void reset();

 private:
  lv_obj_t *cont;
  lv_obj_t *progress_bar;
  lv_obj_t *thumb;
  lv_obj_t *status_label;
  lv_obj_t *chip;
  std::string status;
  std::string eta;
};

#endif //__MINI_PRINT_STATUS__
