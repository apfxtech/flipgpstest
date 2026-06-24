#include <furi.h>
#include <gps/gps.h>
#include <gui/gui.h>

typedef struct {
    FuriMutex* mutex;
    bool connected;
    bool waiting;
    bool has_fix;
    GpsStatus status;
    GpsLocation location;
} GpsOnce;

static void gps_once_location_callback(
    GpsStatus status,
    const GpsLocation* location,
    void* context) {
    GpsOnce* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->connected = true;
    app->waiting = false;
    app->status = status;
    if(status == GpsStatusOk && location) {
        app->location = *location;
        app->has_fix = true;
    } else {
        app->location = (GpsLocation){0};
        app->has_fix = false;
    }
    furi_mutex_release(app->mutex);
}

static void gps_once_draw_location(Canvas* canvas, const GpsLocation* location) {
    char buffer[64];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 32, 8, AlignCenter, AlignBottom, "Latitude");
    canvas_draw_str_aligned(canvas, 96, 8, AlignCenter, AlignBottom, "Longitude");
    canvas_draw_str_aligned(canvas, 21, 30, AlignCenter, AlignBottom, "Course");
    canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignBottom, "Speed");
    canvas_draw_str_aligned(canvas, 107, 30, AlignCenter, AlignBottom, "Altitude");
    canvas_draw_str_aligned(canvas, 32, 52, AlignCenter, AlignBottom, "Satellites");
    canvas_draw_str_aligned(canvas, 96, 52, AlignCenter, AlignBottom, "Accuracy");

    canvas_set_font(canvas, FontSecondary);
    gps_location_format_coordinate(buffer, sizeof(buffer), location->latitude);
    canvas_draw_str_aligned(canvas, 32, 18, AlignCenter, AlignBottom, buffer);
    gps_location_format_coordinate(buffer, sizeof(buffer), location->longitude);
    canvas_draw_str_aligned(canvas, 96, 18, AlignCenter, AlignBottom, buffer);
    gps_location_format_heading(buffer, sizeof(buffer), location->heading);
    canvas_draw_str_aligned(canvas, 21, 40, AlignCenter, AlignBottom, buffer);
    gps_location_format_speed(buffer, sizeof(buffer), location->speed);
    strlcat(buffer, " m/s", sizeof(buffer));
    canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignBottom, buffer);
    gps_location_format_altitude(buffer, sizeof(buffer), location->altitude);
    strlcat(buffer, " m", sizeof(buffer));
    canvas_draw_str_aligned(canvas, 107, 40, AlignCenter, AlignBottom, buffer);
    snprintf(buffer, sizeof(buffer), "%lu", location->satellites);
    canvas_draw_str_aligned(canvas, 32, 62, AlignCenter, AlignBottom, buffer);
    gps_location_format_accuracy(buffer, sizeof(buffer), location->accuracy);
    strlcat(buffer, " m", sizeof(buffer));
    canvas_draw_str_aligned(canvas, 96, 62, AlignCenter, AlignBottom, buffer);
}

static void gps_once_render_callback(Canvas* canvas, void* context) {
    GpsOnce* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    bool connected = app->connected;
    bool waiting = app->waiting;
    bool has_fix = app->has_fix;
    GpsStatus status = app->status;
    GpsLocation location = app->location;

    furi_mutex_release(app->mutex);

    if(!connected) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignBottom, "No USB/BLE connection");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignBottom, "Press OK to request");
    } else if(waiting) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignBottom, "Waiting for data...");
    } else if(status == GpsStatusNotSupported) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignBottom, "GPS not available");
    } else if(status == GpsStatusNoPermission) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignBottom, "Permission denied");
    } else if(status == GpsStatusDisabled) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignBottom, "Location disabled");
    } else if(status == GpsStatusUnknown) {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignBottom, "Location error");
    } else if(has_fix) {
        gps_once_draw_location(canvas, &location);
    } else {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignBottom, "Press OK");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignBottom, "single location request");
    }
}

static void gps_once_input_callback(InputEvent* input_event, void* context) {
    FuriMessageQueue* event_queue = context;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

int32_t gps_once_app(void* p) {
    UNUSED(p);

    GpsOnce* app = malloc(sizeof(GpsOnce));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->connected = true;
    app->waiting = false;
    app->has_fix = false;
    app->status = GpsStatusOk;
    app->location = (GpsLocation){0};

    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    Gps* gps = furi_record_open(RECORD_GPS);
    gps_set_location_callback(gps, gps_once_location_callback, app);

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, gps_once_render_callback, app);
    view_port_input_callback_set(view_port, gps_once_input_callback, event_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent event;
    for(bool processing = true; processing;) {
        if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
            if(event.type == InputTypeShort && event.key == InputKeyBack) {
                processing = false;
            } else if(event.type == InputTypeShort && event.key == InputKeyOk) {
                bool connected = gps_request_location(gps);
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                app->connected = connected;
                app->waiting = connected;
                if(!connected) app->has_fix = false;
                furi_mutex_release(app->mutex);
            }
        }

        view_port_update(view_port);
    }

    gps_set_location_callback(gps, NULL, NULL);
    furi_record_close(RECORD_GPS);

    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_mutex_free(app->mutex);
    free(app);

    return 0;
}
