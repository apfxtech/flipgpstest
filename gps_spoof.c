#include <furi.h>
#include <gps/gps.h>
#include <gui/gui.h>
#include <math.h>

#define GPS_SPOOF_PERIOD_MS       100
#define GPS_SPOOF_CENTER_LAT_E7   557500000
#define GPS_SPOOF_CENTER_LON_E7   376200000
#define GPS_SPOOF_RADIUS_M        2500.0f
#define GPS_SPOOF_SPEED_MM_S      9000U
#define GPS_SPOOF_DEGREE_E7_M     89.83112f
#define GPS_SPOOF_PI              3.14159265358979323846f

typedef struct {
    FuriMutex* mutex;
    bool connected;
    uint32_t reports;
    GpsLocation location;
} GpsSpoof;

static GpsLocation gps_spoof_get_location(float angle) {
    float center_lat = ((float)GPS_SPOOF_CENTER_LAT_E7 / 10000000.0f) * GPS_SPOOF_PI / 180.0f;
    float north_m = GPS_SPOOF_RADIUS_M * sinf(angle);
    float east_m = GPS_SPOOF_RADIUS_M * cosf(angle);
    uint32_t heading = (uint32_t)(fmodf((angle * 180.0f / GPS_SPOOF_PI) + 90.0f, 360.0f) * 100.0f);

    return (GpsLocation){
        .latitude = GPS_SPOOF_CENTER_LAT_E7 + (int32_t)(north_m * GPS_SPOOF_DEGREE_E7_M),
        .longitude = GPS_SPOOF_CENTER_LON_E7 +
                     (int32_t)((east_m * GPS_SPOOF_DEGREE_E7_M) / cosf(center_lat)),
        .heading = heading,
        .speed = GPS_SPOOF_SPEED_MM_S,
        .altitude = 16500,
        .accuracy = 5000,
        .satellites = 10,
    };
}

static void gps_spoof_draw_location(Canvas* canvas, const GpsLocation* location) {
    char buffer[64];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 32, 8, AlignCenter, AlignBottom, "Latitude");
    canvas_draw_str_aligned(canvas, 96, 8, AlignCenter, AlignBottom, "Longitude");
    canvas_draw_str_aligned(canvas, 32, 34, AlignCenter, AlignBottom, "Heading");
    canvas_draw_str_aligned(canvas, 96, 34, AlignCenter, AlignBottom, "Speed");

    canvas_set_font(canvas, FontSecondary);
    gps_location_format_coordinate(buffer, sizeof(buffer), location->latitude);
    canvas_draw_str_aligned(canvas, 32, 18, AlignCenter, AlignBottom, buffer);
    gps_location_format_coordinate(buffer, sizeof(buffer), location->longitude);
    canvas_draw_str_aligned(canvas, 96, 18, AlignCenter, AlignBottom, buffer);
    gps_location_format_heading(buffer, sizeof(buffer), location->heading);
    canvas_draw_str_aligned(canvas, 32, 44, AlignCenter, AlignBottom, buffer);
    gps_location_format_speed(buffer, sizeof(buffer), location->speed);
    strlcat(buffer, " m/s", sizeof(buffer));
    canvas_draw_str_aligned(canvas, 96, 44, AlignCenter, AlignBottom, buffer);
}

static void gps_spoof_render_callback(Canvas* canvas, void* context) {
    GpsSpoof* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    GpsLocation location = app->location;
    bool connected = app->connected;
    uint32_t reports = app->reports;

    furi_mutex_release(app->mutex);

    gps_spoof_draw_location(canvas, &location);

    char buffer[32];
    canvas_set_font(canvas, FontSecondary);
    snprintf(buffer, sizeof(buffer), "Sent: %lu", reports);
    canvas_draw_str_aligned(canvas, 2, 62, AlignLeft, AlignBottom, buffer);
    canvas_draw_str_aligned(
        canvas, 126, 62, AlignRight, AlignBottom, connected ? "RPC OK" : "No RPC");
}

static void gps_spoof_input_callback(InputEvent* input_event, void* context) {
    FuriMessageQueue* event_queue = context;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

int32_t gps_spoof_app(void* p) {
    UNUSED(p);

    GpsSpoof* app = malloc(sizeof(GpsSpoof));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->connected = false;
    app->reports = 0;
    app->location = gps_spoof_get_location(0.0f);

    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    Gps* gps = furi_record_open(RECORD_GPS);

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, gps_spoof_render_callback, app);
    view_port_input_callback_set(view_port, gps_spoof_input_callback, event_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent event;
    uint32_t next_report = 0;
    float angle = 0.0f;
    for(bool processing = true; processing;) {
        if(furi_message_queue_get(event_queue, &event, 20) == FuriStatusOk) {
            if(event.type == InputTypeShort && event.key == InputKeyBack) {
                processing = false;
            }
        }

        if(furi_get_tick() >= next_report) {
            next_report = furi_get_tick() + furi_ms_to_ticks(GPS_SPOOF_PERIOD_MS);

            const GpsLocation location = gps_spoof_get_location(angle);
            angle += ((float)GPS_SPOOF_SPEED_MM_S / 1000.0f) *
                     ((float)GPS_SPOOF_PERIOD_MS / 1000.0f) / GPS_SPOOF_RADIUS_M;
            if(angle >= 2.0f * GPS_SPOOF_PI) angle -= 2.0f * GPS_SPOOF_PI;
            bool connected = gps_report_location(gps, &location);

            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->location = location;
            app->connected = connected;
            if(connected) app->reports++;
            furi_mutex_release(app->mutex);
        }

        view_port_update(view_port);
    }

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
