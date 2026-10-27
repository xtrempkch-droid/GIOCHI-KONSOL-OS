# Componentes do sistema (inventário)

Confronto entre a **lista consolidada de aplicativos da v1.2** e o que existe
neste repositório. Serve para saber, em uma tela, o que roda no console, o que
roda no PC, o que é artefato de configuração — e **o que falta**.

Legenda de estado:

- ✅ **implementado** e coberto por teste (ou verificado por execução)
- 🟡 **parcial** — a estrutura existe, falta uma parte funcional
- ⛔ **não implementado** — referenciado pela spec, sem código
- 🔒 **reservado** — fora do escopo v1.2, por decisão da spec

Especificação: [architecture-v1.2.md](./architecture-v1.2.md) (+ [adendo 4.6.1](<Registro na Spec (v1.2 — Adendo à Seção 4.6).md>)).
Ordem de trabalho: [roadmap.md](./roadmap.md).

---

## No dispositivo (console)

| # | Componente | Tipo | Onde | Estado |
|---|---|---|---|---|
| 1 | `giochi-init` | daemon C | [src/init](../src/init) | ✅ supervisor, `control.sock`, FIFO, hotkey, `device_profile.json`; ciclo de vida coberto por `lifecycle_test.sh` |
| 2 | `giochi-launcher` | C++17 + SDL2 | [src/launcher](../src/launcher) | 🟡 motor de layout, tema, `settings.json`, overlay, biblioteca prontos; **desenho dos widgets é esboço** (`TODO(v1.2)`) |
| 3 | `giochi-musicd` | daemon C + ALSA | [src/musicd](../src/musicd) | 🟡 socket e protocolo §4.8.4 prontos; **decodificação PCM pendente** |
| 4 | `giochi-store` | app externo | — | ⛔ **não existe**. `layout.json` aponta `APP:giochi-store` e o `giochi-init` responde `ERR launch_failed` com log claro |
| 5 | `portmaster` | app externo | — | ⛔ **não existe**. Mesmo caso da Loja (`APP:portmaster`) |
| 6 | `giochi-thumbgen` | utilitário C | [src/thumbgen](../src/thumbgen) | 🟡 gera o `.rgba` 152×85 e o placeholder; **extração de `thumb.png`/`capa.png` do tar pendente** (precisa de libarchive) |
| 7 | `dropbear` | daemon SSH | [scripts/init.d/S62sshd](../scripts/init.d/S62sshd) | ✅ Modo Desenvolvedor, desligado por padrão, só em `10.55.0.2:22`, senha ou chave |
| 8 | Scripts de init | shell (BusyBox) | [scripts/init.d](../scripts/init.d) | ✅ `S05update`, `S10storage`, `S20graphics`, `S30swap`, `S40dongle`, `S50usb`, `S55usbnet`, `S62sshd`, `S99launcher` |
| 9 | RetroArch | app externo | — | 🔒 fase 2 (§4.2 `runtime`); o schema e o `metadata.cpp` já aceitam/rejeitam |
| 10 | Proton | stub `#ifdef` | [src/launcher/metadata.cpp](../src/launcher/metadata.cpp) | ✅ desativado por padrão (`GIOCHI_ENABLE_PROTON=0`); recusa `runtime=proton` |

Também no console, embora não estejam na lista de "apps":

| Componente | Onde | Estado |
|---|---|---|
| `giochi-verify` (§4.3) | [tools/giochi-verify.c](../tools/giochi-verify.c) | ✅ Ed25519 + SHA-256; **recusa** o update se o binário não tiver suporte |
| `giochi-extract` (§4.2) | [tools/giochi-extract.c](../tools/giochi-extract.c) | ✅ ustar embutido **e** libarchive |
| `umtprd` / `giochi-mtpd` (§4.6) | [buildroot/package/giochi-mtpd](../buildroot/package/giochi-mtpd) | ✅ responde MTP sobre FunctionFS |
| `giochi-usb-mode` | [scripts/giochi-usb-mode](../scripts/giochi-usb-mode) | ✅ gadget composto (rede sempre + `ffs.mtp` ou `mass_storage.0`) |

## No PC

