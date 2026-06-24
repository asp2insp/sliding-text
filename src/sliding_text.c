#include <pebble.h>
#include "num2words.h"

#define KEY_WEATHER_CONDITION  1
#define KEY_TEMP_HIGH          2
#define KEY_TEMP_LOW           3

#define INFO_DISPLAY_MS  4000

typedef enum {
  MOVING_IN,
  IN_FRAME,
  PREPARE_TO_MOVE,
  MOVING_OUT
} SlideState;

typedef struct {
  TextLayer *label;
  SlideState state;
  char *next_string;
  bool unchanged_font;

  int left_pos;
  int right_pos;
  int still_pos;

  int movement_delay;
  int delay_count;
} SlidingRow;

typedef enum {
  INFO_HIDDEN,
  INFO_ENTERING,
  INFO_VISIBLE,
  INFO_LEAVING,
} InfoViewState;

typedef struct {
  TextLayer *demo_label;
  SlidingRow rows[3];
  int last_hour;
  int last_minute;

  GFont bitham42_bold;
  GFont bitham42_light;

  Window *window;
  Animation *animation;

  struct SlidingTextRenderState {
    char hours[2][32];
    uint8_t next_hours;
    char first_minutes[2][32];
    char second_minutes[2][32];
    uint8_t next_minutes;

    struct SlidingTextRenderDemoTime {
      int secs;
      int mins;
      int hour;
    } demo_time;

  } render_state;

  // Info (health + weather) view
  InfoViewState info_state;
  TextLayer *info_labels[3];
  char info_strings[3][40];
  GColor info_colors[3];
  AppTimer *info_timer;

  // Weather received via AppMessage
  char weather_condition[32];
  int weather_temp_high;
  int weather_temp_low;
  bool weather_available;

} SlidingTextData;

SlidingTextData *s_data;

// ===== Color coding =====

static GColor steps_color(int steps) {
#if defined(PBL_COLOR)
  if (steps >= 8000) return GColorGreen;
  if (steps >= 4000) return GColorChromeYellow;
  return GColorRed;
#else
  return GColorWhite;
#endif
}

static GColor hr_color(int bpm) {
#if defined(PBL_COLOR)
  if (bpm == 0) return GColorLightGray;
  if (bpm >= 100) return GColorRed;
  if (bpm >= 60) return GColorGreen;
  return GColorCyan;
#else
  return GColorWhite;
#endif
}

static GColor temp_color(int temp_high) {
#if defined(PBL_COLOR)
  if (temp_high >= 80) return GColorRed;
  if (temp_high >= 50) return GColorChromeYellow;
  return GColorCyan;
#else
  return GColorWhite;
#endif
}

// ===== Info view data =====

static void build_info_view(SlidingTextData *data) {
  int steps = 0;
  int bpm = 0;

#if defined(PBL_HEALTH)
  time_t start = time_start_of_today();
  time_t now = time(NULL);

  HealthServiceAccessibilityMask sm =
    health_service_metric_accessible(HealthMetricStepCount, start, now);
  if (sm & HealthServiceAccessibilityMaskAvailable) {
    steps = (int)health_service_sum(HealthMetricStepCount, start, now);
  }

  HealthServiceAccessibilityMask hm =
    health_service_metric_accessible(HealthMetricHeartRateBPM, start, now);
  if (hm & HealthServiceAccessibilityMaskAvailable) {
    bpm = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
  }
#endif

  if (steps >= 1000) {
    snprintf(data->info_strings[0], sizeof(data->info_strings[0]),
             "%dk steps", steps / 1000);
  } else {
    snprintf(data->info_strings[0], sizeof(data->info_strings[0]),
             "%d steps", steps);
  }
  data->info_colors[0] = steps_color(steps);

  if (bpm > 0) {
    snprintf(data->info_strings[1], sizeof(data->info_strings[1]), "%d bpm", bpm);
  } else {
    snprintf(data->info_strings[1], sizeof(data->info_strings[1]), "-- bpm");
  }
  data->info_colors[1] = hr_color(bpm);

  if (data->weather_available) {
    snprintf(data->info_strings[2], sizeof(data->info_strings[2]),
             "%s %d/%d",
             data->weather_condition, data->weather_temp_high, data->weather_temp_low);
  } else {
    snprintf(data->info_strings[2], sizeof(data->info_strings[2]), "no weather");
  }
  data->info_colors[2] = temp_color(data->weather_temp_high);
}

