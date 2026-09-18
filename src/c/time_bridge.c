#include <pebble.h>

// Time Bridge is intentionally resource-free. The face is drawn with Pebble's
// native Graphics API and the menus use MenuLayer.

#define SETTINGS_SCHEMA_VERSION 5
#define DEFAULT_ZONE_INDEX 1
#define RECENT_ZONE_LIMIT 5

enum {
  PERSIST_KEY_SETTINGS = 1,
  PERSIST_KEY_DATE_FORMAT = 2
};

typedef enum {
  DATE_FORMAT_DAY_MONTH_YEAR,
  DATE_FORMAT_DAY_NAME_YEAR,
  DATE_FORMAT_MONTH_DAY_YEAR,
  DATE_FORMAT_YEAR_MONTH_DAY,
  DATE_FORMAT_NAME_DAY_YEAR,
  DATE_FORMAT_COUNT
} DateFormat;

static const char *s_date_format_names[DATE_FORMAT_COUNT] = {
  "DD-MM-YYYY",
  "DD Mon, YYYY",
  "MM-DD-YYYY",
  "YYYY-MM-DD",
  "Mon DD, YYYY"
};

typedef struct {
  uint8_t dark_theme;
  uint8_t show_date;
  uint8_t use_24_hour;
  uint8_t schema_version;
  int8_t selected_zone;
  uint8_t recent_count;
  int8_t recent_zones[RECENT_ZONE_LIMIT];
} Settings;

typedef struct {
  const char *label;
  const char *subtitle;
  int16_t offset_minutes;
} TimeZone;

// Worldwide fixed-offset timezone catalogue. It includes every current civil
// UTC offset, including the 30- and 45-minute offsets. The watch stays fully
// offline; daylight-saving changes are outside this fixed-offset model.
static const TimeZone s_timezones[] = {
  { "UTC (+0:00)", "Coordinated Universal Time", 0 },
  { "IST (+5:30)", "India Standard Time", 330 },
  { "EST (-5:00)", "Eastern Standard Time", -300 },
  { "PST (-8:00)", "Pacific Standard Time", -480 },
  { "JST (+9:00)", "Japan Standard Time", 540 },
  { "BIT (-12:00)", "Baker Island Time", -720 },
  { "SST (-11:00)", "Samoa Standard Time", -660 },
  { "HST (-10:00)", "Hawaii Standard Time", -600 },
  { "MART (-9:30)", "Marquesas Time", -570 },
  { "AKST (-9:00)", "Alaska Standard Time", -540 },
  { "MST (-7:00)", "Mountain Standard Time", -420 },
  { "CST (-6:00)", "Central Standard Time", -360 },
  { "AST (-4:00)", "Atlantic Standard Time", -240 },
  { "NST (-3:30)", "Newfoundland Standard Time", -210 },
  { "BRT (-3:00)", "Brasilia Time", -180 },
  { "GST (-2:00)", "South Georgia Time", -120 },
  { "CVT (-1:00)", "Cape Verde Time", -60 },
  { "CET (+1:00)", "Central European Time", 60 },
  { "EET (+2:00)", "Eastern European Time", 120 },
  { "MSK (+3:00)", "Moscow Standard Time", 180 },
  { "IRST (+3:30)", "Iran Standard Time", 210 },
  { "GST (+4:00)", "Gulf Standard Time", 240 },
  { "AFT (+4:30)", "Afghanistan Time", 270 },
  { "PKT (+5:00)", "Pakistan Standard Time", 300 },
  { "NPT (+5:45)", "Nepal Time", 345 },
  { "BST (+6:00)", "Bangladesh Standard Time", 360 },
  { "MMT (+6:30)", "Myanmar Time", 390 },
  { "ICT (+7:00)", "Indochina Time", 420 },
  { "WITA (+8:00)", "Central Indonesia Time", 480 },
  { "ACWST (+8:45)", "Australian Central Western Time", 525 },
  { "ACST (+9:30)", "Australian Central Standard Time", 570 },
  { "AEST (+10:00)", "Australian Eastern Standard Time", 600 },
  { "LHST (+10:30)", "Lord Howe Standard Time", 630 },
  { "SBT (+11:00)", "Solomon Islands Time", 660 },
  { "NZST (+12:00)", "New Zealand Standard Time", 720 },
  { "CHAST (+12:45)", "Chatham Standard Time", 765 },
  { "TOT (+13:00)", "Tonga Time", 780 },
  { "LINT (+14:00)", "Line Islands Time", 840 }
};

