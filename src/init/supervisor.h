/* Giochi Konsol OS v1.2 — supervisor de processos (ADR-10). */
#ifndef GIOCHI_SUPERVISOR_H
#define GIOCHI_SUPERVISOR_H

#include <sys/types.h>

#define GIOCHI_MAX_CHILDREN 8
#define GIOCHI_MAX_ARGS 6
#define GIOCHI_MAX_RESTARTS 5
#define GIOCHI_RESTART_DELAY_MS 1000ULL

typedef struct {
    char name[32];
    char program[64];
    char args[GIOCHI_MAX_ARGS][64];
    int arg_count;
    int restart;
    int running;
    int restarts;
    /* Suspensao intencional (secoes 2.2/4.7): enquanto 'held', o auto-restart
     * fica desligado e o filho pode estar parado (SIGSTOP). */
    int held;
    int suspended;
    pid_t pid;
    unsigned long long last_start_ms;
} giochi_child;

typedef struct {
    giochi_child items[GIOCHI_MAX_CHILDREN];
    int count;
    int shutting_down;
} giochi_supervisor;

void giochi_supervisor_init(giochi_supervisor *sup);

/* args pode ser NULL. Copia os argumentos; nao retem ponteiros. */
int giochi_supervisor_add(giochi_supervisor *sup, const char *name,
                          const char *program, const char *const args[],
                          int restart);

int giochi_supervisor_start(giochi_supervisor *sup, const char *name);
int giochi_supervisor_start_all(giochi_supervisor *sup);

/* waitpid(WNOHANG) + politica de restart com backoff. */
void giochi_supervisor_reap(giochi_supervisor *sup);

void giochi_supervisor_stop_all(giochi_supervisor *sup);

/* Lanca um app externo ("APP:portmaster" ou "portmaster"). */
int giochi_supervisor_launch_app(giochi_supervisor *sup, const char *app_id);

int giochi_supervisor_game_running(const giochi_supervisor *sup);

/* Inicia apenas o filho "game" (sem mexer no Launcher). Sem auto-restart. */
int giochi_supervisor_launch_game(giochi_supervisor *sup, const char *path);

/* --- Ciclo de vida (secoes 2.2 e 4.7) --------------------------------
 *
 * Jogo:  SIGTERM no Launcher -> waitpid -> fork/exec do jogo -> ao sair,
 *        o Launcher e reiniciado (estado volta na Home).
 * App externo (Ports/Loja): o Launcher e SUSPENSO (SIGSTOP), nao morto;
 *        ao sair, SIGCONT + Home (secao 4.7).
 */
enum {
    GIOCHI_LAUNCH_KIND_GAME = 0,
    GIOCHI_LAUNCH_KIND_APP = 1
};

/* Orquestra a transicao e inicia o filho. 0 = iniciado. */
int giochi_supervisor_launch(giochi_supervisor *sup, const char *path,
                             int kind);

/* Encerra o jogo (SIGTERM -> 3 s -> SIGKILL) e reativa o Launcher. */
int giochi_supervisor_exit_game(giochi_supervisor *sup);

/* Retoma o Launcher suspenso ou o relanca (limpa 'held'). */
void giochi_supervisor_revive_launcher(giochi_supervisor *sup);

/* Ha jogo ou app externo em execucao (foreground)? */
int giochi_supervisor_foreground_running(const giochi_supervisor *sup);

#endif /* GIOCHI_SUPERVISOR_H */
