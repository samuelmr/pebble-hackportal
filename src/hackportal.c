#include <pebble.h>

#define MAX_PORTAL_COUNT 20
#define MAX_HACKS 34
#define WAKEUP_BEFORE 12
#define OPTIONS_LENGTH 3
#define MAX_WAKEUPS 8
#define FIRST_WAKEUP_STORAGE_KEY 100
#define ZONES_LENGTH 40

static Window *window;
static uint32_t EPOCH = 1388523600; // beginning of cycle 2014.01 in UTC
static const int EPOCH_YEAR = 2014;
static const uint32_t SECS_IN_CYCLE = 630000;
static const uint32_t SECS_IN_CHECKPOINT = 18000;
static const int16_t SIGNIFICANT_TIME = 4 * 60 * 60;
static char time_text[] = "2014.43     29/35    4:53:23";
static int16_t portal_count = 0;
static int seconds_to_next = 300;

static int32_t wakeups[MAX_WAKEUPS];
static int wakeup_index = 0;

enum MODES {
  OFF = 0,
  ON = 1
};
static char *mode[4];

typedef struct{
  int key;
  int value;
  char name[31];
} Option;

static Option options[OPTIONS_LENGTH];
static Option *vibes;
static Option *hide;
static Option *zone;

enum MessageKey {
  PORTALS = 0,   // TUPLE_INT
  INDEX = 1,     // TUPLE_INT
  NAME = 2,      // TUPLE_CSTRING
  COOLDOWN = 3,  // TUPLE_INT
  HACKS = 4,     // TUPLE_INT
  HACKS_DONE = 5 // TUPLE_INT (time_t, sent from watch to phone)
};
static MenuLayer *menu_layer;

typedef struct{
  char name[31];
  time_t hacked[MAX_HACKS + 1];
  time_t burned_out;
  int hacks_done;
  int hacks;
  int cooldown_time;
  int insulation;
  int seconds;
  TextLayer *name_layer;
  AppTimer *timer;
  WakeupId wakeup_id;
} Portal;

static Portal portals[MAX_PORTAL_COUNT];

typedef struct{
  int hour;
  int min;
} Timezone;

static Timezone zones[ZONES_LENGTH];

void send_portal_hacks(int index, Portal *port) {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  if (iter == NULL) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Can not send command to phone!");
    return;
  }
  dict_write_int8(iter, INDEX, index);
  dict_write_int8(iter, HACKS_DONE, port->hacks_done);
  for (int i=0; i<port->hacks_done; i++) {
    if (i > MAX_HACKS) {
      break;
    }
    dict_write_uint32(iter, 1 + HACKS_DONE + i, port->hacked[i]);
  }
  dict_write_end(iter);
  app_message_outbox_send();
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Sent hacks for portal %d (%d) to phone!", index, port->hacks_done);
  if (port->wakeup_id && wakeup_query(port->wakeup_id, NULL)) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Cancelling existing wakeup for portal %ld.", (long) port->wakeup_id);
    wakeup_cancel(port->wakeup_id);
  }
  if (wakeups[wakeup_index] && wakeup_query(wakeups[wakeup_index], NULL)) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Cancelling existing wakeup from queue %ld.", (long) port->wakeup_id);
    wakeup_cancel(wakeups[wakeup_index]);
  }

  if (port->seconds > WAKEUP_BEFORE) {
    time_t now = time(NULL);
    time_t wake = now + port->seconds - WAKEUP_BEFORE;
    port->wakeup_id = wakeup_schedule(wake, (int32_t) index, true);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Now is %ld, wake at %ld.", (long) now, (long) wake);
    if (port->wakeup_id > 0) {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Set wakeup timer for portal %d after %d seconds (%ld).", index, port->seconds - WAKEUP_BEFORE, (long) port->wakeup_id);
      wakeups[wakeup_index] = port->wakeup_id;
      wakeup_index++;
      if (wakeup_index >= MAX_WAKEUPS) {
        wakeup_index = 0;
      }
    }
    else if (port->wakeup_id == E_OUT_OF_RESOURCES) {
      // should not happen
      APP_LOG(APP_LOG_LEVEL_WARNING, "Maximum number of wakeups already set!");
    }
    else {
      APP_LOG(APP_LOG_LEVEL_WARNING, "Could not set wakeup timer for portal %d after %d seconds: %ld.", index, port->seconds - WAKEUP_BEFORE, (long) port->wakeup_id);
    }
  }
}