#define TIMEZONE_COUNT ((int)(sizeof(s_timezones) / sizeof(s_timezones[0])))

static const char *s_months[] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static Window *s_main_window;
static Window *s_menu_window;
static Window *s_settings_window;
static Window *s_timezones_window;
static Window *s_recents_window;
static Window *s_date_formats_window;
static Layer *s_face_layer;
static SimpleMenuLayer *s_menu_layer;
static SimpleMenuLayer *s_settings_menu_layer;
static SimpleMenuLayer *s_timezones_menu_layer;
static SimpleMenuLayer *s_recents_menu_layer;
static SimpleMenuLayer *s_date_formats_menu_layer;
static Settings s_settings;
static uint8_t s_date_format;
// Drawing callbacks share Pebble's event stack. Keep the display model in
// static storage instead of allocating more than 170 bytes every redraw.
static struct tm s_local_display_time;
static struct tm s_selected_display_time;
static char s_local_time_text[16];
static char s_selected_time_text[16];
static char s_local_date_text[20];
static char s_selected_date_text[20];
// SimpleMenuLayer keeps pointers to its entries, so these are retained for the
// lifetime of the menu rather than being composed in a drawing callback.
static SimpleMenuItem s_main_menu_items[3];
static SimpleMenuSection s_main_menu_section;
static SimpleMenuItem s_menu_items[4];
static SimpleMenuSection s_menu_section;
static SimpleMenuItem s_timezone_menu_items[TIMEZONE_COUNT];
static SimpleMenuSection s_timezone_menu_section;
static SimpleMenuItem s_recents_menu_items[RECENT_ZONE_LIMIT];
static SimpleMenuSection s_recents_menu_section;
static SimpleMenuItem s_date_format_menu_items[DATE_FORMAT_COUNT];
static SimpleMenuSection s_date_format_menu_section;

static void main_click_config_provider(void *context);

static GColor background_color(void) {
  return s_settings.dark_theme ? GColorBlack : GColorWhite;
}

static GColor foreground_color(void) {
  return s_settings.dark_theme ? GColorWhite : GColorBlack;
}

static GColor border_color(void) {
  return s_settings.dark_theme ? GColorLightGray : GColorDarkGray;
}

static bool is_valid_timezone_index(int index) {
  return index >= 0 && index < TIMEZONE_COUNT;
}

static void restore_default_settings(void) {
  s_settings.dark_theme = false;
  s_settings.show_date = true;
  s_settings.use_24_hour = false;
  s_settings.schema_version = SETTINGS_SCHEMA_VERSION;
  s_settings.selected_zone = DEFAULT_ZONE_INDEX;
  s_settings.recent_count = 0;
  for (int index = 0; index < RECENT_ZONE_LIMIT; index++) {
    s_settings.recent_zones[index] = -1;
  }
}

static void save_settings(void) {
  persist_write_data(PERSIST_KEY_SETTINGS, &s_settings, sizeof(s_settings));
}

static void save_date_format(void) {
  persist_write_data(PERSIST_KEY_DATE_FORMAT, &s_date_format, sizeof(s_date_format));
}