// ===== Existing sliding row helpers =====

static void init_sliding_row(SlidingTextData *data, SlidingRow *row, GRect pos, GFont font,
        int delay) {
  row->label = text_layer_create(pos);
  text_layer_set_text_alignment(row->label, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft));
  text_layer_set_background_color(row->label, GColorClear);
  text_layer_set_text_color(row->label, GColorOrange);
  if (font) {
    text_layer_set_font(row->label, font);
    row->unchanged_font = true;
  } else {
    row->unchanged_font = false;
  }

  row->state = IN_FRAME;
  row->next_string = NULL;

  row->left_pos = -pos.size.w;
  row->right_pos = pos.size.w;
  row->still_pos = pos.origin.x;

  row->movement_delay = delay;
  row->delay_count = 0;

  data->last_hour = -1;
  data->last_minute = -1;
}

static void slide_in_text(SlidingTextData *data, SlidingRow *row, char* new_text) {
  (void) data;

  const char *old_text = text_layer_get_text(row->label);
  if (old_text) {
    row->next_string = new_text;
    row->state = PREPARE_TO_MOVE;
  } else {
    text_layer_set_text(row->label, new_text);
    GRect frame = layer_get_frame(text_layer_get_layer(row->label));
    frame.origin.x = row->right_pos;
    layer_set_frame(text_layer_get_layer(row->label), frame);
    row->state = MOVING_IN;
  }
}


static bool update_sliding_row(SlidingTextData *data, SlidingRow *row) {
  (void) data;

  GRect frame = layer_get_frame(text_layer_get_layer(row->label));
  bool something_changed = true;
  switch (row->state) {
    case PREPARE_TO_MOVE:
      frame.origin.x = row->still_pos;
      row->delay_count++;
      if (row->delay_count > row->movement_delay) {
        row->state = MOVING_OUT;
        row->delay_count = 0;
      }
    break;

    case MOVING_IN: {
      int speed = abs(frame.origin.x - row->still_pos) / 3 + 1;
      frame.origin.x -= speed;
      if (frame.origin.x <= row->still_pos) {
        frame.origin.x = row->still_pos;
        row->state = IN_FRAME;
      }
    }
    break;

    case MOVING_OUT: {
      int speed = abs(frame.origin.x - row->still_pos) / 3 + 1;
      frame.origin.x -= speed;

      if (frame.origin.x <= row->left_pos) {
        frame.origin.x = row->right_pos;
        row->state = MOVING_IN;
        text_layer_set_text(row->label, row->next_string);
        row->next_string = NULL;
      }
    }
    break;

    case IN_FRAME:
    default:
      something_changed = false;
      break;
  }
  if (something_changed) {
    layer_set_frame(text_layer_get_layer(row->label), frame);
  }
  return something_changed;
}

// ===== Info dismiss callback (forward declaration) =====
static void info_dismiss_callback(void *context);

// ===== Animation update =====