static int clear_old_hacks(int index, Portal *port) {
  int shift = 0;
  // just in case, should never happen
  if (port->hacks_done > MAX_HACKS) {
    port->hacks_done = MAX_HACKS;
  }
  for (int i=0; i<port->hacks_done; i++) {
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Old hack %d: %d, %d (%d)", i, (int) port->hacked[i], port->cooldown_time, (int) time(NULL));
    if ((time(NULL) - port->hacked[i]) >= SIGNIFICANT_TIME) {
      shift++;
    }
    else {
      // APP_LOG(APP_LOG_LEVEL_DEBUG, "Still valid hack %ld - %ld < %d", time(NULL), port->hacked[i], SIGNIFICANT_TIME);
    }
  }
  if (shift > 0) {
    for (int i=shift; i<port->hacks_done; i++) {
      port->hacked[i-shift] = port->hacked[i];
    }
    port->hacks_done -= shift;
  }
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Cleared %d old hacks for portal %d, %d left", shift, index, port->hacks_done);
  return shift;
}

static void handle_tick(struct tm *tick_time, TimeUnits units_changed) {
  time_t rt = time(NULL);
  // time_t t = rt - EPOCH;
  int h = zones[zone->value].hour;
  int m = zones[zone->value].min;
  time_t t = rt - EPOCH + ((h * 60 * 60) + (m * 60));

  uint32_t cycle = (t / SECS_IN_CYCLE)%50;
  uint32_t checkpoint = (t % SECS_IN_CYCLE) / SECS_IN_CHECKPOINT + 1;
  uint32_t countdown = SECS_IN_CHECKPOINT - (t % SECS_IN_CHECKPOINT);
  uint32_t next = rt + countdown;
  int year = EPOCH_YEAR + (int) ((t / SECS_IN_CYCLE) / 50);
  int hours = (int) (countdown / (60 * 60));
  int minutes = (countdown - (hours * 60 * 60)) / 60;
  int seconds = countdown - (hours * 60 * 60) - (minutes * 60);
  char next_str[80];
  struct tm *tms;
  tms = localtime((time_t*) &next);
  strftime(next_str, sizeof(next_str), "%Y-%m-%d %H:%M:%S", tms);
  snprintf(time_text, sizeof(time_text), "%d.%02ld   %02ld/35    %d:%02d:%02d",
    year, (long) cycle, (long) checkpoint, hours, minutes, seconds);
  for (int i=0; i<MAX_PORTAL_COUNT; i++) {
    Portal *port = &portals[i];
    if (port != NULL) {
      // APP_LOG(APP_LOG_LEVEL_DEBUG, "Seconds for portal %d: %d)", i, port->seconds);
      if (!port->seconds) {
        continue;
      }
      if (port->seconds < 0) {
        port->seconds = 0;
      }
      if (port->seconds > 0) {
        if (port->seconds == 1) {
          if (vibes->value) {
            vibes_long_pulse();
          }
          MenuRowAlign align = (portal_count <= 3) ? MenuRowAlignNone : MenuRowAlignCenter;
          MenuIndex cell_index = {0, i};
          menu_layer_set_selected_index(menu_layer, cell_index, align, true);
          APP_LOG(APP_LOG_LEVEL_DEBUG, "Set selected menu index to %d, %d", cell_index.section, cell_index.row);
        }
        else if (port->seconds <= 5) {
          if (vibes->value) {
            vibes_double_pulse();
          }
        }
        port->seconds--;
      }
      if (!port->seconds && port->wakeup_id) {
        if (wakeup_query(port->wakeup_id, NULL)) {
          wakeup_cancel(port->wakeup_id);
        }
        port->wakeup_id = 0;
      }
      clear_old_hacks(i, port);
      if (port->hacks_done >= port->hacks) {
        // APP_LOG(APP_LOG_LEVEL_DEBUG, "Portal %d burned out %ld seconds ago (more than %d seconds between %ld and %ld)", i, (time(NULL) - port->hacked[0]), SIGNIFICANT_TIME, time(NULL), port->hacked[0]);
        port->seconds = SIGNIFICANT_TIME - (time(NULL) - port->hacked[0]);
      }
    }
  }
  layer_mark_dirty(menu_layer_get_layer(menu_layer));
}