static void load_settings(void) {
  restore_default_settings();

  if (persist_exists(PERSIST_KEY_SETTINGS)) {
    int persisted_size = persist_read_data(PERSIST_KEY_SETTINGS, &s_settings, sizeof(s_settings));
    if (persisted_size != (int)sizeof(s_settings)
        || s_settings.schema_version != SETTINGS_SCHEMA_VERSION) {
      restore_default_settings();
      save_settings();
    }
  }

  bool settings_changed = false;
  if (!is_valid_timezone_index(s_settings.selected_zone)) {
    s_settings.selected_zone = DEFAULT_ZONE_INDEX;
    settings_changed = true;
  }
  if (s_settings.recent_count > RECENT_ZONE_LIMIT) {
    s_settings.recent_count = RECENT_ZONE_LIMIT;
    settings_changed = true;
  }
  int valid_recent_count = 0;
  for (int index = 0; index < s_settings.recent_count; index++) {
    int zone_index = s_settings.recent_zones[index];
    bool duplicate = false;
    for (int recent_index = 0; recent_index < valid_recent_count; recent_index++) {
      if (s_settings.recent_zones[recent_index] == zone_index) {
        duplicate = true;
        break;
      }
    }
    if (!is_valid_timezone_index(zone_index) || duplicate) {
      settings_changed = true;
      continue;
    }
    s_settings.recent_zones[valid_recent_count++] = zone_index;
  }
  if (valid_recent_count != s_settings.recent_count) {
    s_settings.recent_count = valid_recent_count;
    settings_changed = true;
  }
  for (int index = valid_recent_count; index < RECENT_ZONE_LIMIT; index++) {
    s_settings.recent_zones[index] = -1;
  }
  if (settings_changed) {
    save_settings();
  }
}

static void load_date_format(void) {
  s_date_format = DATE_FORMAT_NAME_DAY_YEAR;
  if (!persist_exists(PERSIST_KEY_DATE_FORMAT)
      || persist_read_data(PERSIST_KEY_DATE_FORMAT, &s_date_format,
                           sizeof(s_date_format)) != (int)sizeof(s_date_format)
      || s_date_format >= DATE_FORMAT_COUNT) {
    s_date_format = DATE_FORMAT_NAME_DAY_YEAR;
    save_date_format();
  }
}

static void format_date(const struct tm *time_info, char *buffer, size_t buffer_size) {
  int year = time_info->tm_year + 1900;
  int month_index = time_info->tm_mon;
  int day = time_info->tm_mday;

  // localtime/gmtime always supply these ranges. The checks make that contract
  // explicit for the SDK's compiler, preventing false-positive snprintf
  // truncation diagnostics in every numeric date format.
  if (year < 1900 || year > 9999) {
    year = 2000;
  }
  if (month_index < 0 || month_index > 11) {
    month_index = 0;
  }
  if (day < 1 || day > 31) {
    day = 1;
  }

  const char *month_name = s_months[month_index];
  switch (s_date_format) {
    case DATE_FORMAT_DAY_MONTH_YEAR:
      snprintf(buffer, buffer_size, "%02d-%02d-%04d", day, month_index + 1, year);
      break;
    case DATE_FORMAT_DAY_NAME_YEAR:
      snprintf(buffer, buffer_size, "%02d %s, %04d", day, month_name, year);
      break;
    case DATE_FORMAT_MONTH_DAY_YEAR:
      snprintf(buffer, buffer_size, "%02d-%02d-%04d", month_index + 1, day, year);
      break;
    case DATE_FORMAT_YEAR_MONTH_DAY:
      snprintf(buffer, buffer_size, "%04d-%02d-%02d", year, month_index + 1, day);
      break;
    case DATE_FORMAT_NAME_DAY_YEAR:
    default:
      snprintf(buffer, buffer_size, "%s %02d, %04d", month_name, day, year);
      break;
  }
}

static void format_time(const struct tm *time_info, char *buffer, size_t buffer_size) {
  if (s_settings.use_24_hour) {
    snprintf(buffer, buffer_size, "%02d:%02d", time_info->tm_hour, time_info->tm_min);
    return;
  }

  int hour = time_info->tm_hour % 12;
  if (hour == 0) {
    hour = 12;
  }
  snprintf(buffer, buffer_size, "%d:%02d", hour, time_info->tm_min);
}

