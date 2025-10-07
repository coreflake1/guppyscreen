#ifndef __BEDMESH_PANEL_H__
#define __BEDMESH_PANEL_H__

#include "websocket_client.h"
#include "notify_consumer.h"
#include "button_container.h"
#include "lvgl/lvgl.h"
#include "spdlog/spdlog.h"

#include <mutex>
#include <vector>

class BedMeshPanel : public NotifyConsumer {
 public:
  BedMeshPanel(KWebSocketClient &c, std::mutex &l);
  ~BedMeshPanel();

  void consume(json &j);
  void foreground();
  void refresh_views_with_lock(json &);
  void refresh_views(json &);
  void refresh_profile_info(std::string pname);
  void handle_callback(lv_event_t *event);
  void handle_profile_action(lv_event_t *event);
  void handle_prompt_save(lv_event_t *event);
  void handle_prompt_cancel(lv_event_t *event);
  void handle_kb_input(lv_event_t *e);
  void mesh_draw_cb(lv_event_t *e);
  void draw_3d_mesh();
  void resize_canvas();
  void handle_z_zoom_in(lv_event_t *event);
  void handle_z_zoom_out(lv_event_t *event);
  void handle_fov_zoom_in(lv_event_t *event);
  void handle_fov_zoom_out(lv_event_t *event);
  void handle_canvas_drag(lv_event_t *event);
  void handle_toggle_view(lv_event_t *event);
  void handle_canvas_scroll(lv_event_t *event);
  void handle_canvas_gesture(lv_event_t *event);
  void scroll_check_timer_callback();
  bool detect_multitouch_support();

  // Basic 3D point with screen projection for mesh vertices
  struct Point3D {
    double x, y, z;           // 3D world coordinates
    int screen_x, screen_y;   // 2D screen coordinates after perspective projection
    double depth;             // Z-depth for sorting (closer objects rendered last)
  };

  // 3D vertex with color information for smooth interpolation
  struct Vertex3D {
    double x, y, z;           // 3D position in world space
    lv_color_t color;         // Color at this vertex (for gradient blending)
  };

  // 3D quadrilateral representing one mesh cell with depth sorting
  struct Quad3D {
    Vertex3D vertices[4];     // Four corners of the quad (bottom-left, bottom-right, top-left, top-right)
    double avg_depth;         // Average depth for back-to-front sorting
    lv_color_t center_color;  // Fallback color if vertex blending not used
  };

  void draw_mesh_wireframe(const std::vector<std::vector<BedMeshPanel::Point3D>>& points,
                          int rows, int cols, int canvas_width, int canvas_height);
  void draw_axes_and_labels(const std::vector<std::vector<BedMeshPanel::Point3D>>& points,
                           int rows, int cols, int canvas_width, int canvas_height,
                           double min_z, double max_z, double z_scale);
  void draw_mesh_surface(const std::vector<std::vector<BedMeshPanel::Point3D>>& points,
                        int rows, int cols, int canvas_width, int canvas_height,
                        double min_z, double max_z);

  // 3D rendering pipeline functions
  Point3D project_3d_to_2d(double x, double y, double z, int canvas_width, int canvas_height);  // Perspective projection
  void draw_3d_quad(const Quad3D& quad, int canvas_width, int canvas_height);                    // Render a single 3D quad
  std::vector<Quad3D> generate_mesh_quads(int rows, int cols, double min_z, double max_z);       // Create 3D quads from mesh data
  void sort_quads_by_depth(std::vector<Quad3D>& quads);                                         // Z-buffer sorting (back to front)

  // Triangle rasterization for proper 3D shapes
  void fill_triangle(int x1, int y1, int x2, int y2, int x3, int y3, lv_color_t color, int canvas_width, int canvas_height);

  // Triangle rasterization with gradient interpolation between vertex colors
  // Parameters: (x1,y1,c1), (x2,y2,c2), (x3,y3,c3) are the three vertices with positions and colors
  void fill_triangle_gradient(int x1, int y1, lv_color_t c1, int x2, int y2, lv_color_t c2, int x3, int y3, lv_color_t c3, int canvas_width, int canvas_height);

  // Color gradient band rendering
  void draw_color_gradient_band(int canvas_width, int canvas_height, double min_z, double max_z);

  // Axis drawing helper methods
  void draw_horizontal_axes(double mesh_origin_x, double mesh_origin_y, double grid_reference_z,
                           int rows, int cols, int canvas_width, int canvas_height,
                           const lv_draw_line_dsc_t& axis_dsc);


