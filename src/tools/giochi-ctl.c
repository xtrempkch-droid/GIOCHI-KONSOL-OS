/* giochi-ctl — fala com o control.sock DENTRO do console.
 *
 * POR QUE ESTE PROGRAMA EXISTE
 * ---------------------------
 * O protocolo do `/run/giochi/control.sock` (docs/APP-SDK.md) é texto, uma
 * linha por mensagem. Do PC, o caminho natural é SSH + um cliente de soquete
 * Unix — e aí o `tools/giochi-ctl.py` assumia `nc -U` do BusyBox.
 *
 * Essa suposição estava errada. O `nc` do BusyBox 1.36.1 não fala soquete
 * de domínio Unix: `networking/nc.c` tem ZERO ocorrências de `AF_UNIX`, e
 * não existe símbolo `CONFIG_NC_UNIX` (o `-U` que o fragmento pedia era
 * descartado em silêncio, como todo símbolo inexistente). Medido no
 * aparelho:
 *
 *     $ ssh root@10.55.0.2 'printf "PING\n" | nc -U /run/giochi/control.sock'
 *     sh: nc: not found
 *
 * Instalar `socat` resolveria, ao preço de mais um pacote no SquashFS. Mas o
 * projeto já tem `giochi_sock_connect_unix()` e `giochi_sock_write_all()` em
 * `src/common/giochi_sock.c`, usados pelo supervisor e pelo Launcher. Um
 * binário de 20 KB que usa o MESMO código é menor que a dependência, e não
 * pode divergir do protocolo.
 *
 * USO
 *     giochi-ctl PING                      # -> PONG
 *     giochi-ctl USB_MODE MASS             # -> OK usb_mode MASS
 *     giochi-ctl --socket /outro.sock PING
 *     giochi-ctl --timeout 5000 PING
 *
 * Código de saída: 0 se o console respondeu `PONG`/`OK ...`; 1 se respondeu
 * `ERR ...` (ou algo que não é `OK`/`PONG`); 2 se não foi possível falar com o
 * soquete — inclusive quando o console não responde NADA.
 *
 * O caso "nada recebido" valia 0 até 2026-11-02, e isso era mentira: o
 * `giochi_sock_read_line` devolve 0 tanto no fim de conexão quanto no timeout
 * do `poll`, então um console travado (ou morto) saía com sucesso e stdout
 * vazio — num script, indistinguível de um `PING` respondido. O cliente em
 * Python (`tools/giochi-ctl.py`) já tratava resposta vazia como falha;
 * os dois clientes do MESMO protocolo discordavam no veredito.
 */
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "giochi_sock.h"

#define SOCK_PADRAO "/run/giochi/control.sock"
#define LINHA_MAX 1024

static void uso(void)
{
    fprintf(stderr,
            "uso: giochi-ctl [--socket CAMINHO] [--timeout MS] COMANDO [ARG]\n"
            "     COMANDO: PING | USB_MODE MTP|MASS | DEV_MODE ON|OFF | ...\n"
            "     giochi-ctl --comandos   lista o que o console aceita\n");
}

static void lista_comandos(void)
{
    /* A lista e' a mesma de docs/APP-SDK.md e de tools/giochi-ctl.py; a suite
     * `cliente-ssh` (tests/test_giochi_ctl_cliente_c.py) compara as quatro
     * fontes com os verbos que o supervisor aceita, entao uma entrar sem a
     * outra reprova o teste.
     *
     * BT_UP/BT_DOWN ficaram de fora desta lista por duas semanas: o Python e a
     * doc os tinham, o supervisor os implementava, e o `--comandos` do console
     * simplesmente nao os mostrava — quem consultasse o aparelho concluiria
     * que Bluetooth nao existe no protocolo. */
    puts("PING                     responde PONG");
    puts("USB_MODE MTP|MASS        troca o modo do gadget");
    puts("DEV_MODE ON|OFF          liga/desliga o SSH via USB");
    puts("LAUNCH_APP <caminho>     abre um app externo");
    puts("POWEROFF|REBOOT|SUSPEND  energia (derruba a sessao)");
    puts("WIFI_UP|WIFI_DOWN|BT_UP|BT_DOWN  radio");
    puts("SET_DATE <epoch>         acerta o relogio e persiste (docs/RELÓGIO.md)");
    puts("SET_THEME <id>           NAO implementado na v1.2 (responde ERR)");
}

/* O `giochi_sock_read_line` devolve 0 tanto no fim de conexao quanto no
 * timeout, e nao diz qual dos dois. Os dois pedem diagnostico DIFERENTE
 * ("o processo morreu" x "o processo esta' travado"), e um poll de 0 ms
 * responde de graca: com o par ja' fechado, o descritor fica legivel com
 * POLLHUP. */
static int peer_fechou(int fd)
{
    struct pollfd pfd;

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll(&pfd, 1, 0) <= 0) {
        return 0;
    }
    return (pfd.revents & (POLLHUP | POLLERR)) != 0;
}

/* `--timeout` em milissegundos: strtol com faixa, porque `atoi("9e999")` e'
 * zero (recusado por acidente) e `atoi("999999999999")` estoura um int —
 * comportamento indefinido que so' aparece com um argumento absurdo. */
