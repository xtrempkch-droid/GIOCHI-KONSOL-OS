#!/bin/sh
# Giochi Konsol OS v1.2 — grava, CONFERE e prepara o cartao num comando so.
#
# POR QUE EXISTE
# --------------
# Colocar a imagem no cartao sao quatro passos e tres deles ja' falharam numa
# madrugada desta sessao, cada um com sintoma diferente:
#
#   1. `dd` sem `conv=fsync` termina com sucesso e deixa o cartao incompleto
#      se ele for removido cedo. O kernel do aparelho entao da' EIO dentro do
#      SquashFS e o boot para em "No working init found" (AGENTS.md §3.42).
#   2. `dd` no alvo errado (o disco do sistema) nao tem desfazer.
#   3. A particao /user monta como root e o Modo Desenvolvedor (SSH via USB)
#      vem DESLIGADO de fabrica (adendo 4.6.1) — sem reabri-lo, o cartao
#      regravado nao aceita a conexao e parece que "o SSH parou de funcionar".
#
# Este script faz os tres numa ordem que nao deixa o cartao num estado
# ambiguo, e para no primeiro erro. Nada aqui e' novo: ele chama
# `verify-card.py` e `giochi-dev-mode.sh`, que ja' tem teste proprio.
#
# USO
#   tools/flash-card.sh --listar                        # ve os cartoes
#   tools/flash-card.sh /dev/sdd                        # grava a imagem x55
#   tools/flash-card.sh /dev/sdd --device x55 --sem-ssh
#   tools/flash-card.sh --checar /dev/sdd               # so' confere o cartao
#
# Opcoes:
#   --device ID      aparelho da imagem (default: x55)
#   --imagem CAMINHO imagem .img ou .img.xz (default: dist/<id>/giochi-<id>-sdcard.img)
#   --key CAMINHO.pub  chave que autoriza o SSH via USB (default: ~/.ssh/id_*.pub)
#   --sem-ssh        nao reabre o Modo Desenvolvedor depois de gravar
#   --sim            mostra o que faria, sem escrever (para conferir o alvo)
#   --checar         nao grava: so' verifica o cartao contra a imagem
#   --listar         mostra os cartoes/leitores USB conectados
#
# O alvo e' o DISPOSITIVO INTEIRO (/dev/sdd), nunca uma particao (/dev/sdd1).
# O script recusa particao, disco com particao montada e o disco que contem /.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PROG=flash-card

DEVICE=x55
IMAGEM=
ALVO=
SEM_SSH=0
SIMULAR=0
SO_CHECAR=0
CHAVE_PUB=${GIOCHI_SSH_PUBKEY:-}

log()  { printf '%s: %s\n' "$PROG" "$*"; }
erro() { printf '%s: ERRO: %s\n' "$PROG" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --device)   DEVICE=${2:?--device precisa de valor}; shift 2 ;;
        --imagem)   IMAGEM=${2:?--imagem precisa de valor}; shift 2 ;;
        --key)      CHAVE_PUB=${2:?--key precisa de valor}; shift 2 ;;
        --sem-ssh)  SEM_SSH=1; shift ;;
        --sim)      SIMULAR=1; shift ;;
        --checar)   SO_CHECAR=1; shift ;;
        --listar)   listar=1; shift ;;
        -h|--help)  sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*)         erro "opcao desconhecida '$1'" ;;
        *)          ALVO=$1; shift ;;
    esac
done

# A chave publica tem de ser resolvida como o USUARIO, antes de qualquer
# elevacao — e por isso DEPOIS do parsing (`--key` tem de vencer a busca).
# Depois do `pkexec` o HOME e' /root e a chave do mantenedor deixa de existir:
# medido, a gravacao inteira passou, a verificacao deu OK e o Modo
# Desenvolvedor falhou com "chave publica nao encontrada", deixando o cartao
# sem SSH. Resolvida aqui, o caminho vira ABSOLUTO e viaja por argumento ate o
# giochi-dev-mode.sh (que tambem precisa disso, §3.82).
if [ -z "$CHAVE_PUB" ]; then
    # `sudo` define SUDO_USER e `pkexec` define PKEXEC_UID; os dois trocam o
    # HOME. Procurar no home de quem chamou cobre os dois casos, e $HOME
    # continua valendo para quem roda como usuario comum.
    for base in "$HOME" \
                "${SUDO_USER:+/home/$SUDO_USER}" \
                "$(getent passwd "${PKEXEC_UID:-0}" 2>/dev/null | cut -d: -f6)"; do
        [ -n "$base" ] && [ -d "$base/.ssh" ] || continue
        for nome in id_ed25519.pub id_rsa.pub id_ecdsa.pub; do
            [ -f "$base/.ssh/$nome" ] && { CHAVE_PUB="$base/.ssh/$nome"; break 2; }
        done
    done
