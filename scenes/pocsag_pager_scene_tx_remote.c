#include "../pocsag_pager_app_i.h"
#include "../pcsg_serial.h"

/*
 * The control link, held open for as long as this screen is. Owning it here
 * rather than running it in the background means the USB configuration is
 * only ever changed while the user is looking at a screen that says so, and
 * leaving puts it back.
 */
#define PCSG_REMOTE_TICK_MS 250U

static void pocsag_pager_scene_tx_remote_draw(POCSAGPagerApp* app) {
    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 10, AlignCenter, AlignBottom, FontPrimary, "Serial control");

    const bool up = app->serial && pcsg_serial_connected(app->serial);
    widget_add_string_element(
        app->widget,
        64,
        26,
        AlignCenter,
        AlignBottom,
        FontSecondary,
        up ? "Host connected" : "Waiting for host");

    char line[48];
    snprintf(
        line, sizeof(line), "%lu commands",
        app->serial ? (unsigned long)pcsg_serial_commands(app->serial) : 0UL);
    widget_add_string_element(app->widget, 64, 38, AlignCenter, AlignBottom, FontSecondary, line);

    const char* last = app->serial ? pcsg_serial_last(app->serial) : "";
    if(last[0]) {
        widget_add_string_element(app->widget, 64, 50, AlignCenter, AlignBottom, FontSecondary, last);
    }
    widget_add_string_element(
        app->widget, 64, 62, AlignCenter, AlignBottom, FontSecondary, "Back to close the port");
}

void pocsag_pager_scene_tx_remote_on_enter(void* context) {
    POCSAGPagerApp* app = context;
    app->serial = pcsg_serial_alloc(app);
    pocsag_pager_scene_tx_remote_draw(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, POCSAGPagerViewWidget);
}

bool pocsag_pager_scene_tx_remote_on_event(void* context, SceneManagerEvent event) {
    POCSAGPagerApp* app = context;
    if(event.type != SceneManagerEventTypeTick) return false;
    const uint32_t now = furi_get_tick();
    const uint32_t last =
        scene_manager_get_scene_state(app->scene_manager, POCSAGPagerSceneTxRemote);
    if(now - last < furi_ms_to_ticks(PCSG_REMOTE_TICK_MS)) return true;
    scene_manager_set_scene_state(app->scene_manager, POCSAGPagerSceneTxRemote, now);
    pocsag_pager_scene_tx_remote_draw(app);
    return true;
}

void pocsag_pager_scene_tx_remote_on_exit(void* context) {
    POCSAGPagerApp* app = context;
    if(app->serial) {
        pcsg_serial_free(app->serial);
        app->serial = NULL;
    }
    widget_reset(app->widget);
}
