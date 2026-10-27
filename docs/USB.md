# USB: MTP, Mass Storage e SSH (Modo Desenvolvedor)

O console expõe **sempre** duas coisas no mesmo gadget USB composto:

1. **Rede** (`ecm.usb0` ou `rndis.usb0`) — usada por **SSH via USB**.
2. **Dados**: MTP (padrão) **ou** Mass Storage.

Isso vale mesmo quando o modo de dados é Mass Storage: a rede não compete com ele.

```
PC ──USB──┬── usb0 (ECM/RNDIS)  10.55.0.1/24  →  dropbear :22 no console .2
          └── MTP (ffs.mtp)  ou  mass_storage.0 (partição /user)
```

## Modo Desenvolvedor (adendo §4.6.1)

O SSH via USB é **desativado por padrão**. A interface de rede existe no gadget,
mas sem endereço IP e sem daemon: **0 MB de RAM em repouso**.

Ligue em **Configurações → Sistema → Modo Desenvolvedor**. O launcher grava
`developer.ssh_usb_enabled` em `/user/settings.json` e envia `DEV_MODE ON` ao
`giochi-init`, que roda `S55usbnet` e `S62sshd`.

| Item | Valor |
|---|---|
| Console | `10.55.0.2/24` |
| PC | `10.55.0.1/24` — **estático**, não há DHCP no console |
| Porta SSH | `22`, **somente** em `10.55.0.2` (nunca `0.0.0.0`) |
| Orçamento | +1,5 MB SquashFS; 0 MB em repouso; ≤ 3 MB quando ativo |
| Fora de escopo v1.2 | SSH por Wi-Fi, chave exclusiva, túnel reverso |

## Conectar

```sh
# 1. cabo USB no console (porta de dados) e Modo Desenvolvedor ligado
sudo ip addr add 10.55.0.1/24 dev usb0    # Linux
sudo ip link set usb0 up
ssh root@10.55.0.2
```

No Windows o adaptador RNDIS aparece em "Adaptadores de Rede"; atribua
`10.55.0.1/255.255.255.0` manualmente.

| Situação | Comportamento |
|---|---|
| Nenhum adaptador detectado (PC) | verifique o cabo e `dmesg` no console |
| `usb0` sem IP no PC | esperado: configure `10.55.0.1/24` estático |
| SSH recusa conexão | Modo Desenvolvedor desligado, ou sem chave nem senha |
| `dmesg`: `failed to enable ep0out` | aparelho sem USB funcional (ver abaixo) |

## Autenticação SSH (senha OU chave)

O adendo permite **as duas formas**. O `S62sshd` sobe o dropbear apenas se pelo
menos uma estiver disponível; caso contrário registra o motivo e não escuta.

### Por chave (recomendado)

```sh
ssh-keygen -t ed25519
# copie a chave pública para /user/ssh/authorized_keys (via MTP, cartão ou SD)
#   /user/ssh/authorized_keys  →  formato OpenSSH, uma chave por linha
```

O script instala esse arquivo em `/etc/dropbear/authorized_keys`. Como a raiz é
SquashFS (somente leitura), `/etc/dropbear` é um **symlink para
`/user/.ssh/dropbear`** criado no `post-build.sh`. As chaves de host são geradas
na primeira ativação em `/user/.ssh/` e persistem.

### Por senha

A senha de root precisa existir na imagem:

```sh
GIOCHI_ROOT_PASSWORD=giochi tools/build-image.sh x55
```

> ⚠️ Isso cria `/etc/giochi/root_password` com a senha em texto plano. A imagem
> fica marcada para uso interno — **não distribua**. Também é possível definir a
> senha pelo console serial (`passwd`); sem senha definida o acesso é só por chave.

## MTP

O `f_mtp` do kernel é só o transporte; quem responde o protocolo é o
**umtprd** (`/usr/sbin/umtprd`, pacote `giochi-mtpd`), sobre FunctionFS:

```
S50usb   → cria gadget: ffs.mtp + ecm.usb0, monta /dev/ffs-mtp, anexa ao UDC
S55usbnet→ usb0 = 10.55.0.2/24 (só com Modo Desenvolvedor ativo)
S60mtpd  → /usr/sbin/giochi-mtpd start  (umtprd -c /etc/umtprd.conf)
```

O storage exposto é `/user` (jogos, músicas, ports, `.giochi`). Config em
[`umtprd.conf`](../buildroot/package/giochi-mtpd/umtprd.conf) — nunca exponha `/`.

Se o kernel não tiver `CONFIG_USB_F_FS`, o helper cai automaticamente para Mass
Storage e registra o motivo.

### Trocar o modo em runtime

| Entrada | Ação |
|---|---|
| Configurações → **Sistema** → Modo USB → A | alterna MTP ↔ Mass Storage |
| `USB_MODE MTP` / `USB_MODE MASS` em `control.sock` | idem, direto |
| Configurações → **Sistema** → Modo Desenvolvedor → A | liga/desliga SSH via USB |
| `DEV_MODE ON` / `DEV_MODE OFF` em `control.sock` | idem, direto |

O `giochi-init` reaplica rede (se o Modo Desenvolvedor estiver ativo) e MTP
depois da troca (`S55usbnet` + `S60mtpd`).
Mass Storage **desmonta `/user`** enquanto conectado — é uma exigência do
`f_mass_storage`; volte para MTP para continuar usando o console.

## Windows: trocar ECM por RNDIS

O padrão é ECM (Linux e macOS sem driver; Windows 10+ reconhece via USB
Composite). Se o Windows não criar a interface de rede:

```sh
# em buildroot/board/giochi/devices/x55.conf, antes do build:
GIOCHI_USB_NET=rndis
```

O `post-build.sh` grava isso em `/etc/giochi/usb-net.conf`.

## Aparelhos sem USB funcional

X35S e X35H não ancoram (`failed to enable ep0out`,
[hardware-e-drivers.md](./hardware-e-drivers.md) F12). Nessas imagens o
`post-build.sh` cria `/etc/giochi/usb_gadget_disabled`, e `S50usb`, `S55usbnet`,
`S60mtpd` e `S62sshd` saem silenciosamente sem erro repetido.

## Diagnóstico no console

```sh
cat /sys/class/udc/                       # controlador presente?
ls /sys/kernel/config/usb_gadget/giochi/  # gadget criado
cat /sys/kernel/config/usb_gadget/giochi/UDC
mount | grep ffs                          # FunctionFS montado
/usr/sbin/giochi-mtpd status              # running <pid> | stopped
ip addr show usb0
logread | grep -E 'giochi-usb|giochi-mtpd|S55|S62'
```

## Orçamento

| Item | Meta | Nota |
|---|---|---|
| RAM `umtprd` | ≤ 2 MB | §4.6 |
| RAM `dropbear` | ≤ 3 MB | só com Modo Desenvolvedor ativo |
| RAM em repouso (rede USB) | 0 MB | sem daemon nem IP até ativar |
| SquashFS | +1,5 MB | dropbear + scripts (§4.6.1) |
| Latência de detecção no PC | ≤ 3 s | §1.4 |

## Testes automatizados

`tests/integration/usbgadget_test.sh` roda o helper real contra um **configfs
falso** e verifica: `ffs.mtp` presente no modo MTP, rede sempre vinculada,
`mass_storage.0` no modo MASS, `UDC` esvaziado em `off`.

`tests/integration/devmode_test.sh` roda `S55usbnet`/`S62sshd` com `PATH` falso e
verifica o contrato do adendo: desligado por padrão (sem IP, sem daemon), console
em `10.55.0.2/24`, dropbear só em `10.55.0.2:22`, recusa subir sem chave/senha e
derruba pelo pid file no `stop`.
