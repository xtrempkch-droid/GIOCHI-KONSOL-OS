#!/bin/sh
# Giochi Konsol OS v1.2 — habilita o Modo Desenvolvedor (SSH via USB) no cartao.
#
# POR QUE EXISTE
# --------------
# O SSH via USB e' DESLIGADO de fabrica (adendo 4.6.1, §3.9): `developer.
# ssh_usb_enabled = false`. Ligar exige, no aparelho, o menu
# Select > Sistema > Modo Desenvolvedor. Quando a tela nao ajuda — Launcher
# travado, video que nao sobe —, a unica entrada e' USB, e ela esta' fechada.
# Este script abre a porta pelo lado do PC, mexendo so' na particao /user
# (a unica gravavel), sem tocar na imagem.
#
# O que ele escreve (e por que cada um):
#   <user>/settings.json              "developer": {"ssh_usb_enabled": true}
#   <user>/ssh/authorized_keys        a chave publica (sem ela o S62sshd NAO
#                                     sobe: sem chave e sem senha, ele recusa)
#
# NAO gera nem copia chave privada para lugar nenhum dentro do repositorio:
# a chave fica em ~/.ssh, so' a publica vai para o cartao.
#
# USO
#   tools/giochi-dev-mode.sh --listar
#   tools/giochi-dev-mode.sh                # acha GIOCHI_USER e habilita
#   tools/giochi-dev-mode.sh /media/eu/GIOCHI_USER
#   tools/giochi-dev-mode.sh --desligar ...
#   tools/giochi-dev-mode.sh --key ~/.ssh/id_ed25519.pub ...
#
# O argumento e' um DIRETORIO, nao um dispositivo: assim o script roda igual
# num cartao montado, numa copia local e no teste automatizado.
set -eu

PROG=giochi-dev-mode
# Caminho ABSOLUTO do proprio script: a re-execucao com privilegio pode mudar
# o cwd (pkexec vira root e o cwd vira /root), e "$0" relativo viraria
# "arquivo nao encontrado" — ou pior, rodaria outro arquivo.
SELF=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/$(basename -- "$0")
LIGAR=1
LISTAR=0
CHAVE_PUB=${GIOCHI_SSH_PUBKEY:-}
DESTINO=

uso() {
    sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --desligar) LIGAR=0; shift ;;
        --listar) LISTAR=1; shift ;;
        --key) CHAVE_PUB=${2:-}; shift 2 ;;
        -h|--help) uso 0 ;;
        -*) echo "$PROG: opcao desconhecida '$1'" >&2; uso 2 ;;
        *) DESTINO=$1; shift ;;
    esac
done

log() { printf '%s: %s\n' "$PROG" "$*"; }
die() { printf '%s: ERRO: %s\n' "$PROG" "$*" >&2; exit 1; }

