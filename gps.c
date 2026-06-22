#include <furi.h>
#include <gui/gui.h>
#include <gps/gps.h>

#define GPS_STREAM_FREQUENCY 4
#define GPS_POLL_PERIOD_MS 1000

typedef struct {
    FuriMutex* mutex;
    bool connected;
    GpsStatus status;
    bool has_fix;
    GpsLocation location;
} GpsView;

static uint32_t gps_abs_i32(int32_t value) {
    return (value < 0) ? (uint32_t)(-(value + 1)) + 1 : (uint32_t)value;
}

static void gps_format_fixed_i32(
    char* buffer,
    size_t buffer_size,
    int32_t value,
    uint32_t scale,
    uint8_t decimals) {
    uint32_t abs_value = gps_abs_i32(value);
    snprintf(
        buffer,
        buffer_size,
        "%s%lu.%0*lu",
        value < 0 ? "-" : "",
        (unsigned long)(abs_value / scale),
        decimals,
        (unsigned long)(abs_value % scale));
}

static void gps_format_fixed_u32(
    char* buffer,
    size_t buffer_size,
    uint32_t value,
    uint32_t scale,
    uint8_t decimals) {
    snprintf(
        buffer,
        buffer_size,
        "%lu.%0*lu",
        (unsigned long)(value / scale),
        decimals,
        (unsigned long)(value % scale));
}

static void gps_location_callback(GpsStatus status, const GpsLocation* location, void* context) {
    GpsView* gps_view = context;
    furi_mutex_acquire(gps_view->mutex, FuriWaitForever);
    gps_view->status = status;
    if(status == GpsStatusOk && location) {
        gps_view->location = *location;
        gps_view->has_fix = true;
    }
    furi_mutex_release(gps_view->mutex);
}

static void render_callback(Canvas* canvas, void* context) {
    GpsView* gps_view = context;
    furi_mutex_acquire(gps_view->mutex, FuriWaitForever);

    char buffer[64];

    if(!gps_view->connected) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignBottom, "No USB/BLE connection");
    } else if(gps_view->status == GpsStatusNotSupported) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignBottom, "GPS not available");
    } else if(gps_view->status == GpsStatusNoPermission) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignBottom, "Permission denied");
    } else if(!gps_view->has_fix) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignBottom, "Waiting for data...");
    } else {
        const GpsLocation* location = &gps_view->location;

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 32, 8, AlignCenter, AlignBottom, "Latitude");
        canvas_draw_str_aligned(canvas, 96, 8, AlignCenter, AlignBottom, "Longitude");
        canvas_draw_str_aligned(canvas, 21, 30, AlignCenter, AlignBottom, "Course");
        canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignBottom, "Speed");
        canvas_draw_str_aligned(canvas, 107, 30, AlignCenter, AlignBottom, "Altitude");
        canvas_draw_str_aligned(canvas, 32, 52, AlignCenter, AlignBottom, "Satellites");
        canvas_draw_str_aligned(canvas, 96, 52, AlignCenter, AlignBottom, "Accuracy");

        canvas_set_font(canvas, FontSecondary);
        gps_format_fixed_i32(buffer, sizeof(buffer), location->latitude, 10000000, 7);
        canvas_draw_str_aligned(canvas, 32, 18, AlignCenter, AlignBottom, buffer);
        gps_format_fixed_i32(buffer, sizeof(buffer), location->longitude, 10000000, 7);
        canvas_draw_str_aligned(canvas, 96, 18, AlignCenter, AlignBottom, buffer);
        gps_format_fixed_u32(buffer, sizeof(buffer), location->heading / 10, 10, 1);
        canvas_draw_str_aligned(canvas, 21, 40, AlignCenter, AlignBottom, buffer);
        gps_format_fixed_u32(buffer, sizeof(buffer), location->speed / 10, 100, 2);
        strlcat(buffer, " m/s", sizeof(buffer));
        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignBottom, buffer);
        gps_format_fixed_i32(buffer, sizeof(buffer), location->altitude / 10, 10, 1);
        strlcat(buffer, " m", sizeof(buffer));
        canvas_draw_str_aligned(canvas, 107, 40, AlignCenter, AlignBottom, buffer);
        snprintf(buffer, sizeof(buffer), "%lu", location->satellites);
        canvas_draw_str_aligned(canvas, 32, 62, AlignCenter, AlignBottom, buffer);
        gps_format_fixed_u32(buffer, sizeof(buffer), location->accuracy / 100, 10, 1);
        strlcat(buffer, " m", sizeof(buffer));
        canvas_draw_str_aligned(canvas, 96, 62, AlignCenter, AlignBottom, buffer);
    }

    furi_mutex_release(gps_view->mutex);
}

static void input_callback(InputEvent* input_event, void* context) {
    FuriMessageQueue* event_queue = context;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

int32_t gps_app(void* p) {
    UNUSED(p);

    GpsView* gps_view = malloc(sizeof(GpsView));
    gps_view->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    gps_view->connected = false;
    gps_view->status = GpsStatusOk;
    gps_view->has_fix = false;

    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    Gps* gps = furi_record_open(RECORD_GPS);
    gps_set_location_callback(gps, gps_location_callback, gps_view);

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, render_callback, gps_view);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent event;
    uint32_t next_poll = 0;
    for(bool processing = true; processing;) {
        if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
            if(event.type == InputTypeShort && event.key == InputKeyBack) {
                processing = false;
            }
        }

        if(furi_get_tick() >= next_poll) {
            next_poll = furi_get_tick() + furi_ms_to_ticks(GPS_POLL_PERIOD_MS);
            bool connected = gps_request_stream(gps, GPS_STREAM_FREQUENCY);
            furi_mutex_acquire(gps_view->mutex, FuriWaitForever);
            gps_view->connected = connected;
            if(!connected) gps_view->has_fix = false;
            furi_mutex_release(gps_view->mutex);
        }

        view_port_update(view_port);
    }

    gps_stop_stream(gps);
    gps_set_location_callback(gps, NULL, NULL);
    furi_record_close(RECORD_GPS);

    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_mutex_free(gps_view->mutex);
    free(gps_view);

    return 0;
}