static void draw_left_text(GContext *ctx, const char *text, GFont font, GRect frame) {
  graphics_draw_text(ctx, text, font, frame, GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
}

static void draw_centered_text(GContext *ctx, const char *text, GFont font, GRect frame) {
  graphics_draw_text(ctx, text, font, frame, GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
}

static void draw_dotted_divider(GContext *ctx, int width, int y) {
  graphics_context_set_stroke_color(ctx, border_color());
  for (int x = 5; x < width - 5; x += 4) {
    graphics_draw_line(ctx, GPoint(x, y), GPoint(x + 1, y));
  }
}

static bool is_night_time(const struct tm *time_info) {
  return time_info->tm_hour >= 18 || time_info->tm_hour < 6;
}

static GColor day_night_icon_background(bool night) {
#if defined(PBL_COLOR)
  return night ? GColorFromRGB(0, 0, 85) : GColorFromRGB(170, 85, 0);
#else
  return GColorBlack;
#endif
}

static void draw_day_night_icon(GContext *ctx, int center_x, int center_y, bool night) {
  GPoint center = GPoint(center_x, center_y);
  GColor icon_background = day_night_icon_background(night);
  GColor icon_foreground = GColorWhite;

  graphics_context_set_fill_color(ctx, icon_background);
  graphics_fill_circle(ctx, center, 16);
  graphics_context_set_stroke_color(ctx, border_color());
  graphics_draw_circle(ctx, center, 16);

  graphics_context_set_fill_color(ctx, icon_foreground);
  graphics_context_set_stroke_color(ctx, icon_foreground);
  if (night) {
    graphics_fill_circle(ctx, GPoint(center_x - 2, center_y + 1), 7);
    graphics_context_set_fill_color(ctx, icon_background);
    graphics_fill_circle(ctx, GPoint(center_x + 2, center_y - 2), 7);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_pixel(ctx, GPoint(center_x - 9, center_y - 7));
    graphics_draw_pixel(ctx, GPoint(center_x + 8, center_y - 7));
    graphics_draw_pixel(ctx, GPoint(center_x + 8, center_y + 7));
    return;
  }

  graphics_fill_circle(ctx, center, 5);
  graphics_draw_line(ctx, GPoint(center_x, center_y - 11), GPoint(center_x, center_y - 8));
  graphics_draw_line(ctx, GPoint(center_x, center_y + 8), GPoint(center_x, center_y + 11));
  graphics_draw_line(ctx, GPoint(center_x - 11, center_y), GPoint(center_x - 8, center_y));
  graphics_draw_line(ctx, GPoint(center_x + 8, center_y), GPoint(center_x + 11, center_y));
  graphics_draw_line(ctx, GPoint(center_x - 8, center_y - 8), GPoint(center_x - 6, center_y - 6));
  graphics_draw_line(ctx, GPoint(center_x + 6, center_y + 6), GPoint(center_x + 8, center_y + 8));
  graphics_draw_line(ctx, GPoint(center_x + 8, center_y - 8), GPoint(center_x + 6, center_y - 6));
  graphics_draw_line(ctx, GPoint(center_x - 6, center_y + 6), GPoint(center_x - 8, center_y + 8));
}

static void draw_time_group(GContext *ctx, int width, int top, const char *heading,
                            const char *time_text, const char *date_text,
                            const struct tm *time_info) {
  int text_top = s_settings.show_date ? 10 : 18;
  int icon_center_y = s_settings.use_24_hour ? 42 : 33;
  graphics_context_set_text_color(ctx, foreground_color());
  draw_left_text(ctx, heading, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                 GRect(5, top + text_top, width - 46, 14));
  draw_left_text(ctx, time_text, fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS),
                 GRect(5, top + text_top + 13, width - 46, 36));
  if (s_settings.show_date) {
    draw_left_text(ctx, date_text, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                   GRect(5, top + text_top + 50, width - 10, 14));
  }
  draw_day_night_icon(ctx, width - 23, top + icon_center_y,
                      is_night_time(time_info));
  if (!s_settings.use_24_hour) {
    draw_centered_text(ctx, time_info->tm_hour < 12 ? "AM" : "PM",
                       fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                       GRect(width - 43, top + 52, 40, 14));
  }
}

static void face_layer_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int width = bounds.size.w;
  int top = (bounds.size.h - 166) / 2;
  if (top < 1) {
    top = 1;
  }
  time_t now = time(NULL);
  s_local_display_time = *localtime(&now);
  const TimeZone *selected_timezone = &s_timezones[s_settings.selected_zone];
  time_t selected_epoch = now + (selected_timezone->offset_minutes * 60);
  s_selected_display_time = *gmtime(&selected_epoch);

  format_time(&s_local_display_time, s_local_time_text, sizeof(s_local_time_text));
  format_time(&s_selected_display_time, s_selected_time_text, sizeof(s_selected_time_text));
  format_date(&s_local_display_time, s_local_date_text, sizeof(s_local_date_text));
  format_date(&s_selected_display_time, s_selected_date_text, sizeof(s_selected_date_text));

  graphics_context_set_fill_color(ctx, background_color());
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  draw_time_group(ctx, width, top, "Timebridge", s_local_time_text,
                  s_local_date_text, &s_local_display_time);
  draw_dotted_divider(ctx, width, top + 84);
  draw_time_group(ctx, width, top + 84, selected_timezone->label,
                  s_selected_time_text, s_selected_date_text,
                  &s_selected_display_time);
}

