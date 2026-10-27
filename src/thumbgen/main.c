/* Giochi Konsol OS v1.2 — giochi-thumbgen (secao 4.2/S9).
 *
 * Gera /user/.cache/thumbs/<sha256>.rgba (152x85 RGBA = 51 680 bytes).
 * Entrada: pacote .giochi (tar puro). Se thumb.png existir, ele e usado;
 * senao, center-crop 16:9 de capa.png. Ambos exigem decodificacao PNG, que
 * fica atras de GIOCHI_HAVE_LIBPNG. Sem libpng o utilitario grava um
 * placeholder solido (evita quebrar o install) e retorna 2.
 */
#include "giochi_json.h"
#include "giochi_log.h"
#include "giochi_sha256.h"
#include "giochi_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define THUMB_WIDTH 152
#define THUMB_HEIGHT 85
#define THUMB_STRIDE (THUMB_WIDTH * 4)

#ifdef GIOCHI_HAVE_LIBPNG
#include <png.h>
#endif

static void usage(FILE *out)
{
    (void)fprintf(out,
        "uso: giochi-thumbgen --package jogo.giochi --out-dir DIR [--placeholder]\n"
        "  --package PATH     pacote .giochi de origem\n"
        "  --out-dir DIR      destino (<sha256>.rgba)\n"
        "  --placeholder      grava placeholder sem decodificar PNG\n");
}

static unsigned char *solid_placeholder(void)
{
    unsigned char *pixels = malloc((size_t)THUMB_STRIDE * THUMB_HEIGHT);

    if (pixels == NULL) {
        return NULL;
    }
    /* Fundo #1E1E1E opaco (surface do tema). */
    for (int i = 0; i < THUMB_WIDTH * THUMB_HEIGHT; i++) {
        pixels[i * 4 + 0] = 0x1E;
        pixels[i * 4 + 1] = 0x1E;
        pixels[i * 4 + 2] = 0x1E;
        pixels[i * 4 + 3] = 0xFF;
    }
    return pixels;
}

#ifdef GIOCHI_HAVE_LIBPNG
static unsigned char *load_png_rgba(const char *path, int *width, int *height)
{
    FILE *fp = fopen(path, "rb");
    png_structp png;
    png_infop info;
    unsigned char *pixels = NULL;
    png_bytep *rows = NULL;

    if (fp == NULL) {
        return NULL;
    }
    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info = png != NULL ? png_create_info_struct(png) : NULL;
    if (png == NULL || info == NULL) {
        (void)fclose(fp);
        if (png != NULL) {
            png_destroy_read_struct(&png, NULL, NULL);
        }
        return NULL;
    }
    if (setjmp(png_jmpbuf(png)) != 0) {
        png_destroy_read_struct(&png, &info, NULL);
        (void)fclose(fp);
        free(rows);
        return NULL;
    }

    png_init_io(png, fp);
    png_read_info(png, info);
    *width = (int)png_get_image_width(png, info);
    *height = (int)png_get_image_height(png, info);

    png_set_expand(png);
    png_set_strip_16(png);
    png_set_gray_to_rgb(png);
    png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
    (void)png_set_interlace_handling(png);
    png_read_update_info(png, info);

    pixels = malloc((size_t)*width * (size_t)*height * 4U);
    rows = malloc(sizeof(png_bytep) * (size_t)*height);
    if (pixels == NULL || rows == NULL) {
        free(pixels);
        free(rows);
        png_destroy_read_struct(&png, &info, NULL);
        (void)fclose(fp);
        return NULL;
    }
    for (int y = 0; y < *height; y++) {
        rows[y] = pixels + (size_t)y * (size_t)*width * 4U;
    }
    png_read_image(png, rows);
    png_destroy_read_struct(&png, &info, NULL);
    free(rows);
    (void)fclose(fp);
    return pixels;
}

