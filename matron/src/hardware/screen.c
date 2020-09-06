/*
 * screen.c
 *
 * oled
 */

#include <assert.h>
#include <cairo.h>
#include <cairo-ft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "hardware/fb/matron_fb.h"

// skip this if you don't want every screen module call to perform null checks
#ifndef CHECK_CR
#define CHECK_CR      \
    if (cr == NULL) { \
        return;       \
    }
#define CHECK_CRR     \
    if (cr == NULL) { \
        return 0;     \
    }
#endif

#define NUM_FONTS 67
static char font_path[NUM_FONTS][32];

static float c[16] = {0,   0.066666666666667, 0.13333333333333, 0.2, 0.26666666666667, 0.33333333333333,
                      0.4, 0.46666666666667,  0.53333333333333, 0.6, 0.66666666666667, 0.73333333333333,
                      0.8, 0.86666666666667,  0.93333333333333, 1};

static cairo_surface_t *surface;
static cairo_surface_t *image;
static cairo_t *cr;

static matron_fb_t *framebufs;
static size_t framebuf_ct;

static cairo_font_face_t *ct[NUM_FONTS];
static FT_Library value;
static FT_Error status;
static FT_Face face[NUM_FONTS];
static double text_xy[2];

void screen_display_png(const char *filename, double x, double y) {
    int img_w, img_h;
    // fprintf(stderr, "loading: %s\n", filename);

    image = cairo_image_surface_create_from_png(filename);
    if (cairo_surface_status(image)) {
        fprintf(stderr, "display_png: %s\n", cairo_status_to_string(cairo_surface_status(image)));
        return;
    }

    img_w = cairo_image_surface_get_width(image);
    img_h = cairo_image_surface_get_height(image);

    cairo_set_source_surface(cr, image, x, y);
    // cairo_paint (cr);
    cairo_rectangle(cr, x, y, img_w, img_h);
    cairo_fill(cr);
    cairo_surface_destroy(image);
}

int fb_init(matron_fb_t *fb, fb_ops_t *ops) {
    fb->data = malloc(sizeof(ops->data_size));
    if (!fb->data) {
        fprintf(stderr, "ERROR (screen - %s fb) cannot allocate memory\n", ops->name);
	return -1;
    }
    fb->ops = ops;
    fb->surface = fb->ops->init(fb);
        if (!fb->surface) {
        fprintf(stderr, "ERROR (screen - %s fb) cannot create surface\n", ops->name);
        free(fb->data);
        return -1;
    }
    fb->cairo = cairo_create(fb->surface);
    if (!fb->cairo) {
        fprintf(stderr, "ERROR (screen - %s fb) cannot create cairo context\n", ops->name);
        cairo_surface_destroy(fb->surface);
	free(fb->data);
	return -1;
    }
    return 0;
}

fb_ops_t* framebuf_types[] = {
    &linux_fb_ops,
    /* &json_fb_ops, */
    &sdl_fb_ops,
};

const char* args[] = {
    /* "web", */
    "sdl",
};