static void refresh_face(void) {
  if (s_face_layer) {
    layer_mark_dirty(s_face_layer);
  }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  refresh_face();
}

static GColor menu_highlight_color(void) {
#if defined(PBL_COLOR)
  return GColorBlue;
#else
  return GColorBlack;
#endif
}

static void apply_menu_layer_colors(Window *window, SimpleMenuLayer *menu_layer) {
  if (!window || !menu_layer) {
    return;
  }
  window_set_background_color(window, background_color());
  MenuLayer *native_menu = simple_menu_layer_get_menu_layer(menu_layer);
  menu_layer_set_normal_colors(native_menu, background_color(), foreground_color());
  menu_layer_set_highlight_colors(native_menu, menu_highlight_color(), GColorWhite);
  layer_mark_dirty(simple_menu_layer_get_layer(menu_layer));
}

static void apply_menu_colors(void) {
  apply_menu_layer_colors(s_menu_window, s_menu_layer);
  apply_menu_layer_colors(s_settings_window, s_settings_menu_layer);
  apply_menu_layer_colors(s_timezones_window, s_timezones_menu_layer);
  apply_menu_layer_colors(s_recents_window, s_recents_menu_layer);
  apply_menu_layer_colors(s_date_formats_window, s_date_formats_menu_layer);
}

static void main_menu_item_selected_callback(int index, void *context);
static void settings_item_selected_callback(int index, void *context);
static void timezone_item_selected_callback(int index, void *context);
static void recent_item_selected_callback(int index, void *context);
static void date_format_item_selected_callback(int index, void *context);

static void prepare_main_menu(void) {
  s_main_menu_section.title = "Time Bridge";
  s_main_menu_section.items = s_main_menu_items;
  s_main_menu_section.num_items = 3;
  s_main_menu_items[0] = (SimpleMenuItem) {
    .title = "Recents",
    .subtitle = "Last selected timezones",
    .callback = main_menu_item_selected_callback
  };
  s_main_menu_items[1] = (SimpleMenuItem) {
    .title = "Timezones",
    .subtitle = "Choose a timezone",
    .callback = main_menu_item_selected_callback
  };
  s_main_menu_items[2] = (SimpleMenuItem) {
    .title = "Settings",
    .subtitle = "Display preferences",
    .callback = main_menu_item_selected_callback
  };
}

static void prepare_settings_menu(void) {
  s_menu_section.title = "Settings";
  s_menu_section.items = s_menu_items;
  s_menu_section.num_items = 4;
  s_menu_items[0] = (SimpleMenuItem) {
    .title = "Dark theme",
    .subtitle = s_settings.dark_theme ? "On" : "Off",
    .callback = settings_item_selected_callback
  };
  s_menu_items[1] = (SimpleMenuItem) {
    .title = "Show date",
    .subtitle = s_settings.show_date ? "On" : "Off",
    .callback = settings_item_selected_callback
  };
  s_menu_items[2] = (SimpleMenuItem) {
    .title = "Date format",
    .subtitle = s_date_format_names[s_date_format],
    .callback = settings_item_selected_callback
  };
  s_menu_items[3] = (SimpleMenuItem) {
    .title = "24-hour time",
    .subtitle = s_settings.use_24_hour ? "On" : "Off",
    .callback = settings_item_selected_callback
  };
}