/* Center-crop 16:9 + nearest-neighbor para 152x85. */
static unsigned char *crop_to_thumb(const unsigned char *src, int sw, int sh)
{
    unsigned char *out = malloc((size_t)THUMB_STRIDE * THUMB_HEIGHT);
    int crop_w = sw;
    int crop_h;
    int off_x = 0;
    int off_y;

    if (out == NULL) {
        return NULL;
    }
    if (crop_w * 9 > sh * 16) {
        crop_w = sh * 16 / 9;
        off_x = (sw - crop_w) / 2;
    }
    crop_h = crop_w * 9 / 16;
    off_y = (sh - crop_h) / 2;
    if (crop_w <= 0 || crop_h <= 0) {
        free(out);
        return NULL;
    }

    for (int y = 0; y < THUMB_HEIGHT; y++) {
        int sy = off_y + y * crop_h / THUMB_HEIGHT;

        for (int x = 0; x < THUMB_WIDTH; x++) {
            int sx = off_x + x * crop_w / THUMB_WIDTH;
            const unsigned char *in = src + ((size_t)sy * (size_t)sw +
                                             (size_t)sx) * 4U;
            unsigned char *dst = out + ((size_t)y * THUMB_WIDTH +
                                        (size_t)x) * 4U;

            dst[0] = in[0];
            dst[1] = in[1];
            dst[2] = in[2];
            dst[3] = 0xFF;
        }
    }
    return out;
}
#endif /* GIOCHI_HAVE_LIBPNG */

int main(int argc, char **argv)
{
    const char *package = NULL;
    const char *out_dir = NULL;
    const char *force_placeholder = NULL;
    unsigned char digest[GIOCHI_SHA256_DIGEST_SIZE];
    char hex[GIOCHI_SHA256_HEX_SIZE];
    char out_path[1024];
    unsigned char *pixels;
    int rc = 0;
    FILE *fp;

    giochi_log_set_level(giochi_log_level_from_env());

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 0;
        }
        if (strcmp(argv[i], "--package") == 0 && i + 1 < argc) {
            package = argv[++i];
        } else if (strcmp(argv[i], "--out-dir") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (strcmp(argv[i], "--placeholder") == 0) {
            force_placeholder = "1";
        } else {
            (void)fprintf(stderr, "giochi-thumbgen: opcao desconhecida '%s'\n",
                          argv[i]);
            return 2;
        }
    }
    if (package == NULL || out_dir == NULL) {
        usage(stderr);
        return 2;
    }

    if (giochi_sha256_file(package, digest) != 0) {
        GLOG_ERR("thumbgen: nao foi possivel ler %s", package);
        return 1;
    }
    giochi_sha256_hex(digest, hex);

    pixels = solid_placeholder();
    if (pixels == NULL) {
        GLOG_ERR("thumbgen: sem memoria");
        return 1;
    }

#ifdef GIOCHI_HAVE_LIBPNG
    if (force_placeholder == NULL) {
        /* TODO(v1.2): extrair thumb.png/capa.png do tar em streaming
         * (ADR-11) para /run/giochi/thumbsrc/ antes do decode. */
        GLOG_WARN("thumbgen: extracao tar pendente; usando placeholder");
        rc = 2;
    }
#else
    (void)force_placeholder;
    GLOG_WARN("thumbgen: compilado sem libpng; usando placeholder");
    rc = 2;
#endif

    if (giochi_mkdir_p(out_dir, 0755) != 0) {
        GLOG_ERR("thumbgen: nao foi possivel criar %s", out_dir);
        free(pixels);
        return 1;
    }
    if (snprintf(out_path, sizeof(out_path), "%s/%s.rgba", out_dir, hex)
            >= (int)sizeof(out_path)) {
        free(pixels);
        return 1;
    }

    fp = fopen(out_path, "wb");
    if (fp == NULL) {
        GLOG_ERR("thumbgen: nao foi possivel gravar %s", out_path);
        free(pixels);
        return 1;
    }
    if (fwrite(pixels, 1, (size_t)THUMB_STRIDE * THUMB_HEIGHT, fp) !=
        (size_t)THUMB_STRIDE * THUMB_HEIGHT) {
        GLOG_ERR("thumbgen: escrita incompleta em %s", out_path);
        rc = rc != 0 ? rc : 1;
    }
    (void)fclose(fp);
    free(pixels);

    GLOG_INFO("thumbgen: %s (%dx%d)", out_path, THUMB_WIDTH, THUMB_HEIGHT);
    return rc;
}
