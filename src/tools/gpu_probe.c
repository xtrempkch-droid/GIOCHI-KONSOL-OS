/* giochi-gpu-probe — diz POR QUE a UI não consegue abrir o vídeo.
 *
 * POR QUE ESTE PROGRAMA EXISTE
 * ---------------------------
 * No aparelho o Launcher só registrava
 *
 *     W: render: SDL2 indisponivel (SDL_CreateWindow: EGL not initialized)
 *
 * e "EGL not initialized" é a mensagem do SDL quando o carregamento do EGL
 * falhou — ela não diz se o problema foi abrir /dev/dri, o GBM, a plataforma
 * EGL, a escolha de config ou a criação do contexto. Cada um desses passos
 * tem uma correção diferente, e adivinhar custou vários ciclos de flash.
 *
 * Este programa faz os cinco passos em ordem, imprimindo o resultado de cada
 * um. É diagnóstico: não entra no caminho de boot normal, roda sob demanda
 * (o S20graphics chama uma vez, com a saída no log do cartão).
 *
 * USO
 *     giochi-gpu-probe              # resumo legível, para o log
 *     giochi-gpu-probe -v           # inclui modos e extensões EGL
 *
 * Código de saída: 0 = todos os passos passaram; 1 = parou no primeiro que
 * falhou (a linha "FALHOU" diz qual).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dirent.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

static int verbose;

static void linha(const char *estado, const char *fmt, ...)
{
    va_list ap;

    printf("%-7s ", estado);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

/* EGL devolve 0x3000+n; traduzir os comuns evita uma tabela na cabeça. */
static const char *egl_nome(EGLint e)
{
    switch (e) {
    case EGL_SUCCESS: return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
    case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
    case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
    case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
    case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
    case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
    case EGL_BAD_SURFACE: return "EGL_BAD_SURFACE";
    case EGL_CONTEXT_LOST: return "EGL_CONTEXT_LOST";
    default: return "EGL_erro_desconhecido";
    }
}

