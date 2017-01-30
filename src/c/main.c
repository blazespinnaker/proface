#include <pebble.h>
#include "pebble_fonts.h"
#include "common.h"

#define NO_EVENTS "No events."

static Window *s_main_window;
static TextLayer *s_time_layer;
static TextLayer *s_line_layer;
static TextLayer *s_date_layer;
static TextLayer *s_calendar_layer;
static TextLayer *s_list_layer;
static uint8_t battery_level = 113;
static bool battery_plugged;
static bool g_next = FALSE;
static bool g_two_events = FALSE;
static int8_t g_count = -1;
static uint8_t g_received_rows = 0;
static int8_t r_count = -1;
static uint8_t r_received_rows = 0;
static int8_t rl_count = -1;
static uint8_t rl_received_rows = 0;
static EVENT_TYPE g_event;
static EVENT_TYPE g2_event;
static void battery_state_handler(BatteryChargeState charge);
static void main_window_load(Window *window);
static void update_time_str();
void log_message(DictionaryIterator *received);

static void reminder_request() {
    APP_LOG(APP_LOG_LEVEL_INFO, "reminder Request");
    if (!bluetooth_connection_service_peek()) {
      APP_LOG(APP_LOG_LEVEL_INFO, "No bluetooth");
      return;
    }
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    if (!iter)
      return;
    r_count = -1;
    r_received_rows = 0;
    rl_count = -1;
    rl_received_rows = 0;
    dict_write_int8(iter, REQUEST_REMINDERS_KEY, -1);
    dict_write_end(iter);
    app_message_outbox_send();
    APP_LOG(APP_LOG_LEVEL_INFO, "Reminder Messages sent, bt connected.");
  }

static void calendar_request() {
    APP_LOG(APP_LOG_LEVEL_INFO, "Calendar Request");
    if (!bluetooth_connection_service_peek()) {
      APP_LOG(APP_LOG_LEVEL_INFO, "No bluetooth");
      return;
    }
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    if (!iter)
      return;

    dict_write_int8(iter, REQUEST_CALENDAR_KEY, -1);
    dict_write_uint8(iter, CALENDAR_RESPONSE_FORMAT_KEY, CALENDAR_RESPONSE_FORMAT_SELECTED);
    dict_write_end(iter);
    g_count = -1;
    g_received_rows = 0;
    g_two_events = FALSE;
    g_next = FALSE;
    app_message_outbox_send();
    APP_LOG(APP_LOG_LEVEL_INFO, "Messages sent, bt connected.");
  }


