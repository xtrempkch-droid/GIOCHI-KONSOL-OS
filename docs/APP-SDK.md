# SDK do app externo (jogo, Loja, PortMaster)

O que um app precisa saber para rodar no Giochi Konsol OS v1.2 e conversar com o
sistema. Tudo aqui é **contrato**: o que está escrito, o código cobra, e há teste
rodando o binário real.

Verificado por: [`tests/test_control_protocol.py`](../tests/test_control_protocol.py)
(14 testes, `giochi-init` de verdade) e
[`tests/test_dev_mode_card.py`](../tests/test_dev_mode_card.py).

---

## 1. Como o app é lançado

O `giochi-init` é o supervisor. O app **não** é iniciado pelo usuário nem pelo
sistema de init: ele é lançado por um destes caminhos, e em ambos o pai é o
supervisor.

| Origem | Caminho | Efeito |
|---|---|---|
| Launcher, aba com `APP:<id>` | FIFO `/run/giochi/transition` | `giochi_supervisor_launch(path, KIND_APP)` |
| `.giochi` da Biblioteca | idem, `GIOCHI_TRANS_LAUNCH` | extrai o pacote (ADR-11) e resolve o `entrypoint` |
| `control.sock` | `LAUNCH_APP <caminho>` | idem, direto |
| Launcher abre um jogo | idem | o Launcher recebe `SIGSTOP` enquanto o jogo roda |

Consequências práticas, todas medidas no código:

- **O app roda como filho do `giochi-init`**, no mesmo grupo de processos. Ele
  herda `PATH`, o `HOME=/user` e o ambiente do init.
- **Não há terminal.** `stdin` não é um TTY; escrever em `stdout` vai para o
  `logger` do supervisor. Para diagnóstico, use `syslog` (`logger -t meuapp`)
  ou escreva em `/user/`.
- **Sair com `0` é normal**; o supervisor registra o código e devolve o controle
  ao Launcher (`RETURN_HOME`). Código diferente de zero também é tratado — o
  supervisor **não** entra em loop de reinício para apps (§2.2).
- **`/` é somente leitura.** Só `/user` (rw) e `/run` (tmpfs) são graváveis. Um
  app que escreve em `/etc` ou `/usr` falha em silêncio no aparelho e funciona
  no PC — a armadilha §3.6.

### O que o app recebe do supervisor

Nada além do contrato do sistema de arquivos e ambiente. Informação de estado
fica em arquivos que ele pode ler:

| Caminho | Conteúdo |
|---|---|
| `/user/.giochi/device_profile.json` | SoC, RAM, VRAM, versão, aparelho (regenerado a cada boot) |
| `/etc/giochi/device.conf` | identidade do aparelho (`GIOCHI_ID`, `GIOCHI_SOC`, …) |
| `/user/settings.json` | escolhas do usuário (idioma, tema, volume, USB) |
| `/usr/share/giochi/device-theme.json` | tema de fábrica do aparelho |

**Não escreva em `settings.json`.** É propriedade do Launcher (§4.9.5); dois
escritores = um sobrescreve o outro.

---

## 2. `control.sock` — o canal de comandos

`/run/giochi/control.sock`, socket UNIX de **stream**, um comando por conexão.

### Enquadramento (exato)

```
cliente  ->  "<VERBO>[ <arg>]\n"        uma linha, até 1 s para chegar
servidor ->  "<resposta>\n"             uma linha, e a conexão é fechada
```

Regras que o teste cobra:

| Regra | Detalhe |
|---|---|
| Terminador | `\n` **obrigatório**. Sem ele a conexão fecha sem resposta |
| Uma conexão = um comando | mandar dois na mesma conexão: só o primeiro é atendido |
| Caixa | verbos são **case-sensitive**: `ping` → `ERR unknown_command` |
| Espaços à esquerda | ignorados: `"   PING"` funciona |
| Sucesso | começa com `OK ` ou é `PONG` |
| Falha | começa com `ERR ` e o motivo é um **identificador estável**, nunca uma frase traduzível |
| Socket existe antes do `listen()` | o cliente tem de **tentar de novo** em `ECONNREFUSED` (§3.41) |

### Comandos

| Comando | Resposta | Observação |
|---|---|---|
| `PING` | `PONG` | sonda de vida; use antes de qualquer coisa |
| `USB_MODE MTP` / `USB_MODE MASS` | `OK usb_mode <arg>` | `ERR missing_mode` / `ERR invalid_mode` |
| `DEV_MODE ON` / `DEV_MODE OFF` | `OK dev_mode <arg>` | liga o SSH via USB; `ERR invalid_mode` |
| `LAUNCH_APP <caminho>` | `OK launch <caminho>` / `ERR launch_failed` | app ausente é erro, não sucesso |
| `POWEROFF` / `REBOOT` / `SUSPEND` | `OK <ação>` / `ERR <ação>_failed` | derruba a sessão (menos `SUSPEND`) |
| `WIFI_UP` / `WIFI_DOWN` | `OK wifi_up` / `ERR no_adapter` | sem rádio → `ERR no_adapter` |
| `BT_UP` / `BT_DOWN` | `OK bt_up` / `ERR no_adapter` | idem |
| `SET_THEME <id>` | **`ERR not_implemented`** | ver abaixo |

**`SET_THEME` não funciona na v1.2, e agora diz isso.** O canal existente é
Launcher → init (o Launcher grava `settings.json` e manda o comando). Trocar o
tema é o caminho inverso, e não há evento init → Launcher. Antes a resposta era
`OK theme <id>` **sem fazer nada**: um app acreditava que o tema tinha mudado.
Hoje a resposta é `ERR not_implemented (troque pelo menu do Launcher)`.
Quem troca o tema é **Select → Sistema → Tema**.

