#pragma once
#include "misc.h"
#include "platform.h"
#include "widgets.h"

typedef struct painter painter;

struct painter {
    platform_window* window;
    urect16 clip_rect;
    u32* bits;
};

typedef void (*on_tick_callback)(void);

ui_window* orui_init(u16 width, u16 height, char* title);
ui_window* orui_create_window(u16 width, u16 height, char* title);
void orui_set_reactive(bool reactive, on_tick_callback callback);
i32 orui_message_loop(void);
bool orui_is_animating(void);
//=== PLATFORM LAYER INTEROP ====
void ui_input(void);
void orui_animate(void);
void orui_paint(platform_window* window);