void received_message(DictionaryIterator *received, void *context) {
  log_message(received);
  static char r_buffer[256];
  static char tr_buffer[256];
  Tuple *tuple = dict_find(received, RECONNECT_KEY);
  if (tuple) {
    calendar_request();
  }
  
   tuple = dict_find(received, REMINDERS_RESPONSE_KEY);

  if (tuple) {
    Reminder *r = (Reminder *)tuple->value;
    next_reminder:
    r_received_rows++;
    if (r_count == -1) {
      r = (Reminder *)&tuple->value->data[1];
       APP_LOG(APP_LOG_LEVEL_INFO, "Reminder First %s %d", r->title, tuple->length);
      r_count = tuple->value->data[0];
      snprintf(r_buffer, sizeof(r_buffer), "%s", r->title);
      if (tuple->length > (sizeof(Reminder)+1)) {
        r = (Reminder *)&tuple->value->data[1+sizeof(Reminder)];
       APP_LOG(APP_LOG_LEVEL_INFO, "Reminder First Second %s %d", r->title, tuple->length);
        tuple->length = tuple->length -  (sizeof(Reminder)+1);
        goto next_reminder;
      }
    } else {
      next_reminder_2:
       APP_LOG(APP_LOG_LEVEL_INFO, "Reminder 2 %s %d", r->title, tuple->length);
       if (r_received_rows < 4) {
         snprintf(tr_buffer, sizeof(tr_buffer), "%s", r_buffer);
         snprintf(r_buffer, sizeof(r_buffer), "%s\n%s", tr_buffer, r->title);
       } 
      if (tuple->length > sizeof(Reminder)) {
        r = (Reminder *)&tuple->value->data[0+sizeof(Reminder)];
       APP_LOG(APP_LOG_LEVEL_INFO, "Reminder 2 second %s %d", r->title, tuple->length);
        tuple->length = tuple->length - (sizeof(Reminder));
        goto next_reminder_2;
      }
    }
    text_layer_set_text(s_list_layer, r_buffer);
   
    APP_LOG(APP_LOG_LEVEL_INFO, "Reminders r %s", r_buffer);
    APP_LOG(APP_LOG_LEVEL_INFO, "Reminders tr %s", tr_buffer);
    return;
  }
  
   tuple = dict_find(received, REMINDER_LISTS_RESPONSE_KEY);

  if (tuple) {
    ReminderList *r = (ReminderList *)tuple->value;
    rl_received_rows++;
    if (rl_count == -1) {
      r = (ReminderList *)&tuple->value->data[1];
      rl_count = tuple->value->data[0];
      APP_LOG(APP_LOG_LEVEL_INFO, "ReminderList %d %s %d %d", r->index, r->title, rl_count, rl_received_rows);
    } else {
       APP_LOG(APP_LOG_LEVEL_INFO, "ReminderList %d %s", r->index, r->title, rl_count, rl_received_rows);
       return;
    }
  }
  
  
   // Gather the bits of a calendar together
  tuple = dict_find(received, CALENDAR_RESPONSE_KEY);

  if (tuple) {
   
    if (g_count == -1) {
      g_count = tuple->value->data[0];
      g_next = TRUE;
      memcpy(&g_event, &tuple->value->data[1], sizeof(EVENT_TYPE));
      APP_LOG(APP_LOG_LEVEL_INFO, "Gcount set to %d gettinf from offset 1", g_count);
      update_time_str();
      g_received_rows=1;
    } else {
       g_received_rows++;
       APP_LOG(APP_LOG_LEVEL_INFO, "Next message %d %d %d", g_next, g_received_rows, g_count);
       if (g_next == TRUE) {
         g_next = FALSE;
         g_two_events = TRUE;
         memcpy(&g2_event, &tuple->value->data[0], sizeof(EVENT_TYPE));
         APP_LOG(APP_LOG_LEVEL_INFO, "Next message annd two events %d %d", g_next, g_two_events);
         update_time_str();
       }
    }
    if (g_received_rows == g_count) {
      reminder_request();
    }
  }
  
}

static void update_time() {
  // Get a tm structure
  time_t temp = time(NULL); 
  struct tm *tick_time = localtime(&temp);
  if (battery_level == 113) {
    battery_level = 0;
    battery_state_handler(battery_state_service_peek());
    return;
  }
  // Write the current hours and minutes into a buffer
  static char t_buffer[32];
  static char s_buffer[32];
  static char d_buffer[32];
  bool bt = bluetooth_connection_service_peek();
  strftime(t_buffer, sizeof(t_buffer), "%H:%M", tick_time);
  strftime(s_buffer, sizeof(s_buffer), "%m/%d", tick_time);
  snprintf(d_buffer, sizeof(d_buffer), "%s\n%d%s %s", s_buffer, battery_level, battery_plugged ? "+" : "%", bt ? "B" : "-");
  // Display this time on the TextLayer
  text_layer_set_text(s_time_layer, t_buffer);
  text_layer_set_text(s_date_layer, d_buffer);
  update_time_str();
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
}

static TextLayer *get_layer(Layer *window_layer, int startx, int starty, int width, int height, GFont font, GColor bg, GColor fg) {
  TextLayer *layer = text_layer_create(
      GRect(startx, starty, width, height));

  text_layer_set_background_color(layer, bg);
  text_layer_set_text_color(layer, fg);
  text_layer_set_font(layer, font);
  text_layer_set_text_alignment(layer, GTextAlignmentLeft);

  // Add it as a child layer to the Window's root layer
  layer_add_child(window_layer, text_layer_get_layer(layer));

  return layer;
}

static void battery_state_handler(BatteryChargeState charge) {
  battery_level = charge.charge_percent;
  battery_plugged = charge.is_plugged;
  update_time();
}


static void main_window_unload(Window *window) {
  // Destroy TextLayer
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_line_layer);
}