static void wakeup_handler(WakeupId id, int32_t row) {
  MenuIndex index = MenuIndex(0, (int) row);
  MenuRowAlign align = (portal_count <= 3) ? MenuRowAlignNone : MenuRowAlignCenter;
  menu_layer_set_selected_index(menu_layer, index, align, true);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Set selected menu index to %d, %d", index.section, index.row);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Woken up by wakeup %ld (portal %ld)!", (long) id, (long) row);
  Portal *port = &portals[row];
  if (port->wakeup_id) {
    time_t left = 0;
    wakeup_query(port->wakeup_id, &left);
    if (left > 0) {
      port->seconds = left + WAKEUP_BEFORE;
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Found wakeup timer %d for portal %ld, set seconds to %d", (int) port->wakeup_id, (long) row, port->seconds);
      port->wakeup_id = 0;
    }
  }
  clear_old_hacks(row, port);
}
static void hide_all(void *data) {
  window_stack_pop(window);
}
void in_received_handler(DictionaryIterator *received, void *context) {
  Tuple *lt = dict_find(received, PORTALS);
  portal_count = (lt->value->int8 < MAX_PORTAL_COUNT) ? lt->value->int8 : MAX_PORTAL_COUNT;
  Tuple *it = dict_find(received, INDEX);
  int index = it->value->int8;
  if (index < MAX_PORTAL_COUNT) {
    Portal *port = &portals[index];
    Tuple *nt = dict_find(received, NAME);
    strcpy(port->name, nt->value->cstring);
    Tuple *ct = dict_find(received, COOLDOWN);
    port->cooldown_time = ct->value->uint32;
    Tuple *ht = dict_find(received, HACKS);
    port->hacks = ht->value->int8;
    Tuple *hd = dict_find(received, HACKS_DONE);
    port->hacks_done = hd->value->int8;
    for (int i=0; i<port->hacks_done; i++) {
      if (i > MAX_HACKS) {
        break;
      }
      Tuple *hack = dict_find(received, 1 + HACKS_DONE + i);
      port->hacked[i] = hack->value->uint32;
    };
    port->seconds = (port->hacked[port->hacks_done-1] + port->cooldown_time) - time(NULL);
    if (port->seconds < 0) {
      port->seconds = 0;
    }
    if ((port->hacks_done < port->hacks) && (port->seconds > port->cooldown_time)) {
      port->seconds = 0;
    }
    if (port->hacks_done >= port->hacks) {
      // APP_LOG(APP_LOG_LEVEL_DEBUG, "Portal %d burned out %ld seconds ago (more than %d seconds between %ld and %ld)", i, (time(NULL) - port->hacked[0]), SIGNIFICANT_TIME, time(NULL), port->hacked[0]);
      port->seconds = SIGNIFICANT_TIME - (time(NULL) - port->hacked[0]);
    }
    clear_old_hacks(index, port);
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Last hack %d done %d seconds ago (%d)", port->hacks_done, port->seconds, (int) port->hacked[port->hacks_done-1]);
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Got configuration for portal %d: %s, %d, %d, %d, %d", index, port->name, port->cooldown_time, port->hacks, port->hacks_done, port->seconds);
    if ((port->seconds > 0) && (port->seconds < seconds_to_next)) {
      MenuRowAlign align = (portal_count <= 3) ? MenuRowAlignNone : MenuRowAlignCenter;
      MenuIndex cell_index = {0, index};
      menu_layer_set_selected_index(menu_layer, cell_index, align, true);
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Set selected menu index to %d, %d", cell_index.section, cell_index.row);
      seconds_to_next = port->seconds;
    }
  }
  menu_layer_reload_data(menu_layer);
  layer_mark_dirty(menu_layer_get_layer(menu_layer));
}

void in_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "Message from phone dropped: %d", reason);
}

static uint16_t menu_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
  return 2;
}

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Menu rows: %d", portal_count);
  if (section_index > 0) {
    return OPTIONS_LENGTH;
  }
  return portal_count;
}

static int16_t menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Menu header height: %d", MENU_CELL_BASIC_HEADER_HEIGHT);
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void menu_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Time: %s", time_text);
  if (section_index > 0) {
    menu_cell_basic_header_draw(ctx, cell_layer, "Options");
    return;
  }
  menu_cell_basic_header_draw(ctx, cell_layer, time_text);
}