# --- achar o cartao ------------------------------------------------------
achar_cartao() {
    # NAO usar $USER: sob `sudo`/`pkexec` ele vira `root` e a busca iria para
    # /run/media/root, que nao existe — medido nesta sessao (o script dizia
    # "nao achei a particao" com o cartao montado em /run/media/charles).
    # A fonte confiavel e' o proprio sistema de arquivos: lista os pontos de
    # montagem e olha a assinatura da particao /user.
    for d in $(findmnt -rno TARGET 2>/dev/null | grep -E '^/(run/)?media/') \
             /mnt /media /run/media/*; do
        [ -d "$d" ] || continue
        # Assinatura de particao /user: o diretorio `games` (criado pelo
        # S10storage) ou o settings.json que o Launcher grava.
        if [ -d "$d/games" ] || [ -f "$d/settings.json" ]; then
            printf '%s\n' "$d"
            return 0
        fi
    done
    return 1
}

if [ "$LISTAR" = "1" ]; then
    encontrado=$(achar_cartao || true)
    if [ -z "$encontrado" ]; then
        log "nenhuma particao GIOCHI_USER montada."
        log "monte o cartao (o gerenciador de arquivos monta ao clicar) e repita;"
        log "ou passe o caminho: $PROG /media/\$USER/GIOCHI_USER"
        exit 1
    fi
    log "particao /user: $encontrado"
    exit 0
fi

if [ -z "$DESTINO" ]; then
    DESTINO=$(achar_cartao) || {
        die "nao achei a particao /user montada. Passe o caminho."
    }
fi

[ -d "$DESTINO" ] || die "'$DESTINO' nao e' diretorio"


# --- a chave publica -----------------------------------------------------
# Resolvida ANTES de elevar: sob `sudo`/`pkexec` o HOME vira /root e
# ~/.ssh/id_ed25519.pub deixa de existir — medido nesta sessao, o script
# pedia privilegio, achava o cartao e entao dizia "chave nao encontrada".
# Depois disso o caminho fica ABSOLUTO e sobrevive a re-execucao.
if [ "$LIGAR" = "1" ]; then
    if [ -z "$CHAVE_PUB" ]; then
        for cand in "$HOME/.ssh/id_ed25519.pub" "$HOME/.ssh/id_rsa.pub" \
                    "${SUDO_USER:+/home/$SUDO_USER/.ssh/id_ed25519.pub}"; do
            [ -n "$cand" ] && [ -f "$cand" ] && { CHAVE_PUB=$cand; break; }
        done
    fi
    [ -n "$CHAVE_PUB" ] && [ -f "$CHAVE_PUB" ] || die \
        "chave publica nao encontrada. Gere com 'ssh-keygen -t ed25519' ou passe --key CAMINHO.pub"
    # Caminho absoluto, para a re-execucao com privilegio achar o mesmo arquivo.
    CHAVE_PUB=$(CDPATH= cd -- "$(dirname -- "$CHAVE_PUB")" && pwd)/$(basename -- "$CHAVE_PUB")
    grep -qE '^(ssh-ed25519|ssh-rsa|ecdsa-)' "$CHAVE_PUB" || die \
        "'$CHAVE_PUB' nao parece chave publica OpenSSH"
fi

# A particao GIOCHI_USER e' ext4 e o gerenciador de arquivos a monta como ROOT
# (udisks, sem `-o uid=`): o usuario comum nao escreve nela. Isso e' o caso
# NORMAL, nao a excecao, entao o script se re-executa com privilegio em vez de
# mandar o usuario descobrir sozinho — foi o que aconteceu na primeira vez.
if [ ! -w "$DESTINO" ]; then
    if [ "${GIOCHI_DEV_MODE_ROOT:-}" = "1" ]; then
        die "'$DESTINO' continua sem escrita como root"
    fi
    log "'$DESTINO' e' do root (montagem do udisks); pedindo privilegio ..."

    # ORDEM importa e o `exec` NAO pode vir antes do teste: `exec cmd || true`
    # nunca executa o `||`, porque o `exec` substitui o processo — medido nesta
    # sessao: com `sudo -n` falhando, o script morria em silencio e o `pkexec`
    # nunca era tentado. Primeiro TESTA o elevador, depois re-executa.
    # A chave resolvida vai por ARGUMENTO, nao por ambiente: `pkexec` LIMPA o
    # ambiente (mantem so' uma lista branca: DISPLAY, XAUTHORITY, TERM...) e o
    # processo elevado voltaria a procurar ~/.ssh com HOME=/root — medido, o
    # script falhou DUAS vezes por isso, uma por camada.
    if [ -n "${CHAVE_PUB:-}" ]; then
        set -- --key "$CHAVE_PUB" "$@"
    fi
    if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
        exec sudo -n env GIOCHI_DEV_MODE_ROOT=1 "$SELF" "$@"
    fi
    if command -v pkexec >/dev/null 2>&1; then
        # pkexec abre o dialogo do polkit: exige sessao grafica e alguem para
        # digitar a senha. Se nao houver, esta linha vai falhar — e a mensagem
        # abaixo diz ao usuario o que rodar a mao.
        if [ "$(id -u)" = "0" ]; then
            exec env GIOCHI_DEV_MODE_ROOT=1 "$SELF" "$@"
        fi
        exec env GIOCHI_DEV_MODE_ROOT=1 pkexec "$SELF" "$@"
    fi
    die "precisa de root para escrever em '$DESTINO'.
      rode: sudo $SELF $(printf '%s ' "$@")"
fi

# --- settings.json ------------------------------------------------------
# Reimplementar o JSON inteiro aqui seria a segunda verdade (§3.20). A
# estrategia e' CIRURGICA: trocar so' o valor de ssh_usb_enabled, preservando
# byte a byte o resto do arquivo. Se nao houver arquivo, cria um minimo valido
# no formato que `giochi_settings_save` usa (o Launcher o reescreve inteiro no
# primeiro boot de qualquer forma).
SETTINGS="$DESTINO/settings.json"

if [ -f "$SETTINGS" ]; then
    cp "$SETTINGS" "$SETTINGS.bak"
    if grep -q '"ssh_usb_enabled"' "$SETTINGS"; then
        val=$([ "$LIGAR" = "1" ] && echo true || echo false)
        sed -i "s/\"ssh_usb_enabled\"[[:space:]]*:[[:space:]]*\(true\|false\)/\"ssh_usb_enabled\": $val/" "$SETTINGS"
    else
        # Sem a chave: acrescentar o bloco `developer` e' mais arriscado por sed.
        # Melhor reescrever a partir do backup com o Bloco inteiro no fim.
        die "settings.json existe mas sem 'developer.ssh_usb_enabled'; edite a mao ou apague o arquivo"
    fi
    log "settings.json: ssh_usb_enabled = $([ "$LIGAR" = "1" ] && echo true || echo false) (backup em .bak)"
else
    val=$([ "$LIGAR" = "1" ] && echo true || echo false)
    # Parte dos defaults do REPOSITORIO quando existirem: escrever os valores a
    # mao aqui seria a segunda verdade (§3.20), e o primeiro boot do Launcher
    # reescreveria tudo mesmo.
    DEFAULTS="$(dirname -- "$0")/../assets/defaults.json"
    if [ -f "$DEFAULTS" ] && command -v python3 >/dev/null 2>&1; then
        python3 - "$DEFAULTS" "$SETTINGS" "$val" <<'PYEOF'
import json, sys
origem, destino, val = sys.argv[1], sys.argv[2], sys.argv[3] == "true"
doc = json.load(open(origem, encoding="utf-8"))
doc.setdefault("developer", {})["ssh_usb_enabled"] = val
with open(destino, "w", encoding="utf-8") as fh:
    json.dump(doc, fh, indent=2, ensure_ascii=False)
    fh.write("\n")
PYEOF
        log "settings.json criado a partir de assets/defaults.json, ssh_usb_enabled = $val"
        SETTINGS_CRIADO=1
    else
    cat > "$SETTINGS" <<EOF
{
  "schema_version": "1.1",
  "appearance": { "theme_id": "dark_default", "accent_color": "#E50914", "font_scale": 1.00, "ui_scale": 1.00 },
  "system": { "language": "pt-BR", "timezone": "America/Sao_Paulo", "brightness": 80, "suspend_timeout_s": 300, "usb_mode": "mtp" },
  "sound": { "master_volume": 70, "game_volume": 80, "music_volume": 60, "muted": false },
  "store": { "region": "BR", "source": "curated" },
  "network": {
    "wifi_enabled": false,
    "wifi_last_ssid": null,
    "wifi_credentials": [],
    "bluetooth_enabled": false,
    "bluetooth_paired": []
  },
  "developer": { "ssh_usb_enabled": $val }
}
EOF
    log "settings.json criado com ssh_usb_enabled = $val"
    fi
fi

# --- chave autorizada ----------------------------------------------------
if [ "$LIGAR" = "1" ]; then
    mkdir -p "$DESTINO/ssh"
    # Preserva chaves que ja' estejam la'; so' acrescenta a nossa se faltar.
    if [ -f "$DESTINO/ssh/authorized_keys" ] && \
       grep -qxF "$(cat "$CHAVE_PUB")" "$DESTINO/ssh/authorized_keys"; then
        log "chave ja' autorizada em ssh/authorized_keys"
    elif [ -f "$DESTINO/ssh/authorized_keys" ]; then
        cat "$CHAVE_PUB" >> "$DESTINO/ssh/authorized_keys"
        log "chave acrescentada a ssh/authorized_keys ($(wc -l < "$DESTINO/ssh/authorized_keys") chave(s))"
    else
        cp "$CHAVE_PUB" "$DESTINO/ssh/authorized_keys"
        log "ssh/authorized_keys criado a partir de $(basename "$CHAVE_PUB")"
    fi
    chmod 700 "$DESTINO/ssh" 2>/dev/null || true
    chmod 600 "$DESTINO/ssh/authorized_keys" 2>/dev/null || true
    # /user/.ssh e' onde o dropbear guarda as chaves de HOST (persistem entre
    # boots, porque a raiz SquashFS e' somente leitura — §3.6).
    mkdir -p "$DESTINO/.ssh"
    chmod 700 "$DESTINO/.ssh" 2>/dev/null || true
fi

sync

log ""
if [ "$LIGAR" = "1" ]; then
    log "pronto. No proximo boot:"
    log "  1. cabo USB de DADOS no console (o de carga nao serve);"
    log "  2. no PC: tools/giochi-ctl.py preparar"
    log "  3. tools/giochi-ctl.py ctl PING      # espera PONG"
else
    log "Modo Desenvolvedor DESLIGADO no cartao (o SSH nao sobe)."
fi
