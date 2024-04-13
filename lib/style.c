#include <stdlib.h>
#include "allocators.h"
#define STYLE_STRINGS_IMPLEMENTATION
#include "style.h"
#include "map.h"
#include "console.h"
#include "widgets.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// map_t of ui_state_property_def(s)
// TODOOOO: somehow check that a property is valid for a given widget
map_t prop_map = (map_t){0};
// map_t of ui_style_state(s)
map_t classes = (map_t){0};
// map_t of u64
map_t enum_values = (map_t){0};

void style_add_prop(char* id, u8 len, u8 offset, ui_state_property_kind kind)
{
    ui_state_property_def def;
    def.kind = kind; def.offset = offset;
    // oh no
    u64 conv_hack = reinterpret(def, u64);
    map_set(&prop_map, id, len, (void*)conv_hack);
}

void style_add_props(str id, u8 offset, ui_state_property_kind kind)
{
    return style_add_prop(id.data, id.len, offset, kind);
}

void style_add_enum_value(char *id, u8 len, u64 value)
{
    if (map_get(&enum_values, id, len) != null) {
        log_fatal("Enum value for %s already exists!", id);
        PostQuitMessage(-1);
    }
    map_set(&enum_values, id, len, (void*)value);
}

void style_add_enum_values(str id, u64 value)
{
    return style_add_enum_value(id.data, id.len, value);
}

u64 style_get_enum_value(char *id, u8 len)
{
    void* a = map_get(&enum_values, id, len);
    if (a == null) {
        log_fatal("Enum value %s does not exist!", id);
        PostQuitMessage(-1);
    }
    return (u64)a;
}

u64 style_get_enum_values(str id)
{
    return style_get_enum_value(id.data, id.len);
}

ui_state_property_def style_get_prop(char* id, u8 len)
{
    void* result = map_get(&prop_map, id, len);
    u64 conv_hack = (u64)result;
    return *(ui_state_property_def*)&conv_hack;
}

ui_state_property_def style_get_props(str id)
{
    return style_get_prop(id.data, id.len);
}

// default state is always state 0
ui_style_state* new_style(char* name, u8 len, u8 state_count)
{
    ui_style_state* result = malloc(sizeof(ui_style_state));
    result->state_count = state_count;
    result->cur_state = 0;
    result->transitions = malloc(sizeof(ui_state_transition*) * state_count);
    if (map_get(&classes, name, len) != null) {
        log_fatal("style class %s already exists!", name);
        PostQuitMessage(-1);
    }
    map_set(&classes, name, len, result);
    return result;
}

ui_style_state* new_styles(str name, u8 state_count)
{
    return new_style(name.data, name.len, state_count);
}

//TODO: test this
ui_style_state* style_inherit(char* name, u8 name_len, char* from, u8 len)
{
    ui_style_state* from_style = map_get(&classes, from, len);
    if (from_style == null) {
        log_error("Style class %s not found!", from);
    } 

    ui_style_state* result = new_style(name, name_len, from_style->state_count);
    // copy transition data over
    for (int i = 0; i < result->state_count; i++) {
        ui_state_transition* from_trans = from_style->transitions[i];

        u32 size = sizeof(ui_state_transition) + sizeof(ui_state_transition_delta) * (from_trans->delta_count-1);
        ui_state_transition* transition = malloc(size);
        memcpy_s(transition, size, from_trans, size);

        result->transitions[i] = transition;
    }

    return result;
}

ui_style_state* style_inherits(str name, str from)
{
    return style_inherit(name.data, name.len, from.data, from.len);
}

//=== STYLE CONFIG
// WARNING: SPECIFY STYLE STATES IN THE ORDER OF APPEARANCE IN THE ENUM
u8 change_counter;
ui_state_transition* style_config_state(ui_style_state* style_state, u8 state_id, u8 change_count)
{
    change_counter = 0;    
    if (style_state->transitions[state_id] != null) {
        ui_state_transition* trans = style_state->transitions[state_id];
        if (change_count > trans->delta_count) {
            trans->delta_count = change_count;
            trans = realloc(trans, 
                  sizeof(ui_state_transition) 
                + sizeof(ui_state_transition_delta) * (trans->delta_count-1));
            style_state->transitions[state_id] = trans;
        }
        return trans;
    }

    ui_state_transition* transition = malloc(
          sizeof(ui_state_transition) 
        + sizeof(ui_state_transition_delta) * (change_count-1)
    );
    transition->delta_count = change_count;
    transition->duration = 0; transition->ease = EASE_LINEAR;
    style_state->transitions[state_id] = transition;
    return transition;
}

