#include "bedmesh_panel.h"
#include "state.h"
#include "spdlog/spdlog.h"

#include <vector>
#include <cmath>
#include <algorithm>
#include <string>

#ifndef SIMULATOR
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>

// Helper macros for bit testing
#define NLONGS(x) (((x) + 8*sizeof(long) - 1) / (8*sizeof(long)))
#define test_bit(nr, addr) (((1UL << ((nr) % (8*sizeof(long)))) & ((addr)[(nr) / (8*sizeof(long))])) != 0)
#endif

LV_IMG_DECLARE(chart_img);
LV_IMG_DECLARE(sysinfo_img);

// Z zoom limits
const double MIN_Z_SCALE = 0.1;
const double MAX_Z_SCALE = 5.0;

// FOV/Camera distance limits
const double MIN_CAMERA_DISTANCE = 200.0;
const double MAX_CAMERA_DISTANCE = 800.0;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

LV_IMG_DECLARE(back);
LV_IMG_DECLARE(delete_img);
LV_IMG_DECLARE(bedmesh_img);
LV_IMG_DECLARE(sd_img);


static lv_color_t color_gradient(double offset);
static lv_color_t color_gradient_enhanced(double offset, double min_val, double max_val);

BedMeshPanel::BedMeshPanel(KWebSocketClient &c, std::mutex &l)
  : NotifyConsumer(l)
  , ws(c)
  , cont(lv_obj_create(lv_scr_act()))
  , prompt(lv_obj_create(lv_scr_act()))
  , top_cont(lv_obj_create(cont))
  , display_cont(lv_obj_create(top_cont))
  , mesh_table(lv_table_create(display_cont))
  , mesh_canvas(lv_canvas_create(display_cont))
  , rotation_cont(nullptr)
  , z_zoom_in_btn(lv_btn_create(display_cont))
  , z_zoom_out_btn(lv_btn_create(display_cont))
  , fov_zoom_in_btn(lv_btn_create(display_cont))
  , fov_zoom_out_btn(lv_btn_create(display_cont))
  , profile_cont(lv_obj_create(top_cont))
  , profile_table(lv_table_create(profile_cont))
  , profile_info(lv_table_create(profile_cont))
  , controls_cont(lv_obj_create(cont))
  , save_btn(controls_cont, &sd_img, "Save Profile", &BedMeshPanel::_handle_callback, this)
  , clear_btn(controls_cont, &delete_img, "Clear Profile", &BedMeshPanel::_handle_callback, this)
  , calibrate_btn(controls_cont, &bedmesh_img, "Calibrate", &BedMeshPanel::_handle_callback, this)
  , toggle_view_btn(controls_cont, &sysinfo_img, "Table View", &BedMeshPanel::_handle_toggle_view, this)
  , back_btn(controls_cont, &back, "Back", &BedMeshPanel::_handle_callback, this)
  , msgbox(lv_obj_create(prompt))
  , input(lv_textarea_create(msgbox))
  , kb(lv_keyboard_create(prompt))
  , current_view_angle(VIEW_ANGLE_Z_DEGREES)
  , canvas_draw_buf(nullptr)
  , show_3d_view(true)
  , z_display_scale(2.4)
  , camera_distance(CAMERA_DISTANCE)
  , mesh_min_x(0.0)
  , mesh_max_x(0.0)
  , mesh_min_y(0.0)
  , mesh_max_y(0.0)
  , is_dragging(false)
  , drag_start_x(0)
  , last_drag_x(0)
  , drag_start_y(0)
  , last_drag_y(0)
  , current_x_angle(VIEW_ANGLE_X_DEGREES)  // Start with the default viewing angle
  , has_multitouch(false)
  , use_gesture_zoom(false)
  , mouse_over_canvas(false)
  , scroll_check_timer(nullptr)
{
  lv_obj_move_background(cont);

  lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_set_width(top_cont, LV_PCT(100));
  lv_obj_clear_flag(top_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(top_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(top_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_flex_grow(top_cont, 1);
  lv_obj_set_style_pad_all(top_cont, 0, 0);

  lv_obj_set_height(display_cont, LV_PCT(100));
  lv_obj_clear_flag(display_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(display_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(display_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_flex_grow(display_cont, 1);
  lv_obj_set_style_pad_all(display_cont, 1, 0);  // Absolute minimal padding for maximum canvas space

  auto screen_width = lv_disp_get_physical_hor_res(NULL);
  if (screen_width < 800) {
    lv_obj_set_style_text_font(mesh_table, &lv_font_montserrat_8, LV_STATE_DEFAULT);
  } else {
    lv_obj_set_style_text_font(mesh_table, &lv_font_montserrat_10, LV_STATE_DEFAULT);
  }

  // mesh table
  lv_obj_set_width(mesh_table, LV_PCT(100));
  lv_obj_set_flex_grow(mesh_table, 1);
  lv_obj_clear_flag(mesh_table, LV_OBJ_FLAG_SCROLLABLE);

  // Setup 3D canvas - start with larger initial size
  auto canvas_width = 400;
  auto canvas_height = 400;

  canvas_draw_buf = lv_mem_alloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(canvas_width, canvas_height));
  lv_canvas_set_buffer(mesh_canvas, canvas_draw_buf, canvas_width, canvas_height, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_size(mesh_canvas, canvas_width, canvas_height);
  lv_obj_set_flex_grow(mesh_canvas, 1);
  lv_obj_set_align(mesh_canvas, LV_ALIGN_CENTER);

  // Fill canvas with dark slate gray background initially
  lv_canvas_fill_bg(mesh_canvas, lv_color_make(40, 40, 40), LV_OPA_COVER);

  spdlog::debug("Initial canvas size: {}x{}", canvas_width, canvas_height);

  // Add resize callback to update canvas size when container changes
  lv_obj_add_event_cb(display_cont, [](lv_event_t *e) {
    BedMeshPanel *panel = static_cast<BedMeshPanel *>(lv_event_get_user_data(e));
    panel->resize_canvas();
  }, LV_EVENT_SIZE_CHANGED, this);

  // Create an invisible overlay button on top of canvas to capture input events
  // Canvas objects don't receive input events well in LVGL
  // Create it as a child of the canvas itself to ensure proper positioning
  lv_obj_t *canvas_overlay = lv_btn_create(mesh_canvas);

  // Position it to cover the entire canvas area
  lv_obj_set_size(canvas_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(canvas_overlay, 0, 0);

  // Make the overlay invisible but clickable
  lv_obj_set_style_bg_opa(canvas_overlay, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(canvas_overlay, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_opa(canvas_overlay, LV_OPA_TRANSP, 0);
  lv_obj_set_style_outline_opa(canvas_overlay, LV_OPA_TRANSP, 0);

  // Remove any padding/margin
  lv_obj_set_style_pad_all(canvas_overlay, 0, 0);

  // Ensure it doesn't interfere with layout
  lv_obj_add_flag(canvas_overlay, LV_OBJ_FLAG_FLOATING);

  // Add drag event handler to the overlay button
  lv_obj_add_event_cb(canvas_overlay, &BedMeshPanel::_handle_canvas_drag, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(canvas_overlay, &BedMeshPanel::_handle_canvas_drag, LV_EVENT_PRESSING, this);
  lv_obj_add_event_cb(canvas_overlay, &BedMeshPanel::_handle_canvas_drag, LV_EVENT_RELEASED, this);

  // Detect input capabilities and configure zoom behavior
#ifdef SIMULATOR
  // In simulation mode, add mouse wheel zoom support via encoder polling
  use_gesture_zoom = true;
  // Create timer to continuously check for mouse wheel input in 3D view mode
  scroll_check_timer = lv_timer_create(&BedMeshPanel::_scroll_check_timer_cb, 50, this);  // Check every 50ms
  spdlog::debug("Simulator mode detected - mouse wheel zoom enabled (use_gesture_zoom={}) with 50ms timer always active", use_gesture_zoom);
#else
  // On real hardware, check for multi-touch support
  has_multitouch = detect_multitouch_support();
  if (has_multitouch) {
    use_gesture_zoom = true;
    lv_obj_add_event_cb(canvas_overlay, &BedMeshPanel::_handle_canvas_gesture, LV_EVENT_GESTURE, this);
    spdlog::debug("Multi-touch detected: pinch zoom enabled");
  } else {
    spdlog::debug("No multi-touch support: using zoom buttons");
  }
#endif


  // Create single controls container for all 3D controls
  rotation_cont = lv_obj_create(display_cont);
  lv_obj_set_size(rotation_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(rotation_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(rotation_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(rotation_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(rotation_cont, 15, 0);  // More padding to prevent slider clipping
  lv_obj_set_style_pad_column(rotation_cont, 10, 0);  // Add spacing between items

  // Move all controls to the single container - Z buttons first, then FOV buttons
  lv_obj_set_parent(z_zoom_out_btn, rotation_cont);  // Z- (minus) first
  lv_obj_set_parent(z_zoom_in_btn, rotation_cont);   // Z+ (plus) second
  lv_obj_set_parent(fov_zoom_out_btn, rotation_cont); // FOV- (minus) third
  lv_obj_set_parent(fov_zoom_in_btn, rotation_cont);  // FOV+ (plus) fourth


  // Setup Z zoom buttons with correct symbols
  setup_button_with_label(z_zoom_in_btn, LV_SYMBOL_PLUS, 40, 30, &BedMeshPanel::_handle_z_zoom_in);
  setup_button_with_label(z_zoom_out_btn, LV_SYMBOL_MINUS, 40, 30, &BedMeshPanel::_handle_z_zoom_out);

  // Setup FOV zoom buttons with correct symbols (hidden for now, kept for future debugging)
  setup_button_with_label(fov_zoom_in_btn, LV_SYMBOL_UP, 40, 30, &BedMeshPanel::_handle_fov_zoom_in);
  setup_button_with_label(fov_zoom_out_btn, LV_SYMBOL_DOWN, 40, 30, &BedMeshPanel::_handle_fov_zoom_out);

  // Hide FOV buttons (debugging only)
  lv_obj_add_flag(fov_zoom_in_btn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(fov_zoom_out_btn, LV_OBJ_FLAG_HIDDEN);

  // Hide Z zoom buttons if gesture zoom is available
  if (use_gesture_zoom) {
    lv_obj_add_flag(z_zoom_in_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(z_zoom_out_btn, LV_OBJ_FLAG_HIDDEN);
    spdlog::debug("Z zoom buttons hidden - using gesture zoom");
  }

  // Start with 3D view by default
  lv_obj_add_flag(mesh_table, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(mesh_canvas, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(rotation_cont, LV_OBJ_FLAG_HIDDEN);

  // profile container
  lv_obj_set_height(profile_cont, LV_PCT(100));
  lv_obj_clear_flag(profile_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(profile_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(profile_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_flex_grow(profile_cont, 1);
  lv_obj_set_style_pad_all(profile_cont, 0, 0);

  // profile table
  lv_obj_add_event_cb(profile_cont, [](lv_event_t *e) {
    lv_obj_t *profile_cont = lv_event_get_target(e);
    lv_obj_t *profile_table = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
    int profile_cont_width = lv_obj_get_width(profile_cont);
    lv_table_set_col_width(profile_table, 0, profile_cont_width * 0.7);
    lv_table_set_col_width(profile_table, 1, profile_cont_width * 0.15);
    lv_table_set_col_width(profile_table, 2, profile_cont_width * 0.15);
    }, LV_EVENT_SIZE_CHANGED, profile_table);
  lv_obj_set_flex_grow(profile_table, 1);
  lv_obj_set_style_pad_all(profile_table, 0, 0);

  // profile info
  lv_obj_add_event_cb(profile_cont, [](lv_event_t *e) {
    lv_obj_t *profile_cont = lv_event_get_target(e);
    lv_obj_t *profile_info = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
    int profile_cont_width = lv_obj_get_width(profile_cont);
    lv_table_set_col_width(profile_info, 0, profile_cont_width * 0.5);
    lv_table_set_col_width(profile_info, 1, profile_cont_width * 0.5);
    }, LV_EVENT_SIZE_CHANGED, profile_info);
  lv_obj_set_flex_grow(profile_info, 1);
  lv_obj_set_style_pad_all(profile_info, 0, 0);
  lv_obj_set_style_pad_top(profile_info, 2, LV_PART_ITEMS | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_bottom(profile_info, 2, LV_PART_ITEMS | LV_STATE_DEFAULT);

  // button controls
  lv_obj_set_size(controls_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_align(controls_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_flex_flow(controls_cont, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(controls_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(controls_cont, 0, 0);

  lv_obj_set_flex_grow(save_btn.get_container(), 1);
  lv_obj_set_flex_grow(clear_btn.get_container(), 1);
  lv_obj_set_flex_grow(calibrate_btn.get_container(), 1);
  lv_obj_set_flex_grow(toggle_view_btn.get_container(), 1);
  lv_obj_set_flex_grow(back_btn.get_container(), 1);

  lv_obj_add_event_cb(mesh_table, &BedMeshPanel::_mesh_draw_cb, LV_EVENT_DRAW_PART_BEGIN, this);
  lv_obj_add_event_cb(profile_table, &BedMeshPanel::_handle_profile_action, LV_EVENT_VALUE_CHANGED, this);

  // prompt
  lv_obj_set_style_pad_all(prompt, 0, 0);
  lv_obj_set_size(prompt, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(prompt, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(prompt, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_bg_opa(prompt, LV_OPA_70, 0);

  lv_textarea_set_one_line(input, true);
  lv_obj_set_width(input, LV_PCT(100));

  // lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_flex_flow(msgbox, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(msgbox, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(msgbox, 25, 0);

  lv_obj_clear_flag(msgbox, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_size(msgbox, LV_PCT(60), LV_PCT(40));
  lv_obj_set_style_border_width(msgbox, 2, 0);
  lv_obj_set_style_bg_color(msgbox, lv_palette_darken(LV_PALETTE_GREY, 1), 0);
  lv_obj_align(msgbox, LV_ALIGN_TOP_MID, 0, 20);

  lv_obj_t *label = NULL;

  label = lv_label_create(msgbox);
  lv_obj_set_width(label, LV_PCT(100));
  lv_label_set_text(label, "Saving the profile will restart the printer.");

  lv_obj_t *prompt_save_btn = lv_btn_create(msgbox);
  lv_obj_t *prompt_cancel_btn = lv_btn_create(msgbox);

  label = lv_label_create(prompt_save_btn);
  lv_label_set_text(label, "Save");

  label = lv_label_create(prompt_cancel_btn);
  lv_label_set_text(label, "Cancel");
  lv_obj_center(label);

  lv_obj_add_event_cb(prompt_save_btn, &BedMeshPanel::_handle_prompt_save, LV_EVENT_CLICKED, this);

  lv_obj_add_event_cb(prompt_cancel_btn, &BedMeshPanel::_handle_prompt_cancel, LV_EVENT_CLICKED, this);

  lv_obj_add_event_cb(input, &BedMeshPanel::_handle_kb_input, LV_EVENT_ALL, this);
  lv_keyboard_set_textarea(kb, input);

  lv_obj_move_background(prompt);

  ws.register_notify_update(this);
}

BedMeshPanel::~BedMeshPanel() {
  if (canvas_draw_buf != nullptr) {
    lv_mem_free(canvas_draw_buf);
    canvas_draw_buf = nullptr;
  }

#ifdef SIMULATOR
  if (scroll_check_timer != nullptr) {
    lv_timer_del(scroll_check_timer);
    scroll_check_timer = nullptr;
    spdlog::debug("Scroll check timer cleaned up");
  }
#endif

  if (cont != NULL) {
    lv_obj_del(cont);
    cont = NULL;
  }

  if (prompt != NULL) {
    lv_obj_del(prompt);
    prompt = NULL;
  }
}

void BedMeshPanel::consume(json &j) {
  auto bm = j["/params/0/bed_mesh"_json_pointer];
  if (!bm.is_null()) {
    spdlog::trace("bedmesh panel consume {}", bm["/profiles"_json_pointer].dump());
    refresh_views_with_lock(bm);
  }
}

void BedMeshPanel::refresh_views_with_lock(json &bm) {
  std::lock_guard<std::mutex> lock(lv_lock); // more grandular?
  refresh_views(bm);
}

void BedMeshPanel::refresh_views(json &bm) {
  if (!bm.is_null()) {
    auto active_profile_j = bm["/profile_name"_json_pointer];
    size_t row_idx = 0;
    if (active_profile_j.is_null()) {
      save_btn.disable();
      return;
    }

    active_profile = active_profile_j.template get<std::string>();
    if (active_profile.length() > 0) {
      save_btn.enable();

      refresh_profile_info(active_profile);

      auto mesh_json = bm["/probed_matrix"_json_pointer];
      if (mesh_json.is_null()) {
        mesh_json = State::get_instance()->get_data("/printer_state/bed_mesh/probed_matrix"_json_pointer);
      }

      mesh = mesh_json.template get<std::vector<std::vector<double>>>();
      spdlog::debug("Loaded mesh data: {}x{} points", mesh.size(), mesh.empty() ? 0 : mesh[0].size());

      // Draw the 3D mesh
      draw_3d_mesh();

      // Force complete layout update chain to ensure proper table sizing
      lv_obj_update_layout(cont);  // Update main container first
      lv_obj_update_layout(top_cont);  // Then top container
      lv_obj_update_layout(display_cont);  // Then display container

      // Add a small delay to let the layout settle completely
      // lv_timer_t* timer = lv_timer_create([](lv_timer_t* timer) {
      //   BedMeshPanel* panel = static_cast<BedMeshPanel*>(timer->user_data);
      //   panel->delayed_table_setup();
      //   lv_timer_del(timer);
      // }, 10, this);

      // calculate cell width
      if (mesh.size() > 0 && mesh[0].size() > 0) {
        // Force table to use available height
        auto display_height = lv_obj_get_height(display_cont);
        auto target_table_height = display_height - 60; // Leave room for controls
        lv_obj_set_height(mesh_table, target_table_height);

        auto height = lv_obj_get_height(mesh_table);
        auto width = lv_obj_get_width(mesh_table);
        spdlog::debug("Table dimensions: {}x{} (forced to {}), display_cont height: {}, mesh size: {}x{}", width, height, target_table_height, display_height, mesh[0].size(), mesh.size());

        // Adjust spacing based on mesh size
        bool is_large_mesh = (mesh.size() > 6 || mesh[0].size() > 6);

        int cel_height, col_width;
        // Calculate actual row height needed: total height divided by number of rows
        // Each row needs space for text + top padding + bottom padding
        int available_row_height = height / mesh.size();

        if (is_large_mesh) {
          // Minimal padding for large meshes to fit more data
          cel_height = std::max(1, available_row_height / 3);
          col_width = std::max(35, (int)(width / mesh[0].size()));
        } else {
          // More comfortable padding for small meshes
          cel_height = std::max(2, available_row_height / 2);
          col_width = std::max(4, (int)(width / mesh[0].size()));
        }

        lv_obj_set_style_pad_top(mesh_table, cel_height, LV_PART_ITEMS | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_bottom(mesh_table, cel_height, LV_PART_ITEMS | LV_STATE_DEFAULT);

        // Reduce left/right padding for large meshes to prevent text wrapping
        if (is_large_mesh) {
          lv_obj_set_style_pad_left(mesh_table, 1, LV_PART_ITEMS | LV_STATE_DEFAULT);
          lv_obj_set_style_pad_right(mesh_table, 1, LV_PART_ITEMS | LV_STATE_DEFAULT);
        } else {
          lv_obj_set_style_pad_left(mesh_table, 3, LV_PART_ITEMS | LV_STATE_DEFAULT);
          lv_obj_set_style_pad_right(mesh_table, 3, LV_PART_ITEMS | LV_STATE_DEFAULT);
        }

        lv_table_set_col_cnt(mesh_table, mesh[0].size());
        for (int i = 0; i < mesh[0].size(); i++) {
          lv_table_set_col_width(mesh_table, i, col_width);
        }
      }

      // Use minimum 10pt font for readability, only drop to 8pt for very large meshes
      if (mesh.size() > 9 || mesh[0].size() > 9) {
        lv_obj_set_style_text_font(mesh_table, &lv_font_montserrat_8, LV_STATE_DEFAULT);
      } else {
        lv_obj_set_style_text_font(mesh_table, &lv_font_montserrat_10, LV_STATE_DEFAULT);
      }

      for (int i = mesh.size() - 1; i >= 0; i--) {
        auto row = mesh[i];
        for (int j = 0; j < row.size(); j++) {
          // Use shorter format for large meshes to reduce wrapping
          const char* format = (mesh.size() > 6 || mesh[0].size() > 6) ? "{:.1f}" : "{:.2f}";
          lv_table_set_cell_value(mesh_table, row_idx, j, fmt::format(format, row[j]).c_str());
        }
        row_idx++;
      }
      lv_table_set_row_cnt(mesh_table, row_idx);
    } else {
      // no active profile, hide mesh matrix
      lv_obj_add_flag(mesh_table, LV_OBJ_FLAG_HIDDEN);
      save_btn.disable();
    }

    // populate profiles tables
    auto profiles = bm["/profiles"_json_pointer];
    if (profiles.is_null()) {
      profiles = State::get_instance()->get_data("/printer_state/bed_mesh/profiles"_json_pointer);
    }

    spdlog::trace("active {}, profiles is {}", active_profile, profiles.dump());

    if (profiles.size() > 0) {
      lv_obj_clear_flag(profile_cont, LV_OBJ_FLAG_HIDDEN);

      row_idx = 0;
      for (auto &el : profiles.items()) {
        lv_table_clear_cell_ctrl(profile_table, row_idx, 0, LV_TABLE_CELL_CTRL_MERGE_RIGHT);

        bool is_active = el.key() == active_profile;
        if (is_active) {
          lv_table_add_cell_ctrl(profile_table, row_idx, 0, LV_TABLE_CELL_CTRL_MERGE_RIGHT);
          lv_table_set_cell_value(profile_table, row_idx, 1, "");
        } else {
          lv_table_set_cell_value(profile_table, row_idx, 1, LV_SYMBOL_UPLOAD);
        }

        lv_table_set_cell_value(profile_table, row_idx, 0, el.key().c_str());
        lv_table_set_cell_value(profile_table, row_idx, 2, LV_SYMBOL_CLOSE);
        row_idx++;
      }
      lv_table_set_row_cnt(profile_table, row_idx);
    } else {
      // no profiles, hide profile table
      lv_obj_add_flag(profile_cont, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void BedMeshPanel::refresh_profile_info(std::string profile) {
  auto mesh_params = State::get_instance()->get_data(json::json_pointer(
    fmt::format("/printer_state/bed_mesh/profiles/{}/mesh_params", profile)));
  spdlog::trace("refreshing profile info {}, {}", profile, mesh_params.dump());

  if (!mesh_params.is_null()) {
    uint32_t rowidx = 0;
    auto &v = mesh_params["/algo"_json_pointer];
    if (!v.is_null()) {
      lv_table_set_cell_value(profile_info, rowidx, 0, "Algorithm");
      lv_table_set_cell_value(profile_info, rowidx, 1, v.template get<std::string>().c_str());
      rowidx++;
    }

    v = mesh_params["/tension"_json_pointer];
    if (!v.is_null()) {
      lv_table_set_cell_value(profile_info, rowidx, 0, "Tension");
      lv_table_set_cell_value(profile_info, rowidx, 1, fmt::format("{}", v.template get<double>()).c_str());
      rowidx++;
    }

    std::vector<int> xvalues;
    std::vector<const char*> x_params = {"min_x", "max_x", "x_count", "mesh_x_pps"};
    extract_mesh_parameter_values(mesh_params, x_params, xvalues);

    v = mesh_params["/min_x"_json_pointer];
    if (!v.is_null()) {
      mesh_min_x = v.template get<double>();
    }
    v = mesh_params["/max_x"_json_pointer];
    if (!v.is_null()) {
      mesh_max_x = v.template get<double>();
    }

    lv_table_set_cell_value(profile_info, rowidx, 0, "X (min, max, count, pps)");
    lv_table_set_cell_value(profile_info, rowidx, 1, fmt::format("{}", fmt::join(xvalues, ", ")).c_str());
    rowidx++;

    std::vector<int> yvalues;
    std::vector<const char*> y_params = {"min_y", "max_y", "y_count", "mesh_y_pps"};
    extract_mesh_parameter_values(mesh_params, y_params, yvalues);

    v = mesh_params["/min_y"_json_pointer];
    if (!v.is_null()) {
      mesh_min_y = v.template get<double>();
    }
    v = mesh_params["/max_y"_json_pointer];
    if (!v.is_null()) {
      mesh_max_y = v.template get<double>();
    }

    lv_table_set_cell_value(profile_info, rowidx, 0, "Y (min, max, count, pps)");
    lv_table_set_cell_value(profile_info, rowidx, 1, fmt::format("{}", fmt::join(yvalues, ", ")).c_str());
    rowidx++;
  }
}

void BedMeshPanel::foreground() {
  auto bm = State::get_instance()->get_data("/printer_state/bed_mesh"_json_pointer);
  spdlog::trace("bm {}", bm.dump());
  refresh_views(bm);

  lv_obj_move_foreground(cont);
}

void BedMeshPanel::handle_callback(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);
  if (btn == save_btn.get_container()) {
    spdlog::trace("mesh save pressed");
    lv_obj_clear_flag(prompt, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);

    if (active_profile.length() > 0) {
      lv_textarea_set_text(input, active_profile.c_str());
    }

    lv_obj_move_foreground(prompt);

  } else if (btn == clear_btn.get_container()) {
    spdlog::trace("mesh clear pressed");
    ws.gcode_script("BED_MESH_CLEAR");

  } else if (btn == calibrate_btn.get_container()) {
    spdlog::trace("mesh calibrate pressed");
    auto v = State::get_instance()
      ->get_data("/printer_state/toolhead/homed_axes"_json_pointer);
    if (!v.is_null()) {
      if (v.template get<std::string>() == "xy") {
        ws.gcode_script("BED_MESH_CALIBRATE");
        return;
      }
    }
    ws.gcode_script("G28 X Y Z\nBED_MESH_CALIBRATE");

  } else if (btn == back_btn.get_container()) {
    spdlog::trace("back button pressed");
    lv_obj_move_background(cont);
  }
}

void BedMeshPanel::handle_profile_action(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_VALUE_CHANGED) {
    uint16_t row;
    uint16_t col;

    lv_table_get_selected_cell(profile_table, &row, &col);
    uint16_t row_count = lv_table_get_row_cnt(profile_table);
    if (row == LV_TABLE_CELL_NONE || col == LV_TABLE_CELL_NONE || row >= row_count) {
      return;
    }
    const char *selected = lv_table_get_cell_value(profile_table, row, col);
    const char *profile_name = lv_table_get_cell_value(profile_table, row, 0);
    spdlog::trace("selected {}, {}, value {}", row, col, profile_name);
    if (col == 2) {
      // delete profile
      spdlog::trace("delete mesh {}", profile_name);
      ws.gcode_script(fmt::format("BED_MESH_PROFILE REMOVE=\"{}\"\nSAVE_CONFIG", profile_name));
    } else if (col == 1 && selected != NULL && strlen(selected) != 0) {
      // load profile
      spdlog::trace("selected {}, load mesh {}", strlen(selected), profile_name);
      ws.gcode_script(fmt::format("BED_MESH_PROFILE LOAD=\"{}\"", profile_name));

      // // populate profile info
      // refresh_profile_info(profile_name);
    } else if (col == 0) {
      // display mesh info and refresh bed mesh matrix
    }

  }
}

void BedMeshPanel::handle_prompt_save(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED) {
    const char *profile_name = lv_textarea_get_text(input);
    if (profile_name == NULL || strlen(profile_name) == 0) {
      return;
    }

    ws.gcode_script(fmt::format("BED_MESH_PROFILE SAVE=\"{}\"\nSAVE_CONFIG", profile_name));

    lv_textarea_set_text(input, "");
    lv_obj_add_flag(prompt, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(prompt);
  }
}

void BedMeshPanel::handle_prompt_cancel(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_CLICKED) {
    lv_obj_add_flag(prompt, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(prompt);
  }
}


void BedMeshPanel::handle_kb_input(lv_event_t *e)
{
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) {
    const char *profile_name = lv_textarea_get_text(input);
    if (profile_name == NULL || strlen(profile_name) == 0) {
      return;
    }

    ws.gcode_script(fmt::format("BED_MESH_PROFILE SAVE=\"{}\"\nSAVE_CONFIG", profile_name));

    lv_textarea_set_text(input, "");
    lv_obj_add_flag(prompt, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(prompt);
  }
}

void BedMeshPanel::mesh_draw_cb(lv_event_t *e)
{
  lv_obj_t *obj = lv_event_get_target(e);
  lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
  if (dsc->part == LV_PART_ITEMS && !mesh.empty()) {
    uint32_t row = dsc->id / lv_table_get_col_cnt(obj);
    uint32_t col = dsc->id - row * lv_table_get_col_cnt(obj);

    dsc->label_dsc->align = LV_TEXT_ALIGN_CENTER;
    dsc->label_dsc->color = lv_palette_darken(LV_PALETTE_GREY, 3);

    // rows of the mesh is reversed
    int32_t reversed_row_idx = mesh.size() - row - 1;
    double offset = mesh[reversed_row_idx][col];
    lv_color_t color = color_gradient(offset);

    dsc->rect_dsc->bg_color = color;
    dsc->rect_dsc->bg_opa = LV_OPA_90;
  }
}

void BedMeshPanel::resize_canvas()
{
  if (canvas_draw_buf == nullptr) {
    return;
  }

  // Get available space for canvas (leave room for controls)
  auto cont_width = lv_obj_get_width(display_cont); // No padding - use full width
  auto cont_height = lv_obj_get_height(display_cont) - 80; // Leave space for controls

  // Use full width with proper padding
  auto canvas_width = std::max(300, cont_width);
  auto canvas_height = std::max(300, std::min(cont_height, canvas_width)); // Height limited by width or container

  // Only resize if significantly different to avoid constant redraws
  auto current_width = lv_obj_get_width(mesh_canvas);
  auto current_height = lv_obj_get_height(mesh_canvas);

  if (abs(canvas_width - current_width) > 10 || abs(canvas_height - current_height) > 10) {
    // Free old buffer and create new one
    lv_mem_free(canvas_draw_buf);
    canvas_draw_buf = lv_mem_alloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(canvas_width, canvas_height));
    lv_canvas_set_buffer(mesh_canvas, canvas_draw_buf, canvas_width, canvas_height, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(mesh_canvas, canvas_width, canvas_height);

    // Fill with dark slate gray background
    lv_canvas_fill_bg(mesh_canvas, lv_color_make(40, 40, 40), LV_OPA_COVER);

    spdlog::debug("Resized canvas from {}x{} to {}x{}", current_width, current_height, canvas_width, canvas_height);

    // Redraw with new size
    if (show_3d_view && !mesh.empty()) {
      draw_3d_mesh();
    }
  }
}

void BedMeshPanel::draw_mesh_surface(const std::vector<std::vector<BedMeshPanel::Point3D>>& points,
                                    int rows, int cols, int canvas_width, int canvas_height,
                                    double min_z, double max_z)
{
  // Draw surface quads with proper size and connection
  for (int row = 0; row < rows - 1; row++) {
    for (int col = 0; col < cols - 1; col++) {
      // Get the four corner points of the current quad
      const auto& p1 = points[row][col];         // bottom-left
      const auto& p2 = points[row][col+1];       // bottom-right
      const auto& p3 = points[row+1][col];       // top-left
      const auto& p4 = points[row+1][col+1];     // top-right

      // Calculate average height for color
      double avg_height = (mesh[row][col] + mesh[row][col+1] +
                          mesh[row+1][col] + mesh[row+1][col+1]) / 4.0;

      lv_color_t surface_color = color_gradient_enhanced(avg_height, min_z, max_z);

      // Create a larger, more connected surface quad
      int min_x = std::min({p1.screen_x, p2.screen_x, p3.screen_x, p4.screen_x});
      int max_x = std::max({p1.screen_x, p2.screen_x, p3.screen_x, p4.screen_x});
      int min_y = std::min({p1.screen_y, p2.screen_y, p3.screen_y, p4.screen_y});
      int max_y = std::max({p1.screen_y, p2.screen_y, p3.screen_y, p4.screen_y});

      // Expand the quad to ensure surface connection
      int width = std::max(8, max_x - min_x + 4);   // Minimum width for visibility
      int height = std::max(8, max_y - min_y + 4);  // Minimum height for visibility

      // Center the expanded quad
      int center_x = (min_x + max_x) / 2;
      int center_y = (min_y + max_y) / 2;
      min_x = center_x - width / 2;
      min_y = center_y - height / 2;

      // Check bounds
      if (min_x >= 0 && min_y >= 0 &&
          min_x + width < canvas_width && min_y + height < canvas_height) {

        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.bg_color = surface_color;
        rect_dsc.bg_opa = LV_OPA_90;
        rect_dsc.border_width = 0;

        lv_canvas_draw_rect(mesh_canvas, min_x, min_y, width, height, &rect_dsc);
      }
    }
  }
}

void BedMeshPanel::draw_mesh_wireframe(const std::vector<std::vector<BedMeshPanel::Point3D>>& points,
                                      int rows, int cols, int canvas_width, int canvas_height)
{
  lv_draw_line_dsc_t line_dsc;
  lv_draw_line_dsc_init(&line_dsc);
  line_dsc.color = lv_color_make(80, 80, 80); // Dark gray wireframe
  line_dsc.width = 1;
  line_dsc.opa = LV_OPA_60;

  // Draw horizontal grid lines
  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols - 1; col++) {
      const auto& p1 = points[row][col];
      const auto& p2 = points[row][col + 1];

      // Check bounds
      if (p1.screen_x >= 0 && p1.screen_x < canvas_width && p1.screen_y >= 0 && p1.screen_y < canvas_height &&
          p2.screen_x >= 0 && p2.screen_x < canvas_width && p2.screen_y >= 0 && p2.screen_y < canvas_height) {
        lv_point_t line_points[2] = {{p1.screen_x, p1.screen_y}, {p2.screen_x, p2.screen_y}};
        lv_canvas_draw_line(mesh_canvas, line_points, 2, &line_dsc);
      }
    }
  }

  // Draw vertical grid lines
  for (int col = 0; col < cols; col++) {
    for (int row = 0; row < rows - 1; row++) {
      const auto& p1 = points[row][col];
      const auto& p2 = points[row + 1][col];

      // Check bounds
      if (p1.screen_x >= 0 && p1.screen_x < canvas_width && p1.screen_y >= 0 && p1.screen_y < canvas_height &&
          p2.screen_x >= 0 && p2.screen_x < canvas_width && p2.screen_y >= 0 && p2.screen_y < canvas_height) {
        lv_point_t line_points[2] = {{p1.screen_x, p1.screen_y}, {p2.screen_x, p2.screen_y}};
        lv_canvas_draw_line(mesh_canvas, line_points, 2, &line_dsc);
      }
    }
  }
}

void BedMeshPanel::draw_axes_and_labels(const std::vector<std::vector<BedMeshPanel::Point3D>>& points,
                                       int rows, int cols, int canvas_width, int canvas_height,
                                       double min_z, double max_z, double z_scale)
{
  // Draw coordinate axes and labels with wireframe style
  lv_draw_line_dsc_t axis_dsc;
  lv_draw_line_dsc_init(&axis_dsc);
  axis_dsc.color = lv_color_make(160, 160, 160); // Slightly darker gray
  axis_dsc.width = 1; // Thin lines
  axis_dsc.opa = LV_OPA_70; // More visible but still subtle

  lv_draw_label_dsc_t label_dsc;
  lv_draw_label_dsc_init(&label_dsc);
  label_dsc.color = lv_color_white();
  label_dsc.font = &lv_font_montserrat_12;


  // Draw Z-axis (vertical) - connect to X/Y axes origin with centered Z range
  // Use the mesh corner coordinates for X/Y positioning (same as X/Y axes)
  double mesh_origin_x = (0 - cols / 2.0) * MESH_SCALE;
  double mesh_origin_y = (0 - rows / 2.0) * MESH_SCALE;

  // Draw wireframe grid for X-Y plane at the same Z level as the bottom of the Z-axis (minimum Z value)
  double z_range = max_z - min_z;
  double grid_reference_z = (-z_range * Z_AXIS_EXTENSION) * z_scale; // At the bottom of the visualization
  spdlog::debug("Drawing grid at Z={}, z_range={}, z_scale={}, (matches Z-axis bottom/min)", grid_reference_z, z_range, z_scale);

  // Draw grid lines parallel to X-axis and Y-axis
  draw_grid_lines_parallel_to_axis(rows, false, rows, cols, grid_reference_z, canvas_width, canvas_height, axis_dsc);
  draw_grid_lines_parallel_to_axis(cols, true, rows, cols, grid_reference_z, canvas_width, canvas_height, axis_dsc);

  // Draw wireframe grid for X/Z plane (vertical plane at Y edge)
  double y_edge = (0 - rows / 2.0) * MESH_SCALE; // Front edge of mesh (minimum Y)
  for (int col = 0; col < cols; col++) {
    double grid_x = (col - cols / 2.0) * MESH_SCALE;
    Point3D start = project_3d_to_2d(grid_x, y_edge, (-z_range * Z_AXIS_EXTENSION) * z_scale, canvas_width, canvas_height);
    Point3D end = project_3d_to_2d(grid_x, y_edge, (z_range * Z_AXIS_EXTENSION) * z_scale, canvas_width, canvas_height);


    lv_point_t grid_line[2] = {{start.screen_x, start.screen_y}, {end.screen_x, end.screen_y}};
    lv_canvas_draw_line(mesh_canvas, grid_line, 2, &axis_dsc);
  }

  // Add horizontal lines to complete X/Z plane wireframe with multiple Z divisions
  double x_left = (0 - cols / 2.0) * MESH_SCALE;
  double x_right = ((cols-1) - cols / 2.0) * MESH_SCALE;

  // Use same number of Z divisions as X/Y grid divisions
  int z_divisions = std::max(rows, cols);
  double z_min = (-z_range * Z_AXIS_EXTENSION) * z_scale;
  double z_max = (z_range * Z_AXIS_EXTENSION) * z_scale;

  for (int z_div = 0; z_div <= z_divisions; z_div++) {
    double z_level = z_min + (z_div * (z_max - z_min) / z_divisions);

    Point3D xz_left = project_3d_to_2d(x_left, y_edge, z_level, canvas_width, canvas_height);
    Point3D xz_right = project_3d_to_2d(x_right, y_edge, z_level, canvas_width, canvas_height);
    lv_point_t xz_line[2] = {{xz_left.screen_x, xz_left.screen_y}, {xz_right.screen_x, xz_right.screen_y}};
    lv_canvas_draw_line(mesh_canvas, xz_line, 2, &axis_dsc);
  }

  // Draw wireframe grid for Y/Z plane (vertical plane at X edge)
  double x_edge = (0 - cols / 2.0) * MESH_SCALE; // Left edge of mesh (minimum X)
  for (int row = 0; row < rows; row++) {
    double grid_y = (row - rows / 2.0) * MESH_SCALE;
    Point3D start = project_3d_to_2d(x_edge, grid_y, (-z_range * Z_AXIS_EXTENSION) * z_scale, canvas_width, canvas_height);
    Point3D end = project_3d_to_2d(x_edge, grid_y, (z_range * Z_AXIS_EXTENSION) * z_scale, canvas_width, canvas_height);


    lv_point_t grid_line[2] = {{start.screen_x, start.screen_y}, {end.screen_x, end.screen_y}};
    lv_canvas_draw_line(mesh_canvas, grid_line, 2, &axis_dsc);
  }

  // Add horizontal lines to complete Y/Z plane wireframe with multiple Z divisions
  double y_front = (0 - rows / 2.0) * MESH_SCALE;
  double y_back = ((rows-1) - rows / 2.0) * MESH_SCALE;

  for (int z_div = 0; z_div <= z_divisions; z_div++) {
    double z_level = z_min + (z_div * (z_max - z_min) / z_divisions);

    Point3D yz_front = project_3d_to_2d(x_edge, y_front, z_level, canvas_width, canvas_height);
    Point3D yz_back = project_3d_to_2d(x_edge, y_back, z_level, canvas_width, canvas_height);
    lv_point_t yz_line[2] = {{yz_front.screen_x, yz_front.screen_y}, {yz_back.screen_x, yz_back.screen_y}};
    lv_canvas_draw_line(mesh_canvas, yz_line, 2, &axis_dsc);
  }

  // Z-axis using extended range for better visibility
  Point3D z_bottom = project_3d_to_2d(mesh_origin_x, mesh_origin_y, (-z_range * Z_AXIS_EXTENSION) * z_scale, canvas_width, canvas_height);
  Point3D z_top = project_3d_to_2d(mesh_origin_x, mesh_origin_y, (z_range * Z_AXIS_EXTENSION) * z_scale, canvas_width, canvas_height);

  // Check if both points are reasonably within view (with culling margin)
  if (z_bottom.screen_x >= -CULLING_MARGIN && z_bottom.screen_x < canvas_width + CULLING_MARGIN &&
      z_bottom.screen_y >= -CULLING_MARGIN && z_bottom.screen_y < canvas_height + CULLING_MARGIN &&
      z_top.screen_x >= -CULLING_MARGIN && z_top.screen_x < canvas_width + CULLING_MARGIN &&
      z_top.screen_y >= -CULLING_MARGIN && z_top.screen_y < canvas_height + CULLING_MARGIN) {

    lv_point_t z_line[2] = {{z_bottom.screen_x, z_bottom.screen_y}, {z_top.screen_x, z_top.screen_y}};
    lv_canvas_draw_line(mesh_canvas, z_line, 2, &axis_dsc);

    // Draw X and Y axis lines
    draw_horizontal_axes(mesh_origin_x, mesh_origin_y, grid_reference_z, rows, cols, canvas_width, canvas_height, axis_dsc);

    // Add Z-axis labels along the front left edge (min X, max Y)
    lv_draw_label_dsc_t z_label_dsc;
    lv_draw_label_dsc_init(&z_label_dsc);
    z_label_dsc.color = lv_color_white();
    z_label_dsc.font = &lv_font_montserrat_10;

    lv_draw_label_dsc_t z_axis_label_dsc;
    lv_draw_label_dsc_init(&z_axis_label_dsc);
    z_axis_label_dsc.color = lv_color_white();
    z_axis_label_dsc.font = &lv_font_montserrat_16;

    double left_x = (0 - cols / 2.0) * MESH_SCALE;
    double front_y = ((rows-1) - rows / 2.0) * MESH_SCALE;

    int z_divisions = std::max(rows, cols);
    double z_min = (-z_range * Z_AXIS_EXTENSION) * z_scale;
    double z_max = (z_range * Z_AXIS_EXTENSION) * z_scale;

    // Draw Z-axis tick values
    draw_z_axis_tick_values(left_x, front_y, z_min, z_max, z_divisions, z_scale, canvas_width, canvas_height, z_label_dsc, min_z, max_z);

    // Draw Z-axis label
    double middle_z_level = z_min + ((z_divisions/2) * (z_max - z_min) / z_divisions);
    draw_axis_label("Z", left_x - TICK_MARK_LENGTH * 6, front_y, middle_z_level, -10, -10, canvas_width, canvas_height, z_axis_label_dsc);



  }

  // Add mesh info (left-justified) and Z-range info (right-justified) on same line using smaller font
  char mesh_info[64];
  snprintf(mesh_info, sizeof(mesh_info), "Mesh: %dx%d", cols, rows);

  char z_info[64];
  snprintf(z_info, sizeof(z_info), "Z: %.3f to %.3f", min_z, max_z);

  // Use smaller font for both labels
  lv_draw_label_dsc_t small_label_dsc;
  lv_draw_label_dsc_init(&small_label_dsc);
  small_label_dsc.color = lv_color_white();
  small_label_dsc.font = &lv_font_montserrat_10;

  // Left-justified mesh info
  lv_area_t mesh_info_area;
  if (create_and_check_label_area(mesh_info_area, 10, canvas_height - 25, 100, LABEL_HEIGHT, canvas_width, canvas_height)) {
    lv_canvas_draw_text(mesh_canvas, mesh_info_area.x1, mesh_info_area.y1, 100, &small_label_dsc, mesh_info);
  }

  // Right-justified Z info
  lv_draw_label_dsc_t right_label_dsc;
  lv_draw_label_dsc_init(&right_label_dsc);
  right_label_dsc.color = lv_color_white();
  right_label_dsc.font = &lv_font_montserrat_10;

#ifndef CROSS_COMPILE
  // Debug info for simulation mode: show current viewing angles at top
  char debug_info[128];
  // The Y rotation is fixed at 0° since we only control X (tilt) and Z (spin) rotations
  snprintf(debug_info, sizeof(debug_info), "View: X=%.1f° Y=0.0° Z=%.1f°",
           current_x_angle, (double)current_view_angle);

  lv_draw_label_dsc_t debug_label_dsc;
  lv_draw_label_dsc_init(&debug_label_dsc);
  debug_label_dsc.color = lv_color_make(255, 255, 0); // Yellow for visibility
  debug_label_dsc.font = &lv_font_montserrat_10;

  lv_area_t debug_area;
  if (create_and_check_label_area(debug_area, 10, 10, 200, LABEL_HEIGHT, canvas_width, canvas_height)) {
    lv_canvas_draw_text(mesh_canvas, debug_area.x1, debug_area.y1, 200, &debug_label_dsc, debug_info);
  }
#endif
  right_label_dsc.align = LV_TEXT_ALIGN_RIGHT;

  lv_area_t z_info_area;
  if (create_and_check_label_area(z_info_area, canvas_width - 160, canvas_height - 25, 150, LABEL_HEIGHT, canvas_width, canvas_height)) {
    lv_canvas_draw_text(mesh_canvas, z_info_area.x1, z_info_area.y1, 150, &right_label_dsc, z_info);
  }
}

// Convert 3D world coordinates to 2D screen coordinates using perspective projection
BedMeshPanel::Point3D BedMeshPanel::project_3d_to_2d(double x, double y, double z, int canvas_width, int canvas_height)
{
  // Camera parameters for perspective projection using dynamic camera distance
  double fov_scale = this->fov_scale;

  // Apply user-controlled Z-axis rotation (rotate around vertical axis in the horizontal X/Y plane)
  double z_angle = current_view_angle * M_PI / 180.0;
  double cos_z = cos(z_angle);
  double sin_z = sin(z_angle);

  // Rotate around Z-axis (this rotates in the X/Y horizontal plane)
  double rotated_x = x * cos_z - y * sin_z;
  double rotated_y = x * sin_z + y * cos_z;
  double rotated_z = z * z_display_scale; // Apply Z display scaling

  // Apply dynamic X-axis rotation controlled by user
  double x_angle = current_x_angle * M_PI / 180.0;
  double cos_x = cos(x_angle);
  double sin_x = sin(x_angle);

  double final_x = rotated_x;
  double final_y = rotated_y * cos_x + rotated_z * sin_x;  // Fixed: + instead of - for Z component
  double final_z = rotated_y * sin_x - rotated_z * cos_x;  // Fixed: - instead of + for Z component

  // Move camera back to avoid clipping
  final_z += this->camera_distance;

  // Perspective projection: project 3D to 2D using similar triangles
  // screen_pos = (3d_pos * focal_length) / depth
  double perspective_x = (final_x * fov_scale) / final_z;
  double perspective_y = (final_y * fov_scale) / final_z;

  // Convert to screen coordinates with offset for gradient band space
  // Center mesh in the available space (canvas minus gradient band area)
  int effective_width = canvas_width - GRADIENT_BAND_TOTAL_WIDTH;
  int screen_x = GRADIENT_BAND_TOTAL_WIDTH + effective_width / 2 + (int)perspective_x;
  int screen_y = canvas_height * Z_ORIGIN_VERTICAL_POSITION + (int)perspective_y;

  Point3D result;
  result.x = final_x;
  result.y = final_y;
  result.z = final_z;
  result.screen_x = screen_x;
  result.screen_y = screen_y;
  result.depth = final_z;  // Store depth for sorting (larger = further away)

  return result;
}

// Triangle rasterization using scan-line algorithm for proper 3D polygon filling
void BedMeshPanel::fill_triangle(int x1, int y1, int x2, int y2, int x3, int y3, lv_color_t color, int canvas_width, int canvas_height)
{
  // Sort vertices by Y coordinate (y1 <= y2 <= y3)
  if (y1 > y2) { std::swap(x1, x2); std::swap(y1, y2); }
  if (y2 > y3) { std::swap(x2, x3); std::swap(y2, y3); }
  if (y1 > y2) { std::swap(x1, x2); std::swap(y1, y2); }

  // Skip degenerate triangles
  if (y1 == y3) return;

  // For each scanline, interpolate left and right X coordinates
  for (int y = std::max(0, y1); y <= std::min(canvas_height - 1, y3); y++) {
    int x_left, x_right;

    if (y < y2) {
      // Upper half of triangle
      if (y2 != y1) {
        x_left = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
      } else {
        x_left = x1;
      }
      if (y3 != y1) {
        x_right = x1 + (x3 - x1) * (y - y1) / (y3 - y1);
      } else {
        x_right = x1;
      }
    } else {
      // Lower half of triangle
      if (y3 != y2) {
        x_left = x2 + (x3 - x2) * (y - y2) / (y3 - y2);
      } else {
        x_left = x2;
      }
      if (y3 != y1) {
        x_right = x1 + (x3 - x1) * (y - y1) / (y3 - y1);
      } else {
        x_right = x1;
      }
    }

    // Ensure x_left <= x_right
    if (x_left > x_right) std::swap(x_left, x_right);

    // Clamp to canvas bounds and draw horizontal line
    x_left = std::max(0, x_left);
    x_right = std::min(canvas_width - 1, x_right);

    if (x_left <= x_right) {
      // Draw horizontal line using LVGL rect (width 1 for line)
      lv_draw_rect_dsc_t rect_dsc;
      lv_draw_rect_dsc_init(&rect_dsc);
      rect_dsc.bg_color = color;
      rect_dsc.bg_opa = LV_OPA_90;
      rect_dsc.border_width = 0;

      int width = x_right - x_left + 1;
      if (width > 0) {
        lv_canvas_draw_rect(mesh_canvas, x_left, y, width, 1, &rect_dsc);
      }
    }
  }
}

// Triangle rasterization with gradient interpolation between vertex colors
void BedMeshPanel::fill_triangle_gradient(int x1, int y1, lv_color_t c1, int x2, int y2, lv_color_t c2, int x3, int y3, lv_color_t c3, int canvas_width, int canvas_height)
{
  // Helper struct to keep vertex data together during sorting
  struct Vertex {
    int x, y;
    lv_color_t color;
  };

  // Create vertex array and sort by Y coordinate
  Vertex vertices[3] = {{x1, y1, c1}, {x2, y2, c2}, {x3, y3, c3}};

  // Sort vertices by Y coordinate (bubble sort for 3 elements)
  if (vertices[0].y > vertices[1].y) std::swap(vertices[0], vertices[1]);
  if (vertices[1].y > vertices[2].y) std::swap(vertices[1], vertices[2]);
  if (vertices[0].y > vertices[1].y) std::swap(vertices[0], vertices[1]);

  // Skip degenerate triangles
  if (vertices[0].y == vertices[2].y) return;

  // Extract RGB components for interpolation
  auto extract_rgb = [](lv_color_t color) -> std::tuple<int, int, int> {
    return {LV_COLOR_GET_R(color), LV_COLOR_GET_G(color), LV_COLOR_GET_B(color)};
  };

  auto [r1, g1, b1] = extract_rgb(vertices[0].color);
  auto [r2, g2, b2] = extract_rgb(vertices[1].color);
  auto [r3, g3, b3] = extract_rgb(vertices[2].color);

  // For each scanline, interpolate left and right X coordinates and colors
  for (int y = std::max(0, vertices[0].y); y <= std::min(canvas_height - 1, vertices[2].y); y++) {
    int x_left, x_right;
    int r_left, g_left, b_left, r_right, g_right, b_right;

    if (y < vertices[1].y) {
      // Upper half of triangle: interpolate between v0-v1 and v0-v2
      float t1 = (vertices[1].y != vertices[0].y) ? (float)(y - vertices[0].y) / (vertices[1].y - vertices[0].y) : 0.0f;
      float t2 = (vertices[2].y != vertices[0].y) ? (float)(y - vertices[0].y) / (vertices[2].y - vertices[0].y) : 0.0f;

      x_left = vertices[0].x + (int)(t1 * (vertices[1].x - vertices[0].x));
      x_right = vertices[0].x + (int)(t2 * (vertices[2].x - vertices[0].x));

      r_left = r1 + (int)(t1 * (r2 - r1));
      g_left = g1 + (int)(t1 * (g2 - g1));
      b_left = b1 + (int)(t1 * (b2 - b1));

      r_right = r1 + (int)(t2 * (r3 - r1));
      g_right = g1 + (int)(t2 * (g3 - g1));
      b_right = b1 + (int)(t2 * (b3 - b1));
    } else {
      // Lower half of triangle: interpolate between v1-v2 and v0-v2
      float t1 = (vertices[2].y != vertices[1].y) ? (float)(y - vertices[1].y) / (vertices[2].y - vertices[1].y) : 0.0f;
      float t2 = (vertices[2].y != vertices[0].y) ? (float)(y - vertices[0].y) / (vertices[2].y - vertices[0].y) : 0.0f;

      x_left = vertices[1].x + (int)(t1 * (vertices[2].x - vertices[1].x));
      x_right = vertices[0].x + (int)(t2 * (vertices[2].x - vertices[0].x));

      r_left = r2 + (int)(t1 * (r3 - r2));
      g_left = g2 + (int)(t1 * (g3 - g2));
      b_left = b2 + (int)(t1 * (b3 - b2));

      r_right = r1 + (int)(t2 * (r3 - r1));
      g_right = g1 + (int)(t2 * (g3 - g1));
      b_right = b1 + (int)(t2 * (b3 - b1));
    }

    // Ensure x_left <= x_right (swap if needed, including colors)
    if (x_left > x_right) {
      std::swap(x_left, x_right);
      std::swap(r_left, r_right);
      std::swap(g_left, g_right);
      std::swap(b_left, b_right);
    }

    // Clamp to canvas bounds
    int x_start = std::max(0, x_left);
    int x_end = std::min(canvas_width - 1, x_right);

    // Draw horizontal line with approximated gradient using fewer segments
    if (x_start <= x_end) {
      int line_width = x_end - x_start + 1;

      // For performance, use solid color for very thin lines
      if (line_width <= GRADIENT_MIN_LINE_WIDTH) {
        // Use average color for short lines
        int r = (r_left + r_right) / 2;
        int g = (g_left + g_right) / 2;
        int b = (b_left + b_right) / 2;

        r = std::max(0, std::min(255, r));
        g = std::max(0, std::min(255, g));
        b = std::max(0, std::min(255, b));

        lv_color_t avg_color = lv_color_make(r, g, b);

        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.bg_color = avg_color;
        rect_dsc.bg_opa = LV_OPA_90;
        rect_dsc.border_width = 0;

        lv_canvas_draw_rect(mesh_canvas, x_start, y, line_width, 1, &rect_dsc);
      } else {
        // For longer lines, draw gradient using segments (max GRADIENT_MAX_SEGMENTS for performance)
        int segments = std::min(GRADIENT_MAX_SEGMENTS, line_width / 4);
        segments = std::max(1, segments);

        for (int seg = 0; seg < segments; seg++) {
          int seg_start = x_start + (seg * line_width) / segments;
          int seg_end = x_start + ((seg + 1) * line_width) / segments - 1;
          int seg_width = seg_end - seg_start + 1;

          if (seg_width <= 0) continue;

          // Interpolate color at segment center
          float t = (x_right != x_left) ? (float)(seg_start + seg_width/2 - x_left) / (x_right - x_left) : 0.0f;

          int r = r_left + (int)(t * (r_right - r_left));
          int g = g_left + (int)(t * (g_right - g_left));
          int b = b_left + (int)(t * (b_right - b_left));

          r = std::max(0, std::min(255, r));
          g = std::max(0, std::min(255, g));
          b = std::max(0, std::min(255, b));

          lv_color_t seg_color = lv_color_make(r, g, b);

          lv_draw_rect_dsc_t rect_dsc;
          lv_draw_rect_dsc_init(&rect_dsc);
          rect_dsc.bg_color = seg_color;
          rect_dsc.bg_opa = LV_OPA_90;
          rect_dsc.border_width = 0;

          lv_canvas_draw_rect(mesh_canvas, seg_start, y, seg_width, 1, &rect_dsc);
        }
      }
    }
  }
}

// Generate 3D quads from mesh data with proper vertex positioning and colors
std::vector<BedMeshPanel::Quad3D> BedMeshPanel::generate_mesh_quads(int rows, int cols, double min_z, double max_z)
{
  std::vector<Quad3D> quads;

  // Use the same dynamic Z scaling as draw_3d_mesh with defined constants
  double z_range = max_z - min_z;
  double z_center = (min_z + max_z) / 2.0;  // Center of the data range
  double z_scale = DEFAULT_Z_SCALE;
  if (z_range > 0.001) {  // Avoid division by zero for flat surfaces
    z_scale = DEFAULT_Z_TARGET_HEIGHT / z_range;
    z_scale = std::max(DEFAULT_Z_MIN_SCALE, std::min(DEFAULT_Z_MAX_SCALE, z_scale));
  }

  // Generate quads for each mesh cell using dynamic scaling
  for (int row = 0; row < rows - 1; row++) {
    for (int col = 0; col < cols - 1; col++) {
      Quad3D quad;

      // Calculate world positions for the four corners of this mesh cell
      // Center the mesh around origin for better rotation
      double base_x = (col - cols / 2.0) * MESH_SCALE;  // Restore original X: col 0 = left (min X)
      double base_y = ((rows - 1 - row) - rows / 2.0) * MESH_SCALE;  // Invert Y: row 0 = front (min Y)

      // Bottom-left vertex (row, col) - center Z values around zero
      quad.vertices[0].x = base_x;
      quad.vertices[0].y = base_y;
      quad.vertices[0].z = (mesh[row][col] - z_center) * z_scale;
      quad.vertices[0].color = color_gradient_enhanced(mesh[row][col], min_z, max_z);

      // Bottom-right vertex (row, col+1)
      quad.vertices[1].x = base_x + MESH_SCALE;
      quad.vertices[1].y = base_y;
      quad.vertices[1].z = (mesh[row][col + 1] - z_center) * z_scale;
      quad.vertices[1].color = color_gradient_enhanced(mesh[row][col + 1], min_z, max_z);

      // Top-left vertex (row+1, col) - use inverted Y for row+1
      quad.vertices[2].x = base_x;
      quad.vertices[2].y = ((rows - 1 - (row + 1)) - rows / 2.0) * MESH_SCALE;
      quad.vertices[2].z = (mesh[row + 1][col] - z_center) * z_scale;
      quad.vertices[2].color = color_gradient_enhanced(mesh[row + 1][col], min_z, max_z);

      // Top-right vertex (row+1, col+1)
      quad.vertices[3].x = base_x + MESH_SCALE;
      quad.vertices[3].y = ((rows - 1 - (row + 1)) - rows / 2.0) * MESH_SCALE;
      quad.vertices[3].z = (mesh[row + 1][col + 1] - z_center) * z_scale;
      quad.vertices[3].color = color_gradient_enhanced(mesh[row + 1][col + 1], min_z, max_z);

      // Calculate average height for center color (fallback)
      double avg_height = (mesh[row][col] + mesh[row][col + 1] +
                          mesh[row + 1][col] + mesh[row + 1][col + 1]) / 4.0;
      quad.center_color = color_gradient_enhanced(avg_height, min_z, max_z);

      // Calculate average depth for sorting (will be updated after projection)
      quad.avg_depth = 0;  // Will be calculated during projection

      quads.push_back(quad);
    }
  }

  return quads;
}

// Sort quads by depth for proper back-to-front rendering (painter's algorithm)
void BedMeshPanel::sort_quads_by_depth(std::vector<Quad3D>& quads)
{
  // Sort by average depth - furthest away (largest depth) first
  std::sort(quads.begin(), quads.end(), [](const Quad3D& a, const Quad3D& b) {
    return a.avg_depth > b.avg_depth;  // Render back-to-front
  });
}

// Render a single 3D quad as two triangles using proper triangle rasterization
void BedMeshPanel::draw_3d_quad(const Quad3D& quad, int canvas_width, int canvas_height)
{
  // Project all vertices to 2D screen coordinates
  Point3D projected[4];
  for (int i = 0; i < 4; i++) {
    projected[i] = project_3d_to_2d(quad.vertices[i].x, quad.vertices[i].y, quad.vertices[i].z,
                                   canvas_width, canvas_height);
  }

  // Simple culling with margin - skip if all vertices are far off-screen
  // The margin allows partially visible triangles to be rendered
  bool any_visible = false;
  for (int i = 0; i < 4; i++) {
    if (projected[i].screen_x >= -CULLING_MARGIN && projected[i].screen_x < canvas_width + CULLING_MARGIN &&
        projected[i].screen_y >= -CULLING_MARGIN && projected[i].screen_y < canvas_height + CULLING_MARGIN) {
      any_visible = true;
      break;
    }
  }

  if (!any_visible) return;  // Skip if completely off-screen

  // Use solid colors during dragging for performance, gradients when static
  if (is_dragging) {
    // Fast solid color rendering during drag operations
    // Triangle 1: vertices 0, 1, 2 (bottom-left, bottom-right, top-left)
    fill_triangle(projected[0].screen_x, projected[0].screen_y,
                  projected[1].screen_x, projected[1].screen_y,
                  projected[2].screen_x, projected[2].screen_y,
                  quad.center_color, canvas_width, canvas_height);

    // Triangle 2: vertices 1, 2, 3 (bottom-right, top-left, top-right)
    fill_triangle(projected[1].screen_x, projected[1].screen_y,
                  projected[2].screen_x, projected[2].screen_y,
                  projected[3].screen_x, projected[3].screen_y,
                  quad.center_color, canvas_width, canvas_height);
  } else {
    // High-quality gradient rendering when not dragging
    // Triangle 1: vertices 0, 1, 2 (bottom-left, bottom-right, top-left)
    fill_triangle_gradient(projected[0].screen_x, projected[0].screen_y, quad.vertices[0].color,
                          projected[1].screen_x, projected[1].screen_y, quad.vertices[1].color,
                          projected[2].screen_x, projected[2].screen_y, quad.vertices[2].color,
                          canvas_width, canvas_height);

    // Triangle 2: vertices 1, 2, 3 (bottom-right, top-left, top-right)
    fill_triangle_gradient(projected[1].screen_x, projected[1].screen_y, quad.vertices[1].color,
                          projected[2].screen_x, projected[2].screen_y, quad.vertices[2].color,
                          projected[3].screen_x, projected[3].screen_y, quad.vertices[3].color,
                          canvas_width, canvas_height);
  }
}

void BedMeshPanel::draw_3d_mesh()
{
  if (mesh.empty()) {
    spdlog::debug("Cannot draw 3D mesh: mesh data is empty");
    return;
  }

  if (canvas_draw_buf == nullptr) {
    spdlog::debug("Cannot draw 3D mesh: canvas buffer is null");
    return;
  }

  spdlog::debug("draw_3d_mesh: z_display_scale={}", z_display_scale);

  int canvas_width = lv_obj_get_width(mesh_canvas);
  int canvas_height = lv_obj_get_height(mesh_canvas);
  int mesh_rows = mesh.size();
  int mesh_cols = mesh[0].size();

  spdlog::debug("Drawing proper 3D mesh: canvas {}x{}, mesh {}x{}", canvas_width, canvas_height, mesh_rows, mesh_cols);

  // Clear canvas with dark slate gray background
  lv_canvas_fill_bg(mesh_canvas, lv_color_make(40, 40, 40), LV_OPA_COVER);

  // Find min/max Z values for color mapping
  double min_z = mesh[0][0], max_z = mesh[0][0];
  for (const auto& row : mesh) {
    for (double val : row) {
      min_z = std::min(min_z, val);
      max_z = std::max(max_z, val);
    }
  }

  // Calculate dynamic Z scaling using defined constants
  double z_range = max_z - min_z;
  double z_scale = DEFAULT_Z_SCALE;
  if (z_range > 0.001) {  // Avoid division by zero for flat surfaces
    z_scale = DEFAULT_Z_TARGET_HEIGHT / z_range;
    z_scale = std::max(DEFAULT_Z_MIN_SCALE, std::min(DEFAULT_Z_MAX_SCALE, z_scale));
  }

  // Calculate dynamic FOV scale to fit mesh in canvas with padding
  fov_scale = calculate_dynamic_fov_scale(mesh_rows, mesh_cols, canvas_width, canvas_height);

  spdlog::debug("Bed mesh display: rotation={:.1f}°, Z range=[{:.3f} to {:.3f}]mm (span={:.3f}mm), scale={:.2f}, fov={:.1f}, camera_distance={:.1f}",
                VIEW_ANGLE_X_DEGREES, min_z, max_z, z_range, z_scale, fov_scale, camera_distance);

  // Generate 3D quads from mesh data
  std::vector<Quad3D> quads = generate_mesh_quads(mesh_rows, mesh_cols, min_z, max_z);

  // Project quads to screen coordinates and calculate depths for sorting
  for (auto& quad : quads) {
    double total_depth = 0;
    for (int i = 0; i < 4; i++) {
      Point3D projected = project_3d_to_2d(quad.vertices[i].x, quad.vertices[i].y, quad.vertices[i].z,
                                          canvas_width, canvas_height);
      total_depth += projected.depth;
    }
    quad.avg_depth = total_depth / 4.0;  // Average depth of all vertices
  }

  // Create point grid for wireframe and axes (convert quads back to point grid)
  std::vector<std::vector<Point3D>> point_grid(mesh_rows, std::vector<Point3D>(mesh_cols));

  // Calculate Z center for consistent centering with quads
  double z_center = (min_z + max_z) / 2.0;

  for (int row = 0; row < mesh_rows; row++) {
    for (int col = 0; col < mesh_cols; col++) {
      double world_x = (col - mesh_cols / 2.0) * MESH_SCALE;  // Restore original X: col 0 = left (min X)
      double world_y = ((mesh_rows - 1 - row) - mesh_rows / 2.0) * MESH_SCALE;  // Invert Y: row 0 = front (min Y)
      double world_z = (mesh[row][col] - z_center) * z_scale;  // Center Z values around zero

      point_grid[row][col] = project_3d_to_2d(world_x, world_y, world_z, canvas_width, canvas_height);
    }
  }

  // Draw coordinate axes and labels FIRST (behind the mesh)
  draw_axes_and_labels(point_grid, mesh_rows, mesh_cols, canvas_width, canvas_height, min_z, max_z, z_scale);

  // Sort quads by depth (back-to-front rendering for proper overlapping)
  sort_quads_by_depth(quads);

  // Render all quads in sorted order (ON TOP of axes/grid)
  for (const auto& quad : quads) {
    draw_3d_quad(quad, canvas_width, canvas_height);
  }

  // Draw wireframe grid overlay following the 3D surface (on top)
  draw_mesh_wireframe(point_grid, mesh_rows, mesh_cols, canvas_width, canvas_height);

  // Draw vertical color gradient band on the left side (on top of everything)
  draw_color_gradient_band(canvas_width, canvas_height, min_z, max_z);

  // Force canvas redraw
  lv_obj_invalidate(mesh_canvas);
  spdlog::debug("3D mesh rendering completed with {} quads", quads.size());
}

void BedMeshPanel::handle_z_zoom_in(lv_event_t *event)
{
  double old_display_scale = z_display_scale;

  // Z+ = ZOOM IN: increase Z visual height only (not color sensitivity)
  z_display_scale *= 1.1; // Increase Z display range by 10%

  if (z_display_scale > MAX_Z_SCALE) z_display_scale = MAX_Z_SCALE;

  spdlog::debug("Z+ ZOOM IN: display_scale {} -> {}",
                old_display_scale, z_display_scale);

  // Update button states
  lv_obj_clear_state(z_zoom_out_btn, LV_STATE_DISABLED);
  if (z_display_scale >= MAX_Z_SCALE) {
    lv_obj_add_state(z_zoom_in_btn, LV_STATE_DISABLED);
  }

  draw_3d_mesh();
}

void BedMeshPanel::handle_z_zoom_out(lv_event_t *event)
{
  double old_display_scale = z_display_scale;

  // Z- = ZOOM OUT: reduce Z visual height only (not color sensitivity)
  z_display_scale *= 0.9; // Reduce Z display range by 10%

  if (z_display_scale < MIN_Z_SCALE) z_display_scale = MIN_Z_SCALE;

  spdlog::debug("Z- ZOOM OUT: display_scale {} -> {}",
                old_display_scale, z_display_scale);

  // Update button states
  lv_obj_clear_state(z_zoom_in_btn, LV_STATE_DISABLED);
  if (z_display_scale <= MIN_Z_SCALE) {
    lv_obj_add_state(z_zoom_out_btn, LV_STATE_DISABLED);
  }

  draw_3d_mesh();
}

void BedMeshPanel::handle_fov_zoom_in(lv_event_t *event)
{
  double old_camera_distance = camera_distance;

  // FOV+ = ZOOM IN: decrease camera distance for closer view (tighter FOV)
  camera_distance *= 0.9; // Decrease distance by 10%

  if (camera_distance < MIN_CAMERA_DISTANCE) camera_distance = MIN_CAMERA_DISTANCE;

  spdlog::debug("FOV+ ZOOM IN: camera_distance {} -> {} (closer view)",
                old_camera_distance, camera_distance);

  // Update button states
  lv_obj_clear_state(fov_zoom_out_btn, LV_STATE_DISABLED);
  if (camera_distance <= MIN_CAMERA_DISTANCE) {
    lv_obj_add_state(fov_zoom_in_btn, LV_STATE_DISABLED);
  }

  draw_3d_mesh();
}

void BedMeshPanel::handle_fov_zoom_out(lv_event_t *event)
{
  double old_camera_distance = camera_distance;

  // FOV- = ZOOM OUT: increase camera distance for wider view (wider FOV)
  camera_distance *= 1.1; // Increase distance by 10%

  if (camera_distance > MAX_CAMERA_DISTANCE) camera_distance = MAX_CAMERA_DISTANCE;

  spdlog::debug("FOV- ZOOM OUT: camera_distance {} -> {} (wider view)",
                old_camera_distance, camera_distance);

  // Update button states
  lv_obj_clear_state(fov_zoom_in_btn, LV_STATE_DISABLED);
  if (camera_distance >= MAX_CAMERA_DISTANCE) {
    lv_obj_add_state(fov_zoom_out_btn, LV_STATE_DISABLED);
  }

  draw_3d_mesh();
}


static lv_color_t color_gradient(double offset)
{
  double red_max = 0.25;
  uint32_t color = static_cast<uint32_t>(std::min(1.0, 1.0 - 1.0 / red_max * std::abs(offset)) * 255);
  if (offset > 0) {
    return lv_color_make(255, color, color);
  }

  if (offset < 0) {
    return lv_color_make(color, color, 255);
  }

  return lv_color_make(255, 255, 255);
}

static lv_color_t color_gradient_enhanced(double offset, double min_val, double max_val)
{
  double range = max_val - min_val;
  if (range == 0) {
    return lv_color_make(0, 255, 0); // Green for flat surface
  }

  // Use a dynamic color scale range based on actual mesh data variation
  // This automatically adapts to the data: flat beds get high contrast, varied beds get appropriate scaling
  double data_range = range; // Full range of actual mesh data

  // Apply compression factor for enhanced visual contrast
  // Smaller factor = more dramatic colors for small variations
  double adjusted_range = data_range * BedMeshPanel::COLOR_COMPRESSION_FACTOR;

  // Ensure minimum range to prevent division by zero and ensure visible colors
  const double min_color_range = 0.01; // mm
  adjusted_range = std::max(adjusted_range, min_color_range);

  // static int call_count = 0;
  // call_count++;
  // if (call_count <= 5 || call_count % 20 == 0) { // Log first 5 calls, then every 20th
  //   spdlog::debug("color_gradient_enhanced[{}]: data_range={:.3f}mm, compression_factor={}, adjusted_range={:.3f}mm",
  //                 call_count, data_range, BedMeshPanel::COLOR_COMPRESSION_FACTOR, adjusted_range);
  // }

  // Center the data range within our color scale
  double data_center = (min_val + max_val) / 2.0;
  double color_scale_min = data_center - (adjusted_range / 2.0);

  // Map offset to the adjusted color scale
  double normalized = (offset - color_scale_min) / adjusted_range;
  normalized = std::max(0.0, std::min(1.0, normalized)); // Clamp to [0,1]

  // Create smooth scientific heat map gradient using continuous interpolation
  // Classic "jet" colormap: Purple → Blue → Cyan → Green → Yellow → Orange → Red
  // High normalized values = high heights = red, low normalized values = low heights = purple
  uint8_t r, g, b;

  // Use smooth continuous functions to eliminate banding
  // Each transition point is carefully matched to ensure continuity

  // Clamp to ensure we're in valid range
  normalized = std::max(0.0, std::min(1.0, normalized));

  if (normalized < 0.125) {
    // Purple to Blue (0.0 to 0.125)
    double t = normalized / 0.125;
    r = (uint8_t)(128 * (1.0 - t));     // 128 → 0
    g = (uint8_t)(0 + t * 128);         // 0 → 128
    b = (uint8_t)(255);                 // constant blue
  } else if (normalized < 0.375) {
    // Blue to Cyan (0.125 to 0.375)
    double t = (normalized - 0.125) / 0.25;
    r = (uint8_t)(0);                   // constant 0
    g = (uint8_t)(128 + t * 127);       // 128 → 255
    b = (uint8_t)(255);                 // constant blue
  } else if (normalized < 0.625) {
    // Cyan to Yellow (0.375 to 0.625)
    double t = (normalized - 0.375) / 0.25;
    r = (uint8_t)(t * 255);             // 0 → 255
    g = (uint8_t)(255);                 // constant green
    b = (uint8_t)(255 * (1.0 - t));     // 255 → 0
  } else if (normalized < 0.875) {
    // Yellow to Red (0.625 to 0.875)
    double t = (normalized - 0.625) / 0.25;
    r = (uint8_t)(255);                 // constant red
    g = (uint8_t)(255 * (1.0 - t));     // 255 → 0
    b = (uint8_t)(0);                   // constant 0
  } else {
    // Deep Red (0.875 to 1.0) - keep it pure red
    r = (uint8_t)(255);                 // constant bright red
    g = (uint8_t)(0);                   // keep green at 0 to avoid pink
    b = (uint8_t)(0);                   // keep blue at 0 to avoid pink
  }

  // De-saturate all colors by 35% by blending with gray
  const double desaturation = 0.35;
  uint8_t gray = (uint8_t)((r + g + b) / 3); // Calculate grayscale value

  r = (uint8_t)(r * (1.0 - desaturation) + gray * desaturation);
  g = (uint8_t)(g * (1.0 - desaturation) + gray * desaturation);
  b = (uint8_t)(b * (1.0 - desaturation) + gray * desaturation);

  return lv_color_make(r, g, b);
}

void BedMeshPanel::draw_horizontal_axes(double mesh_origin_x, double mesh_origin_y, double grid_reference_z,
                                        int rows, int cols, int canvas_width, int canvas_height,
                                        const lv_draw_line_dsc_t& axis_dsc)
{
  // Draw X-axis (horizontal line extending in X direction from origin)
  double x_axis_start = mesh_origin_x;
  double x_axis_end = ((cols-1) - cols / 2.0) * MESH_SCALE; // Right edge of mesh
  Point3D x_start = project_3d_to_2d(x_axis_start, mesh_origin_y, grid_reference_z, canvas_width, canvas_height);
  Point3D x_end = project_3d_to_2d(x_axis_end, mesh_origin_y, grid_reference_z, canvas_width, canvas_height);

  lv_point_t x_line[2] = {{x_start.screen_x, x_start.screen_y}, {x_end.screen_x, x_end.screen_y}};
  lv_canvas_draw_line(mesh_canvas, x_line, 2, &axis_dsc);

  // Draw Y-axis (horizontal line extending in Y direction from origin)
  double y_axis_start = mesh_origin_y;
  double y_axis_end = ((rows-1) - rows / 2.0) * MESH_SCALE; // Back edge of mesh
  Point3D y_start = project_3d_to_2d(mesh_origin_x, y_axis_start, grid_reference_z, canvas_width, canvas_height);
  Point3D y_end = project_3d_to_2d(mesh_origin_x, y_axis_end, grid_reference_z, canvas_width, canvas_height);

  lv_point_t y_line[2] = {{y_start.screen_x, y_start.screen_y}, {y_end.screen_x, y_end.screen_y}};
  lv_canvas_draw_line(mesh_canvas, y_line, 2, &axis_dsc);

  // Add X-axis labels along the front edge (max Y)
  lv_draw_label_dsc_t label_dsc;
  lv_draw_label_dsc_init(&label_dsc);
  label_dsc.color = lv_color_white();
  label_dsc.font = &lv_font_montserrat_10;

  double front_y = ((rows-1) - rows / 2.0) * MESH_SCALE;

  // Draw X-axis tick values
  draw_axis_tick_values(cols, true, rows, cols, grid_reference_z, canvas_width, canvas_height, label_dsc);

  // Draw X-axis label
  lv_draw_label_dsc_t axis_label_dsc;
  lv_draw_label_dsc_init(&axis_label_dsc);
  axis_label_dsc.color = lv_color_white();
  axis_label_dsc.font = &lv_font_montserrat_16;

  double middle_x_coord = ((cols/2) - cols / 2.0) * MESH_SCALE;
  draw_axis_label("X", middle_x_coord, front_y + TICK_MARK_LENGTH * 6, grid_reference_z, -10, 0, canvas_width, canvas_height, axis_label_dsc);

  // Draw Y-axis tick values
  draw_axis_tick_values(rows, false, rows, cols, grid_reference_z, canvas_width, canvas_height, label_dsc);

  // Draw Y-axis label
  double right_x = ((cols-1) - cols / 2.0) * MESH_SCALE;
  double middle_y_coord = ((rows/2) - rows / 2.0) * MESH_SCALE;
  draw_axis_label("Y", right_x + TICK_MARK_LENGTH * 6, middle_y_coord, grid_reference_z, 0, -10, canvas_width, canvas_height, axis_label_dsc);

}


void BedMeshPanel::draw_color_gradient_band(int canvas_width, int canvas_height, double min_z, double max_z)
{
  // Calculate gradient band position and dimensions
  int band_x = GRADIENT_BAND_MARGIN;
  int band_width = GRADIENT_BAND_WIDTH;
  int band_height = (canvas_height * GRADIENT_BAND_HEIGHT_RATIO) / 100;
  int band_y = (canvas_height - band_height) / 2; // Center vertically

  // Calculate the same extended color range that color_gradient_enhanced uses
  double data_range = max_z - min_z;
  double adjusted_range = data_range * COLOR_COMPRESSION_FACTOR;
  const double min_color_range = 0.01; // mm (same as in color_gradient_enhanced)
  adjusted_range = std::max(adjusted_range, min_color_range);

  // Center the data range within our color scale (same logic as color_gradient_enhanced)
  double data_center = (min_z + max_z) / 2.0;
  double color_scale_min = data_center - (adjusted_range / 2.0);
  double color_scale_max = data_center + (adjusted_range / 2.0);

  // Draw the vertical gradient band pixel by pixel
  for (int y = 0; y < band_height; y++) {
    // Calculate the Z value this pixel represents using the extended color scale range
    // Flip the gradient: top should be high Z (color_scale_max), bottom should be low Z (color_scale_min)
    double progress = (double)y / (band_height - 1);
    double z_value = color_scale_max - progress * (color_scale_max - color_scale_min); // Top = color_scale_max, bottom = color_scale_min

    // Get the color for this Z value using the same function as the mesh
    lv_color_t pixel_color = color_gradient_enhanced(z_value, min_z, max_z);

    // Draw a horizontal line at this Y position across the band width
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = pixel_color;
    line_dsc.width = 1;
    line_dsc.opa = LV_OPA_COVER;

    lv_point_t line_points[2];
    line_points[0].x = band_x;
    line_points[0].y = band_y + y;
    line_points[1].x = band_x + band_width - 1;
    line_points[1].y = band_y + y;

    lv_canvas_draw_line(mesh_canvas, line_points, 2, &line_dsc);
  }

  // Add text labels for min and max values
  lv_draw_label_dsc_t label_dsc;
  lv_draw_label_dsc_init(&label_dsc);
  label_dsc.color = lv_color_white();
  label_dsc.opa = LV_OPA_COVER;
  label_dsc.font = &lv_font_montserrat_10;

  // Max value label at the top (showing extended color scale max)
  char max_label[16];
  snprintf(max_label, sizeof(max_label), "%.2f", color_scale_max);
  lv_area_t max_area;
  max_area.x1 = band_x + band_width + 5;
  max_area.y1 = band_y - 5;
  max_area.x2 = max_area.x1 + 40;
  max_area.y2 = max_area.y1 + LABEL_HEIGHT;

  if (max_area.x2 < canvas_width && max_area.y1 >= 0) {
    lv_canvas_draw_text(mesh_canvas, max_area.x1, max_area.y1, 40, &label_dsc, max_label);
  }

  // Min value label at the bottom (showing extended color scale min)
  char min_label[16];
  snprintf(min_label, sizeof(min_label), "%.2f", color_scale_min);
  lv_area_t min_area;
  min_area.x1 = band_x + band_width + 5;
  min_area.y1 = band_y + band_height - LABEL_HEIGHT + 5;
  min_area.x2 = min_area.x1 + 40;
  min_area.y2 = min_area.y1 + LABEL_HEIGHT;

  if (min_area.x2 < canvas_width && min_area.y2 < canvas_height) {
    lv_canvas_draw_text(mesh_canvas, min_area.x1, min_area.y1, 40, &label_dsc, min_label);
  }
}

void BedMeshPanel::setup_button_with_label(lv_obj_t* btn, const char* text, int width, int height, lv_event_cb_t callback)
{
  lv_obj_set_size(btn, width, height);
  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  lv_obj_add_event_cb(btn, callback, LV_EVENT_CLICKED, this);
}

void BedMeshPanel::draw_grid_lines_parallel_to_axis(int axis_count, bool is_x_axis, int rows, int cols, double grid_reference_z, int canvas_width, int canvas_height, const lv_draw_line_dsc_t& line_dsc)
{
  for (int i = 0; i < axis_count; i++) {
    double start_x, start_y, end_x, end_y;

    if (is_x_axis) {
      // Draw lines parallel to X-axis (for each column)
      start_x = (i - cols / 2.0) * MESH_SCALE;
      start_y = (0 - rows / 2.0) * MESH_SCALE;
      end_x = (i - cols / 2.0) * MESH_SCALE;
      end_y = ((rows-1) - rows / 2.0) * MESH_SCALE;
    } else {
      // Draw lines parallel to Y-axis (for each row)
      start_x = (0 - cols / 2.0) * MESH_SCALE;
      start_y = (i - rows / 2.0) * MESH_SCALE;
      end_x = ((cols-1) - cols / 2.0) * MESH_SCALE;
      end_y = (i - rows / 2.0) * MESH_SCALE;
    }

    Point3D start = project_3d_to_2d(start_x, start_y, grid_reference_z, canvas_width, canvas_height);
    Point3D end = project_3d_to_2d(end_x, end_y, grid_reference_z, canvas_width, canvas_height);


    lv_point_t grid_line[2] = {{start.screen_x, start.screen_y}, {end.screen_x, end.screen_y}};
    lv_canvas_draw_line(mesh_canvas, grid_line, 2, &line_dsc);
  }
}

bool BedMeshPanel::create_and_check_label_area(lv_area_t& area, int x, int y, int width, int height, int canvas_width, int canvas_height)
{
  area.x1 = x;
  area.y1 = y;
  area.x2 = area.x1 + width;
  area.y2 = area.y1 + height;
  return (area.x1 >= 0 && area.x2 < canvas_width && area.y1 >= 0 && area.y2 < canvas_height);
}

void BedMeshPanel::extract_mesh_parameter_values(const json& mesh_params, const std::vector<const char*>& param_names, std::vector<int>& values)
{
  values.clear();
  for (const auto& param : param_names) {
    auto v = mesh_params[json::json_pointer(fmt::format("/{}", param))];
    if (!v.is_null()) {
      values.push_back(v.template get<int>());
    }
  }
}

double BedMeshPanel::calculate_dynamic_fov_scale(int mesh_rows, int mesh_cols, int canvas_width, int canvas_height)
{
  // Calculate the 2D bounding box of the mesh in world coordinates
  double mesh_world_width = (mesh_cols - 1) * MESH_SCALE;
  double mesh_world_height = (mesh_rows - 1) * MESH_SCALE;

  // Account for the rotation by using the diagonal as the conservative bound
  double mesh_diagonal = sqrt(mesh_world_width * mesh_world_width + mesh_world_height * mesh_world_height);

  // Calculate available canvas space after padding and gradient band
  double effective_canvas_width = canvas_width - GRADIENT_BAND_TOTAL_WIDTH;
  double available_width = effective_canvas_width * (1.0 - 2.0 * CANVAS_PADDING_RATIO);
  double available_height = canvas_height * (1.0 - 2.0 * CANVAS_PADDING_RATIO);

  // Use the smaller dimension to ensure the mesh fits in both directions
  double available_space = std::min(available_width, available_height);

  // Calculate the FOV scale needed to fit the mesh diagonal in the available space
  // The formula is: fov_scale = (available_pixels * camera_distance) / world_size
  double fov_scale = (available_space * CAMERA_DISTANCE) / mesh_diagonal;

  return fov_scale;
}

void BedMeshPanel::handle_canvas_drag(lv_event_t *event)
{
  lv_event_code_t code = lv_event_get_code(event);

  // Optional debug logging (disabled for now)
  // if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED) {
  //   spdlog::debug("Canvas drag event: {}", (int)code);
  // }

  lv_indev_t *indev = lv_indev_get_act();
  if (indev == nullptr) {
    spdlog::debug("No active input device");
    return;
  }


  lv_point_t point;
  lv_indev_get_point(indev, &point);

  if (code == LV_EVENT_PRESSED) {
    // Start drag
    is_dragging = true;
    drag_start_x = point.x;
    last_drag_x = point.x;
    drag_start_y = point.y;
    last_drag_y = point.y;
    // spdlog::debug("Canvas drag started at x={}, y={}", point.x, point.y);
  }
  else if (code == LV_EVENT_PRESSING && is_dragging) {
    // Continue drag - calculate rotation and tilt based on movement
    int current_x = point.x;
    int current_y = point.y;
    int drag_distance_x = current_x - last_drag_x;
    int drag_distance_y = current_y - last_drag_y;

    bool mesh_changed = false;

    // Horizontal drag = Z rotation (around vertical axis)
    if (abs(drag_distance_x) > 0) {
      // Convert horizontal drag distance to rotation angle
      // Scale factor: 1 pixel = 0.5 degrees, reversed for natural feel
      double z_angle_change = -drag_distance_x * 0.5;

      // Update Z rotation angle (round to nearest integer)
      current_view_angle += (int)round(z_angle_change);

      // Wrap angle to 0-359 degrees
      while (current_view_angle < 0) current_view_angle += 360;
      while (current_view_angle >= 360) current_view_angle -= 360;

      mesh_changed = true;
    }

    // Vertical drag = X tilt (tilt up/down)
    if (abs(drag_distance_y) > 0) {
      // Convert vertical drag distance to tilt angle
      // Scale factor: 1 pixel = 0.5 degrees, natural direction (drag up = tilt up)
      double x_angle_change = drag_distance_y * 0.5;

      // Update X tilt angle with limits (-90 to -10 degrees for good viewing)
      current_x_angle += x_angle_change;

      // Clamp to reasonable viewing limits
      const double MIN_X_ANGLE = -85.0;  // Don't go too vertical
      const double MAX_X_ANGLE = -10.0;  // Don't go too horizontal
      current_x_angle = std::max(MIN_X_ANGLE, std::min(MAX_X_ANGLE, current_x_angle));

      mesh_changed = true;
    }

    // Update last position
    last_drag_x = current_x;
    last_drag_y = current_y;

    // Redraw the mesh if any angle changed
    if (mesh_changed) {
      draw_3d_mesh();
    }

    // if (abs(drag_distance_x) > 0 || abs(drag_distance_y) > 0) {
    //   spdlog::debug("Canvas drag: x={}, y={}, z_angle={}, x_angle={:.1f}",
    //                 current_x, current_y, current_view_angle, current_x_angle);
    // }
  }
  else if (code == LV_EVENT_RELEASED) {
    // End drag
    is_dragging = false;

    // Redraw with high-quality gradients now that dragging has stopped
    if (show_3d_view && !mesh.empty()) {
      draw_3d_mesh();
    }
    // spdlog::debug("Canvas drag ended at z_angle={}, x_angle={:.1f}", current_view_angle, current_x_angle);
  }
}

// Helper function to draw axis tick values for X and Y axes
void BedMeshPanel::draw_axis_tick_values(int count, bool is_x_axis, int rows, int cols, double grid_reference_z,
                                        int canvas_width, int canvas_height, lv_draw_label_dsc_t& label_dsc) {
  if (is_dragging) return; // Skip during drag for performance

  double edge_coord = is_x_axis ? ((rows-1) - rows / 2.0) * MESH_SCALE    // front_y for X-axis
                                : ((cols-1) - cols / 2.0) * MESH_SCALE;     // right_x for Y-axis

  for (int i = 0; i < count; i += 2) {
    double coord, actual_value;
    Point3D label_pos;

    if (is_x_axis) {
      // X-axis: labels along front edge
      coord = (i - cols / 2.0) * MESH_SCALE;
      actual_value = mesh_min_x + (i * (mesh_max_x - mesh_min_x) / (cols - 1));
      label_pos = project_3d_to_2d(coord, edge_coord + TICK_MARK_LENGTH * 2, grid_reference_z, canvas_width, canvas_height);
    } else {
      // Y-axis: labels along right edge
      coord = (i - rows / 2.0) * MESH_SCALE;
      actual_value = mesh_min_y + (i * (mesh_max_y - mesh_min_y) / (rows - 1));
      label_pos = project_3d_to_2d(edge_coord + TICK_MARK_LENGTH * 2, coord, grid_reference_z, canvas_width, canvas_height);
    }

    char label_text[16];
    snprintf(label_text, sizeof(label_text), "%.0f", actual_value);

    lv_area_t label_area;
    int offset_x = is_x_axis ? -15 : 0;
    int offset_y = is_x_axis ? 0 : -5;

    if (create_and_check_label_area(label_area, label_pos.screen_x + offset_x, label_pos.screen_y + offset_y, 30, LABEL_HEIGHT, canvas_width, canvas_height)) {
      lv_canvas_draw_text(mesh_canvas, label_area.x1, label_area.y1, 30, &label_dsc, label_text);
    }
  }
}

// Helper function to draw Z-axis tick values
void BedMeshPanel::draw_z_axis_tick_values(double left_x, double front_y, double z_min, double z_max,
                                          int z_divisions, double z_scale, int canvas_width, int canvas_height,
                                          lv_draw_label_dsc_t& label_dsc, double min_z, double max_z) {
  if (is_dragging) return; // Skip during drag for performance

  for (int z_div = 0; z_div <= z_divisions; z_div += 2) {
    double z_level = z_min + (z_div * (z_max - z_min) / z_divisions);
    double actual_z = (z_level / z_scale / Z_AXIS_EXTENSION + (min_z + max_z) / 2.0);

    Point3D label_pos = project_3d_to_2d(left_x - TICK_MARK_LENGTH * 2, front_y, z_level, canvas_width, canvas_height);

    char label_text[16];
    snprintf(label_text, sizeof(label_text), "%.2f", actual_z);

    lv_area_t label_area;
    if (create_and_check_label_area(label_area, label_pos.screen_x - 20, label_pos.screen_y - 5, 40, LABEL_HEIGHT, canvas_width, canvas_height)) {
      lv_canvas_draw_text(mesh_canvas, label_area.x1, label_area.y1, 40, &label_dsc, label_text);
    }
  }
}

// Helper function to draw main axis labels (X, Y, Z)
void BedMeshPanel::draw_axis_label(const char* label_text, double x, double y, double z, int offset_x, int offset_y,
                                  int canvas_width, int canvas_height, lv_draw_label_dsc_t& label_dsc) {
  Point3D label_pos = project_3d_to_2d(x, y, z, canvas_width, canvas_height);

  lv_area_t label_area;
  if (create_and_check_label_area(label_area, label_pos.screen_x + offset_x, label_pos.screen_y + offset_y, 20, LABEL_HEIGHT, canvas_width, canvas_height)) {
    lv_canvas_draw_text(mesh_canvas, label_area.x1, label_area.y1, 20, &label_dsc, label_text);
  }
}

void BedMeshPanel::handle_toggle_view(lv_event_t *event) {
  auto code = lv_event_get_code(event);
  if (code == LV_EVENT_CLICKED) {
    // Toggle between 3D view and table view
    show_3d_view = !show_3d_view;

    if (show_3d_view) {
      // Show 3D view
      lv_obj_add_flag(mesh_table, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(mesh_canvas, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(rotation_cont, LV_OBJ_FLAG_HIDDEN);
      toggle_view_btn.set_text("Table View");  // Button shows what you'll switch TO
      toggle_view_btn.set_image(&sysinfo_img); // Grid-like icon for table view

      // Redraw 3D mesh if we have data
      if (!mesh.empty()) {
        draw_3d_mesh();
      }
    } else {
      // Show table view
      lv_obj_clear_flag(mesh_table, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(mesh_canvas, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(rotation_cont, LV_OBJ_FLAG_HIDDEN);
      toggle_view_btn.set_text("3D View");     // Button shows what you'll switch TO
      toggle_view_btn.set_image(&chart_img);   // Chart icon for 3D view
    }
  }
}

bool BedMeshPanel::detect_multitouch_support()
{
#ifdef SIMULATOR
  return false;  // Simulation mode doesn't support multi-touch
#else
  // Check if evdev device supports multi-touch
  int fd = open("/dev/input/event0", O_RDONLY);
  if (fd < 0) {
    spdlog::debug("Cannot open input device for multi-touch detection");
    return false;
  }

  unsigned long abs_bits[NLONGS(ABS_CNT)] = {0};
  bool has_mt = false;

  if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits) >= 0) {
    // Check for multi-touch absolute events
    has_mt = test_bit(ABS_MT_POSITION_X, abs_bits) && test_bit(ABS_MT_POSITION_Y, abs_bits);
  }

  close(fd);
  spdlog::debug("Multi-touch support detected: {}", has_mt);
  return has_mt;
#endif
}

void BedMeshPanel::handle_canvas_scroll(lv_event_t *event)
{
  if (!show_3d_view) {
    spdlog::debug("handle_canvas_scroll: not in 3D view, ignoring");
    return;
  }

#ifdef SIMULATOR
  spdlog::debug("handle_canvas_scroll: checking for encoder input in simulation mode");

  // In simulation mode, check for encoder (mouse wheel) input
  // We need to find the encoder input device and check for changes
  lv_indev_t *encoder_indev = nullptr;
  lv_indev_t *indev = lv_indev_get_next(nullptr);
  int indev_count = 0;

  // Find the encoder input device (mouse wheel)
  while (indev) {
    lv_indev_type_t type = lv_indev_get_type(indev);
    spdlog::debug("Found input device {}: type={}", indev_count, (int)type);

    if (type == LV_INDEV_TYPE_ENCODER) {
      encoder_indev = indev;
      spdlog::debug("Found encoder device at index {}", indev_count);
      break;
    }
    indev = lv_indev_get_next(indev);
    indev_count++;
  }

  if (!encoder_indev) {
    spdlog::debug("No encoder input device found after checking {} devices", indev_count);
    return;
  }

  // Check if encoder has new data
  lv_indev_data_t data;
  encoder_indev->driver->read_cb(encoder_indev->driver, &data);

  spdlog::debug("Encoder data: enc_diff={}, state={}", data.enc_diff, (int)data.state);

  if (data.enc_diff != 0) {
    double old_scale = z_display_scale;
    spdlog::debug("Mouse wheel detected: enc_diff={}, current_scale={}", data.enc_diff, old_scale);

    if (data.enc_diff > 0) {
      // Scroll up = zoom in
      z_display_scale *= 1.1;
      if (z_display_scale > MAX_Z_SCALE) z_display_scale = MAX_Z_SCALE;
      spdlog::debug("Zoom IN: {} -> {} (clamped to max {})", old_scale, z_display_scale, MAX_Z_SCALE);
    } else if (data.enc_diff < 0) {
      // Scroll down = zoom out
      z_display_scale *= 0.9;
      if (z_display_scale < MIN_Z_SCALE) z_display_scale = MIN_Z_SCALE;
      spdlog::debug("Zoom OUT: {} -> {} (clamped to min {})", old_scale, z_display_scale, MIN_Z_SCALE);
    }

    if (old_scale != z_display_scale) {
      spdlog::debug("Scale changed, redrawing 3D mesh");
      draw_3d_mesh();
    } else {
      spdlog::debug("Scale unchanged (hit limit), no redraw needed");
    }
  } else {
    spdlog::debug("No encoder movement detected");
  }
#else
  spdlog::debug("handle_canvas_scroll: not in simulation mode, ignoring");
#endif
}

void BedMeshPanel::scroll_check_timer_callback()
{
#ifdef SIMULATOR
  if (!show_3d_view || !use_gesture_zoom) {
    return;
  }

  // Don't process scroll events if the panel is not visible
  if (!cont || lv_obj_has_flag(cont, LV_OBJ_FLAG_HIDDEN)) {
    return;
  }

  // Find the encoder input device (mouse wheel)
  lv_indev_t *encoder_indev = nullptr;
  lv_indev_t *indev = lv_indev_get_next(nullptr);

  while (indev) {
    if (lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
      encoder_indev = indev;
      break;
    }
    indev = lv_indev_get_next(indev);
  }

  if (!encoder_indev) return;

  // Check if encoder has new data
  lv_indev_data_t data;
  encoder_indev->driver->read_cb(encoder_indev->driver, &data);

  if (data.enc_diff != 0) {
    double old_scale = z_display_scale;
    spdlog::debug("TIMER: Mouse wheel detected: enc_diff={}, current_scale={}", data.enc_diff, old_scale);

    if (data.enc_diff > 0) {
      // Positive enc_diff = zoom out
      z_display_scale *= 0.9;
      if (z_display_scale < MIN_Z_SCALE) z_display_scale = MIN_Z_SCALE;
      spdlog::debug("TIMER: Zoom OUT: {} -> {} (clamped to min {})", old_scale, z_display_scale, MIN_Z_SCALE);
    } else if (data.enc_diff < 0) {
      // Negative enc_diff = zoom in
      z_display_scale *= 1.1;
      if (z_display_scale > MAX_Z_SCALE) z_display_scale = MAX_Z_SCALE;
      spdlog::debug("TIMER: Zoom IN: {} -> {} (clamped to max {})", old_scale, z_display_scale, MAX_Z_SCALE);
    }

    if (old_scale != z_display_scale) {
      spdlog::debug("TIMER: Scale changed, redrawing 3D mesh");
      draw_3d_mesh();
    }
  }
#endif
}

void BedMeshPanel::handle_canvas_gesture(lv_event_t *event)
{
  if (!show_3d_view) return;  // Only zoom in 3D view mode

  lv_indev_t * indev = lv_indev_get_act();
  if (!indev) return;

  // TODO: Implement pinch gesture detection
  // This requires LVGL gesture recognition to be enabled
  // and proper multi-touch input processing
  spdlog::debug("Gesture event detected (pinch zoom not yet implemented)");
}