| # | Componente | Tipo | Onde | Estado |
|---|---|---|---|---|
| 11 | `giochi-forge` | app desktop (Python) | [src/forge](../src/forge) | 🟡 `profile`, `check` (classificação §4.5.3), `build` (empacota + assina) e `keys` prontos; **faltam**: detectar o console via USB, transferir o `.giochi` e compilar Godot → ARM64 |

Ferramentas de PC que **não** vão para a imagem (ver
[AGENTS.md §3.12](../AGENTS.md) e `tools/check-image-contents.sh`):
`giochi-pack.sh`, `giochi-sign.sh`, `giochi-keys.sh`, `tools/*.py`
(`make-bootlogo`, `make-screenshots`, `check-*`, `build-image`,
`gen-defconfigs`) e os scripts de stress/bench.

## Artefatos de configuração

| Artefato | Fica em | Estado |
|---|---|---|
| `layout.json` (schema 1.6) | SquashFS `/usr/share/giochi/` | ✅ [assets/layout.json](../assets/layout.json) + override por aparelho em `assets/devices/<id>/` |
| `home.json` | idem | ✅ três regiões (Continuar, Novidades, Recentes) |
| `library.json` | idem | ✅ grid 4×3, tile 152×85, título 20, scrollbar 4 |
| `music.json` | idem | ✅ now playing 120×120, progresso, controles, fila |
| `theme.json` | idem | ✅ tema `Netflix Dark` |
| `defaults.json` | idem | ✅ fallback de `settings.json` (inclui `developer`) |
| `settings.json` | `/user/` (rw) | ✅ escrito **só** pelo Launcher (§4.9.5) |
| `device_profile.json` | `/user/.giochi/` (rw) | ✅ regenerado a cada boot pelo `giochi-init` |
| `buttons.map` | SquashFS `/etc/giochi/` | ✅ canônico, com seção `[default]` e por aparelho |
| `news.json` | `/user/.cache/` (rw) | 🟡 o Launcher lê com fallback de dicas compiladas; **ninguém escreve** — depende da Loja (⛔) |
| `forge.json` | raiz do projeto Godot | ✅ lido pelo `giochi-forge` + [schema](../schemas/forge.schema.json) |
| `metadata.json` | dentro do `.giochi` | ✅ [schema](../schemas/metadata.schema.json), validado pelo pack e pelo Launcher |

> **Divergência de caminho:** a lista consolidada diz `layout.json` em
> `/etc/giochi/layouts/default/`. O código usa `/usr/share/giochi/` (padrão
> FHS para dado somente-leitura, e é o que o Launcher e o `--asset-dir`
> esperam). A [spec §4.1.3](./architecture-v1.2.md) não fixa o caminho. Ficamos
> com `/usr/share/giochi/`; se você preferir `/etc/giochi/layouts/`, é trocar
> `GIOCHI_ASSETDIR` + `post-build.sh` + o default em `main.cpp`.

---

## Lacunas, em ordem de impacto

1. **`giochi-store` e `portmaster` não existem.** Abas Loja e Ports do Launcher
   apontam para binários ausentes. Correção já aplicada no `giochi-init`: falha
   com `ERR launch_failed` e log nomeando o programa e o caminho — antes era um
   `exit 127` silencioso. Falta o app em si (§4.1.3, §4.7).
2. **`news.json` sem produtor.** A Home mostra as dicas compiladas; a Loja
   deveria alimentar o arquivo.
3. **`giochi-forge` sem o lado "físico"**: detecção por USB, leitura de
   `device_profile.json` do aparelho, `scp`/MTP e o export do Godot. Hoje ele
   classifica e empacota, mas o perfil vem de arquivo ou de `--device`.
4. **`giochi-thumbgen` sem extração de imagem** do pacote (usa placeholder
   quando não há `.rgba` pronto).
5. **RetroArch** reservado (fase 2) — correto pela spec.
6. **`docs/architecture-v1.2.md` não tem §4.5.4.** A lista consolidada cita
   "§4.5.4 (integração com `giochi-forge`)", incluindo transferência por `scp`
   e `plink`/`pscp` no Windows. Esse texto não está na v1.2 deste repositório —
   ou é de uma revisão posterior, ou do adendo. **Decisão pendente**: se a
   transferência por SSH faz parte do escopo, ela precisa entrar na spec e no
   `giochi-forge`; hoje o §4.6.1 diz explicitamente que o Modo Desenvolvedor é
   para diagnóstico, não para o fluxo de publicação.