static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  if (cell_index->section > 0) {
    Option *opt = &options[cell_index->row];
    if (cell_index->row == 2) {
      char zone_text[11];
      int h = zones[opt->value].hour;
      int m = zones[opt->value].min;
      snprintf(zone_text, sizeof(zone_text), "UTC %s%d:%02d", (h > 0 ? "+" : ""), h, m);
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Hour %d, min %d", h, m);
      menu_cell_basic_draw(ctx, cell_layer, opt->name, zone_text, NULL);
      return;
    }
    menu_cell_basic_draw(ctx, cell_layer, opt->name, mode[opt->value], NULL);
    return;
  }
  Portal *port = &portals[cell_index->row];
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Menu row for portal %d", cell_index->row);
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Seconds for portal %d: %d", cell_index->row, port->seconds);
  char pre_text[25];
  if (port->hacks_done < port->hacks) {
    snprintf(pre_text, sizeof(pre_text), "Hack %d/%d in", port->hacks_done + 1, port->hacks);
  }
  else {
    strcpy(pre_text, "Burned out for");
  }
  int hours = (int) port->seconds/3600;
  int minutes = (int) (port->seconds - (hours * 3600))/60;
  int seconds = port->seconds - (hours * 3600) - (minutes * 60);
  char time_text[27];
  snprintf(time_text, sizeof(time_text), "%s %d:%02d:%02d", pre_text, hours, minutes, seconds);
  menu_cell_basic_draw(ctx, cell_layer, port->name, time_text, NULL);
}

void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  Option *opt = &options[cell_index->row];
  if (cell_index->section > 0) {
    if (cell_index->row == 2) {
      opt->value += 1;
      if (opt->value >= ZONES_LENGTH) {
        opt->value = 0;
      }
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Set %s to %d:%02d", opt->name, zones[opt->value].hour, zones[opt->value].min);
      return;
    }
    opt->value = !opt->value; // toggle
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Toggled %s to %s", opt->name, mode[opt->value]);
    return;
  }
  Portal *port = &portals[cell_index->row];
  port->hacked[port->hacks_done++] = time(NULL);
  if (port->hacks_done < port->hacks) {
    port->seconds = port->cooldown_time;
  }
  else {
    port->seconds = SIGNIFICANT_TIME - (int) (time(NULL) - port->hacked[0]);
  }
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Hacked portal %d: %s (%d/%d)", cell_index->row, port->name, port->hacks_done, port->hacks);
  layer_mark_dirty(menu_layer_get_layer(menu_layer));
  send_portal_hacks(cell_index->row, port);
  if (hide->value) {
    app_timer_register(3000, hide_all, NULL);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Scheduled auto hide: %d", hide->value);
  }
  else {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "No auto hide: %d", hide->value);
  }
}

void menu_long_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  Portal *port = &portals[cell_index->row];
  port->seconds = 0;
  port->hacks_done = 0;
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Reset portal %d", cell_index->row);
  layer_mark_dirty(menu_layer_get_layer(menu_layer));
  send_portal_hacks(cell_index->row, port);
  if (port->wakeup_id) {
    if (wakeup_query(port->wakeup_id, NULL)) {
      wakeup_cancel(port->wakeup_id);
    }
    port->wakeup_id = 0;
  }
}

void window_load(Window *window) {

  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_frame(window_layer);
  menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(menu_layer, NULL, (MenuLayerCallbacks){
    .get_num_sections = menu_get_num_sections_callback,
    .get_num_rows = menu_get_num_rows_callback,
    .get_header_height = menu_get_header_height_callback,
    .draw_header = menu_draw_header_callback,
    .draw_row = menu_draw_row_callback,
    .select_click = menu_select_callback,
    .select_long_click = menu_long_callback
  });

  menu_layer_set_click_config_onto_window(menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(menu_layer));

  Portal *port1 = &portals[0];
  port1->cooldown_time = 300;
  strcpy(port1->name, "Unnamed Portal 1");
  port1->hacks = 4;
  port1->hacks_done = 0;
  port1->seconds = 0;
  portal_count++;
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Initialized portal 1: %s", port1->name);
  menu_layer_reload_data(menu_layer);
}

static void window_unload(Window *window) {
  menu_layer_destroy(menu_layer);
}