static void prepare_timezone_menu(void) {
  s_timezone_menu_section.title = "Timezones";
  s_timezone_menu_section.items = s_timezone_menu_items;
  s_timezone_menu_section.num_items = TIMEZONE_COUNT;

  for (int index = 0; index < TIMEZONE_COUNT; index++) {
    s_timezone_menu_items[index] = (SimpleMenuItem) {
      .title = s_timezones[index].label,
      .subtitle = s_timezones[index].subtitle,
      .callback = timezone_item_selected_callback
    };
  }
}

static void prepare_recents_menu(void) {
  s_recents_menu_section.title = "Recents";
  s_recents_menu_section.items = s_recents_menu_items;

  int item_count = 0;
  for (int index = 0; index < s_settings.recent_count; index++) {
    int zone_index = s_settings.recent_zones[index];
    if (!is_valid_timezone_index(zone_index)) {
      continue;
    }
    s_recents_menu_items[item_count] = (SimpleMenuItem) {
      .title = s_timezones[zone_index].label,
      .subtitle = s_timezones[zone_index].subtitle,
      .callback = recent_item_selected_callback
    };
    item_count++;
  }

  if (item_count == 0) {
    s_recents_menu_items[0] = (SimpleMenuItem) {
      .title = "No recent timezones",
      .subtitle = "Choose one in Timezones",
      .callback = NULL
    };
    item_count = 1;
  }
  s_recents_menu_section.num_items = item_count;
}

static void prepare_date_formats_menu(void) {
  s_date_format_menu_section.title = "Date format";
  s_date_format_menu_section.items = s_date_format_menu_items;
  s_date_format_menu_section.num_items = DATE_FORMAT_COUNT;

  s_date_format_menu_items[DATE_FORMAT_DAY_MONTH_YEAR] = (SimpleMenuItem) {
    .title = s_date_format_names[DATE_FORMAT_DAY_MONTH_YEAR],
    .subtitle = "22-01-1998",
    .callback = date_format_item_selected_callback
  };
  s_date_format_menu_items[DATE_FORMAT_DAY_NAME_YEAR] = (SimpleMenuItem) {
    .title = s_date_format_names[DATE_FORMAT_DAY_NAME_YEAR],
    .subtitle = "22 Jan, 1998",
    .callback = date_format_item_selected_callback
  };
  s_date_format_menu_items[DATE_FORMAT_MONTH_DAY_YEAR] = (SimpleMenuItem) {
    .title = s_date_format_names[DATE_FORMAT_MONTH_DAY_YEAR],
    .subtitle = "01-22-1998",
    .callback = date_format_item_selected_callback
  };
  s_date_format_menu_items[DATE_FORMAT_YEAR_MONTH_DAY] = (SimpleMenuItem) {
    .title = s_date_format_names[DATE_FORMAT_YEAR_MONTH_DAY],
    .subtitle = "1998-01-22",
    .callback = date_format_item_selected_callback
  };
  s_date_format_menu_items[DATE_FORMAT_NAME_DAY_YEAR] = (SimpleMenuItem) {
    .title = s_date_format_names[DATE_FORMAT_NAME_DAY_YEAR],
    .subtitle = "Jan 22, 1998",
    .callback = date_format_item_selected_callback
  };
}

static void main_menu_item_selected_callback(int index, void *context) {
  if (index == 0 && s_recents_window && s_recents_menu_layer) {
    prepare_recents_menu();
    // This layer starts with the single empty-state row. Reloading updates
    // MenuLayer's native content height and scroll bounds when recents grow.
    MenuLayer *native_recents_menu = simple_menu_layer_get_menu_layer(s_recents_menu_layer);
    menu_layer_reload_data(native_recents_menu);
    menu_layer_set_selected_index(native_recents_menu, MenuIndex(0, 0),
                                  MenuRowAlignTop, false);
    window_stack_push(s_recents_window, true);
  } else if (index == 1 && s_timezones_window && s_timezones_menu_layer) {
    window_stack_push(s_timezones_window, true);
  } else if (index == 2 && s_settings_window && s_settings_menu_layer) {
    window_stack_push(s_settings_window, true);
  }
}