fi
if [ -n "$CHAVE_PUB" ] && [ -f "$CHAVE_PUB" ]; then
    CHAVE_PUB=$(CDPATH= cd -- "$(dirname -- "$CHAVE_PUB")" && pwd)/$(basename -- "$CHAVE_PUB")
else
    CHAVE_PUB=
fi

# --- listar -------------------------------------------------------------
if [ "${listar:-0}" = "1" ]; then
    achou=0
    for d in /sys/block/sd*; do
        [ -e "$d" ] || continue
        nome=$(basename "$d")
        [ -r "$d/removable" ] || continue
        # So' o que esta' no barramento USB: o cartao do console e' sempre
        # leitor USB, e o disco interno (nvme) nunca deve aparecer aqui.
        case "$(readlink -f "/sys/block/$nome" 2>/dev/null)" in
            *usb*) ;;
            *) continue ;;
        esac
        achou=1
        tam=$(awk '{printf "%.1f GiB", $1*512/1073741824}' \
              "/sys/block/$nome/size" 2>/dev/null || echo "?")
        modelo=$(cat "/sys/block/$nome/device/model" 2>/dev/null || echo "?")
        partes=$(ls "/sys/block/$nome" 2>/dev/null | grep -c "^${nome}[0-9]" || true)
        printf '  /dev/%-6s %-10s %-28s %s particao(oes)\n' \
               "$nome" "$tam" "$modelo" "$partes"
    done
    if [ "$achou" = "0" ]; then
        log "nenhum cartao/leitor USB encontrado."
        log "plugue o cartao no PC e repita (o do console precisa sair dele)."
    fi
    exit 0
fi

[ -n "$ALVO" ] || erro "informe o dispositivo: $PROG /dev/sdX  (veja $PROG --listar)"

# --- imagem -------------------------------------------------------------
if [ -z "$IMAGEM" ]; then
    for ext in img img.xz; do
        cand="$ROOT/dist/$DEVICE/giochi-$DEVICE-sdcard.$ext"
        [ -f "$cand" ] && { IMAGEM=$cand; break; }
    done
fi
[ -n "$IMAGEM" ] && [ -f "$IMAGEM" ] || erro \
    "imagem nao encontrada para '$DEVICE'. Gere com: tools/docker-build.sh $DEVICE"
log "imagem: $IMAGEM ($(du -h "$IMAGEM" | cut -f1))"