static void init(void) {

  mode[ON] = "On";
  mode[OFF] = "Off";

  vibes = &options[0];
  vibes->key = 1;
  strcpy(vibes->name, "Vibrations");
  vibes->value = persist_exists(vibes->key) ? persist_read_int(vibes->key) : ON;

  hide = &options[1];
  hide->key = 2;
  strcpy(hide->name, "Auto hide");
  hide->value = persist_exists(hide->key) ? persist_read_int(hide->key) : OFF;

  zone = &options[2];
  zone->key = 3;
  strcpy(zone->name, "Timezone");
  zone->value = persist_exists(zone->key) ? persist_read_int(zone->key) : 15;

  zones[0] = (Timezone) {-12, 0};
  zones[1] = (Timezone) {-11, 0};
  zones[2] = (Timezone) {-10, 0};
  zones[3] = (Timezone) {-9, 30};
  zones[4] = (Timezone) {-9, 0};
  zones[5] = (Timezone) {-8, 0};
  zones[6] = (Timezone) {-7, 0};
  zones[7] = (Timezone) {-6, 0};
  zones[8] = (Timezone) {-5, 0};
  zones[9] = (Timezone) {-4, 30};
  zones[10] = (Timezone) {-4, 0};
  zones[11] = (Timezone) {-3, 30};
  zones[12] = (Timezone) {-3, 0};
  zones[13] = (Timezone) {-2, 0};
  zones[14] = (Timezone) {-1, 0};
  zones[15] = (Timezone) {0, 0};
  zones[16] = (Timezone) {1, 0};
  zones[17] = (Timezone) {2, 0};
  zones[18] = (Timezone) {3, 0};
  zones[19] = (Timezone) {3, 30};
  zones[20] = (Timezone) {4, 0};
  zones[21] = (Timezone) {4, 30};
  zones[22] = (Timezone) {5, 0};
  zones[23] = (Timezone) {5, 30};
  zones[24] = (Timezone) {5, 45};
  zones[25] = (Timezone) {6, 0};
  zones[26] = (Timezone) {6, 30};
  zones[27] = (Timezone) {7, 0};
  zones[28] = (Timezone) {8, 0};
  zones[29] = (Timezone) {8, 45};
  zones[30] = (Timezone) {9, 0};
  zones[31] = (Timezone) {9, 30};
  zones[32] = (Timezone) {10, 0};
  zones[33] = (Timezone) {10, 30};
  zones[34] = (Timezone) {11, 0};
  zones[35] = (Timezone) {11, 30};
  zones[36] = (Timezone) {12, 0};
  zones[37] = (Timezone) {12, 45};
  zones[38] = (Timezone) {13, 0};
  zones[39] = (Timezone) {14, 0};

#ifdef PBL_SDK_3
  time_t temp;
  struct tm *t;
  temp = time(NULL);
  t = localtime(&temp);
  EPOCH += t->tm_gmtoff;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Moving EPOCH back %d seconds", t->tm_gmtoff);
  int offset_hours = (int) t->tm_gmtoff/3600;
  if (t->tm_isdst) {
    // breaks if :30 and :45 zones are using DST
    offset_hours += 1;
    // EPOCH += 3600;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Moving EPOCH back for summertime");
  }
  int offset_mins = (int) t->tm_gmtoff%3600;
  for (int i=0; i<ZONES_LENGTH; i++) {
    if ((zones[i].hour == offset_hours) && (zones[i].min == offset_mins)) {
      zone->value = i;
      break;
    }
  }
#endif

  for (int i=0; i<MAX_WAKEUPS; i++) {
    // wakeup storage keys start from 100
    if (persist_exists(i+FIRST_WAKEUP_STORAGE_KEY)) {
      wakeups[i] = persist_read_int(i+FIRST_WAKEUP_STORAGE_KEY);
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Restored wakeup %d", (int) wakeups[i]);
    }
  }

  tick_timer_service_subscribe(SECOND_UNIT, &handle_tick);
  wakeup_service_subscribe(wakeup_handler);
  app_message_register_inbox_received(in_received_handler);
  app_message_register_inbox_dropped(in_dropped_handler);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

  window = window_create();
  // window_set_fullscreen(window, true);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(window, true);
  if (launch_reason() == APP_LAUNCH_WAKEUP) {
    WakeupId id = 0;
    int32_t reason = 0;
    wakeup_get_launch_event(&id, &reason);
    wakeup_handler(id, reason);
  }
}

static void deinit(void) {
  window_destroy(window);
  for (int i=0; i<OPTIONS_LENGTH; i++) {
    persist_write_int(options[i].key, options[i].value);
  }
  for (int i=0; i<MAX_WAKEUPS; i++) {
    if (wakeup_query(wakeups[i], NULL)) {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Storing valid wakeup %d", (int) wakeups[i]);
      wakeups[i] = persist_write_int(i+FIRST_WAKEUP_STORAGE_KEY, wakeups[i]);
    }
  }
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