static void animation_update(struct Animation *animation, const AnimationProgress time_normalized) {
  SlidingTextData *data = s_data;
  bool something_changed = false;

  // Handle info view slide transitions
  if (data->info_state == INFO_ENTERING || data->info_state == INFO_LEAVING) {
    Layer *window_layer = window_get_root_layer(data->window);
    GRect bounds = layer_get_unobstructed_bounds(window_layer);
    const int16_t width = bounds.size.w;

    // During entering: time goes left (-width), info comes in from right (→0)
    // During leaving:  info goes left (-width), time comes in from right (→0)
    const int16_t time_target = (data->info_state == INFO_ENTERING) ? -width : 0;
    const int16_t info_target = (data->info_state == INFO_ENTERING) ? 0 : -width;

    bool all_done = true;

    for (int i = 0; i < 3; i++) {
      Layer *tl = text_layer_get_layer(data->rows[i].label);
      GRect tf = layer_get_frame(tl);
      int tdist = abs(tf.origin.x - time_target);
      if (tdist > 0) {
        int speed = tdist / 3 + 1;
        if (tdist <= speed) {
          tf.origin.x = time_target;
        } else {
          tf.origin.x += (tf.origin.x > time_target) ? -speed : speed;
        }
        layer_set_frame(tl, tf);
        something_changed = true;
        all_done = false;
      }

      Layer *il = text_layer_get_layer(data->info_labels[i]);
      GRect inf = layer_get_frame(il);
      int idist = abs(inf.origin.x - info_target);
      if (idist > 0) {
        int speed = idist / 3 + 1;
        if (idist <= speed) {
          inf.origin.x = info_target;
        } else {
          inf.origin.x += (inf.origin.x > info_target) ? -speed : speed;
        }
        layer_set_frame(il, inf);
        something_changed = true;
        all_done = false;
      }
    }

    if (all_done) {
      if (data->info_state == INFO_ENTERING) {
        data->info_state = INFO_VISIBLE;
        data->info_timer = app_timer_register(INFO_DISPLAY_MS, info_dismiss_callback, NULL);
      } else {
        data->info_state = INFO_HIDDEN;
        // Reset info layers to off-screen right for next trigger
        for (int i = 0; i < 3; i++) {
          Layer *il = text_layer_get_layer(data->info_labels[i]);
          GRect f = layer_get_frame(il);
          f.origin.x = width;
          layer_set_frame(il, f);
        }
      }
    }

    if (!something_changed) {
      animation_unschedule(animation);
    }
    return;
  }

  // Normal time display logic
  struct SlidingTextRenderState *rs = &data->render_state;

  time_t now = time(NULL);
  struct tm t = *localtime(&now);

  if (data->last_minute != t.tm_min) {
    something_changed = true;

    minute_to_formal_words(t.tm_min, rs->first_minutes[rs->next_minutes], rs->second_minutes[rs->next_minutes]);
    if(data->last_hour != t.tm_hour || t.tm_min <= 20
       || t.tm_min/10 != data->last_minute/10) {
      slide_in_text(data, &data->rows[1], rs->first_minutes[rs->next_minutes]);
    } else {
      text_layer_set_text(data->rows[1].label, rs->first_minutes[rs->next_minutes]);
    }
    slide_in_text(data, &data->rows[2], rs->second_minutes[rs->next_minutes]);
    rs->next_minutes = rs->next_minutes ? 0 : 1;
    data->last_minute = t.tm_min;
  }

  if (data->last_hour != t.tm_hour) {
    hour_to_12h_word(t.tm_hour, rs->hours[rs->next_hours]);
    slide_in_text(data, &data->rows[0], rs->hours[rs->next_hours]);
    rs->next_hours = rs->next_hours ? 0 : 1;
    data->last_hour = t.tm_hour;
  }

  for (size_t i = 0; i < ARRAY_LENGTH(data->rows); ++i) {
    something_changed = update_sliding_row(data, &data->rows[i]) || something_changed;
  }

  if (!something_changed) {
    animation_unschedule(animation);
  }
}

static void make_animation() {
  s_data->animation = animation_create();
  animation_set_duration(s_data->animation, ANIMATION_DURATION_INFINITE);
  static const struct AnimationImplementation s_animation_implementation = {
    .update = animation_update,
  };
  animation_set_implementation(s_data->animation, &s_animation_implementation);
  animation_schedule(s_data->animation);
}

// ===== Info view dismiss =====

