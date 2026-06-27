#include "exclude_object_panel.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

LV_IMG_DECLARE(back);

static bool point_in_polygon(lv_coord_t x, lv_coord_t y, const std::vector<lv_point_t> &poly) {
  if (poly.size() < 3) return false;
  bool inside = false;
  size_t j = poly.size() - 1;
  for (size_t i = 0; i < poly.size(); j = i++) {
    const lv_point_t &pi = poly[i];
    const lv_point_t &pj = poly[j];
    if ((pi.y > y) != (pj.y > y)) {
      double xi = static_cast<double>(pj.x - pi.x) * static_cast<double>(y - pi.y)
                  / static_cast<double>(pj.y - pi.y) + static_cast<double>(pi.x);
      if (static_cast<double>(x) < xi) inside = !inside;
    }
  }
  return inside;
}

// Square canvas for the bed map; the bed is square on the KE (220x220) so a
// square keeps the aspect ratio without letterboxing.
static const lv_coord_t CANVAS_DIM = 250;
static const lv_coord_t MARGIN = 10;  // px inset so edge objects aren't clipped

ExcludeObjectPanel::ExcludeObjectPanel(KWebSocketClient &websocket_client, std::mutex &l)
  : NotifyConsumer(l)
  , ws(websocket_client)
  , panel_cont(lv_obj_create(lv_scr_act()))
  , canvas(lv_canvas_create(panel_cont))
  , canvas_buf((lv_color_t *)malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(CANVAS_DIM, CANVAS_DIM)))
  , info_cont(lv_obj_create(panel_cont))
  , title_label(lv_label_create(info_cont))
  , status_label(lv_label_create(info_cont))
  , back_btn(info_cont, &back, "Back", &ExcludeObjectPanel::_handle_callback, this)
{
  lv_obj_move_background(panel_cont);
  lv_obj_clear_flag(panel_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(panel_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(panel_cont, 6, 0);

  // Bed-map canvas on the left, framed like the app's other cards.
  lv_canvas_set_buffer(canvas, canvas_buf, CANVAS_DIM, CANVAS_DIM, LV_IMG_CF_TRUE_COLOR);
  lv_obj_align(canvas, LV_ALIGN_LEFT_MID, 4, 0);
  lv_obj_set_style_border_width(canvas, 2, 0);
  lv_obj_set_style_border_color(canvas, lv_palette_darken(LV_PALETTE_GREY, 2), 0);
  lv_obj_set_style_radius(canvas, 4, 0);
  lv_canvas_fill_bg(canvas, lv_color_make(30, 30, 30), LV_OPA_COVER);

  // The canvas (an lv_img) isn't clickable, so a tap on the bed map routes up to
  // the nearest clickable ancestor - panel_cont. Handle taps there and hit-test
  // against the drawn object boxes (handle_canvas_click works off absolute
  // coords minus the canvas position, so it doesn't care which object caught it).
  lv_obj_add_flag(panel_cont, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(panel_cont, &ExcludeObjectPanel::_handle_canvas_click,
                      LV_EVENT_RELEASED, this);

  // Right-hand info column: a centered flex stack of title, legend and Back so
  // everything lines up tidily next to the bed map.
  lv_obj_set_style_border_width(info_cont, 0, 0);
  lv_obj_set_style_bg_opa(info_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(info_cont, 4, 0);
  lv_obj_clear_flag(info_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(info_cont, LV_PCT(40), LV_PCT(100));
  lv_obj_align(info_cont, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_flex_flow(info_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(info_cont, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_label_set_text(title_label, "Exclude Object");
  lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, 0);

  lv_label_set_recolor(status_label, true);  // legend colours match the bed map
  lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(status_label, LV_PCT(100));
  lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_set_width(back_btn.get_container(), LV_PCT(80));

  ws.register_notify_update(this);
}

ExcludeObjectPanel::~ExcludeObjectPanel() {
  if (panel_cont != NULL) {
    lv_obj_del(panel_cont);
    panel_cont = NULL;
  }
  if (canvas_buf != NULL) {
    free(canvas_buf);
    canvas_buf = NULL;
  }
}

void ExcludeObjectPanel::foreground() {
  // Called from a button callback, which already runs under lv_lock (held by the
  // main loop) - taking it again would deadlock the UI. consume() locks itself
  // because it runs on the websocket thread.
  is_foreground = true;
  load_bed_bounds();
  redraw();
  lv_obj_move_foreground(panel_cont);
}

void ExcludeObjectPanel::consume(json &j) {
  // Only the exclude_object object matters here; redraw while visible so the
  // current-object highlight and freshly-excluded items stay in sync.
  auto eo = j["/params/0/exclude_object"_json_pointer];
  if (eo.is_null() || !is_foreground) {
    return;
  }
  std::lock_guard<std::mutex> lock(lv_lock);
  redraw();
}

void ExcludeObjectPanel::load_bed_bounds() {
  auto s = State::get_instance();
  auto amin = s->get_data("/printer_state/toolhead/axis_minimum"_json_pointer);
  auto amax = s->get_data("/printer_state/toolhead/axis_maximum"_json_pointer);
  if (amin.is_array() && amin.size() >= 2 && amax.is_array() && amax.size() >= 2) {
    bed_min_x = amin[0].template get<double>();
    bed_min_y = amin[1].template get<double>();
    bed_max_x = amax[0].template get<double>();
    bed_max_y = amax[1].template get<double>();
  }
  // Guard against a degenerate range so the transform never divides by zero.
  if (bed_max_x - bed_min_x < 1.0) { bed_min_x = 0.0; bed_max_x = 220.0; }
  if (bed_max_y - bed_min_y < 1.0) { bed_min_y = 0.0; bed_max_y = 220.0; }
}

lv_point_t ExcludeObjectPanel::to_px(double mx, double my) {
  double bw = bed_max_x - bed_min_x;
  double bh = bed_max_y - bed_min_y;
  double avail = CANVAS_DIM - 2 * MARGIN;
  double scale = avail / std::max(bw, bh);
  double ox = MARGIN + (avail - bw * scale) / 2.0;
  double oy = MARGIN + (avail - bh * scale) / 2.0;
  lv_point_t p;
  p.x = (lv_coord_t)std::lround(ox + (mx - bed_min_x) * scale);
  // Flip Y: bed Y grows toward the rear, screen Y grows downward.
  p.y = (lv_coord_t)std::lround(CANVAS_DIM - oy - (my - bed_min_y) * scale);
  return p;
}

void ExcludeObjectPanel::redraw() {
  obj_boxes.clear();
  lv_canvas_fill_bg(canvas, lv_color_make(30, 30, 30), LV_OPA_COVER);

  // Bed outline.
  lv_point_t bl = to_px(bed_min_x, bed_min_y);
  lv_point_t tr = to_px(bed_max_x, bed_max_y);
  lv_draw_rect_dsc_t bed_dsc;
  lv_draw_rect_dsc_init(&bed_dsc);
  bed_dsc.bg_opa = LV_OPA_TRANSP;
  bed_dsc.border_color = lv_palette_darken(LV_PALETTE_GREY, 2);
  bed_dsc.border_width = 2;
  bed_dsc.border_opa = LV_OPA_COVER;
  lv_canvas_draw_rect(canvas, tr.x, tr.y, bl.x - tr.x, bl.y - tr.y, &bed_dsc);

  auto s = State::get_instance();
  auto objects = s->get_data("/printer_state/exclude_object/objects"_json_pointer);
  auto excluded = s->get_data("/printer_state/exclude_object/excluded_objects"_json_pointer);
  auto current = s->get_data("/printer_state/exclude_object/current_object"_json_pointer);
  std::string current_name = current.is_string() ? current.template get<std::string>() : "";

  auto is_excluded = [&excluded](const std::string &name) {
    if (!excluded.is_array()) return false;
    for (auto &e : excluded) {
      if (e.is_string() && e.template get<std::string>() == name) return true;
      if (e.is_object() && e.contains("name") && e["name"] == name) return true;
    }
    return false;
  };

  if (!objects.is_array() || objects.empty()) {
    lv_label_set_text(status_label, "No excludable objects.\n\nThe gcode must be sliced\nwith object labels.");
    return;
  }

  int idx = 0;
  int n_excluded = 0;
  for (auto &obj : objects) {
    if (!obj.contains("name")) continue;
    std::string name = obj["name"].template get<std::string>();
    bool excl = is_excluded(name);
    bool cur = (name == current_name);
    if (excl) n_excluded++;

    lv_color_t color = excl ? lv_palette_darken(LV_PALETTE_RED, 2)
                            : (cur ? lv_palette_main(LV_PALETTE_GREEN)
                                   : lv_palette_main(LV_PALETTE_BLUE));

    // Collect the polygon (fall back to a small box around the center).
    std::vector<lv_point_t> pts;
    if (obj.contains("polygon") && obj["polygon"].is_array() && !obj["polygon"].empty()) {
      for (auto &v : obj["polygon"]) {
        if (v.is_array() && v.size() >= 2)
          pts.push_back(to_px(v[0].template get<double>(), v[1].template get<double>()));
      }
    } else if (obj.contains("center") && obj["center"].is_array() && obj["center"].size() >= 2) {
      double cx = obj["center"][0].template get<double>();
      double cy = obj["center"][1].template get<double>();
      pts.push_back(to_px(cx - 5, cy - 5));
      pts.push_back(to_px(cx + 5, cy - 5));
      pts.push_back(to_px(cx + 5, cy + 5));
      pts.push_back(to_px(cx - 5, cy + 5));
    }
    if (pts.empty()) continue;

    // Bounding box for the fill + tap target.
    lv_coord_t x0 = pts[0].x, y0 = pts[0].y, x1 = pts[0].x, y1 = pts[0].y;
    for (auto &p : pts) {
      x0 = std::min(x0, p.x); y0 = std::min(y0, p.y);
      x1 = std::max(x1, p.x); y1 = std::max(y1, p.y);
    }
    obj_boxes.push_back({name, x0, y0, x1, y1, excl, pts});

    // Polygon outline.
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = color;
    line.width = 2;
    line.opa = LV_OPA_COVER;
    for (size_t i = 0; i < pts.size(); i++) {
      lv_point_t seg[2] = {pts[i], pts[(i + 1) % pts.size()]};
      lv_canvas_draw_line(canvas, seg, 2, &line);
    }

    // Cross out excluded objects.
    if (excl) {
      lv_point_t d1[2] = {{x0, y0}, {x1, y1}};
      lv_point_t d2[2] = {{x0, y1}, {x1, y0}};
      lv_canvas_draw_line(canvas, d1, 2, &line);
      lv_canvas_draw_line(canvas, d2, 2, &line);
    }

    // Index number at the centroid of the bounding box.
    lv_draw_label_dsc_t lbl;
    lv_draw_label_dsc_init(&lbl);
    lbl.color = lv_color_white();
    lbl.font = &lv_font_montserrat_14;
    lv_canvas_draw_text(canvas, (x0 + x1) / 2 - 6, (y0 + y1) / 2 - 8, 20, &lbl,
                        std::to_string(idx + 1).c_str());
    idx++;
  }

  lv_label_set_text(status_label,
    fmt::format("#4caf50 Printing now#\n"
                "#2196f3 Tap to exclude#\n"
                "#b71c1c Excluded#\n\n"
                "{} object(s), {} excluded",
                (int)objects.size(), n_excluded).c_str());
}

void ExcludeObjectPanel::handle_canvas_click(lv_event_t *e) {
  (void)e;
  // Ignore taps while a confirm dialog is already up (the resistive panel can
  // bounce a single tap into several release events).
  if (confirm_mbox != nullptr) return;

  lv_indev_t *indev = lv_indev_get_act();
  if (indev == NULL) return;

  lv_point_t point;
  lv_indev_get_point(indev, &point);

  // Translate the absolute touch point into canvas-local pixels.
  lv_area_t coords;
  lv_obj_get_coords(canvas, &coords);
  lv_coord_t cx = point.x - coords.x1;
  lv_coord_t cy = point.y - coords.y1;

  // Pick the smallest box that contains the tap (topmost when objects overlap),
  // skipping already-excluded ones.
  const ObjBox *hit = nullptr;
  long best_area = 0;
  for (auto &b : obj_boxes) {
    if (b.excluded) continue;
    if (point_in_polygon(cx, cy, b.polygon)) {
      long area = (long)(b.x1 - b.x0) * (b.y1 - b.y0);
      if (hit == nullptr || area < best_area) { hit = &b; best_area = area; }
    }
  }
  if (hit != nullptr) {
    confirm_exclude(hit->name);
  }
}

void ExcludeObjectPanel::confirm_exclude(const std::string &name) {
  pending_name = name;
  // Object names come from the slicer's labels (often the part/STL name) and can
  // be long - ellipsize so the prompt stays one line and never overruns the card
  // or the button row. The full name still goes to EXCLUDE_OBJECT.
  std::string shown = name;
  const size_t MAXLEN = 22;
  if (shown.size() > MAXLEN) {
    shown = shown.substr(0, MAXLEN) + "...";
  }

  // Stronger verb when excluding the object that's printing right now.
  auto cur = State::get_instance()->get_data("/printer_state/exclude_object/current_object"_json_pointer);
  bool printing_now = cur.is_string() && cur.template get<std::string>() == name;
  std::string msg = printing_now
    ? fmt::format("Stop printing \"{}\"?\nThis cannot be undone.", shown)
    : fmt::format("Exclude \"{}\"?\nThis cannot be undone.", shown);

  static const char *btns[] = {"Exclude", "Cancel", ""};
  lv_obj_t *mbox = lv_msgbox_create(NULL, NULL, msg.c_str(), btns, false);
  confirm_mbox = mbox;
  lv_obj_add_event_cb(mbox, [](lv_event_t *ev) {
    lv_obj_t *obj = lv_obj_get_parent(lv_event_get_target(ev));
    auto *self = static_cast<ExcludeObjectPanel *>(lv_event_get_user_data(ev));
    if (lv_msgbox_get_active_btn(obj) == 0) {
      self->do_exclude();
    }
    self->confirm_mbox = nullptr;
    lv_msgbox_close(obj);
  }, LV_EVENT_VALUE_CHANGED, this);
  KUtils::style_lock_mbox(mbox, 90);  // centered body + centered button row
  // Pin the body to the top so it can't collide with the floating button row
  // (this dialog's message is taller than the one-line print-lock prompts).
  lv_obj_align(((lv_msgbox_t *)mbox)->text, LV_ALIGN_TOP_MID, 0, 0);
}

void ExcludeObjectPanel::do_exclude() {
  if (pending_name.empty()) return;
  spdlog::info("excluding object {}", pending_name);
  ws.gcode_script(fmt::format("EXCLUDE_OBJECT NAME={}", pending_name));
  pending_name.clear();
}

void ExcludeObjectPanel::handle_callback(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t *btn = lv_event_get_current_target(e);
  if (btn == back_btn.get_container()) {
    is_foreground = false;
    lv_obj_move_background(panel_cont);
  }
}