  // Helper methods for reducing code duplication
  void setup_button_with_label(lv_obj_t* btn, const char* text, int width, int height, lv_event_cb_t callback);
  void draw_grid_lines_parallel_to_axis(int axis_count, bool is_x_axis, int rows, int cols, double grid_reference_z, int canvas_width, int canvas_height, const lv_draw_line_dsc_t& line_dsc);
  bool create_and_check_label_area(lv_area_t& area, int x, int y, int width, int height, int canvas_width, int canvas_height);
  void extract_mesh_parameter_values(const json& mesh_params, const std::vector<const char*>& param_names, std::vector<int>& values);
  double calculate_dynamic_fov_scale(int mesh_rows, int mesh_cols, int canvas_width, int canvas_height);

  // Axis tick drawing helper functions
  void draw_axis_tick_values(int count, bool is_x_axis, int rows, int cols, double grid_reference_z,
                            int canvas_width, int canvas_height, lv_draw_label_dsc_t& label_dsc);
  void draw_z_axis_tick_values(double left_x, double front_y, double z_min, double z_max,
                              int z_divisions, double z_scale, int canvas_width, int canvas_height,
                              lv_draw_label_dsc_t& label_dsc, double min_z, double max_z);
  void draw_axis_label(const char* label_text, double x, double y, double z, int offset_x, int offset_y,
                      int canvas_width, int canvas_height, lv_draw_label_dsc_t& label_dsc);

  // Rendering constants
  static constexpr int CULLING_MARGIN = 50;        // Pixels beyond canvas bounds to include for triangle culling
  static constexpr double MESH_SCALE = 50.0;       // Base spacing between mesh points in 3D world units
  static constexpr double DEFAULT_Z_SCALE = 60.0;  // Height amplification factor for dramatic Z-axis visualization (increased for better visibility)

  // Dynamic Z-scaling constants for adaptive height visualization
  static constexpr double DEFAULT_Z_TARGET_HEIGHT = 80.0;  // Target height range in world units for good visual impact
  static constexpr double DEFAULT_Z_MIN_SCALE = 35.0;      // Minimum Z-scale factor to prevent flat surfaces
  static constexpr double DEFAULT_Z_MAX_SCALE = 120.0;     // Maximum Z-scale factor to prevent extreme projections off-screen

  // 3D projection constants for camera positioning and field of view
  static constexpr double CAMERA_DISTANCE = 450.0;         // Distance of camera from origin (affects perspective strength)
  static constexpr double CANVAS_PADDING_RATIO = 0.1;      // Padding as ratio of canvas size (0.1 = 10% padding on each side)
  static constexpr double VIEW_ANGLE_X_DEGREES = -85.0;   // Initial X-axis rotation (-85 degrees) for optimal perspective
  static constexpr double VIEW_ANGLE_Z_DEGREES = 10.0;    // Initial Z-axis rotation (10 degrees) for optimal perspective
  static constexpr double Z_ORIGIN_VERTICAL_POSITION = 0.4; // Position of Z=0 plane in canvas (0.0=top, 0.4=upper center, 1.0=bottom)

  // Z-axis range constants for 3D visualization
  static constexpr double Z_AXIS_EXTENSION = 0.75;  // How far to extend Z-axis beyond actual data range (as multiplier of z_range)

  // Color gradient constants for enhanced visual contrast
  static constexpr double COLOR_COMPRESSION_FACTOR = 0.8;  // Fraction of data range used for color mapping (smaller = more dramatic colors)

  // Axis label positioning constants
  static constexpr int AXIS_LABEL_OFFSET_X = 5;     // Horizontal offset of axis labels from axis line (pixels)
  static constexpr int AXIS_LABEL_OFFSET_Y = 20;    // Vertical offset of axis labels from axis line (pixels) - increased for better visibility
  static constexpr int AXIS_LABEL_MARGIN = 15;      // Additional margin for axis labels to avoid canvas edge (pixels)

  // Axis tick mark constants
  static constexpr int TICK_MARK_LENGTH = 6;         // Length of perpendicular tick marks (pixels)
  static constexpr int TICK_LABEL_OFFSET = 12;       // Distance of tick labels from axis line (pixels)

  // Label height constant
  static constexpr int LABEL_HEIGHT = 15;            // Standard height for text labels (pixels)