static void info_dismiss_callback(void *context) {
  SlidingTextData *data = s_data;
  data->info_timer = NULL;

  Layer *window_layer = window_get_root_layer(data->window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);
  const int16_t width = bounds.size.w;

  // Refresh time text while rows are still off-screen, so correct words slide in
  time_t now = time(NULL);
  struct tm t = *localtime(&now);
  struct SlidingTextRenderState *rs = &data->render_state;

  minute_to_formal_words(t.tm_min,
    rs->first_minutes[rs->next_minutes],
    rs->second_minutes[rs->next_minutes]);
  text_layer_set_text(data->rows[1].label, rs->first_minutes[rs->next_minutes]);
  text_layer_set_text(data->rows[2].label, rs->second_minutes[rs->next_minutes]);
  rs->next_minutes = rs->next_minutes ? 0 : 1;

  hour_to_12h_word(t.tm_hour, rs->hours[rs->next_hours]);
  text_layer_set_text(data->rows[0].label, rs->hours[rs->next_hours]);
  rs->next_hours = rs->next_hours ? 0 : 1;

  data->last_minute = t.tm_min;
  data->last_hour = t.tm_hour;

  // Clear any in-progress text-change animation state
  for (int i = 0; i < 3; i++) {
    data->rows[i].state = IN_FRAME;
    data->rows[i].next_string = NULL;
  }

  // Teleport time rows to off-screen right so they slide back in from the right
  for (int i = 0; i < 3; i++) {
    GRect f = layer_get_frame(text_layer_get_layer(data->rows[i].label));
    f.origin.x = width;
    layer_set_frame(text_layer_get_layer(data->rows[i].label), f);
  }

  data->info_state = INFO_LEAVING;
  make_animation();
}

// ===== Minute tick =====

static void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
  // Don't trigger time animation while info view is active; dismiss callback
  // will refresh the time text when closing the info view.
  if (s_data->info_state == INFO_HIDDEN) {
    make_animation();
  }
}

// ===== Back button / click provider =====

static void back_click_handler(ClickRecognizerRef recognizer, void *context) {
  SlidingTextData *data = s_data;
  if (data->info_state != INFO_HIDDEN) return;

  build_info_view(data);

  for (int i = 0; i < 3; i++) {
    text_layer_set_text(data->info_labels[i], data->info_strings[i]);
    text_layer_set_text_color(data->info_labels[i], data->info_colors[i]);
  }

  // Cancel any in-progress text-change animation and snap rows to center
  if (data->animation) {
    animation_unschedule(data->animation);
  }
  for (int i = 0; i < 3; i++) {
    data->rows[i].state = IN_FRAME;
    data->rows[i].next_string = NULL;
    GRect f = layer_get_frame(text_layer_get_layer(data->rows[i].label));
    f.origin.x = data->rows[i].still_pos;
    layer_set_frame(text_layer_get_layer(data->rows[i].label), f);
  }

  // Ensure info layers start off-screen right
  Layer *window_layer = window_get_root_layer(data->window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);
  const int16_t width = bounds.size.w;
  for (int i = 0; i < 3; i++) {
    GRect f = layer_get_frame(text_layer_get_layer(data->info_labels[i]));
    f.origin.x = width;
    layer_set_frame(text_layer_get_layer(data->info_labels[i]), f);
  }

  data->info_state = INFO_ENTERING;
  make_animation();
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_BACK, back_click_handler);
}

// ===== AppMessage (weather) =====

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  SlidingTextData *data = s_data;

  Tuple *cond_t  = dict_find(iterator, KEY_WEATHER_CONDITION);
  Tuple *high_t  = dict_find(iterator, KEY_TEMP_HIGH);
  Tuple *low_t   = dict_find(iterator, KEY_TEMP_LOW);

  if (cond_t) {
    strncpy(data->weather_condition, cond_t->value->cstring,
            sizeof(data->weather_condition) - 1);
    data->weather_condition[sizeof(data->weather_condition) - 1] = '\0';
    data->weather_available = true;
  }
  if (high_t) data->weather_temp_high = (int)high_t->value->int32;
  if (low_t)  data->weather_temp_low  = (int)low_t->value->int32;
}

// ===== Layout =====

