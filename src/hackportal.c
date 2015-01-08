#include <pebble.h>
  
#define MAX_PORTAL_COUNT 20

static Window *window;
static const uint32_t EPOCH = 1388527200; // beginning of cycle 2014.01
static const int EPOCH_YEAR = 2014;
static const uint32_t SECS_IN_CYCLE = 630000;  
static const uint32_t SECS_IN_CHECKPOINT = 18000;
static const int16_t SIGNIFICANT_TIME = 4 * 60 * 60;
static char time_text[] = "2014.43     29/35    4:53:23";
static int16_t portal_count = 0;

enum MessageKey {
  INDEX = 0,    // TUPLE_INT
  NAME = 1,     // TUPLE_CSTRING
  COOLDOWN = 2, // TUPLE_INT
  HACKS = 3,    // TUPLE_INT
  PORTALS = 4   // TUPLE_INT
};
static MenuLayer *menu_layer;

typedef struct{
  char name[80];
  time_t hacked[16];
  time_t burned_out;
  int hacks_done;
  int hacks;
  int cooldown_time;
  int insulation;
  int seconds;
  TextLayer *name_layer;
  AppTimer *timer;
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
  if (port->seconds > 0) {
    if (port->seconds == 1) {
      vibes_long_pulse();
      MenuRowAlign align = (portal_count <= 3) ? MenuRowAlignNone : MenuRowAlignCenter;
      menu_layer_set_selected_index(menu_layer, *cell_index, align, true);
    }
    else if (port->seconds <= 5) {
      vibes_short_pulse();
    }
    port->seconds--;  
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
}

void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  Portal *port = &portals[cell_index->row];
  // hacks should be stored!
  port->hacked[port->hacks_done++] = time(NULL);
  port->seconds = port->cooldown_time;
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Hacked portal %d: %s (%d/%d)", cell_index->row, port->name, port->hacks_done, port->hacks);
  layer_mark_dirty(menu_layer_get_layer(menu_layer));
}
void in_received_handler(DictionaryIterator *received, void *context) {
  Tuple *it = dict_find(received, 0);
  int index = it->value->int8;
  if (index < MAX_PORTAL_COUNT) {
    portal_count = (portal_count > index) ? portal_count : index;
    Portal *port = &portals[index];
    Tuple *nt = dict_find(received, NAME);
    strcpy(port->name, nt->value->cstring);
    Tuple *ct = dict_find(received, COOLDOWN);
    port->cooldown_time = ct->value->uint32;
    Tuple *ht = dict_find(received, HACKS);
    port->hacks = ht->value->int8;
    port->hacks_done = 0;
    Tuple *lt = dict_find(received, PORTALS);
    portal_count = lt->value->int8;
    if (portal_count > MAX_PORTAL_COUNT) {
      portal_count = MAX_PORTAL_COUNT;
    }
    // APP_LOG(APP_LOG_LEVEL_DEBUG, "Got configuration for portal %d: %s, %d, %d, %d", index, port->name, port->cooldown_time, port->hacks, port->hacks_done);    
  }
  menu_layer_reload_data(menu_layer);
  layer_mark_dirty(menu_layer_get_layer(menu_layer));
}
void in_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Message from phone dropped: %d", reason);
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
    .select_click = menu_select_callback
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
  app_message_register_inbox_received(in_received_handler);
  app_message_register_inbox_dropped(in_dropped_handler);
  const uint32_t inbound_size = 128;
  const uint32_t outbound_size = 128;
  app_message_open(inbound_size, outbound_size);
  window = window_create();
  // window_set_fullscreen(window, true);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(window, true);
}

static void deinit(void) {
  window_destroy(window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}