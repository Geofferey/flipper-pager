#include "pocsag_pager_network_info.h"
#include <input/input.h>
#include <gui/elements.h>
#include <furi.h>

/*
 * What the network has said about itself. Its own view rather than widget
 * elements because the screen takes a key: Select swaps the clock between the
 * network's and the Flipper's, which the widget has no way to report without
 * drawing a button over the bottom row.
 *
 * Nothing is read here. The owning scene gathers on the app thread and hands
 * over finished text, so the decoder's sequence-locked words are never read
 * from the GUI thread.
 */

// Centre of the screen, and the rows above and below the clock.
#define PCSG_NETWORK_MID_X   64
#define PCSG_NETWORK_TOP_Y   9
#define PCSG_NETWORK_CLOCK_Y 34
#define PCSG_NETWORK_DATE_Y  45
#define PCSG_NETWORK_ROW1_Y  55
#define PCSG_NETWORK_ROW2_Y  64
// Inset from either edge for the fields that flank the centre.
#define PCSG_NETWORK_LEFT_X  2
#define PCSG_NETWORK_RIGHT_X 126

struct PCSGNetworkInfo {
    View* view;
    PCSGNetworkInfoCallback callback;
    void* context;
};

void pcsg_view_network_info_set_callback(
    PCSGNetworkInfo* pcsg_network_info,
    PCSGNetworkInfoCallback callback,
    void* context) {
    furi_assert(pcsg_network_info);
    pcsg_network_info->callback = callback;
    pcsg_network_info->context = context;
}

static void pcsg_view_network_info_str(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    Align horizontal,
    Font font,
    const char* text) {
    if(!text[0]) return;
    canvas_set_font(canvas, font);
    canvas_draw_str_aligned(canvas, x, y, horizontal, AlignBottom, text);
}

static void pcsg_view_network_info_draw(Canvas* canvas, PCSGNetworkInfoFields* model) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    pcsg_view_network_info_str(
        canvas, PCSG_NETWORK_LEFT_X, PCSG_NETWORK_TOP_Y, AlignLeft, FontSecondary, model->zone);
    pcsg_view_network_info_str(
        canvas, PCSG_NETWORK_RIGHT_X, PCSG_NETWORK_TOP_Y, AlignRight, FontSecondary, model->roam);

    // Big numbers carry digits and the colon and nothing else, so the
    // no-clock case has to be said in the smaller font.
    if(model->have_clock) {
        pcsg_view_network_info_str(
            canvas,
            PCSG_NETWORK_MID_X,
            PCSG_NETWORK_CLOCK_Y,
            AlignCenter,
            FontBigNumbers,
            model->clock);
    } else {
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(
            canvas,
            PCSG_NETWORK_MID_X,
            PCSG_NETWORK_CLOCK_Y - 8,
            AlignCenter,
            AlignBottom,
            "Waiting for");
        canvas_draw_str_aligned(
            canvas,
            PCSG_NETWORK_MID_X,
            PCSG_NETWORK_CLOCK_Y + 2,
            AlignCenter,
            AlignBottom,
            "the network");
    }

    pcsg_view_network_info_str(
        canvas, PCSG_NETWORK_MID_X, PCSG_NETWORK_DATE_Y, AlignCenter, FontSecondary, model->date);

    pcsg_view_network_info_str(
        canvas, PCSG_NETWORK_LEFT_X, PCSG_NETWORK_ROW1_Y, AlignLeft, FontSecondary, model->ssid1);
    pcsg_view_network_info_str(
        canvas,
        PCSG_NETWORK_RIGHT_X,
        PCSG_NETWORK_ROW1_Y,
        AlignRight,
        FontSecondary,
        model->ssid2);

    pcsg_view_network_info_str(
        canvas, PCSG_NETWORK_LEFT_X, PCSG_NETWORK_ROW2_Y, AlignLeft, FontSecondary, model->frame);
    pcsg_view_network_info_str(
        canvas, PCSG_NETWORK_MID_X, PCSG_NETWORK_ROW2_Y, AlignCenter, FontSecondary, model->msgs);
    pcsg_view_network_info_str(
        canvas,
        PCSG_NETWORK_RIGHT_X,
        PCSG_NETWORK_ROW2_Y,
        AlignRight,
        FontSecondary,
        model->ext_sec);
}

static bool pcsg_view_network_info_input(InputEvent* event, void* context) {
    furi_assert(context);
    PCSGNetworkInfo* pcsg_network_info = context;

    // Everything else, Back included, belongs to the scene manager.
    if(event->key != InputKeyOk) return false;
    if(event->type == InputTypeShort && pcsg_network_info->callback) {
        pcsg_network_info->callback(
            PCSGCustomEventViewNetworkSource, pcsg_network_info->context);
    }
    return true;
}

void pcsg_view_network_info_update(
    PCSGNetworkInfo* pcsg_network_info,
    const PCSGNetworkInfoFields* fields) {
    furi_assert(pcsg_network_info);
    furi_assert(fields);

    with_view_model(
        pcsg_network_info->view, PCSGNetworkInfoFields * model, { *model = *fields; }, true);
}

PCSGNetworkInfo* pcsg_view_network_info_alloc(void) {
    PCSGNetworkInfo* pcsg_network_info = malloc(sizeof(PCSGNetworkInfo));
    pcsg_network_info->callback = NULL;
    pcsg_network_info->context = NULL;

    pcsg_network_info->view = view_alloc();
    view_allocate_model(
        pcsg_network_info->view, ViewModelTypeLocking, sizeof(PCSGNetworkInfoFields));
    view_set_context(pcsg_network_info->view, pcsg_network_info);
    view_set_draw_callback(pcsg_network_info->view, (ViewDrawCallback)pcsg_view_network_info_draw);
    view_set_input_callback(pcsg_network_info->view, pcsg_view_network_info_input);

    return pcsg_network_info;
}

void pcsg_view_network_info_free(PCSGNetworkInfo* pcsg_network_info) {
    furi_assert(pcsg_network_info);
    view_free(pcsg_network_info->view);
    free(pcsg_network_info);
}

View* pcsg_view_network_info_get_view(PCSGNetworkInfo* pcsg_network_info) {
    furi_assert(pcsg_network_info);
    return pcsg_network_info->view;
}
