# USB: MTP, Mass Storage e SSH

O console expõe **sempre** duas coisas no mesmo gadget USB composto:

1. **Rede** (`ecm.usb0` ou `rndis.usb0`) → SSH em `10.55.0.1`, em todos os modos.
2. **Dados**: MTP (padrão) **ou** Mass Storage.

Isso vale mesmo quando o modo de dados é Mass Storage: a rede não compete com ele.

```
PC ──USB──┬── usb0 (ECM/RNDIS)  10.55.0.1/24  →  dropbear :22 (só nesse IP)
          └── MTP (ffs.mtp)  ou  mass_storage.0 (partição /user)
```

## Conectar

```sh
# 1. cabo USB no console (porta de dados)
ip addr show usb0            # PC recebe 10.55.0.2 por DHCP
ssh root@10.55.0.1
```

Sem DHCP no PC: configure `10.55.0.2/24`, gateway `10.55.0.1`.

| Situação | Comportamento |
|---|---|
| Nenhum adaptador detectado (PC) | verifique o cabo e `dmesg` no console |
| `usb0` sem IP no PC | DHCP não respondeu → IP estático `10.55.0.2/24` |
| SSH recusa conexão | sem chave nem senha (padrão seguro) — ver abaixo |

## Autenticação SSH (padrão seguro)

Por padrão o SSH **só aceita chave**:

```sh
ssh-keygen -t ed25519
# copie a chave pública para /user/ssh/authorized_keys (via MTP, cartão ou SD)
#   /user/ssh/authorized_keys  →  formato OpenSSH, uma chave por linha
```

O `S62sshd` instala esse arquivo em `/etc/dropbear/authorized_keys` e sobe o
dropbear **apenas** em `10.55.0.1:22` (nunca em `0.0.0.0`). Chaves de host são
geradas na primeira vez em `/user/.ssh/` (persistem no cartão).

### Senha de root — somente imagem de teste

Para uma imagem de desenvolvimento, o `post-build.sh` pode gravar uma senha:

```sh
GIOCHI_TEST_PASSWORD=1 GIOCHI_ROOT_PASSWORD=giochi \
  GIOCHI_DEVICE=x55 tools/build-image.sh x55
```

> ⚠️ Isso cria `/etc/giochi/root_password` com a senha em texto plano. A imagem
> fica marcada para uso interno — **não distribua**. Sem essa variável o arquivo
> não existe e o SSH permanece chave-only.

## MTP

O `f_mtp` do kernel é só o transporte; quem responde o protocolo é o
**umtprd** (`/usr/sbin/umtprd`, pacote `giochi-mtpd`), sobre FunctionFS:

```
S50usb   → cria gadget: ffs.mtp + ecm.usb0, monta /dev/ffs-mtp, anexa ao UDC
S55usbnet→ usb0 = 10.55.0.1/24 + udhcpd
S60mtpd  → /usr/sbin/giochi-mtpd start  (umtprd -c /etc/umtprd.conf)
```

O storage exposto é `/user` (jogos, músicas, ports, `.giochi`). Config em
[`umtprd.conf`](../buildroot/package/giochi-mtpd/umtprd.conf) — nunca exponha `/`.

Se o kernel não tiver `CONFIG_USB_F_FS`, o helper cai automaticamente para Mass
Storage e registra o motivo.

### Trocar o modo em runtime

| Entrada | Ação |
|---|---|
| Configurações → **Sistema** → A | alterna MTP ↔ Mass Storage |
| `USB_MODE MTP` / `USB_MODE MASS` em `control.sock` | idem, direto |

O `giochi-init` reaplica rede e MTP depois da troca (`S55usbnet` + `S60mtpd`).
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
| RAM `dropbear` | ≤ 3 MB | só quando há chave/senha |
| RAM `udhcpd` | ≤ 1 MB | |
| Latência de detecção no PC | ≤ 3 s | §1.4 |

## Testes automatizados

`tests/integration/usbgadget_test.sh` roda o helper real contra um **configfs
falso** e verifica: `ffs.mtp` presente no modo MTP, rede sempre vinculada,
`mass_storage.0` no modo MASS, `UDC` esvaziado em `off`.