### Exemplo mínimo (Python, dentro do console)

O console **não tem Python**. Para um app em C/C++ ou Godot, o cliente são
~20 linhas:

```c
int giochi_ctl(const char *cmd, char *out, size_t outsz)
{
    struct sockaddr_un a = {0};
    a.sun_family = AF_UNIX;
    strcpy(a.sun_path, "/run/giochi/control.sock");

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) { close(fd); return -1; }

    write(fd, cmd, strlen(cmd));
    write(fd, "\n", 1);

    ssize_t n = read(fd, out, outsz - 1);      /* uma linha, depois EOF */
    close(fd);
    if (n <= 0) return -1;
    out[n] = '\0';
    if (out[n - 1] == '\n') out[n - 1] = '\0';
    return strncmp(out, "OK", 2) == 0 || strcmp(out, "PONG") == 0 ? 0 : -1;
}
```

Do PC, sem SDK nenhum: `tools/giochi-ctl.py ctl PING` (por SSH, ver §5).

---

## 3. `transition` — o canal do Launcher para o init

`/run/giochi/transition`, FIFO binário. **É do Launcher**; um app não deve
escrever nele — dois escritores intercalam mensagens e o init lê lixo.

| Comando | Efeito |
|---|---|
| `GIOCHI_TRANS_LAUNCH` | extrai o `.giochi`, resolve o `entrypoint` e lança como jogo |
| `GIOCHI_TRANS_OPEN_APP` | lança um binário como app externo |
| `GIOCHI_TRANS_EXIT_GAME` | encerra o jogo em execução |
| `GIOCHI_TRANS_RETURN_HOME` | reativa o Launcher |

---

## 4. O `.giochi` (o pacote que a Loja produz e o init consome)

Layout obrigatório, nesta ordem (ADR-11 — o `giochi-extract` do console lê em
streaming e depende da ordem):

```
metadata.json  ->  capa.png  ->  thumb.png (opcional)  ->  bin/
```

`metadata.json` — campos que o console **verifica**:

| Campo | Regra |
|---|---|
| `arch` | `aarch64` **apenas** (ADR-13 proíbe emulação) |
| `runtime` | `godot` \| `retroarch` \| `proton` |
| `entrypoint` | `bin/<algo>`, sem `..` |
| `id`, `title`, `version` | obrigatórios; `version` em semver |

Assinatura Ed25519 + SHA-256 (ADR-01); o `giochi-verify` do console recusa o
pacote se a assinatura não conferir.

O app **não** empacota nem assina nada sozinho: isso é feito no PC por
[`apps/forge`](../apps/forge), e o catálogo/instalação por
[`apps/store`](../apps/store).

---

## 5. Depurar de fora: SSH via USB (Modo Desenvolvedor)

O SSH é **desligado de fábrica** (§4.6.1). Ligar, do lado do PC:

```sh
# 1. com o cartão montado (a partição GIOCHI_USER):
tools/giochi-dev-mode.sh              # acha sozinho; --desligar desfaz
#    escreve settings.json (developer.ssh_usb_enabled=true) e
#    ssh/authorized_keys com a sua ~/.ssh/id_ed25519.pub

# 2. cabo USB de DADOS no console, e no PC:
tools/giochi-ctl.py preparar          # 10.55.0.1/24 em usb0 e espera :22
tools/giochi-ctl.py ctl PING          # -> PONG
tools/giochi-ctl.py status            # uptime, tema, sockets, processos
tools/giochi-ctl.py echo --tail 40 "cat /boot/giochi-boot.log"
```

| Item | Valor |
|---|---|
| Console | `10.55.0.2/24` |
| PC | `10.55.0.1/24`, **estático** (não há DHCP no console) |
| SSH | porta 22, **somente** em `10.55.0.2` |
| Autenticação | chave (`/user/ssh/authorized_keys`) **ou** senha de root; sem nenhuma das duas o daemon não sobe |

Se `ctl` reclamar que falta `nc -U`, o BusyBox da imagem está sem
`CONFIG_NC_UNIX` — nesse caso o protocolo só pode ser exercitado de dentro do
console, e o caminho é o log (`echo --tail`).

---

## 6. Orçamento e limites (o que o app pode gastar)

| Recurso | RK3326 (1 GB) | RK3566 (2 GB) | Fonte |
|---|---|---|---|
| RAM total | 1024 MB | 2048 MB | `device_profile.json` |
| VRAM | 64 MB | 128 MB | idem |
| UI do sistema em repouso | ≤ 60 MB | ≤ 60 MB | §4.1.6 (meta) |
| Atlas da fonte | 1,75 MiB | 1,75 MiB | medido |

O app é responsável pelo próprio orçamento: o supervisor **não** mata processo
por consumo. Quem mede é `scripts/bench.gd` (jogos padrão) e
`tools/bench-games.sh`.

---

## 7. Checklist antes de pedir para alguém testar

1. `arch` é `aarch64` e o binário é **musl** (o rootfs não tem glibc).
2. Não escreve fora de `/user` e `/run`.
3. Termina a linha ao falar com o `control.sock`, e **reconecta** em
   `ECONNREFUSED`.
4. Trata resposta `ERR ...` mostrando o motivo — não só "falhou".
5. Sai com `0` ao fechar normalmente.
6. O `.giochi` está assinado e passa no `giochi-verify` do console.