static void settings_item_selected_callback(int index, void *context) {
  if (index == 0) {
    s_settings.dark_theme = !s_settings.dark_theme;
  } else if (index == 1) {
    s_settings.show_date = !s_settings.show_date;
  } else if (index == 2) {
    if (s_date_formats_window && s_date_formats_menu_layer) {
      window_stack_push(s_date_formats_window, true);
    }
    return;
  } else if (index == 3) {
    s_settings.use_24_hour = !s_settings.use_24_hour;
  } else {
    return;
  }
  save_settings();
  prepare_settings_menu();
  apply_menu_colors();
  refresh_face();
}

static void timezone_item_selected_callback(int index, void *context) {
  if (!is_valid_timezone_index(index)) {
    return;
  }

  for (int item_index = 0; item_index < s_settings.recent_count; item_index++) {
    if (s_settings.recent_zones[item_index] == index) {
      for (int move_index = item_index; move_index < s_settings.recent_count - 1;
           move_index++) {
        s_settings.recent_zones[move_index] = s_settings.recent_zones[move_index + 1];
      }
      s_settings.recent_count--;
      break;
    }
  }
  if (s_settings.recent_count < RECENT_ZONE_LIMIT) {
    for (int move_index = s_settings.recent_count; move_index > 0; move_index--) {
      s_settings.recent_zones[move_index] = s_settings.recent_zones[move_index - 1];
    }
    s_settings.recent_count++;
  } else {
    for (int move_index = RECENT_ZONE_LIMIT - 1; move_index > 0; move_index--) {
      s_settings.recent_zones[move_index] = s_settings.recent_zones[move_index - 1];
    }
  }
  s_settings.recent_zones[0] = index;
  s_settings.selected_zone = index;
  save_settings();
  prepare_recents_menu();
  apply_menu_colors();
  refresh_face();
  window_stack_pop(true);
}

static void recent_item_selected_callback(int index, void *context) {
  if (index < 0 || index >= s_settings.recent_count) {
    return;
  }
  timezone_item_selected_callback(s_settings.recent_zones[index], context);
}

static void date_format_item_selected_callback(int index, void *context) {
  if (index < 0 || index >= DATE_FORMAT_COUNT) {
    return;
  }

  s_date_format = index;
  save_date_format();
  prepare_settings_menu();
  apply_menu_colors();
  refresh_face();
  window_stack_pop(true);
}

static void show_main_menu(void) {
  prepare_main_menu();
  if (s_menu_window && s_menu_layer) {
    layer_mark_dirty(simple_menu_layer_get_layer(s_menu_layer));
    window_stack_push(s_menu_window, true);
  }
}

static void main_select_click_handler(ClickRecognizerRef recognizer, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Select pressed");
  show_main_menu();
}

static void main_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, main_select_click_handler);
}

static void main_window_load(Window *window) {
  Layer *root_layer = window_get_root_layer(window);
  s_face_layer = layer_create(layer_get_bounds(root_layer));
  layer_set_update_proc(s_face_layer, face_layer_update_proc);
  layer_add_child(root_layer, s_face_layer);
  window_set_background_color(window, background_color());
  window_set_click_config_provider(window, main_click_config_provider);
}

static void main_window_unload(Window *window) {
  layer_destroy(s_face_layer);
  s_face_layer = NULL;
}

