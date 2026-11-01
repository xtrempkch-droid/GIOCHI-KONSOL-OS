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
 * `ERR ...`; 2 se não foi possível falar com o soquete.
 */
#include <errno.h>
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
    /* A lista e' a mesma de docs/APP-SDK.md e de tools/giochi-ctl.py; se uma
     * entrar sem a outra, a suite `cliente-ssh` avisa. */
    puts("PING                     responde PONG");
    puts("USB_MODE MTP|MASS        troca o modo do gadget");
    puts("DEV_MODE ON|OFF          liga/desliga o SSH via USB");
    puts("LAUNCH_APP <caminho>     abre um app externo");
    puts("POWEROFF|REBOOT|SUSPEND  energia (derruba a sessao)");
    puts("WIFI_UP|WIFI_DOWN        radio");
    puts("SET_THEME <id>           NAO implementado na v1.2 (responde ERR)");
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
        if (!strcmp(argv[i], "--socket") && i + 1 < argc) {
            socket_path = argv[++i];
            i++;
        } else if (!strcmp(argv[i], "--timeout") && i + 1 < argc) {
            timeout_ms = atoi(argv[++i]);
            if (timeout_ms <= 0) {
                fprintf(stderr, "giochi-ctl: --timeout invalido\n");
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
    close(fd);
    if (n < 0) {
        fprintf(stderr, "giochi-ctl: o console nao respondeu em %d ms\n",
                timeout_ms);
        return 2;
    }

    /* Uma linha, sem o '\n': quem chama le isso de um script. */
    while (n > 0 && (linha[n - 1] == '\n' || linha[n - 1] == '\r')) {
        linha[--n] = '\0';
    }
    puts(linha);

    /* `ERR ...` nao pode sair com 0: num script isso esconde a falha. */
    if (!strncmp(linha, "ERR", 3)) {
        return 1;
    }
    return 0;
}
