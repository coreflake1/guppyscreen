#include "file_panel.h"
#include "config.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <vector>
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

FilePanel::FilePanel(lv_obj_t *parent)
  : file_cont(lv_obj_create(parent))
  , thumbnail_container(lv_obj_create(file_cont))
  , thumbnail(lv_img_create(thumbnail_container))
  , fname_label(lv_label_create(file_cont))
  , detail_label(lv_label_create(file_cont))
{
  // Setup main container
  lv_obj_set_size(file_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(file_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(file_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(file_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(file_cont, 0, 0);
  lv_obj_set_style_pad_row(file_cont, 0, 0);

  // Setup thumbnail container inside main container
  lv_obj_set_width(thumbnail_container, LV_PCT(100));
  lv_obj_clear_flag(thumbnail_container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(thumbnail_container, 0, 0);
  lv_obj_set_flex_grow(thumbnail_container, 1);
  // Prevent the container's own background/border from showing through when there is no thumbnail
  lv_obj_set_style_bg_opa(thumbnail_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(thumbnail_container, LV_OPA_TRANSP, 0);

  // Now set thumbnail parent to container and setup
  lv_obj_clear_flag(thumbnail, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(thumbnail, 0, 0);
  // Start invisible; revealed by refresh_view once positioned correctly
  lv_obj_set_style_opa(thumbnail, LV_OPA_TRANSP, 0);

  // Setup filename label
  lv_label_set_long_mode(fname_label, LV_LABEL_LONG_SCROLL);
  lv_obj_set_width(fname_label, LV_PCT(100));
  lv_obj_set_style_pad_all(fname_label, 0, 0);
  lv_obj_set_style_text_font(fname_label, &lv_font_montserrat_10, 0);

  // Setup detail label
  lv_obj_set_width(detail_label, LV_PCT(100));
  lv_obj_set_style_pad_all(detail_label, 0, 0);
  lv_obj_set_style_text_font(detail_label, &lv_font_montserrat_10, 0);
}

FilePanel::~FilePanel() {
  if (file_cont != NULL) {
    lv_obj_del(file_cont);
    file_cont = NULL;
  }
}

void FilePanel::refresh_view(json &j, const std::string &gcode_path) {
  auto v = j["/result/modified"_json_pointer];
  std::stringstream time_stream;
  if (!v.is_null()) {
    std::time_t timestamp = v.template get<std::time_t>();
    std::tm lt = *std::localtime(&timestamp);
    time_stream << std::put_time(&lt, "%Y-%m-%d %H:%M");
  } else {
    time_stream << "(unknown)";
  }

  v = j["/result/estimated_time"_json_pointer];
  int eta = v.is_null() ? -1 : v.template get<int>();
  v = j["/result/filament_weight_total"_json_pointer];
  int fweight = v.is_null() ? -1 : v.template get<int>();

  auto filename = fs::path(gcode_path).filename();
  // Only set when text changes to avoid restarting the scroll animation mid-scroll
  if (filename.string() != lv_label_get_text(fname_label)) {
    lv_label_set_text(fname_label, filename.string().c_str());
  }

  std::string detail = fmt::format("Filament Weight: {} g\nPrint Time: {}\nSize: {} MB\nModified: {}",
    fweight > 0 ? std::to_string(fweight) : "(unknown)",
    eta > 0 ? KUtils::eta_string(eta) : "(unknown)",
    KUtils::bytes_to_mb(j["result"]["size"].template get<size_t>()),
    time_stream.str());

  // Target ~300px thumbnails (scale=1.0) so slicers that embed a 256/400px
  // thumbnail get picked over the tiny 96px one the old 800-divisor chose.
  auto width_scale = (double)lv_disp_get_physical_hor_res(NULL) / 480.0;
  auto thumb_result = KUtils::get_thumbnail(gcode_path, j, width_scale);
  std::string fullpath = thumb_result.first;
  size_t raw_thumb_w = thumb_result.second.first;
  size_t raw_thumb_h = thumb_result.second.second;

  lv_label_set_text(detail_label, detail.c_str());

  if (!fullpath.empty()) {
    // Hide before lv_refr_now so the stale zoom/position doesn't flash for one frame.
    lv_obj_set_style_opa(thumbnail, LV_OPA_TRANSP, 0);
    lv_img_set_src(thumbnail, ("A:" + fullpath).c_str());

    // lv_refr_now is the only reliable way to resolve thumbnail_container's
    // flex-grown height: lv_obj_update_layout scoped to file_cont or lv_scr_act()
    // both fail because file_cont is LV_PCT(100) and the parent chain isn't
    // resolved in a layout-only pass without a full render tick.
    lv_refr_now(NULL);

    lv_coord_t cont_w = lv_obj_get_width(thumbnail_container);
    lv_coord_t cont_h = lv_obj_get_height(thumbnail_container);

    float scale_w = raw_thumb_w ? (float)cont_w / raw_thumb_w : 1.0f;
    float scale_h = raw_thumb_h ? (float)cont_h / raw_thumb_h : 1.0f;
    float scale = std::min(scale_w, scale_h);
    lv_img_set_zoom(thumbnail, (uint16_t)(scale * 256));
    lv_obj_align_to(thumbnail, thumbnail_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_opa(thumbnail, LV_OPA_COVER, 0);
  } else {
    lv_obj_set_style_opa(thumbnail, LV_OPA_TRANSP, 0);
  }
}

void FilePanel::foreground() {
  lv_obj_move_foreground(file_cont);
}

void FilePanel::show_loading(const std::string &gcode_path) {
  auto filename = fs::path(gcode_path).filename();
  if (filename.string() != lv_label_get_text(fname_label)) {
    lv_label_set_text(fname_label, filename.string().c_str());
  }
  lv_label_set_text(detail_label, "Loading...");
  // Keep the previous thumbnail visible while metadata loads — refresh_view will
  // replace it once the RPC completes, avoiding a blank flash between files.
}

lv_obj_t *FilePanel::get_container() {
  return file_cont;
}

const char *FilePanel::get_thumbnail_path() {
  return (const char *)lv_img_get_src(thumbnail);
}