void style_set_transition(ui_style_state* style_state, u8 state_id, u32 duration, ui_easing easing)
{
    ui_state_transition* trans = style_state->transitions[state_id];
    trans->duration = duration; trans->ease = easing;
}

void style_add_change(ui_state_transition* trans, char* id, u8 len, ui_state_property target)
{
    ui_state_property_def def = style_get_prop(id, len);
    ui_state_transition_delta* delta;
    // check if we need to override an offset
    bool replace = false;
    for (int i = 0; i < trans->delta_count; i++) {
        ui_state_transition_delta* d = &trans->deltas[i];
        if (d->offset == def.offset) {
            delta = d;
            replace = true; break;
        }
    }

    if (!replace) {
        change_counter++;
        if (change_counter > trans->delta_count) {
            log_fatal("More style property changes than specified, TODO: realloc");
            PostQuitMessage(-1);
        }
        delta = &trans->deltas[change_counter-1];
    } 
    
    if (target.kind != def.kind) {
        log_fatal("expected type %s for property %s, but got %s", ui_state_property_kind_strings[def.kind], id, ui_state_property_kind_strings[target.kind]);
        PostQuitMessage(-1);
    }
    delta->offset = def.offset;
    delta->target = target;
}

void style_add_changes(ui_state_transition* trans, str id, ui_state_property target)
{
    return style_add_change(trans, id.data, id.len, target);
}

//==== STATE CHANGING ====
// TODO: check if offsets are applicable
// TODO: support animation
static void apply_transition(ui_widget* w, ui_state_transition* trans, bool dont_animate)
{
    for (int i = 0; i < trans->delta_count; i++) {
        ui_state_transition_delta* d = &trans->deltas[i];
        void* unknown_prop = (char*)w + d->offset;
        
        // first compare, then decide if you want to change
        // TODO: convert to macro
        switch (d->target.kind) {
            case PROPERTY_U16: {
                u16* prop = (u16*)unknown_prop;
                if (*prop != d->target.as.u16_) {
                    if (dont_animate) {
                        *prop = d->target.as.u16_;
                    } else {
                        // TODO: 
                    }
                }
            } break;
            case PROPERTY_COLOR: {
                color* prop = (color*)unknown_prop;
                if (*prop != d->target.as.color_) {
                    if (dont_animate) {
                        *prop = d->target.as.color_;
                    } else {
                        // TODO: 
                    }
                }
            } break;
            case PROPERTY_URECT16: {
                urect16* prop = (urect16*)unknown_prop;
                if (!rect_equals(*prop, d->target.as.urect16_)) {
                    if (dont_animate) {
                        *prop = d->target.as.urect16_;
                    } else {
                        // TODO: 
                    }
                }
            } break;
            // TODO: figure out enums
            case PROPERTY_ENUM: {
                u64* prop = (u64*)unknown_prop;
                if (*prop != d->target.as.enum_) {
                    log_fatal("not implenented!"); 
                }
            } break;
            case PROPERTY_BOOL: {
                bool* prop = (bool*)unknown_prop;
                if (*prop != d->target.as.bool_) {
                    if (dont_animate) {
                        *prop = d->target.as.bool_;
                    } else {
                        // TODO: 
                    }
                }
            } break;
        }
    }

}

void orui_set_state(ui_widget *w, u8 state_id, bool dont_animate)
{
    if (state_id == w->style_state->cur_state) return;

    // collect all changes we need to make
    //  first we need to get from the current state to the active state
    ui_state_transition* to_active = w->style_state->transitions[0];
    apply_transition(w, to_active, dont_animate);

    // then we need to get from the active state to the new state
    // duplicate values are handled (overriden) by the animation system, so we don't need to do any bookkeeping here
    ui_state_transition* to_new = w->style_state->transitions[state_id];
    apply_transition(w, to_new, dont_animate);
}