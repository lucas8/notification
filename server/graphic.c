
#include "graphic.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>

static const char* _font_name = "-*-terminal-medium-r-*-*-14-*-*-*-*-*-iso8859-*";

struct _gcontext {
    char* name;
    xcb_gcontext_t gc;
    struct _gcontext* next;
};
static struct _gcontext* _first = NULL;

static xcb_font_t _font;
static uint32_t _width;
static uint32_t _fg;
static uint32_t _bg;

static int char_value(char c)
{
    if(c >= '0' && c <= '9')
        return c - '0';
    else if(c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else if(c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    else
        return 0;
}

static void get_color_1(const char* str, uint16_t* r, uint16_t* g, uint16_t* b)
{
    int val = char_value(str[0]);
    *r = *g = *b = val * 4369;
}

static void get_color_3(const char* str, uint16_t* r, uint16_t* g, uint16_t* b)
{
    int val = char_value(str[0]);
    *r = val * 4369;
    val = char_value(str[1]);
    *g = val * 4369;
    val = char_value(str[2]);
    *b = val * 4369;
}

static void get_color_6(const char* str, uint16_t* r, uint16_t* g, uint16_t* b)
{
    int val = char_value(str[0]) * 16 + char_value(str[1]);
    *r = val * 257;
    val = char_value(str[2]) * 16 + char_value(str[3]);
    *g = val * 257;
    val = char_value(str[4]) * 16 + char_value(str[5]);
    *b = val * 257;
}

static uint32_t to_color(const char* str, xcb_connection_t* c, srv_screen_t* scr)
{
    uint16_t r, g, b;
    uint32_t ret;
    const char* used = str;
    xcb_colormap_t cmap;
    xcb_alloc_color_cookie_t cookie;
    xcb_alloc_color_reply_t* rep = NULL;

    switch(strlen(str)) {
        case 2: /* Format color : #a -> #AAAAAA */
            ++used;
        case 1: /* Format color : a -> #AAAAAA */
            get_color_1(used, &r, &g, &b);
            break;
        case 4: /* Format color : #abc -> #AABBCC */
            ++used;
        case 3: /* Format color : abc -> #AABBCC */
            get_color_3(used, &r, &g, &b);
            break;
        case 7: /* Format color : #1a2b3c -> #1A2B3C */
            ++used;
        case 6: /* Format color : 1a2b3c -> #1A2B3C */
            get_color_6(used, &r, &g, &b);
            break;
        default:
            return scr->xcbscr->black_pixel;
            break;
    }

    cmap   = scr->xcbscr->default_colormap;
    cookie = xcb_alloc_color(c, cmap, r, g, b);
    rep    = xcb_alloc_color_reply(c, cookie, NULL);
    ret    = rep->pixel;

    if(rep)
        free(rep);
    return ret;
}

static uint32_t load_font(const char* name, xcb_connection_t* c)
{
    xcb_font_t font;
    font = xcb_generate_id(c);
    xcb_open_font(c, font, strlen(name), name);
    return font;
}

static int load_defaults(xcb_connection_t* c, srv_screen_t* scr)
{
    const char* font_name = _font_name;

    if(has_entry("gc.font"))
        font_name = get_string("gc.font");
    _font = load_font(font_name, c);

    if(has_entry("gc.width"))
        _width = get_int("gc.width");
    else
        _width = 5;

    if(has_entry("gc.fg"))
        _fg = to_color(get_string("gc.fg"), c, scr);
    else
        _fg = scr->xcbscr->white_pixel;

    if(has_entry("gc.bg"))
        _bg = to_color(get_string("gc.bg"), c, scr);
    else
        _bg = scr->xcbscr->black_pixel;

    return 1;
}

static void populate_defaults(uint32_t* values)
{
    values[0] = _fg;
    values[1] = _bg;
    values[2] = _width;
    values[3] = _font;
}

static void add_gc(const char* name, xcb_connection_t* c, srv_screen_t* scr)
{
    xcb_gcontext_t gc;
    uint32_t mask;
    uint32_t values[4];
    char buffer[256];
    struct _gcontext* ctx;

    gc = xcb_generate_id(c);
    mask = XCB_GC_FOREGROUND
        | XCB_GC_BACKGROUND
        | XCB_GC_LINE_WIDTH
        | XCB_GC_FONT;
    populate_defaults(values);

    snprintf(buffer, 256, "gc.%s.fg", name);
    if(has_entry(buffer))
        values[0] = to_color(get_string(buffer), c, scr);
    snprintf(buffer, 256, "gc.%s.bg", name);
    if(has_entry(buffer))
        values[1] = to_color(get_string(buffer), c, scr);
    snprintf(buffer, 256, "gc.%s.width", name);
    if(has_entry(buffer))
        values[2] = get_int(buffer);
    snprintf(buffer, 256, "gc.%s.font", name);
    if(has_entry(buffer))
        values[3] = load_font(get_string(buffer), c);

    xcb_create_gc(c, gc, scr->xcbscr->root, mask, values);
    ctx = malloc(sizeof(struct _gcontext));
    ctx->gc = gc;
    ctx->name = malloc(strlen(name) + 1);
    strcpy(ctx->name, name);
    ctx->next = _first;
    _first = ctx;
}

int load_gcontexts(xcb_connection_t* c, srv_screen_t* scr)
{
    const char* cfg;
    char* entries;
    char* entry;
    if(!has_entry("gc.list"))
        return 0;
    if(!load_defaults(c, scr))
        return 0;

    cfg = get_string("gc.list");
    entries = malloc(strlen(cfg) + 1);
    strcpy(entries, cfg);

    entry = strtok(entries, ",");
    while(entry) {
        add_gc(entry, c, scr);
        entry = strtok(NULL, ",");
    }

    free(entries);
    return 1;
}

void free_gcontexts()
{
    struct _gcontext* act = _first;
    struct _gcontext* tofree;

    while(act) {
        free(act->name);
        tofree = act;
        act = act->next;
        free(tofree);
    }
}

int has_gcontext(const char* name)
{
    return get_gcontext(name, NULL);
}

int get_gcontext(const char* name, xcb_gcontext_t* gc)
{
    struct _gcontext* act = _first;
    while(act) {
        if(strcmp(act->name, name) == 0) {
            if(gc)
                *gc = act->gc;
            return 1;
        }
    }

    return 0;
}

