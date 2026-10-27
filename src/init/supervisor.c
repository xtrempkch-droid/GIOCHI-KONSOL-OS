#include "supervisor.h"

#include "giochi_log.h"
#include "giochi_util.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef GIOCHI_LIBEXECDIR
#define GIOCHI_LIBEXECDIR "/usr/bin"
#endif

#define GIOCHI_STOP_GRACE_MS 3000ULL

static void sleep_ms(unsigned long ms)
{
    struct timespec ts;

    ts.tv_sec = (time_t)(ms / 1000UL);
    ts.tv_nsec = (long)((ms % 1000UL) * 1000000UL);
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
        /* continua o restante */
    }
}

static int spawn_child(giochi_child *child)
{
    char dir[PATH_MAX];
    char path[PATH_MAX];
    char *argv[GIOCHI_MAX_ARGS + 2];
    pid_t pid;
    int i;

    if (strchr(child->program, '/') != NULL) {
        giochi_strlcpy(path, child->program, sizeof(path));
    } else {
        if (giochi_path(dir, sizeof(dir), GIOCHI_LIBEXECDIR) != 0) {
            return -1;
        }
        if (snprintf(path, sizeof(path), "%s/%s", dir, child->program)
                >= (int)sizeof(path)) {
            return -1;
        }
    }

    argv[0] = path;
    for (i = 0; i < child->arg_count; i++) {
        argv[i + 1] = child->args[i];
    }
    argv[child->arg_count + 1] = NULL;

    /* App externo opcional (Loja/Ports) pode nao estar instalado nesta imagem:
     * sem este teste, execv falhava no filho e o unico rastro era um exit 127,
     * sem dizer QUAL programa faltou nem o que fazer. */
    if (access(path, X_OK) != 0) {
        GLOG_ERR("%s: '%s' ausente ou sem permissao de execucao (%s)",
                 child->name, path, strerror(errno));
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        GLOG_ERR("fork(%s) falhou: %s", child->name, strerror(errno));
        return -1;
    }
    if (pid == 0) {
        execv(path, argv);
        _exit(127);
    }

    child->pid = pid;
    child->running = 1;
    child->last_start_ms = giochi_now_ms();
    GLOG_INFO("%s iniciado (pid %ld)", child->name, (long)pid);
    return 0;
}

static giochi_child *find_by_name(giochi_supervisor *sup, const char *name)
{
    int i;

    for (i = 0; i < sup->count; i++) {
        if (strcmp(sup->items[i].name, name) == 0) {
            return &sup->items[i];
        }
    }
    return NULL;
}

void giochi_supervisor_init(giochi_supervisor *sup)
{
    memset(sup, 0, sizeof(*sup));
}

int giochi_supervisor_add(giochi_supervisor *sup, const char *name,
                          const char *program, const char *const args[],
                          int restart)
{
    giochi_child *child;
    int i;

    if (sup == NULL || name == NULL || program == NULL) {
        return -1;
    }
    if (find_by_name(sup, name) != NULL) {
        return -1;
    }
    if (sup->count >= GIOCHI_MAX_CHILDREN) {
        GLOG_ERR("limite de filhos atingido ao adicionar %s", name);
        return -1;
    }

    child = &sup->items[sup->count];
    memset(child, 0, sizeof(*child));
    giochi_strlcpy(child->name, name, sizeof(child->name));
    giochi_strlcpy(child->program, program, sizeof(child->program));
    child->restart = restart;

    if (args != NULL) {
        for (i = 0; args[i] != NULL; i++) {
            if (i >= GIOCHI_MAX_ARGS) {
                GLOG_WARN("%s: argumentos extras ignorados", name);
                break;
            }
            giochi_strlcpy(child->args[i], args[i], sizeof(child->args[i]));
        }
        child->arg_count = i;
    }

    sup->count++;
    return 0;
}

int giochi_supervisor_start(giochi_supervisor *sup, const char *name)
{
    giochi_child *child = find_by_name(sup, name);

    if (child == NULL) {
        return -1;
    }
    if (child->running) {
        return 0;
    }
    return spawn_child(child);
}

int giochi_supervisor_start_all(giochi_supervisor *sup)
{
    int i;
    int rc = 0;

    for (i = 0; i < sup->count; i++) {
        if (sup->items[i].running) {
            continue;
        }
        if (spawn_child(&sup->items[i]) != 0) {
            rc = -1;
        }
    }
    return rc;
}