void screen_init(void) {
    framebuf_ct = sizeof(args) / sizeof(char*);
    framebufs = malloc(sizeof(matron_fb_t) * framebuf_ct);
    if (framebufs == NULL) {
        return;
    }
    for (size_t i = 0; i < framebuf_ct; i++) {
        fprintf(stderr, "asked for framebuffer %s\n", args[i]);
        for (size_t j = 0; j < sizeof(framebuf_types) / sizeof(fb_ops_t*); j++) {
            if (strcmp(framebuf_types[j]->name, args[i]) == 0) {
                if (fb_init(&framebufs[i], framebuf_types[j])) {
                    fprintf(stderr, "ERROR (screen) could not initialize all framebufs\n");
                    free(framebufs);
                    return;
                }
            }
        }
    }

    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 128, 64);
    cr = cairo_create(surface);

    status = FT_Init_FreeType(&value);
    if (status != 0) {
        fprintf(stderr, "ERROR (screen) freetype init\n");
        return;
    }

    strcpy(font_path[0], "04B_03__.TTF");
    strcpy(font_path[1], "liquid.ttf");
    strcpy(font_path[2], "Roboto-Thin.ttf");
    strcpy(font_path[3], "Roboto-Light.ttf");
    strcpy(font_path[4], "Roboto-Regular.ttf");
    strcpy(font_path[5], "Roboto-Medium.ttf");
    strcpy(font_path[6], "Roboto-Bold.ttf");
    strcpy(font_path[7], "Roboto-Black.ttf");
    strcpy(font_path[8], "Roboto-ThinItalic.ttf");
    strcpy(font_path[9], "Roboto-LightItalic.ttf");
    strcpy(font_path[10], "Roboto-Italic.ttf");
    strcpy(font_path[11], "Roboto-MediumItalic.ttf");
    strcpy(font_path[12], "Roboto-BoldItalic.ttf");
    strcpy(font_path[13], "Roboto-BlackItalic.ttf");
    strcpy(font_path[14], "VeraBd.ttf");
    strcpy(font_path[15], "VeraBI.ttf");
    strcpy(font_path[16], "VeraIt.ttf");
    strcpy(font_path[17], "VeraMoBd.ttf");
    strcpy(font_path[18], "VeraMoBI.ttf");
    strcpy(font_path[19], "VeraMoIt.ttf");
    strcpy(font_path[20], "VeraMono.ttf");
    strcpy(font_path[21], "VeraSeBd.ttf");
    strcpy(font_path[22], "VeraSe.ttf");
    strcpy(font_path[23], "Vera.ttf");
    //------------------
    // bitmap fonts
    strcpy(font_path[24], "bmp/tom-thumb.bdf");
    // FIXME: this is totally silly...
    int i = 25;
    strcpy(font_path[i++], "bmp/creep.bdf");
    strcpy(font_path[i++], "bmp/ctrld-fixed-10b.bdf");
    strcpy(font_path[i++], "bmp/ctrld-fixed-10r.bdf");
    strcpy(font_path[i++], "bmp/ctrld-fixed-13b.bdf");
    strcpy(font_path[i++], "bmp/ctrld-fixed-13b-i.bdf");
    strcpy(font_path[i++], "bmp/ctrld-fixed-13r.bdf");
    strcpy(font_path[i++], "bmp/ctrld-fixed-13r-i.bdf");
    strcpy(font_path[i++], "bmp/ctrld-fixed-16b.bdf");
    strcpy(font_path[i++], "bmp/ctrld-fixed-16b-i.bdf");
    strcpy(font_path[i++], "bmp/ctrld-fixed-16r.bdf");
    strcpy(font_path[i++], "bmp/ctrld-fixed-16r-i.bdf");
    strcpy(font_path[i++], "bmp/scientifica-11.bdf");
    strcpy(font_path[i++], "bmp/scientificaBold-11.bdf");
    strcpy(font_path[i++], "bmp/scientificaItalic-11.bdf");
    strcpy(font_path[i++], "bmp/ter-u12b.bdf");
    strcpy(font_path[i++], "bmp/ter-u12n.bdf");
    strcpy(font_path[i++], "bmp/ter-u14b.bdf");
    strcpy(font_path[i++], "bmp/ter-u14n.bdf");
    strcpy(font_path[i++], "bmp/ter-u14v.bdf");
    strcpy(font_path[i++], "bmp/ter-u16b.bdf");
    strcpy(font_path[i++], "bmp/ter-u16n.bdf");
    strcpy(font_path[i++], "bmp/ter-u16v.bdf");
    strcpy(font_path[i++], "bmp/ter-u18b.bdf");
    strcpy(font_path[i++], "bmp/ter-u18n.bdf");
    strcpy(font_path[i++], "bmp/ter-u20b.bdf");
    strcpy(font_path[i++], "bmp/ter-u20n.bdf");
    strcpy(font_path[i++], "bmp/ter-u22b.bdf");
    strcpy(font_path[i++], "bmp/ter-u22n.bdf");
    strcpy(font_path[i++], "bmp/ter-u24b.bdf");
    strcpy(font_path[i++], "bmp/ter-u24n.bdf");
    strcpy(font_path[i++], "bmp/ter-u28b.bdf");
    strcpy(font_path[i++], "bmp/ter-u28n.bdf");
    strcpy(font_path[i++], "bmp/ter-u32b.bdf");
    strcpy(font_path[i++], "bmp/ter-u32n.bdf");
    strcpy(font_path[i++], "bmp/unscii-16-full.pcf");
    strcpy(font_path[i++], "bmp/unscii-16.pcf");
    strcpy(font_path[i++], "bmp/unscii-8-alt.pcf");
    strcpy(font_path[i++], "bmp/unscii-8-fantasy.pcf");
    strcpy(font_path[i++], "bmp/unscii-8-mcr.pcf");
    strcpy(font_path[i++], "bmp/unscii-8.pcf");
    strcpy(font_path[i++], "bmp/unscii-8-tall.pcf");
    strcpy(font_path[i++], "bmp/unscii-8-thin.pcf");

    assert(i == NUM_FONTS);

    fprintf(stderr, "fonts: \n");
    for (int i = 0; i < NUM_FONTS; ++i) {
        fprintf(stderr, "  %d: %s\n", i, font_path[i]);
    }

    char filename[256];

    for (int i = 0; i < NUM_FONTS; i++) {
        // FIXME should be path relative to norns/
        status = snprintf(filename, 256, "%s/norns/resources/%s", getenv("HOME"), font_path[i]);
        if (status > 256) {
            fprintf(stderr, "ERROR (screen) path too long: %s\n", filename);
            return;
        }

        status = FT_New_Face(value, filename, 0, &face[i]);
        if (status != 0) {
            fprintf(stderr, "ERROR (screen) font load: %s\n", filename);
            return;
        } else {
            ct[i] = cairo_ft_font_face_create_for_ft_face(face[i], 0);
        }
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    cairo_font_options_t *font_options = cairo_font_options_create();
    cairo_font_options_set_antialias(font_options, CAIRO_ANTIALIAS_SUBPIXEL);
    cairo_set_font_options(cr, font_options);
    cairo_font_options_destroy(font_options);

    // default font
    cairo_set_font_face(cr, ct[0]);
    cairo_set_font_size(cr, 8.0);

    // config buffer
    for (size_t i = 0; i < framebuf_ct; i++) {
        framebufs[i].ops->bind(&framebufs[i], surface);
        fprintf(stderr, "bound fb %s\n", framebufs[i].ops->name);
    }
}