void log_message(DictionaryIterator *received) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Logging messages");
  Tuple* tuple = dict_read_first(received);
        if(!tuple) return;
        do {
            // Don't pass along reserved values.
            APP_LOG(APP_LOG_LEVEL_INFO, "Tuple: key %ld type %d length %d", tuple->key, tuple->type, tuple->length);
            if (tuple->type == TUPLE_CSTRING || tuple->type == TUPLE_BYTE_ARRAY) {
              APP_LOG(APP_LOG_LEVEL_INFO, "Type is CSTRING %s", (char *)tuple->value);
            }
        }while((tuple = dict_read_next(received)));
}
  
  
int get_time_str(char *buffer, int buff_size, EVENT_TYPE *gevent) {
  time_t diff = gevent->start_date - time(NULL);
   APP_LOG(APP_LOG_LEVEL_INFO, "gevent %p diff %ld", gevent, diff);
  if (strlen(gevent->title) > 13) {
    gevent->title[13]=0;
  }
  int days = diff / (3600 * 24);
  if (days > 0) {
    diff = diff - days * (3600 * 24);
  }
  int hours = diff / 3600;
  if (hours > 0) {
    diff = diff - hours * 3600;
  }
  int minutes = diff / 60;
  if (days < 0 || days > 100 || minutes < -60 || minutes > 61 || hours < 0 || hours > 25) {
    snprintf(buffer, buff_size, NO_EVENTS);
    return -1;
  }
  if (hours == 0 && days == 0 && minutes < 6 && minutes > -1) {
     vibes_long_pulse();
  }
  if (days > 0) {
    snprintf(buffer, buff_size, "%dD%d:%s", days, hours, gevent->title);
  } else if (hours > 0) {
    snprintf(buffer, buff_size, "%dH%d:%s", hours, minutes, gevent->title);
  } else {
    snprintf(buffer, buff_size, "%dm:%s", minutes, gevent->title);    
  }
  return minutes;
}


void update_time_str() {
  static char buffer[63];
  static char nbuffer[63];
  static char fbuffer[64];
  if (g_count == -1) {
    return;
  }
  int buff_size=sizeof(buffer);
  int minutes = get_time_str(buffer, buff_size, &g_event);
  text_layer_set_text(s_calendar_layer, buffer);
  if (strncmp(buffer, NO_EVENTS, buff_size) != 0) {
    if (g_two_events == TRUE) {
      minutes = get_time_str(nbuffer, sizeof(nbuffer), &g2_event);
      snprintf(fbuffer, sizeof(fbuffer), "%s\n%s", buffer, nbuffer);
      APP_LOG(APP_LOG_LEVEL_INFO, "Fbuffer: %s", fbuffer);
      text_layer_set_text(s_calendar_layer, fbuffer);
      if (minutes % 10 == 0) {
        calendar_request();
      }
    }
  }   
}


void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "App Msg Send Fail %d", reason);
}

void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Tapped");
}
  
static void init() {
  // Create main Window element and assign to pointer
  s_main_window = window_create();

  // Set handlers to manage the elements inside the Window
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  // Show the Window on the watch, with animated=true
  window_stack_push(s_main_window, true);

  // Make sure the time is displayed from the start
  update_time();

  // Register with TickTimerService
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_state_handler);
  //accel_tap_service_subscribe(accel_tap_handler);
  const int inbound_size = app_message_inbox_size_maximum();
  const int outbound_size = app_message_outbox_size_maximum();
  app_message_open(inbound_size, outbound_size);
  app_message_register_inbox_received(received_message);
  app_message_register_outbox_failed(out_failed_handler);
  calendar_request();
}

static void deinit() {
  // Destroy Window
  window_destroy(s_main_window);
  tick_timer_service_unsubscribe();
  app_message_deregister_callbacks();
  battery_state_service_unsubscribe();
  accel_tap_service_unsubscribe();
}

static void main_window_load(Window *window) {
  // Get information about the Window
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  int window_width = bounds.size.w;
  
  // Time, Date, Battery and Bluetooth layers
  s_time_layer = get_layer(window_layer, 1,0,window_width,40,
    fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK), GColorClear, GColorDarkCandyAppleRed);
  s_date_layer = get_layer(window_layer, window_width/2+25,0,window_width,40,
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GColorClear, GColorBlack);

  //Calendar and List layers
  s_calendar_layer = get_layer(window_layer, 0,38,window_width,40,
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GColorOrange, GColorBlack);
 
  s_list_layer = get_layer(window_layer, 0,78,window_width,bounds.size.h-78,
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GColorYellow, GColorBlack);
  
  //Lines
  get_layer(window_layer, window_width/2+21,3,1,34,
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GColorBlack, GColorBlack);
  get_layer(window_layer, 0,38,window_width,2,
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), GColorBlack, GColorBlack);

}


int main(void) {
  init();
  app_event_loop();
  deinit();
}