static void layout_rows(void) {
  Layer *window_layer = window_get_root_layer(s_data->window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);
  const int16_t width = bounds.size.w;

#if defined(PBL_PLATFORM_EMERY)
  const int16_t row_h = 48;
  const int16_t frame_h = 70;
#elif defined(PBL_PLATFORM_GABBRO)
  const int16_t row_h = 38;
  const int16_t frame_h = 56;
#else
  const int16_t row_h = 0;
  const int16_t frame_h = 0;
  (void)frame_h;
#endif

#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  const int16_t total_h = row_h * 2 + frame_h;
  const int16_t y0 = (bounds.size.h - total_h) / 2;
  const int16_t ys[3] = {y0, y0 + row_h, y0 + row_h * 2};
  const int16_t hs[3] = {frame_h, frame_h, frame_h};
#else
  const int16_t y_off = (bounds.size.h > 168) ? (bounds.size.h - 168) / 2 : 0;
  const int16_t ys[3] = {20 + y_off, 56 + y_off, 92 + y_off};
  const int16_t hs[3] = {60, 96, 132};
#endif

  for (int i = 0; i < 3; i++) {
    GRect frame = layer_get_frame(text_layer_get_layer(s_data->rows[i].label));
    frame.origin.x = 0;
    frame.origin.y = ys[i];
    frame.size.w = width;
    frame.size.h = hs[i];
    layer_set_frame(text_layer_get_layer(s_data->rows[i].label), frame);
    s_data->rows[i].still_pos = 0;
    s_data->rows[i].left_pos = -width;
    s_data->rows[i].right_pos = width;

    // Keep info layers off-screen right when time view is active
    if (s_data->info_state == INFO_HIDDEN) {
      GRect inf = layer_get_frame(text_layer_get_layer(s_data->info_labels[i]));
      inf.origin.y = ys[i];
      inf.size.w = width;
      inf.size.h = hs[i];
      inf.origin.x = width;
      layer_set_frame(text_layer_get_layer(s_data->info_labels[i]), inf);
    }
  }
}

static void handle_unobstructed_change(AnimationProgress progress, void *context) {
  layout_rows();
}

// ===== Deinit =====

static void handle_deinit(void) {
  tick_timer_service_unsubscribe();
  unobstructed_area_service_unsubscribe();
  if (s_data->animation) {
    animation_unschedule(s_data->animation);
  }
  if (s_data->info_timer) {
    app_timer_cancel(s_data->info_timer);
  }
  for (int i = 0; i < 3; i++) {
    text_layer_destroy(s_data->rows[i].label);
    text_layer_destroy(s_data->info_labels[i]);
  }
  text_layer_destroy(s_data->demo_label);
  window_destroy(s_data->window);
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  fonts_unload_custom_font(s_data->bitham42_bold);
  fonts_unload_custom_font(s_data->bitham42_light);
#endif
  free(s_data);
}

// ===== Init =====

static void handle_init() {
  SlidingTextData *data = (SlidingTextData*)malloc(sizeof(SlidingTextData));
  s_data = data;

  data->render_state.next_hours = 0;
  data->render_state.next_minutes = 0;
  data->render_state.demo_time.secs = 0;
  data->render_state.demo_time.mins = 0;
  data->render_state.demo_time.hour = 0;

  data->info_state = INFO_HIDDEN;
  data->info_timer = NULL;
  data->weather_available = false;
  data->weather_condition[0] = '\0';
  data->weather_temp_high = 0;
  data->weather_temp_low = 0;

  data->window = window_create();
  window_set_background_color(data->window, GColorBlack);

#if defined(PBL_PLATFORM_EMERY)
  data->bitham42_bold = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_GOTHAM_BOLD_50));
  data->bitham42_light = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_GOTHAM_LIGHT_50));
  const int16_t row_h = 48;
  const int16_t frame_h = 70;
  const int16_t row_gap = 0;
#elif defined(PBL_PLATFORM_GABBRO)
  data->bitham42_bold = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_GOTHAM_BOLD_40));
  data->bitham42_light = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_GOTHAM_LIGHT_32));
  const int16_t row_h = 38;
  const int16_t frame_h = 56;
  const int16_t row_gap = 0;
#else
  data->bitham42_bold = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  data->bitham42_light = fonts_get_system_font(FONT_KEY_BITHAM_42_LIGHT);
  const int16_t row_h = 0;
  const int16_t frame_h = 0;
  const int16_t row_gap = 0;
  (void)row_h; (void)frame_h; (void)row_gap;