static void init(void) {
  load_settings();
  load_date_format();
  s_main_window = window_create();
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });
  window_stack_push(s_main_window, true);
  // Construct all native menus before button input begins. Keeping their
  // backing entries and layers static avoids allocating UI objects on a click.
  prepare_main_menu();
  s_menu_window = window_create();
  if (s_menu_window) {
    Layer *menu_root_layer = window_get_root_layer(s_menu_window);
    if (menu_root_layer) {
      s_menu_layer = simple_menu_layer_create(layer_get_bounds(menu_root_layer),
                                               s_menu_window, &s_main_menu_section,
                                               1, NULL);
      if (s_menu_layer) {
        layer_add_child(menu_root_layer, simple_menu_layer_get_layer(s_menu_layer));
        apply_menu_colors();
      }
    }
  }
  prepare_settings_menu();
  s_settings_window = window_create();
  if (s_settings_window) {
    Layer *settings_root_layer = window_get_root_layer(s_settings_window);
    if (settings_root_layer) {
      s_settings_menu_layer = simple_menu_layer_create(
          layer_get_bounds(settings_root_layer), s_settings_window,
          &s_menu_section, 1, NULL);
      if (s_settings_menu_layer) {
        layer_add_child(settings_root_layer,
                        simple_menu_layer_get_layer(s_settings_menu_layer));
        apply_menu_colors();
      }
    }
  }
  prepare_timezone_menu();
  s_timezones_window = window_create();
  if (s_timezones_window) {
    Layer *timezones_root_layer = window_get_root_layer(s_timezones_window);
    if (timezones_root_layer) {
      s_timezones_menu_layer = simple_menu_layer_create(
          layer_get_bounds(timezones_root_layer), s_timezones_window,
          &s_timezone_menu_section, 1, NULL);
      if (s_timezones_menu_layer) {
        layer_add_child(timezones_root_layer,
                        simple_menu_layer_get_layer(s_timezones_menu_layer));
        apply_menu_colors();
      }
    }
  }
  prepare_recents_menu();
  s_recents_window = window_create();
  if (s_recents_window) {
    Layer *recents_root_layer = window_get_root_layer(s_recents_window);
    if (recents_root_layer) {
      s_recents_menu_layer = simple_menu_layer_create(
          layer_get_bounds(recents_root_layer), s_recents_window,
          &s_recents_menu_section, 1, NULL);
      if (s_recents_menu_layer) {
        layer_add_child(recents_root_layer,
                        simple_menu_layer_get_layer(s_recents_menu_layer));
        apply_menu_colors();
      }
    }
  }
  prepare_date_formats_menu();
  s_date_formats_window = window_create();
  if (s_date_formats_window) {
    Layer *date_formats_root_layer = window_get_root_layer(s_date_formats_window);
    if (date_formats_root_layer) {
      s_date_formats_menu_layer = simple_menu_layer_create(
          layer_get_bounds(date_formats_root_layer), s_date_formats_window,
          &s_date_format_menu_section, 1, NULL);
      if (s_date_formats_menu_layer) {
        layer_add_child(date_formats_root_layer,
                        simple_menu_layer_get_layer(s_date_formats_menu_layer));
        apply_menu_colors();
      }
    }
  }
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  if (s_date_formats_window) {
    if (s_date_formats_menu_layer) {
      simple_menu_layer_destroy(s_date_formats_menu_layer);
      s_date_formats_menu_layer = NULL;
    }
    window_destroy(s_date_formats_window);
  }
  if (s_recents_window) {
    if (s_recents_menu_layer) {
      simple_menu_layer_destroy(s_recents_menu_layer);
      s_recents_menu_layer = NULL;
    }
    window_destroy(s_recents_window);
  }
  if (s_timezones_window) {
    if (s_timezones_menu_layer) {
      simple_menu_layer_destroy(s_timezones_menu_layer);
      s_timezones_menu_layer = NULL;
    }
    window_destroy(s_timezones_window);
  }
  if (s_settings_window) {
    if (s_settings_menu_layer) {
      simple_menu_layer_destroy(s_settings_menu_layer);
      s_settings_menu_layer = NULL;
    }
    window_destroy(s_settings_window);
  }
  if (s_menu_window) {
    if (s_menu_layer) {
      simple_menu_layer_destroy(s_menu_layer);
      s_menu_layer = NULL;
    }
    window_destroy(s_menu_window);
  }
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