void giochi_supervisor_reap(giochi_supervisor *sup)
{
    unsigned long long now;
    int i;

    for (;;) {
        int status = 0;
        pid_t pid = waitpid(-1, &status, WNOHANG);

        if (pid <= 0) {
            break;
        }
        for (i = 0; i < sup->count; i++) {
            giochi_child *child = &sup->items[i];

            if (child->pid != pid) {
                continue;
            }
            child->running = 0;
            child->pid = 0;
            if (WIFEXITED(status)) {
                GLOG_WARN("%s terminou com codigo %d", child->name,
                          WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                GLOG_WARN("%s morto pelo sinal %d", child->name,
                          WTERMSIG(status));
            }
            if (child->restart) {
                child->restarts++;
            }
            break;
        }
    }

    if (sup->shutting_down) {
        return;
    }

    /* Sem jogo/app em primeiro plano: o Launcher volta (2.2/4.7). */
    if (!giochi_supervisor_foreground_running(sup)) {
        giochi_child *launcher = find_by_name(sup, "launcher");

        if (launcher != NULL && launcher->held) {
            giochi_supervisor_revive_launcher(sup);
        }
    }

    now = giochi_now_ms();
    for (i = 0; i < sup->count; i++) {
        giochi_child *child = &sup->items[i];

        if (child->running || !child->restart || child->held) {
            continue;
        }
        if (child->restarts > GIOCHI_MAX_RESTARTS) {
            continue;
        }
        if (now - child->last_start_ms < GIOCHI_RESTART_DELAY_MS) {
            continue;
        }
        GLOG_INFO("reiniciando %s (tentativa %d/%d)", child->name,
                  child->restarts, GIOCHI_MAX_RESTARTS);
        (void)spawn_child(child);
    }
}

void giochi_supervisor_stop_all(giochi_supervisor *sup)
{
    unsigned long long deadline;
    int i;

    sup->shutting_down = 1;

    for (i = 0; i < sup->count; i++) {
        giochi_child *child = &sup->items[i];

        if (child->running && child->pid > 0) {
            (void)kill(child->pid, SIGTERM);
        }
    }

    deadline = giochi_now_ms() + GIOCHI_STOP_GRACE_MS;
    for (;;) {
        int alive = 0;

        for (i = 0; i < sup->count; i++) {
            giochi_child *child = &sup->items[i];

            if (!child->running) {
                continue;
            }
            if (kill(child->pid, 0) != 0) {
                child->running = 0;
                continue;
            }
            alive++;
        }
        if (alive == 0) {
            break;
        }
        if (giochi_now_ms() >= deadline) {
            for (i = 0; i < sup->count; i++) {
                if (sup->items[i].running) {
                    GLOG_WARN("%s nao respondeu ao SIGTERM; SIGKILL",
                              sup->items[i].name);
                    (void)kill(sup->items[i].pid, SIGKILL);
                }
            }
            break;
        }
        sleep_ms(50);
    }

    for (i = 0; i < sup->count; i++) {
        if (sup->items[i].pid > 0) {
            (void)waitpid(sup->items[i].pid, NULL, WNOHANG);
        }
        sup->items[i].running = 0;
        sup->items[i].pid = 0;
    }
}

int giochi_supervisor_launch_app(giochi_supervisor *sup, const char *app_id)
{
    const char *program = app_id;
    giochi_child *child;

    if (sup == NULL || app_id == NULL || *app_id == '\0') {
        return -1;
    }
    if (strncmp(program, "APP:", 4) == 0) {
        program += 4;
    }
    if (*program == '\0') {
        return -1;
    }

    child = find_by_name(sup, program);
    if (child != NULL) {
        if (child->running) {
            return 0;
        }
        return spawn_child(child);
    }
    if (giochi_supervisor_add(sup, program, program, NULL, 0) != 0) {
        return -1;
    }
    child = find_by_name(sup, program);
    return child != NULL ? spawn_child(child) : -1;
}

/* --- Jogo (S4): entrada dedicada, sem auto-restart ----------------------- */
/* --- Ciclo de vida (secoes 2.2 e 4.7) --------------------------------- */

/* Espera o processo sair por ate timeout_ms. Retorna 1 se saiu. */
static int wait_exit(giochi_child *child, unsigned long timeout_ms)
{
    unsigned long long deadline = giochi_now_ms() + timeout_ms;

    if (child == NULL || child->pid <= 0) {
        return 1;
    }
    for (;;) {
        int status = 0;
        pid_t rc = waitpid(child->pid, &status, WNOHANG);

        if (rc == child->pid) {
            if (WIFEXITED(status)) {
                GLOG_INFO("%s saiu com codigo %d", child->name,
                          WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                GLOG_INFO("%s terminado pelo sinal %d", child->name,
                          WTERMSIG(status));
            }
            child->running = 0;
            child->pid = 0;
            return 1;
        }
        if (rc < 0 && errno != EINTR) {
            child->running = 0;
            child->pid = 0;
            return 1;
        }
        if (giochi_now_ms() >= deadline) {
            return 0;
        }
        sleep_ms(20);
    }
}

static void kill_and_wait(giochi_child *child, unsigned long grace_ms)
{
    if (child == NULL || !child->running || child->pid <= 0) {
        return;
    }
    (void)kill(child->pid, SIGTERM);
    if (!wait_exit(child, grace_ms)) {
        GLOG_WARN("%s nao respondeu ao SIGTERM; SIGKILL", child->name);
        (void)kill(child->pid, SIGKILL);
        (void)wait_exit(child, 1000);
    }
}

void giochi_supervisor_revive_launcher(giochi_supervisor *sup)
{
    giochi_child *launcher;

    if (sup == NULL) {
        return;
    }
    launcher = find_by_name(sup, "launcher");
    if (launcher == NULL) {
        return;
    }

    launcher->held = 0;

    if (launcher->running && launcher->pid > 0) {
        if (launcher->suspended) {
            /* Suspenso pelo §4.7: retoma sem reiniciar o estado da UI. */
            if (kill(launcher->pid, SIGCONT) == 0) {
                launcher->suspended = 0;
                GLOG_INFO("launcher retomado (SIGCONT)");
                return;
            }
            launcher->running = 0;
            launcher->pid = 0;
        } else {
            return; /* ja esta rodando normalmente */
        }
    }

    launcher->suspended = 0;
    GLOG_INFO("relancando launcher na Home");
    launcher->restarts = 0;
    if (spawn_child(launcher) != 0) {
        GLOG_ERR("falha ao relancar o launcher");
    }
}

int giochi_supervisor_launch(giochi_supervisor *sup, const char *path,
                             int kind)
{
    giochi_child *launcher;
    int rc;

    if (sup == NULL || path == NULL || *path == '\0') {
        return -1;
    }
    if (giochi_supervisor_game_running(sup)) {
        GLOG_WARN("jogo ja em execucao; transicao '%s' ignorada", path);
        return -1;
    }

    launcher = find_by_name(sup, "launcher");

    if (kind == GIOCHI_LAUNCH_KIND_APP) {
        /* 4.7: Launcher SUSPENSO (nao morto), PortMaster fullscreen. */
        if (launcher != NULL && launcher->running && launcher->pid > 0) {
            launcher->held = 1;
            if (kill(launcher->pid, SIGSTOP) != 0) {
                GLOG_WARN("nao foi possivel suspender o launcher: %s",
                          strerror(errno));
                launcher->held = 0;
            } else {
                launcher->suspended = 1;
                GLOG_INFO("launcher suspenso (SIGSTOP) para %s", path);
            }
        }
        rc = giochi_supervisor_launch_app(sup, path);
        if (rc != 0) {
            giochi_supervisor_revive_launcher(sup);
        }
        return rc;
    }

    /* 2.2: SIGTERM no Launcher + waitpid antes de executar o jogo.
     * 'held' evita que o auto-restart o ressuscite durante o jogo. */
    if (launcher != NULL) {
        launcher->held = 1;
        if (launcher->running && launcher->pid > 0) {
            kill_and_wait(launcher, 3000);
        }
    }

    rc = giochi_supervisor_launch_game(sup, path);
    if (rc != 0) {
        GLOG_ERR("falha ao lancar jogo '%s'; restaurando launcher", path);
        giochi_supervisor_revive_launcher(sup);
    }
    return rc;
}

int giochi_supervisor_launch_game(giochi_supervisor *sup, const char *path)
{
    giochi_child *child;

    if (sup == NULL || path == NULL || *path == '\0') {
        return -1;
    }

    child = find_by_name(sup, "game");
    if (child != NULL) {
        if (child->running) {
            GLOG_WARN("jogo ja em execucao (pid %ld); ignorando",
                      (long)child->pid);
            return -1;
        }
        giochi_strlcpy(child->program, path, sizeof(child->program));
        return spawn_child(child);
    }

    /* restart=0: jogo nao e reiniciado automaticamente. */
    if (giochi_supervisor_add(sup, "game", path, NULL, 0) != 0) {
        return -1;
    }
    child = find_by_name(sup, "game");
    return child != NULL ? spawn_child(child) : -1;
}

int giochi_supervisor_game_running(const giochi_supervisor *sup)
{
    const giochi_child *child = NULL;
    int i;

    if (sup == NULL) {
        return 0;
    }
    for (i = 0; i < sup->count; i++) {
        if (strcmp(sup->items[i].name, "game") == 0) {
            child = &sup->items[i];
            break;
        }
    }
    return child != NULL && child->running;
}

int giochi_supervisor_foreground_running(const giochi_supervisor *sup)
{
    int i;

    if (sup == NULL) {
        return 0;
    }
    for (i = 0; i < sup->count; i++) {
        const giochi_child *child = &sup->items[i];

        if (strcmp(child->name, "launcher") == 0) {
            continue;
        }
        /* Daemons (restart=1) nao sao foreground; jogo/app sao (restart=0). */
        if (child->restart == 0 && child->running) {
            return 1;
        }
    }
    return 0;
}

int giochi_supervisor_exit_game(giochi_supervisor *sup)
{
    giochi_child *child = find_by_name(sup, "game");

    if (child == NULL || !child->running || child->pid <= 0) {
        return 0;
    }

    GLOG_INFO("encerrando jogo (pid %ld)", (long)child->pid);
    kill_and_wait(child, 3000);
    giochi_supervisor_revive_launcher(sup);
    return 0;
}
