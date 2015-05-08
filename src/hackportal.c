#include <pebble.h>
  
#define MAX_PORTAL_COUNT 20
#define MAX_HACKS 32
#define WAKEUP_BEFORE 12

static Window *window;
static const uint32_t EPOCH = 1388527200; // beginning of cycle 2014.01
static const int EPOCH_YEAR = 2014;
static const uint32_t SECS_IN_CYCLE = 630000;  
static const uint32_t SECS_IN_CHECKPOINT = 18000;
static const int16_t SIGNIFICANT_TIME = 4 * 60 * 60;
static char time_text[] = "2014.43     29/35    4:53:23";
static int16_t portal_count = 0;

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

Portal portals[MAX_PORTAL_COUNT];

static void handle_tick(struct tm *tick_time, TimeUnits units_changed) {
  time_t rt = time(NULL);
  uint32_t t = rt - EPOCH;
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
  snprintf(time_text, sizeof(time_text), "%d.%02lu     %02lu/35    %d:%02d:%02d",
                                         year, cycle, checkpoint, hours, minutes, seconds);  
  layer_mark_dirty(menu_layer_get_layer(menu_layer));
}

static void wakeup_handler(WakeupId id, int32_t row) {
  MenuIndex index = MenuIndex(0, (int) row);
  MenuRowAlign align = (portal_count <= 3) ? MenuRowAlignNone : MenuRowAlignCenter;
  menu_layer_set_selected_index(menu_layer, index, align, true);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Woken up by wakeup %lu (portal %lu)!", id, row);
}

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
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Sent hacks for portal %d (%d) to phone!", index, port->hacks_done);
  if (port->wakeup_id && wakeup_query(port->wakeup_id, NULL)) {
    wakeup_cancel(port->wakeup_id);
  }
  port->wakeup_id = wakeup_schedule(time(NULL) + port->seconds - WAKEUP_BEFORE, (int32_t) index, true);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Set wakeup timer for portal %d (%lu).", index, port->wakeup_id);
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
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Last hack %d done %d seconds ago (%d)", port->hacks_done, port->seconds, (int) port->hacked[port->hacks_done-1]);
    if (port->seconds > port->cooldown_time) {
      port->seconds = 0;
    }
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Got configuration for portal %d: %s, %d, %d, %d", index, port->name, port->cooldown_time, port->hacks, port->hacks_done);    
  }
  menu_layer_reload_data(menu_layer);
  layer_mark_dirty(menu_layer_get_layer(menu_layer));
}

void in_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Message from phone dropped: %d", reason);
}

static uint16_t menu_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
  return 1;
}

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Menu rows: %d", portal_count);
  return portal_count;
}

static int16_t menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Menu header height: %d", MENU_CELL_BASIC_HEADER_HEIGHT);
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void menu_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Time: %s", time_text);
  menu_cell_basic_header_draw(ctx, cell_layer, time_text);
}

static void menu_draw_row_callback(GContext* ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  Portal *port = &portals[cell_index->row];
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Menu row for portal %d", cell_index->row);
  if (!port->seconds || (port->seconds < 0)) {
    if (port->wakeup_id) {
      time_t left;
      wakeup_query(port->wakeup_id, &left);
      port->seconds = left + WAKEUP_BEFORE;
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Found wakeup timer %lu for portal %d, set seconds to %d", port->wakeup_id, cell_index->row, port->seconds);
      port->wakeup_id = 0;
    }
    else {
      port->seconds = 0;
    }
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Seconds for portal %d: %d", cell_index->row, port->seconds);
  if (port->seconds > 0) {
    if (port->seconds == 1) {
      vibes_long_pulse();
      MenuRowAlign align = (portal_count <= 3) ? MenuRowAlignNone : MenuRowAlignCenter;
      menu_layer_set_selected_index(menu_layer, *cell_index, align, true);
    }
    else if (port->seconds <= 5) {
      vibes_double_pulse();
    }
    port->seconds--;  
  }
  if (!port->seconds && port->wakeup_id) {
    if (wakeup_query(port->wakeup_id, NULL)) {
      wakeup_cancel(port->wakeup_id);
    }
    port->wakeup_id = 0;
  }
  int shift = 0;
  time_t now = time(NULL);
  for (int i=0; i<port->hacks_done; i++) {
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Old hack %d: %d, %d (%d)", i, (int) port->hacked[i], port->cooldown_time, (int) now);
    if ((int) (now - port->hacked[i]) > SIGNIFICANT_TIME) {
      shift++;
    }
  }
  if (shift > 0) {
    for (int i=shift; i<port->hacks_done; i++) {
      port->hacked[i-shift] = port->hacked[i];
    }
  }
  port->hacks_done -= shift;

  char pre_text[25];
  if (port->hacks_done < port->hacks) {
    snprintf(pre_text, sizeof(pre_text), "Hack %d/%d in", port->hacks_done + 1, port->hacks);
  }
  else {
    strcpy(pre_text, "Burned out for");
    port->seconds = SIGNIFICANT_TIME - (int) (now - port->hacked[0]);
  }
  int hours = (int) port->seconds/3600;
  int minutes = (int) (port->seconds - (hours * 3600))/60;
  int seconds = port->seconds - (hours * 3600) - (minutes * 60);
  char time_text[27];
  snprintf(time_text, sizeof(time_text), "%s %d:%02d:%02d", pre_text, hours, minutes, seconds);  
  menu_cell_basic_draw(ctx, cell_layer, port->name, time_text, NULL);

  if (shift > 0) {
    send_portal_hacks(cell_index->row, port);
  }
}

void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
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
  // for (int i=0; i<MAX_PORTAL_COUNT; i++) {
  //   if (portals[i]) {
  //     free(portals[i]);
  //   }
  // }
  // free(portals);
  menu_layer_destroy(menu_layer);
  // delete portals?
}

static void init(void) {
  tick_timer_service_subscribe(SECOND_UNIT, &handle_tick);
  wakeup_service_subscribe(wakeup_handler);
  app_message_register_inbox_received(in_received_handler);
  app_message_register_inbox_dropped(in_dropped_handler);
/*
const uint32_t inbound_size = 256;
  const uint32_t outbound_size = 256;
  app_message_open(inbound_size, outbound_size);
*/
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
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
