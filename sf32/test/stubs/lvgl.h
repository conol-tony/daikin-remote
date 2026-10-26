#ifndef TEST_LVGL_H
#define TEST_LVGL_H

/* Declaration-only screen API. The linker drops unused rendering/thread
 * functions; test_ui.c implements only the status/help calls used by events. */
#include <stdint.h>

typedef struct { int unused; } lv_obj_t;
typedef struct { int unused; } lv_font_t;
typedef struct { int unused; } lv_style_t;
typedef uint32_t lv_color_t;

#define LV_OPA_TRANSP 0
#define LV_OPA_COVER 255
#define LV_SCROLLBAR_MODE_OFF 0
#define LV_OBJ_FLAG_SCROLLABLE 1
#define LV_OBJ_FLAG_HIDDEN 2
#define LV_TEXT_ALIGN_CENTER 1
#define LV_TEXT_ALIGN_RIGHT 2
#define LV_SYMBOL_UP "\xEF\x81\xB7"
#define LV_SYMBOL_DOWN "\xEF\x81\xB8"
#define LV_SYMBOL_HOME "\xEF\x80\x95"
#define LV_SYMBOL_ENVELOPE "\xEF\x83\xA0"
#define LV_SYMBOL_NEW_LINE "\xEF\xA2\xA2"
#define LV_SYMBOL_BACKSPACE "\xEF\x95\x9A"
#define LV_SYMBOL_STOP "\xEF\x81\x8D"

extern const lv_font_t lv_font_montserrat_16;
extern const lv_font_t lv_font_montserrat_20;
extern const lv_font_t lv_font_montserrat_24;
extern const lv_font_t lv_font_montserrat_30;
extern const lv_font_t lv_font_montserrat_36;
extern const lv_font_t lv_font_montserrat_48;

static inline lv_color_t lv_color_hex(uint32_t color) { return color; }
lv_obj_t *lv_screen_active(void);
lv_obj_t *lv_label_create(lv_obj_t *);
lv_obj_t *lv_obj_create(lv_obj_t *);
void lv_label_set_text(lv_obj_t *, const char *);
void lv_obj_set_pos(lv_obj_t *, int, int);
void lv_obj_set_size(lv_obj_t *, int, int);
void lv_obj_set_width(lv_obj_t *, int);
void lv_obj_set_scrollbar_mode(lv_obj_t *, int);
void lv_obj_add_flag(lv_obj_t *, int);
void lv_obj_clear_flag(lv_obj_t *, int);
void lv_obj_remove_flag(lv_obj_t *, int);
void lv_obj_move_foreground(lv_obj_t *);
void lv_obj_add_style(lv_obj_t *, const lv_style_t *, int);
void lv_obj_set_style_text_font(lv_obj_t *, const lv_font_t *, int);
void lv_obj_set_style_text_color(lv_obj_t *, lv_color_t, int);
void lv_obj_set_style_text_align(lv_obj_t *, int, int);
void lv_obj_set_style_pad_left(lv_obj_t *, int, int);
void lv_obj_set_style_pad_right(lv_obj_t *, int, int);
void lv_obj_set_style_pad_top(lv_obj_t *, int, int);
void lv_obj_set_style_pad_bottom(lv_obj_t *, int, int);
void lv_obj_set_style_pad_all(lv_obj_t *, int, int);
void lv_obj_set_style_border_width(lv_obj_t *, int, int);
void lv_obj_set_style_border_color(lv_obj_t *, lv_color_t, int);
void lv_obj_set_style_radius(lv_obj_t *, int, int);
void lv_obj_set_style_bg_opa(lv_obj_t *, int, int);
void lv_obj_set_style_bg_color(lv_obj_t *, lv_color_t, int);
void lv_style_init(lv_style_t *);
void lv_style_set_bg_color(lv_style_t *, lv_color_t);
void lv_style_set_bg_opa(lv_style_t *, int);
void lv_style_set_pad_all(lv_style_t *, int);
void lv_style_set_border_width(lv_style_t *, int);
uint32_t lv_task_handler(void);

#endif