# --- validacao do alvo --------------------------------------------------
# A ordem das recusas importa: cada uma corresponde a um jeito de destruir o
# computador de quem esta' rodando o script, e a mensagem tem de dizer qual
# foi. O teste de PARTICao vem antes do teste de "e' dispositivo de bloco":
# `/dev/sdd1` de um cartao vazio nao existe, e o erro sairia como "nao e'
# bloco" quando o problema e' outro (medido ao testar este script).
case "$ALVO" in
    /dev/*[0-9]|/dev/*p[0-9])
        erro "'$ALVO' parece PARTICAO. Use o disco inteiro (ex.: /dev/sdd)." ;;
esac
[ -b "$ALVO" ] || [ "$SIMULAR" = "1" ] || erro "'$ALVO' nao e' dispositivo de bloco"
nome_alvo=$(basename "$ALVO")

# Disco que contem a raiz: comparar o dispositivo-pai de /.
raiz_dev=$(findmnt -no SOURCE / 2>/dev/null || true)
if [ -n "$raiz_dev" ]; then
    raiz_pai=$(lsblk -no PKNAME "$raiz_dev" 2>/dev/null | head -n1 || true)
    [ -n "$raiz_pai" ] && [ "$raiz_pai" = "$nome_alvo" ] && \
        erro "'$ALVO' contem a raiz do sistema. Abortando."
fi

# Qualquer particao montada do alvo: no cartao isso e' o caso normal (o
# gerenciador de arquivos monta GIOCHI_USER ao inserir), e gravar por cima
# de um sistema de arquivos montado corrompe o cache do kernel.
montadas=$(lsblk -rno MOUNTPOINT "$ALVO" 2>/dev/null | grep -v '^$' || true)
if [ -n "$montadas" ]; then
    erro "particoes montadas em '$ALVO':
$(printf '  %s\n' "$montadas")
      Desmonte antes:  udisksctl unmount -b /dev/${nome_alvo}1   (ou 'umount')"
fi

tam_alvo=$(awk '{printf "%.1f", $1*512/1073741824}' "/sys/block/$nome_alvo/size" 2>/dev/null || echo 0)
tam_img=$(stat -c %s "$IMAGEM")
log "alvo: $ALVO (${tam_alvo} GiB), imagem: $((tam_img / 1024 / 1024)) MiB"

# Um alvo menor que a imagem so' pode terminar de um jeito: truncado, com o
# `dd` "bem-sucedido" ate' o ultimo setor. Recusar aqui evita descobrir isso
# no aparelho, com o SquashFS cortado.
setores_alvo=$(cat "/sys/block/$nome_alvo/size" 2>/dev/null || echo 0)
case "${setores_alvo:-0}" in
    ''|*[!0-9]*) setores_alvo=0 ;;
esac
# Leitor USB SEM cartao reporta 0 setores. Tentar gravar nisso "funciona" ate'
# o `dd` falhar no meio, e o usuario fica sem saber se o cartao foi tocado.
if [ "$setores_alvo" -eq 0 ]; then
    [ "$SIMULAR" = "1" ] && { log "AVISO: o alvo reporta 0 setores (leitor sem cartao?)."; exit 0; }
    erro "$ALVO reporta 0 setores: nao ha cartao no leitor (ou ele nao foi \
reconhecido). Insira o cartao e rode $PROG --listar."
fi
if [ $((setores_alvo * 512)) -lt "$tam_img" ]; then
    erro "o alvo tem $((setores_alvo * 512 / 1024 / 1024)) MiB e a imagem tem \
$((tam_img / 1024 / 1024)) MiB. Nao cabe: a gravacao sairia truncada."
fi

modelo=$(cat "/sys/block/$nome_alvo/device/model" 2>/dev/null || echo "?")
log "modelo do alvo: $modelo"

if [ "$SIMULAR" = "1" ]; then
    log "--sim: nada foi escrito. Os passos seriam:"
    log "  dd if=$IMAGEM of=$ALVO bs=4M conv=fsync"
    log "  verify-card.py $IMAGEM $ALVO"
    [ "$SEM_SSH" = "1" ] || log "  giochi-dev-mode.sh (reabrir SSH via USB)"
    exit 0
fi

# --- elevacao -----------------------------------------------------------
# Uma unica re-execucao com privilegio, ANTES de tocar no dispositivo, para
# que toda a saida a partir daqui seja de root. Licao do §3.82: testar o
# elevador antes de `exec` (o `exec` substitui o processo, e `exec cmd || x`
# nunca roda o `x`) e passar tudo por ARGUMENTO — `pkexec` limpa o ambiente.
SELF=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/$(basename -- "$0")
if [ "$(id -u)" != "0" ] && [ "${GIOCHI_FLASH_ELEVADO:-}" != "1" ]; then
    # Os argumentos originais, com o alvo e a imagem ja' resolvidos.
    set -- --device "$DEVICE" --imagem "$IMAGEM" "$ALVO"
    [ "$SEM_SSH" = "1" ] && set -- --sem-ssh "$@"
    [ "$SO_CHECAR" = "1" ] && set -- --checar "$@"
    # A chave resolvida vai por ARGUMENTO: `pkexec` limpa o ambiente, e um
    # `HOME=/home/charles` no `env` nao sobrevive a ele (§3.82).
    [ -n "$CHAVE_PUB" ] && set -- --key "$CHAVE_PUB" "$@"
    if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
        exec sudo -n env GIOCHI_FLASH_ELEVADO=1 "$SELF" "$@"
    fi
    if command -v pkexec >/dev/null 2>&1; then
        log "pedindo privilegio (pkexec) ..."
        exec env GIOCHI_FLASH_ELEVADO=1 pkexec "$SELF" "$@"
    fi
    erro "precisa de root para escrever em '$ALVO'.
      rode: sudo $SELF $*"
fi

# --- so' conferir -------------------------------------------------------
if [ "$SO_CHECAR" = "1" ]; then
    log "verificando o cartao (somente leitura) ..."
    python3 "$ROOT/tools/verify-card.py" "$IMAGEM" "$ALVO" || \
        erro "o cartao NAO confere com a imagem. Regrave antes de usar."
    exit 0
fi

# --- gravar -------------------------------------------------------------
log "gravando (isto apaga o cartao inteiro) ..."
# conv=fsync: o `dd` so' retorna depois que o kernel confirmou a escrita.
# Sem isso o comando "termina" com dados ainda no cache de pagina — foi a
# causa medida do EIO dentro do SquashFS (AGENTS.md §3.42).
dd if="$IMAGEM" of="$ALVO" bs=4M conv=fsync status=progress || {
    erro "dd falhou. O cartao pode estar em estado parcial: REGRAVE."
}
log "gravacao concluida; sincronizando ..."
sync

# --- verificar ----------------------------------------------------------
# Verificar faz parte de GRAVAR, nao e' um extra: um cartao pode gravar "sem
# erro" e entregar lixo ao aparelho.
log "verificando byte a byte ..."
python3 "$ROOT/tools/verify-card.py" "$IMAGEM" "$ALVO" || \
    erro "o cartao NAO confere com a imagem. Regrave (e, se repetir, troque o cartao)."

# --- Modo Desenvolvedor -------------------------------------------------
if [ "$SEM_SSH" = "1" ]; then
    log "Modo Desenvolvedor NAO reaberto (--sem-ssh)."
    log "pronto: pode tirar o cartao."
    exit 0
fi

# A particao /user acabou de ser criada VAZIA pelo genimage: nao ha
# settings.json nem a assinatura que o giochi-dev-mode.sh procura. Ele aceita
# um DIRETORIO, entao montamos a particao 4 (a /user) num ponto temporario,
# preparamos e desmontamos — deixando o cartao no estado que o `S10storage`
# espera encontrar no primeiro boot.
part_user=""
for cand in "${ALVO}4" "${ALVO}p4"; do
    [ -b "$cand" ] && { part_user=$cand; break; }
done

if [ -z "$part_user" ]; then
    log "AVISO: particao /user (4) nao apareceu; SSH via USB fica desligado."
    log "       Depois do primeiro boot, rode: tools/giochi-dev-mode.sh"
    exit 0
fi

# O kernel precisa reler a tabela de particoes: o `dd` acabou de reescreve-la.
partprobe "$ALVO" 2>/dev/null || blockdev --rereadpt "$ALVO" 2>/dev/null || \
    log "AVISO: nao consegui forcar releitura da tabela; tentando mesmo assim"

mnt=$(mktemp -d)
limpar() {
    mountpoint -q "$mnt" && umount "$mnt" 2>/dev/null || true
    rmdir "$mnt" 2>/dev/null || true
}
trap limpar EXIT INT TERM

if ! mount "$part_user" "$mnt" 2>/dev/null; then
    log "AVISO: nao consegui montar $part_user; SSH via USB fica desligado."
    log "       Depois do primeiro boot, rode: tools/giochi-dev-mode.sh"
    exit 0
fi
log "particao /user montada em $mnt; preparando o Modo Desenvolvedor ..."
# A chave vai por ARGUMENTO tambem aqui: chegando por `pkexec`, este processo
# ja' esta' com HOME=/root, e o giochi-dev-mode.sh procuraria a chave em
# /root/.ssh (foi assim que a primeira gravacao saiu sem SSH).
if [ -z "$CHAVE_PUB" ]; then
    log "AVISO: nenhuma chave publica encontrada (~/.ssh/id_*.pub)."
    log "       O cartao boota, mas sem SSH. Depois rode:"
    log "       tools/giochi-dev-mode.sh --key CAMINHO.pub"
elif ! sh "$ROOT/tools/giochi-dev-mode.sh" --key "$CHAVE_PUB" "$mnt"; then
    log "AVISO: giochi-dev-mode.sh falhou; o cartao boota, mas sem SSH."
fi

umount "$mnt"
sync
log "pronto: pode tirar o cartao do PC e colocar no console."
log "no console: ligue e, no PC, rode tools/giochi-usb-diag.sh --esperar 30"
exit 0