  // Color gradient band constants
  static constexpr int GRADIENT_BAND_WIDTH = 20;     // Width of the vertical color gradient band (pixels)
  static constexpr int GRADIENT_BAND_MARGIN = 10;    // Margin from left edge of canvas (pixels)
  static constexpr int GRADIENT_BAND_HEIGHT_RATIO = 80; // Percentage of canvas height to use for gradient band
  static constexpr int GRADIENT_BAND_TOTAL_WIDTH = 85; // Total space reserved for gradient band (band + margin + labels)

  // Triangle gradient performance constants
  static constexpr int GRADIENT_MAX_SEGMENTS = 6;     // Maximum segments per scanline for gradient approximation
  static constexpr int GRADIENT_MIN_LINE_WIDTH = 3;   // Minimum line width to use gradient (shorter lines use solid color)

  static void _handle_callback(lv_event_t *event) {
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_callback(event);
  };

  static void _handle_profile_action(lv_event_t *event) {
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_profile_action(event);
  };
  
  static void _handle_prompt_save(lv_event_t *event) {
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_prompt_save(event);
  };
  
  static void _handle_prompt_cancel(lv_event_t *event) {
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_prompt_cancel(event);
  };

  static void _handle_kb_input(lv_event_t *e) {
    BedMeshPanel *panel = (BedMeshPanel*)e->user_data;
    panel->handle_kb_input(e);
  };

  static void _mesh_draw_cb(lv_event_t *e) {
    BedMeshPanel *panel = (BedMeshPanel*)e->user_data;
    panel->mesh_draw_cb(e);
  };

  static void _handle_z_zoom_in(lv_event_t *event) {
    spdlog::debug("_handle_z_zoom_in static wrapper called");
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_z_zoom_in(event);
  };

  static void _handle_z_zoom_out(lv_event_t *event) {
    spdlog::debug("_handle_z_zoom_out static wrapper called");
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_z_zoom_out(event);
  };

  static void _handle_fov_zoom_in(lv_event_t *event) {
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_fov_zoom_in(event);
  };

  static void _handle_fov_zoom_out(lv_event_t *event) {
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_fov_zoom_out(event);
  };

  static void _handle_canvas_drag(lv_event_t *event) {
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_canvas_drag(event);
  };

  static void _handle_canvas_scroll(lv_event_t *event) {
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_canvas_scroll(event);
  };

  static void _handle_canvas_gesture(lv_event_t *event) {
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_canvas_gesture(event);
  };

  static void _scroll_check_timer_cb(lv_timer_t *timer) {
    BedMeshPanel *panel = (BedMeshPanel*)timer->user_data;
    panel->scroll_check_timer_callback();
  };

  static void _handle_toggle_view(lv_event_t *event) {
    BedMeshPanel *panel = (BedMeshPanel*)event->user_data;
    panel->handle_toggle_view(event);
  };


 private:
  KWebSocketClient &ws;
  lv_obj_t *cont;
  lv_obj_t *prompt;
  lv_obj_t *top_cont;
  lv_obj_t *display_cont;
  lv_obj_t *mesh_table;
  lv_obj_t *mesh_canvas;
  lv_obj_t *rotation_cont;
  lv_obj_t *z_zoom_in_btn;
  lv_obj_t *z_zoom_out_btn;
  lv_obj_t *fov_zoom_in_btn;
  lv_obj_t *fov_zoom_out_btn;
  lv_obj_t *profile_cont;
  lv_obj_t *profile_table;
  lv_obj_t *profile_info;
  lv_obj_t *controls_cont;
  ButtonContainer save_btn;
  ButtonContainer clear_btn;
  ButtonContainer calibrate_btn;
  ButtonContainer toggle_view_btn;
  ButtonContainer back_btn;
  lv_obj_t *msgbox;
  lv_obj_t *input;
  lv_obj_t *kb;
  std::string active_profile;
  std::vector<std::vector<double>> mesh;
  int current_view_angle;
  void *canvas_draw_buf;
  bool show_3d_view;
  double z_display_scale;
  double fov_scale;
  double camera_distance;
  double mesh_min_x;
  double mesh_max_x;
  double mesh_min_y;
  double mesh_max_y;

  // Touch drag state for rotation control
  bool is_dragging;
  int drag_start_x;
  int last_drag_x;
  int drag_start_y;
  int last_drag_y;
  double current_x_angle;  // Current X-axis viewing angle (tilt up/down)
  bool has_multitouch;
  bool use_gesture_zoom;
  bool mouse_over_canvas;
  lv_timer_t* scroll_check_timer;
};

#endif // __BEDMESH_PANEL_H__