#endif

  Layer *window_layer = window_get_root_layer(data->window);
  GRect layer_frame = layer_get_frame(window_layer);
  const int16_t width = layer_frame.size.w;

#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  const int16_t total_h = row_h * 2 + frame_h;
  const int16_t y0 = (layer_frame.size.h - total_h) / 2;
  init_sliding_row(data, &data->rows[0], GRect(0, y0, width, frame_h), data->bitham42_bold, 6);
  layer_add_child(window_layer, text_layer_get_layer(data->rows[0].label));
  init_sliding_row(data, &data->rows[1], GRect(0, y0 + row_h + row_gap, width, frame_h), data->bitham42_light, 3);
  layer_add_child(window_layer, text_layer_get_layer(data->rows[1].label));
  init_sliding_row(data, &data->rows[2], GRect(0, y0 + (row_h + row_gap) * 2, width, frame_h), data->bitham42_light, 0);
  layer_add_child(window_layer, text_layer_get_layer(data->rows[2].label));

  const int16_t info_ys[3] = {y0, y0 + row_h, y0 + row_h * 2};
  const int16_t info_hs[3] = {frame_h, frame_h, frame_h};
#else
  const int16_t y_off = (layer_frame.size.h > 168) ? (layer_frame.size.h - 168) / 2 : 0;
  init_sliding_row(data, &data->rows[0], GRect(0, 20 + y_off, width, 60), data->bitham42_bold, 6);
  layer_add_child(window_layer, text_layer_get_layer(data->rows[0].label));
  init_sliding_row(data, &data->rows[1], GRect(0, 56 + y_off, width, 96), data->bitham42_light, 3);
  layer_add_child(window_layer, text_layer_get_layer(data->rows[1].label));
  init_sliding_row(data, &data->rows[2], GRect(0, 92 + y_off, width, 132), data->bitham42_light, 0);
  layer_add_child(window_layer, text_layer_get_layer(data->rows[2].label));

  const int16_t info_ys[3] = {20 + y_off, 56 + y_off, 92 + y_off};
  const int16_t info_hs[3] = {60, 96, 132};
#endif

  // Info view layers — same positions as time rows, initially off-screen right
  GFont info_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  for (int i = 0; i < 3; i++) {
    data->info_labels[i] = text_layer_create(GRect(width, info_ys[i], width, info_hs[i]));
    text_layer_set_background_color(data->info_labels[i], GColorClear);
    text_layer_set_text_color(data->info_labels[i], GColorOrange);
    text_layer_set_font(data->info_labels[i], info_font);
    text_layer_set_text_alignment(data->info_labels[i],
      PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft));
    text_layer_set_overflow_mode(data->info_labels[i], GTextOverflowModeWordWrap);
    layer_add_child(window_layer, text_layer_get_layer(data->info_labels[i]));
  }

  GFont norm14 = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  data->demo_label = text_layer_create(GRect(0, -3, 100, 20));
  text_layer_set_background_color(data->demo_label, GColorClear);
  text_layer_set_text_color(data->demo_label, GColorOrange);
  text_layer_set_font(data->demo_label, norm14);
  text_layer_set_text(data->demo_label, "demo mode");
  layer_add_child(window_layer, text_layer_get_layer(data->demo_label));
  layer_set_hidden(text_layer_get_layer(data->demo_label), true);
  layer_mark_dirty(window_layer);

  // Click config for back button info view trigger
  window_set_click_config_provider(data->window, click_config_provider);

  // AppMessage for weather data from phone
  app_message_register_inbox_received(inbox_received_callback);
  app_message_open(128, 64);

  make_animation();

  tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);

  UnobstructedAreaHandlers ua_handlers = { .change = handle_unobstructed_change };
  unobstructed_area_service_subscribe(ua_handlers, NULL);

  const bool animated = true;
  window_stack_push(data->window, animated);
}

int main(void) {
  handle_init();

  app_event_loop();

  handle_deinit();
}