/* O conector que o DRM expõe: sem ele não há para onde desenhar. */
static int checar_drm(int fd)
{
    drmModeRes *res;
    int ativos = 0;
    int i;

    res = drmModeGetResources(fd);
    if (res == NULL) {
        linha("FALHOU", "drmModeGetResources: %s", strerror(errno));
        return -1;
    }

    linha("OK", "DRM: %d conector(es), %d CRTC(s), %d encoder(s)",
          res->count_connectors, res->count_crtcs, res->count_encoders);

    for (i = 0; i < res->count_connectors; i++) {
        drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
        int conectado;

        if (c == NULL) {
            continue;
        }
        conectado = (c->connection == DRM_MODE_CONNECTED);
        if (conectado && c->count_modes > 0) {
            ativos++;
            linha("OK", "conector %u: conectado, %dx%d@%d", c->connector_id,
                  c->modes[0].hdisplay, c->modes[0].vdisplay,
                  c->modes[0].vrefresh);
            if (verbose) {
                int m;

                for (m = 1; m < c->count_modes && m < 6; m++) {
                    printf("        modo %d: %dx%d@%d\n", m,
                           c->modes[m].hdisplay, c->modes[m].vdisplay,
                           c->modes[m].vrefresh);
                }
            }
        } else {
            linha("--", "conector %u: %s", c->connector_id,
                  conectado ? "conectado sem modo" : "desconectado");
        }
        drmModeFreeConnector(c);
    }
    drmModeFreeResources(res);

    if (ativos == 0) {
        linha("FALHOU", "nenhum conector conectado com modo: nada a desenhar");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *dev = "/dev/dri/card0";
    struct gbm_device *gbm;
    EGLDisplay dpy;
    EGLint major = 0, minor = 0, e;
    EGLConfig cfg = NULL;
    EGLint ncfg = 0;
    EGLContext ctx;
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                       EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                       EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                       EGL_ALPHA_SIZE, 8, EGL_NONE };
    EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    int fd;
    int rc = 0;

    if (argc > 1 && strcmp(argv[1], "-v") == 0) {
        verbose = 1;
    }
    if (getenv("GIOCHI_GPU_DEV") != NULL) {
        dev = getenv("GIOCHI_GPU_DEV");
    }

    printf("== giochi-gpu-probe ==\n");

    /* O override do Mesa é o primeiro suspeito quando troca de driver. */
    linha("info", "MESA_LOADER_DRIVER_OVERRIDE=%s",
          getenv("MESA_LOADER_DRIVER_OVERRIDE") != NULL
              ? getenv("MESA_LOADER_DRIVER_OVERRIDE") : "(nao definido)");
    linha("info", "SDL_VIDEODRIVER=%s",
          getenv("SDL_VIDEODRIVER") != NULL ? getenv("SDL_VIDEODRIVER")
                                            : "(nao definido)");

    /* 0. inventario: quais nos DRM e quais drivers DRI existem.
     * Sem /dev/dri/renderD* o panfrost nao tem por onde ser usado, e sem
     * kms_swrast_dri.so nao existe caminho de software — os dois casos dao o
     * mesmo "EGL not initialized", e as correcoes sao opostas. */
    {
        const char *dir = "/dev/dri";
        DIR *d = opendir(dir);
        struct dirent *e;
        int tem_card = 0;
        int tem_render = 0;

        if (d == NULL) {
            linha("FALHOU", "%s nao existe: %s", dir, strerror(errno));
            return 1;
        }
        while ((e = readdir(d)) != NULL) {
            if (strncmp(e->d_name, "card", 4) == 0) {
                tem_card++;
            } else if (strncmp(e->d_name, "renderD", 7) == 0) {
                tem_render++;
            }
        }
        closedir(d);
        linha(tem_card > 0 ? "OK" : "FALHOU", "%s: %d card(s), %d render node(s)",
              dir, tem_card, tem_render);
        if (tem_render == 0) {
            linha("info", "sem render node: o kmsro nao pode delegar render "
                          "ao GPU (panfrost)");
        }
    }
    {
        const char *dir = "/usr/lib/dri";
        static const char *precisa[] = {"rockchip_dri.so", "panfrost_dri.so",
                                        "kms_swrast_dri.so", "swrast_dri.so"};
        char caminho[256];

        for (size_t i = 0; i < sizeof(precisa) / sizeof(precisa[0]); i++) {
            (void)snprintf(caminho, sizeof(caminho), "%s/%s", dir, precisa[i]);
            linha(access(caminho, R_OK) == 0 ? "OK" : "aviso", "%s %s",
                  precisa[i], access(caminho, R_OK) == 0 ? "presente"
                                                        : "AUSENTE");
        }
    }

    /* 1. o nó DRM existe e abre. */
    fd = open(dev, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        linha("FALHOU", "open(%s): %s", dev, strerror(errno));
        return 1;
    }
    {
        drmVersion *v = drmGetVersion(fd);

        linha("OK", "%s aberto (driver '%s')", dev,
              v != NULL ? v->name : "?");
        if (v != NULL) {
            drmFreeVersion(v);
        }
    }

    /* 2. conectores. */
    if (checar_drm(fd) != 0) {
        rc = 1;
        goto out_fd;
    }

    /* 3. GBM: é o display nativo que o SDL/KMSDRM entrega ao EGL. */
    gbm = gbm_create_device(fd);
    if (gbm == NULL) {
        linha("FALHOU", "gbm_create_device: %s", strerror(errno));
        rc = 1;
        goto out_fd;
    }
    linha("OK", "gbm_create_device (backend '%s')",
          gbm_device_get_backend_name(gbm));

    /* 4. plataforma EGL sobre o GBM. */
    dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, (void *)gbm, NULL);
    if (dpy == EGL_NO_DISPLAY) {
        linha("FALHOU", "eglGetPlatformDisplay(GBM): %s",
              egl_nome(eglGetError()));
        rc = 1;
        goto out_gbm;
    }
    if (eglInitialize(dpy, &major, &minor) != EGL_TRUE) {
        linha("FALHOU", "eglInitialize: %s", egl_nome(eglGetError()));
        rc = 1;
        goto out_dpy;
    }
    linha("OK", "eglInitialize (EGL %d.%d, vendor '%s')", major, minor,
          eglQueryString(dpy, EGL_VENDOR) != NULL
              ? eglQueryString(dpy, EGL_VENDOR) : "?");
    if (verbose) {
        const char *ext = eglQueryString(dpy, EGL_EXTENSIONS);

        linha("info", "EGL_EXTENSIONS: %s", ext != NULL ? ext : "(nulo)");
    }

    /* 5. config + contexto GLES2: é exatamente o que o renderizador SDL pede. */
    if (eglChooseConfig(dpy, attrs, &cfg, 1, &ncfg) != EGL_TRUE || ncfg < 1) {
        e = eglGetError();
        linha("FALHOU", "eglChooseConfig (XRGB8888 + WINDOW): %s (n=%d)",
              egl_nome(e), ncfg);
        rc = 1;
        goto out_term;
    }
    linha("OK", "eglChooseConfig: %d config(s)", ncfg);

    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (ctx == EGL_NO_CONTEXT) {
        linha("FALHOU", "eglCreateContext(GLES2): %s",
              egl_nome(eglGetError()));
        rc = 1;
        goto out_term;
    }
    linha("OK", "eglCreateContext(GLES2) — o caminho do SDL/KMSDRM funciona");
    eglDestroyContext(dpy, ctx);

out_term:
    eglTerminate(dpy);
out_dpy:
    eglReleaseThread();
out_gbm:
    gbm_device_destroy(gbm);
out_fd:
    close(fd);
    printf("== fim (%s) ==\n", rc == 0 ? "tudo OK" : "ver a linha FALHOU");
    return rc;
}