static int ler_timeout(const char *arg, int *saida)
{
    char *fim = NULL;
    long v;

    if (arg == NULL || *arg == '\0') {
        return -1;
    }
    errno = 0;
    v = strtol(arg, &fim, 10);
    if (errno != 0 || fim == arg || *fim != '\0') {
        return -1;
    }
    if (v <= 0 || v > 600000) {
        return -1;
    }
    *saida = (int)v;
    return 0;
}

int main(int argc, char **argv)
{
    const char *socket_path = SOCK_PADRAO;
    int timeout_ms = 3000;
    int i = 1, fd, n;
    char linha[LINHA_MAX];
    char pedido[LINHA_MAX];
    size_t usado = 0;

    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "--socket")) {
            /* Opcao sem valor tem de dizer QUE faltou o valor. Antes caia no
             * `else` e imprimia "opcao desconhecida '--socket'" — mandando
             * procurar uma opcao que existe e esta' escrita certo. */
            if (i + 1 >= argc) {
                fprintf(stderr, "giochi-ctl: --socket exige um caminho "
                                "(ex.: --socket /run/giochi/control.sock)\n");
                return 2;
            }
            socket_path = argv[++i];
            i++;
        } else if (!strcmp(argv[i], "--timeout")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "giochi-ctl: --timeout exige milissegundos "
                                "(ex.: --timeout 3000)\n");
                return 2;
            }
            if (ler_timeout(argv[++i], &timeout_ms) != 0) {
                fprintf(stderr, "giochi-ctl: --timeout invalido '%s' "
                                "(use 1..600000 ms)\n", argv[i]);
                return 2;
            }
            i++;
        } else if (!strcmp(argv[i], "--comandos")) {
            lista_comandos();
            return 0;
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            uso();
            return 0;
        } else {
            fprintf(stderr, "giochi-ctl: opcao desconhecida '%s'\n", argv[i]);
            uso();
            return 2;
        }
    }

    if (i >= argc) {
        uso();
        return 2;
    }

    /* Junta o resto dos argumentos numa linha: o protocolo e' uma linha por
     * mensagem, com o comando e o argumento separados por espaco. */
    for (; i < argc; i++) {
        size_t n_arg = strlen(argv[i]);
        if (usado + n_arg + 2 >= sizeof(pedido)) {
            fprintf(stderr, "giochi-ctl: comando longo demais\n");
            return 2;
        }
        if (usado) {
            pedido[usado++] = ' ';
        }
        memcpy(pedido + usado, argv[i], n_arg);
        usado += n_arg;
    }
    pedido[usado++] = '\n';
    pedido[usado] = '\0';

    fd = giochi_sock_connect_unix(socket_path, timeout_ms);
    if (fd < 0) {
        /* Dizer o caminho e o errno: "nao conectou" sem isso manda procurar
         * no lugar errado — foi o que custou a sessao inteira do USB. */
        fprintf(stderr, "giochi-ctl: nao consegui conectar em %s: %s\n",
                socket_path, strerror(errno));
        if (access(socket_path, F_OK) != 0) {
            fprintf(stderr, "giochi-ctl:   o soquete nao existe; "
                            "o giochi-init esta' rodando? (ps | grep giochi-init)\n");
        } else {
            fprintf(stderr, "giochi-ctl:   o soquete existe mas recusou; "
                            "dono/permissoes (ls -l %s)\n", socket_path);
        }
        return 2;
    }

    if (giochi_sock_write_all(fd, pedido, usado) < 0) {
        fprintf(stderr, "giochi-ctl: falha ao enviar: %s\n", strerror(errno));
        close(fd);
        return 2;
    }

    n = giochi_sock_read_line(fd, linha, sizeof(linha), timeout_ms);
    if (n <= 0) {
        /* n == 0 e' "o console nao mandou nada": o read_line devolve 0 tanto
         * no timeout do poll quanto no fim de conexao. Aqui os dois casos
         * viram falha (antes viravam SUCESSO com stdout vazio) e a mensagem
         * diz qual dos dois foi, porque a acao e' diferente. */
        const char *quem = peer_fechou(fd) ? "fechou a conexao sem responder"
                                           : "nao respondeu nada";
        if (n < 0) {
            fprintf(stderr, "giochi-ctl: falha ao ler a resposta: %s\n",
                    strerror(errno));
        } else {
            fprintf(stderr, "giochi-ctl: o console %s (limite de %d ms)\n",
                    quem, timeout_ms);
            fprintf(stderr, "giochi-ctl:   travado? veja /boot/giochi-boot.log "
                            "(docs/BOOT-DEBUG.md)\n");
        }
        close(fd);
        return 2;
    }
    close(fd);

    /* Uma linha, sem o '\n': quem chama le isso de um script. */
    while (n > 0 && (linha[n - 1] == '\n' || linha[n - 1] == '\r')) {
        linha[--n] = '\0';
    }
    puts(linha);

    /* `ERR ...` nao pode sair com 0: num script isso esconde a falha. E uma
     * resposta que nao e' `OK`/`PONG` tambem nao: o protocolo nao tem outro
     * formato, entao linha estranha e' defeito de um dos lados. O cliente em
     * Python ja' tratava assim (`startswith(("OK","PONG"))`); este aceitava. */
    if (!strncmp(linha, "ERR", 3)) {
        return 1;
    }
    if (strncmp(linha, "OK", 2) != 0 && strcmp(linha, "PONG") != 0) {
        fprintf(stderr, "giochi-ctl: resposta inesperada (esperado OK/PONG/ERR): "
                        "'%s'\n", linha);
        return 1;
    }
    return 0;
}
