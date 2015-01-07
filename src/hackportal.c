#include <pebble.h>
  
#define MAX_PORTAL_COUNT 3

static Window *window;
static const int MAX_HACKS = 16;
static const uint32_t EPOCH = 1388527200;
static const uint32_t SECS_IN_CYCLE = 630000;  
static const uint32_t SECS_IN_CHECKPOINT = 18000;
static char time_text[] = "2014.43     29/35    4:53:23";
int portal_count = 0;

enum MessageKey {
  INDEX = 0x1,    // TUPLE_INT
  NAME = 0x2,     // TUPLE_CSTRING
  COOLDOWN = 0x3, // TUPLE_INT
  HACKS = 0x4    // TUPLE_INT
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
  uint32_t cycle = t / SECS_IN_CYCLE;
	uint32_t checkpoint = (t % SECS_IN_CYCLE) / SECS_IN_CHECKPOINT + 1;
	uint32_t countdown = SECS_IN_CHECKPOINT - (t % SECS_IN_CHECKPOINT);
	uint32_t next = rt + countdown;
  int year = tick_time->tm_year + 1900;
  int month = tick_time->tm_mon;
  if ((cycle > 47) && (month < 2)) {
    year--;
  }
  int hours = (int) (countdown / (60 * 60));
  int minutes = (countdown - (hours * 60 * 60)) / 60;
  int seconds = countdown - (hours * 60 * 60) - (minutes * 60);
  char next_str[80];
  struct tm *tms;
  tms = localtime((time_t*) &next);
  strftime(next_str, sizeof(next_str), "%Y-%m-%d %H:%M:%S", tms);
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Time: %lu, (now: %lu), countdown %lu", t, rt, countdown);
  // APP_LOG(APP_LOG_LEVEL_DEBUG, "Coming up: %s", next_str);
  snprintf(time_text, sizeof(time_text), "%d.%02lu     %02lu/35    %d:%02d:%02d", year, cycle, checkpoint, hours, minutes, seconds);  
  // text_layer_set_text(text_time_layer, time_text);
  layer_mark_dirty(menu_layer_get_layer(menu_layer));
}

static uint16_t menu_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
  return 1;
}

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return portal_count;
}

static int16_t menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void menu_draw_header_callback(GContext* ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
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
  int hours = (int) port->seconds/3600;
  int minutes = (int) (port->seconds - (hours * 3600))/60;
  int seconds = port->seconds - (hours * 3600) - (minutes * 60);
  char time_text[25];
  snprintf(time_text, sizeof(time_text), "Hack %d/%d in %d:%02d:%02d", port->hacks_done + 1, port->hacks, hours, minutes, seconds);  
  menu_cell_basic_draw(ctx, cell_layer, port->name, time_text, NULL);
}

void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  Portal *port = &portals[cell_index->row];
  port->hacked[port->hacks_done++] = time(NULL);
  port->seconds = port->cooldown_time;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Hacked portal %d: %s (%d/%d)", cell_index->row, port->name, port->hacks_done, port->hacks);
}
void in_received_handler(DictionaryIterator *received, void *context) {
  // does not remove portals from list!
  Tuple *it = dict_find(received, INDEX);
  int index = it->value->int8;
  portal_count = (portal_count > index) ? portal_count : index;
  Portal *port = &portals[index];
  Tuple *nt = dict_find(received, NAME);
  strcpy(port->name, nt->value->cstring);
  Tuple *ct = dict_find(received, COOLDOWN);
  port->cooldown_time = ct->value->int8;
  Tuple *ht = dict_find(received, HACKS);
  port->hacks = ht->value->int8;
  port->hacks_done = 0;
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
    .select_click = menu_select_callback,
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
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Initialized portal 1: %s", port1->name);
  Portal *port2 = &portals[1];
  strcpy(port2->name, "Unnamed Portal 2");
  port2->cooldown_time = 300;
  port2->hacks = 4;
  port2->hacks_done = 0;
  port2->seconds = 0;
  portal_count++;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Initialized portal 2: %s", port2->name);
  Portal *port3 = &portals[2];
  strcpy(port3->name, "Unnamed Portal 3");
  port3->cooldown_time = 300;
  port3->hacks = 4;
  port3->hacks_done = 0;
  port3->seconds = 0;
  portal_count++;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Initialized portal 3: %s", port3->name); 
}

static void window_unload(Window *window) {
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
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(window, true);
  app_event_loop();
  window_destroy(window);
}

static void deinit(void) {
  window_destroy(window);
}

int main(void) {
  init();
  deinit();
}
