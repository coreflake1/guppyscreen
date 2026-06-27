#include "print_panel.h"
#include "file_panel.h"
#include "spoolman_panel.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <cstdlib>
#include <map>
#include <sstream>

LV_IMG_DECLARE(info_img);
LV_IMG_DECLARE(print);
LV_IMG_DECLARE(back);

#define SORTED_BY_NAME 1 << 0
#define SORTED_BY_MODIFIED  1 << 1

PrintPanel::PrintPanel(KWebSocketClient &websocket, std::mutex &lock, PrintStatusPanel &ps, SpoolmanPanel &spool)
  : NotifyConsumer(lock)
  , ws(websocket)
  , sm(spool)
  , files_cont(lv_obj_create(lv_scr_act()))
  , filament_cont(lv_obj_create(lv_scr_act()))
  , filament_box(lv_obj_create(filament_cont))
  , filament_content_cont(lv_obj_create(filament_box))
  , filament_row_cont(lv_obj_create(filament_content_cont))
  , filament_swatch(lv_obj_create(filament_row_cont))
  , filament_name_label(lv_label_create(filament_row_cont))
  , filament_detail_label(lv_label_create(filament_content_cont))
  , filament_enough_label(lv_label_create(filament_content_cont))
  , filament_yes_btn(lv_btn_create(filament_box))
  , filament_no_btn(lv_btn_create(filament_box))
  , left_cont(lv_obj_create(files_cont))
  , file_table_btns(lv_obj_create(left_cont))
  , refresh_btn(lv_btn_create(file_table_btns))
  , modified_sort_btn(lv_btn_create(file_table_btns))
  , az_sort_btn(lv_btn_create(file_table_btns))
  , file_table(lv_table_create(left_cont))
  , file_view(lv_obj_create(files_cont))
  , status_btn(file_view, &info_img, "Status", &PrintPanel::_handle_status_btn, this)
  , print_btn(file_view, &print, "Print", &PrintPanel::_handle_print_callback, this)
  , back_btn(file_view, &back, "Back", &PrintPanel::_handle_back_btn, this)
  , root("", "", 0)
  , cur_dir(&root)
  , cur_file(NULL)
  , file_panel(file_view)
  , print_status(ps)
  , sort_mode(SORTED_BY_MODIFIED)
  , sort_reversed(true)
{
  spdlog::trace("building print panel");
  lv_obj_move_background(files_cont);

  lv_obj_set_size(files_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(files_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(files_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_all(files_cont, 0, 0);

  // left side cont
  lv_obj_set_height(left_cont, LV_PCT(100));
  lv_obj_clear_flag(left_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(left_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_grow(left_cont, 1);
  lv_obj_set_style_pad_all(left_cont, 0, 0);

  // file view buttons
  lv_obj_t *label = NULL;

  /* Explicit m10 override - theme cascade alone isn't reaching these labels for
   * some reason, possibly because they're children of plain lv_btn which carries
   * its own default style. Forcing the font here matches the rest of the UI. */
  label = lv_label_create(refresh_btn);
  lv_label_set_text(label, LV_SYMBOL_REFRESH " Reload");
  lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
  lv_obj_center(label);

  label = lv_label_create(modified_sort_btn);
  lv_label_set_text(label, LV_SYMBOL_LIST " Modified");
  lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
  lv_obj_center(label);

  label = lv_label_create(az_sort_btn);
  lv_label_set_text(label, LV_SYMBOL_LIST " A-Z");
  lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
  lv_obj_center(label);

  lv_obj_add_event_cb(refresh_btn, &PrintPanel::_handle_btns, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(modified_sort_btn, &PrintPanel::_handle_btns, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(az_sort_btn, &PrintPanel::_handle_btns, LV_EVENT_CLICKED, this);

  lv_obj_set_style_pad_hor(refresh_btn, 19, 0);
  lv_obj_set_style_pad_hor(modified_sort_btn, 19, 0);
  lv_obj_set_style_pad_hor(az_sort_btn, 19, 0);

  lv_obj_set_size(file_table_btns, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(file_table_btns, 0, 0);

  lv_obj_clear_flag(file_table_btns, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(file_table_btns, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(file_table_btns, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_set_size(file_table, LV_PCT(100), LV_PCT(100));
  lv_table_set_col_width(file_table, 0, LV_PCT(100));
  lv_table_set_col_cnt(file_table, 1);
  lv_obj_add_event_cb(file_table, &PrintPanel::_handle_callback, LV_EVENT_ALL, this);
  lv_obj_set_scroll_dir(file_table, LV_DIR_TOP | LV_DIR_BOTTOM);

  lv_obj_set_height(file_view, LV_PCT(100));
  lv_obj_clear_flag(file_view, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_grow(file_view, 1);

  static lv_coord_t grid_main_row_dsc[] = {LV_GRID_FR(3), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t grid_main_col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(file_view, grid_main_col_dsc, grid_main_row_dsc);
  lv_obj_set_grid_cell(file_panel.get_container(), LV_GRID_ALIGN_STRETCH, 0, 3, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_style_pad_all(file_panel.get_container(), 0, 0);

  lv_obj_set_grid_cell(status_btn.get_container(), LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(print_btn.get_container(), LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(back_btn.get_container(), LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 1, 1);

  lv_obj_move_foreground(back_btn.get_container());
  lv_obj_move_foreground(print_btn.get_container());
  lv_obj_move_foreground(status_btn.get_container());

  // spoolman "use the same filament?" confirm dialog (shared dialog look)
  KUtils::style_dialog_overlay(filament_cont);
  lv_obj_add_flag(filament_cont, LV_OBJ_FLAG_HIDDEN);

  KUtils::style_dialog_box(filament_box);
  lv_obj_set_size(filament_box, LV_PCT(80), LV_PCT(74));
  lv_obj_align(filament_box, LV_ALIGN_CENTER, 0, 0);

  // header (text swapped per spool state in show_filament_dialog)
  filament_title_label = lv_label_create(filament_box);
  lv_label_set_text(filament_title_label, "Use the same filament?");
  KUtils::style_dialog_title(filament_title_label);

  // content block: a centered group of rows, centered in the box
  lv_obj_remove_style_all(filament_content_cont);
  lv_obj_set_width(filament_content_cont, LV_PCT(100));
  lv_obj_set_height(filament_content_cont, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(filament_content_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(filament_content_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(filament_content_cont, 7, 0);
  lv_obj_clear_flag(filament_content_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(filament_content_cont, LV_ALIGN_CENTER, 0, 2);

  // swatch + name, centered together as a group
  lv_obj_remove_style_all(filament_row_cont);
  lv_obj_set_width(filament_row_cont, LV_SIZE_CONTENT);
  lv_obj_set_height(filament_row_cont, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(filament_row_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(filament_row_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(filament_row_cont, 8, 0);
  lv_obj_clear_flag(filament_row_cont, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_size(filament_swatch, 20, 20);
  lv_obj_set_style_border_width(filament_swatch, 1, 0);
  lv_obj_set_style_border_color(filament_swatch, lv_color_white(), 0);
  lv_obj_set_style_radius(filament_swatch, 4, 0);
  lv_obj_clear_flag(filament_swatch, LV_OBJ_FLAG_SCROLLABLE);

  lv_label_set_long_mode(filament_name_label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(filament_name_label, 280, 0);
  lv_obj_set_style_text_font(filament_name_label, &lv_font_montserrat_14, 0);
  lv_label_set_text(filament_name_label, "");

  lv_obj_set_style_text_align(filament_detail_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(filament_detail_label, "");
  lv_obj_set_style_text_align(filament_enough_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(filament_enough_label, "");

  lv_obj_set_size(filament_yes_btn, 96, LV_SIZE_CONTENT);
  lv_obj_add_event_cb(filament_yes_btn, &PrintPanel::_handle_filament_btns, LV_EVENT_CLICKED, this);
  lv_obj_align(filament_yes_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  label = lv_label_create(filament_yes_btn);
  lv_label_set_text(label, "Yes");
  lv_obj_center(label);

  lv_obj_set_size(filament_no_btn, 96, LV_SIZE_CONTENT);
  lv_obj_add_event_cb(filament_no_btn, &PrintPanel::_handle_filament_btns, LV_EVENT_CLICKED, this);
  lv_obj_align(filament_no_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  label = lv_label_create(filament_no_btn);
  lv_label_set_text(label, "No");
  lv_obj_center(label);

  ws.register_notify_update(this);
}

PrintPanel::~PrintPanel() {
  if (files_cont != NULL) {
    lv_obj_del(files_cont);
    files_cont = NULL;
  }

}

void PrintPanel::populate_files(json &j) {
  sort_mode = SORTED_BY_MODIFIED;
  sort_reversed = true;
  show_dir(cur_dir);
}

void PrintPanel::consume(json &j) {
  if (j["method"] == "notify_filelist_changed") {
    if (refreshing_files) {
      refresh_pending = true;
    } else {
      subscribe();
    }
    return;
  }
  json &pstat_state = j["/params/0/print_stats/state"_json_pointer];
  if (pstat_state.is_null()) {
    return;
  }

  std::lock_guard<std::mutex> lock(lv_lock);
  if (pstat_state.template get<std::string>() != "printing"
    && pstat_state.template get<std::string>() != "paused") {
    status_btn.disable();
  } else {
    status_btn.enable();
  }
}

void PrintPanel::subscribe() {
  refreshing_files = true;
  refresh_pending = false;
  ws.send_jsonrpc("server.files.list", R"({"root":"gcodes"})"_json, [this](json &d) {
    std::lock_guard<std::mutex> lock(lv_lock);
    std::string cur_path = cur_dir->full_path;
    root.clear();
    cur_file = NULL;
    cur_dir = NULL;

    if (d.contains("result")) {
      for (auto f : d["result"]) {
        root.add_path(KUtils::split(f["path"], '/'), f["path"], f["modified"].template get<uint32_t>());
      }
    }
    Tree *dir = root.find_path(KUtils::split(cur_path, '/'));
    // need to simply this using the directory endpoint
    cur_dir = dir;
    this->populate_files(d);
    refreshing_files = false;
    if (refresh_pending) {
      refresh_pending = false;
      subscribe();
    }
    });
}

void PrintPanel::foreground() {
  json pstat_state = State::get_instance()
    ->get_data("/printer_state/print_stats/state"_json_pointer);
  spdlog::debug("print panel print stats {}",
    pstat_state.is_null() ? "nil" : pstat_state.template get<std::string>());

  if (!pstat_state.is_null()
    && pstat_state.template get<std::string>() != "printing"
    && pstat_state.template get<std::string>() != "paused") {
    status_btn.disable();
  } else {
    status_btn.enable();
  }

  lv_obj_move_foreground(files_cont);
  show_dir(cur_dir);
  subscribe();
}

void PrintPanel::handle_callback(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_VALUE_CHANGED) {
    const char *str_fn = NULL;
    uint16_t row;
    uint16_t col;

    lv_table_get_selected_cell(file_table, &row, &col);
    uint16_t row_count = lv_table_get_row_cnt(file_table);
    if (row == LV_TABLE_CELL_NONE || col == LV_TABLE_CELL_NONE || row >= row_count) {
      return;
    }

    str_fn = lv_table_get_cell_value(file_table, row, col);

    const char *filename = str_fn + 5; // +5 skips the LV_SYMBOL and spaces
    if (std::memcmp(LV_SYMBOL_DIRECTORY, str_fn, 3) == 0) {
      if ((strcmp(filename, "..") == 0)) {
        if (cur_dir->parent != cur_dir) {
          cur_dir = cur_dir->parent;
          show_dir(cur_dir);
        }
      } else {
        Tree *dir = cur_dir->get_child(filename);
        if (dir != NULL) {
          cur_dir = dir;
          show_dir(cur_dir);
        }
      }
    } else {
      Tree *next = cur_dir->get_child(filename);
      if (next != nullptr && (cur_file == nullptr || cur_file->full_path != next->full_path)) {
        cur_file = next;
        show_file_detail(cur_file);
      }
    }
  }
}

void PrintPanel::show_dir(Tree *dir) {
  uint32_t index = 0;
  lv_table_set_cell_value_fmt(file_table, index++, 0, LV_SYMBOL_DIRECTORY "  %s", "..");

  std::vector<Tree> sorted_files;
  if (sort_mode == SORTED_BY_MODIFIED) {
    KUtils::sort_map_values<std::string, Tree>(dir->children, sorted_files, [this](Tree &x, Tree &y) {
      if (x.is_leaf() && !y.is_leaf()) {
        return false;
      } else if (!x.is_leaf() && y.is_leaf()) {
        return true;
      }

      return sort_reversed ? x.date_modified > y.date_modified : y.date_modified > x.date_modified;
      });
  } else {
    KUtils::sort_map_values<std::string, Tree>(dir->children, sorted_files, [this](Tree &x, Tree &y) {
      if (x.is_leaf() && !y.is_leaf()) {
        return false;
      } else if (!x.is_leaf() && y.is_leaf()) {
        return true;
      }

      return sort_reversed ? x.name > y.name : y.name > x.name;
      });
  }
  for (const auto &c : sorted_files) {
    if (c.is_leaf()) {
      lv_table_set_cell_value_fmt(file_table, index, 0, LV_SYMBOL_FILE "  %s", c.name.c_str());
    } else {
      lv_table_set_cell_value_fmt(file_table, index, 0, LV_SYMBOL_DIRECTORY "  %s", c.name.c_str());
    }
    index++;
  }

  lv_table_set_row_cnt(file_table, index);
  lv_obj_scroll_to_y(file_table, 0, LV_ANIM_OFF);

  // XXX: maybe use the directory instead of file endpoint in moonraker
  for (auto &c : sorted_files) {
    if (c.is_leaf()) {
      const auto &selected = dir->children.find(c.name);
      if (selected != dir->children.cend()) {
        cur_file = &selected->second;
        show_file_detail(cur_file);
      }
      break;
    }
  }

}

void PrintPanel::show_file_detail(Tree *f) {
  if (!f->is_leaf()) return;
  if (f->contains_metadata()) {
    file_panel.refresh_view(f->metadata, f->full_path);
  } else {
    spdlog::trace("getting metadata for {}", f->name);
    file_panel.show_loading(f->full_path);
    std::string path = f->full_path;
    ws.send_jsonrpc("server.files.metadata",
      json{{"filename", path}},
      [path, this](json &d) {
        std::lock_guard<std::mutex> lock(lv_lock);
        if (cur_file == nullptr || cur_file->full_path != path) return;
        if (d.contains("result")) {
          cur_file->set_metadata(d);
          file_panel.refresh_view(cur_file->metadata, cur_file->full_path);
        } else {
          spdlog::warn("metadata fetch failed for {}", path);
          file_panel.show_no_metadata();
        }
      });
  }
}

void PrintPanel::handle_back_btn(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);
  if (btn == back_btn.get_container()) {
    lv_obj_move_background(files_cont);
    print_status.background();
  }
}

void PrintPanel::handle_print_callback(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_CLICKED && cur_file != NULL) {

    json pstat_state = State::get_instance()
      ->get_data("/printer_state/print_stats/state"_json_pointer);
    spdlog::debug("print panel print stats {}",
      pstat_state.is_null() ? "nil" : pstat_state.template get<std::string>());

    if (!pstat_state.is_null()
      && pstat_state.template get<std::string>() != "printing"
      && pstat_state.template get<std::string>() != "paused") {

      // ws.send_jsonrpc("printer.gcode.script",
      // 		    json::parse(R"({"script":"PRINT_PREPARE_CLEAR"})"));

      pending_print_path = cur_file->full_path;

      // filament required by this print, from the cached gcode metadata
      pending_needed_g = -1;
      pending_needed_mm = -1;
      if (cur_file->contains_metadata()) {
        auto w = cur_file->metadata["/result/filament_weight_total"_json_pointer];
        if (!w.is_null()) {
          pending_needed_g = w.template get<double>();
        }
        auto l = cur_file->metadata["/result/filament_total"_json_pointer];
        if (!l.is_null()) {
          pending_needed_mm = l.template get<double>();
        }
      }

      if (spoolman_enabled) {
        // ask whether to keep the currently selected spool before printing
        show_filament_dialog();
      } else {
        start_pending_print();
      }

    } else {
      KUtils::notify_toast("A print is already in progress.", 2500);
    }
  }
}

void PrintPanel::start_pending_print() {
  if (pending_print_path.empty()) {
    return;
  }
  spdlog::debug("printer ready to print. print file {}", pending_print_path);
  json fname_input = {{"filename", pending_print_path}};
  ws.send_jsonrpc("printer.print.start", fname_input);
  pending_print_path.clear();
  print_status.foreground();
}

void PrintPanel::show_filament_dialog() {
  json spool = sm.get_active_spool();

  if (spool.is_null()) {
    lv_label_set_text(filament_title_label, "No spool selected - print anyway?");
    lv_obj_add_flag(filament_swatch, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(filament_name_label, "");
    lv_label_set_text(filament_detail_label, "");
    lv_label_set_text(filament_enough_label, "");
  } else {
    lv_label_set_text(filament_title_label, "Use the same filament?");
    // name: "Vendor - Name"
    auto vendor_json = spool["/filament/vendor/name"_json_pointer];
    auto vendor = !vendor_json.is_null() ? vendor_json.template get<std::string>() : "";
    auto name_json = spool["/filament/name"_json_pointer];
    auto name = !name_json.is_null() ? name_json.template get<std::string>() : "";
    lv_label_set_text(filament_name_label, fmt::format("{} - {}", vendor, name).c_str());

    // color swatch from color_hex ("RRGGBB")
    auto color_json = spool["/filament/color_hex"_json_pointer];
    if (!color_json.is_null()) {
      uint32_t rgb = static_cast<uint32_t>(strtol(color_json.template get<std::string>().c_str(), NULL, 16));
      lv_obj_set_style_bg_color(filament_swatch, lv_color_hex(rgb), 0);
      lv_obj_clear_flag(filament_swatch, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(filament_swatch, LV_OBJ_FLAG_HIDDEN);
    }

    // material + remaining weight
    auto material_json = spool["/filament/material"_json_pointer];
    auto material = !material_json.is_null() ? material_json.template get<std::string>() : "?";
    auto remaining_json = spool["/remaining_weight"_json_pointer];
    if (!remaining_json.is_null()) {
      lv_label_set_text(filament_detail_label,
        fmt::format("{}, {:.0f} g left", material, remaining_json.template get<double>()).c_str());
    } else {
      lv_label_set_text(filament_detail_label, material.c_str());
    }

    // enough for this print? compare grams first, fall back to length
    lv_color_t ok_col = lv_palette_main(LV_PALETTE_GREEN);
    lv_color_t no_col = lv_palette_main(LV_PALETTE_RED);
    std::string verdict;
    bool known = false, enough = false;
    if (pending_needed_g > 0 && !remaining_json.is_null()) {
      known = true;
      enough = remaining_json.template get<double>() >= pending_needed_g;
      verdict = fmt::format("Print needs {:.0f} g  {}", pending_needed_g,
        enough ? LV_SYMBOL_OK " enough" : LV_SYMBOL_CLOSE " not enough");
    } else {
      auto rem_len_json = spool["/remaining_length"_json_pointer];
      if (pending_needed_mm > 0 && !rem_len_json.is_null()) {
        known = true;
        enough = rem_len_json.template get<double>() >= pending_needed_mm;
        verdict = fmt::format("Print needs {:.1f} m  {}", pending_needed_mm / 1000.0,
          enough ? LV_SYMBOL_OK " enough" : LV_SYMBOL_CLOSE " not enough");
      }
    }
    if (known) {
      lv_obj_set_style_text_color(filament_enough_label, enough ? ok_col : no_col, 0);
      lv_label_set_text(filament_enough_label, verdict.c_str());
    } else {
      lv_obj_set_style_text_color(filament_enough_label, lv_color_white(), 0);
      lv_label_set_text(filament_enough_label, "Filament usage unknown");
    }
  }

  lv_obj_clear_flag(filament_cont, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(filament_cont);
}

void PrintPanel::handle_filament_btns(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  lv_obj_t *btn = lv_event_get_current_target(event);

  // dismiss the dialog regardless of choice
  lv_obj_move_background(filament_cont);
  lv_obj_add_flag(filament_cont, LV_OBJ_FLAG_HIDDEN);

  if (btn == filament_yes_btn) {
    start_pending_print();
  } else if (btn == filament_no_btn) {
    // open Spoolman; once a spool is chosen there, re-show this dialog with the
    // newly selected filament so the user confirms before the print starts.
    sm.request_select_for_print([this]() { this->show_filament_dialog(); });
    sm.foreground();
  }
}

void PrintPanel::enable_spoolman() {
  spoolman_enabled = true;
}

void PrintPanel::handle_status_btn(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_CLICKED && cur_file != NULL) {
    spdlog::trace("status button clicked");
    print_status.foreground();
  }
}

void PrintPanel::handle_btns(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_CLICKED) {
    lv_obj_t *btn = lv_event_get_current_target(event);
    if (btn == refresh_btn) {
      subscribe();

    } else if (btn == modified_sort_btn) {
      if (sort_mode == SORTED_BY_MODIFIED) sort_reversed = !sort_reversed;
      else { sort_mode = SORTED_BY_MODIFIED; sort_reversed = false; }
      show_dir(cur_dir);

    } else if (btn == az_sort_btn) {
      if (sort_mode == SORTED_BY_NAME) sort_reversed = !sort_reversed;
      else { sort_mode = SORTED_BY_NAME; sort_reversed = false; }
      show_dir(cur_dir);
    }
  }
}