void screen_deinit(void) {
    CHECK_CR
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    for (size_t i = 0; i < framebuf_ct; i++) {
        framebufs[i].ops->destroy(&framebufs[i]);
    }
    free(framebufs);
}

void screen_update(void) {
    CHECK_CR
    for (size_t i = 0; i < framebuf_ct; i++) {
        framebufs[i].ops->paint(&framebufs[i]);
    }
}

void screen_save(void) {
    CHECK_CR
    cairo_save(cr);
}

void screen_restore(void) {
    CHECK_CR
    cairo_restore(cr);
}

void screen_font_face(int i) {
    CHECK_CR
    if ((i >= 0) && (i < NUM_FONTS)) {
        cairo_set_font_face(cr, ct[i]);
    }
}

void screen_font_size(double z) {
    CHECK_CR
    cairo_set_font_size(cr, z);
}

void screen_aa(int s) {
    CHECK_CR

    cairo_font_options_t *font_options = cairo_font_options_create();
    if (s == 0) {
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        cairo_font_options_set_antialias(font_options, CAIRO_ANTIALIAS_NONE);
    } else {
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
        cairo_font_options_set_antialias(font_options, CAIRO_ANTIALIAS_SUBPIXEL);
    }
    cairo_set_font_options(cr, font_options);
    cairo_font_options_destroy(font_options);
}

void screen_level(int z) {
    CHECK_CR
    cairo_set_source_rgb(cr, c[z], c[z], c[z]);
}

void screen_line_width(double w) {
    CHECK_CR
    cairo_set_line_width(cr, w);
}

void screen_line_cap(const char *style) {
    CHECK_CR
    if (strcmp(style, "round") == 0) {
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    } else if (strcmp(style, "square") == 0) {
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
    } else {
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    }
}

void screen_line_join(const char *style) {
    CHECK_CR
    if (strcmp(style, "round") == 0) {
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    } else if (strcmp(style, "bevel") == 0) {
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_BEVEL);
    } else {
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
    }
}

void screen_miter_limit(double limit) {
    CHECK_CR
    cairo_set_miter_limit(cr, limit);
}

void screen_move(double x, double y) {
    CHECK_CR
    cairo_move_to(cr, x, y);
}

void screen_line(double x, double y) {
    CHECK_CR
    cairo_line_to(cr, x, y);
}

void screen_line_rel(double x, double y) {
    CHECK_CR
    cairo_rel_line_to(cr, x, y);
}

void screen_move_rel(double x, double y) {
    CHECK_CR
    cairo_rel_move_to(cr, x, y);
}

void screen_curve(double x1, double y1, double x2, double y2, double x3, double y3) {
    CHECK_CR
    cairo_curve_to(cr, x1, y1, x2, y2, x3, y3);
}

void screen_curve_rel(double dx1, double dy1, double dx2, double dy2, double dx3, double dy3) {
    CHECK_CR
    cairo_rel_curve_to(cr, dx1, dy1, dx2, dy2, dx3, dy3);
}

void screen_arc(double x, double y, double r, double a1, double a2) {
    CHECK_CR
    cairo_arc(cr, x, y, r, a1, a2);
}

void screen_rect(double x, double y, double w, double h) {
    CHECK_CR
    cairo_rectangle(cr, x, y, w, h);
}

void screen_close_path(void) {
    CHECK_CR
    cairo_close_path(cr);
}

void screen_stroke(void) {
    CHECK_CR
    cairo_stroke(cr);
}

void screen_fill(void) {
    CHECK_CR
    cairo_fill(cr);
}

void screen_text(const char *s) {
    CHECK_CR
    cairo_show_text(cr, s);
}

void screen_clear(void) {
    CHECK_CR
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
}

double *screen_text_extents(const char *s) {
    CHECK_CRR
    cairo_text_extents_t extents;
    cairo_text_extents(cr, s, &extents);
    text_xy[0] = extents.width;
    text_xy[1] = extents.height;
    return text_xy;
}

extern void screen_export_png(const char *s) {
    CHECK_CR
    cairo_surface_write_to_png(surface, s);
}

#undef CHECK_CR
#undef CHECK_CRR
