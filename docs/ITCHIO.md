# itch.io como fonte de jogos para a Loja: análise de viabilidade

**Estado: avaliado e NÃO adotado como catálogo da Loja** (2026-10-31). Este
documento guarda as medições para que a próxima sessão não refaça a pesquisa e
para que a decisão seja reversível com números na mão.

A pergunta era: *usar o itch.io para adquirir jogos grátis e pôr no app da
Loja*.

Resposta curta: **o itch.io não serve como catálogo**, e o motivo não é
licença, preço nem ferramenta — é que o **modelo de dados do itch.io não tem a
palavra "ARM"**. A Loja não teria como perguntar "quais destes rodam neste
aparelho". O que existe de aproveitável está em §6.

| Dimensão | Veredito |
|---|---|
| Baixar jogos do itch.io no aparelho (cliente) | **viável** — `butler` tem build arm64 (§4) |
| Pré-instalar jogos do itch.io na imagem | **não** — ToS e licença de cada jogo (§2) |
| Filtrar por arquitetura pelo catálogo | **impossível** — só existe `all/386/amd64` (§3) |
| Rodar jogos x86 via emulação | **proibido** pela spec (ADR-13) |
| Oferta de jogos ARM64 nativos | **~18** em 1,7 milhão (§3.3) |
| **Compilar do fonte para aarch64** | **sim — e é o único caminho que funciona** (§3.4) |
| `giochi-store` existe hoje | **não** — é app a escrever do zero (§5) |

**A pergunta que muda o veredito:** *"e se o itch.io liberar o código fonte, não
dá para compilar em arm64?"* Dá — e essa é a saída. Mas §3 vale só para
**binários**; §3.4 mostra que, com fonte, a arquitetura deixa de ser o problema
e o gargalo vira **motor + licença**.

Contexto de hardware que vale para tudo abaixo: alvo é **aarch64 (Cortex-A55)**,
RK3566 com 2 GB de RAM / 128 MB de VRAM, RK3326 com 1 GB / 64 MB, orçamento de
UI de 700 MB e sem desktop, sem navegador, sem systemd.

---

## 1. O que o itch.io é (e o que não é)

Não é um repositório de pacotes. É uma **loja com contrato de uso** (ToS), onde
cada título tem seu próprio licenciamento — o padrão da indústria é *todos os
direitos reservados*, mesmo quando o download é grátis.

"Grátis" no itch.io significa **custo zero para baixar**, não licença livre. A
maioria dos jogos gratuitos de lá não pode ser redistribuída, empacotada nem
incluída em imagem de aparelho. Isso é o oposto da regra que o repositório já
aplicou ao Shattered Pixel Dungeon (ver
[Jogos/Shattered Pixel Dungeon/Shattered Pixel Dungeon.md](<../Jogos/Shattered Pixel Dungeon/Shattered Pixel Dungeon.md>)).

## 2. Os três modelos possíveis — e qual é legal

### Modelo A — pré-instalar na imagem (o que "pôr no app da Loja" costuma querer dizer)

**Não.** Os Termos de Uso do itch.io dão ao usuário uma licença

> "to access the content and to use, reproduce, distribute, display and perform
> such content **as permitted through the functionality of the Service**"

A cláusula que manda é *"as permitted through the functionality of the
Service"*: o usuário pode reproduzir e distribuir **através da
funcionalidade do serviço** — baixar no próprio aparelho, pelo próprio cliente.
Copiar arquivos para dentro de uma imagem de aparelho e distribuí-la é
**fora** dessa funcionalidade. O mesmo documento diz que "Publishers retain all
ownership rights to the submitted content": quem pode autorizar redistribuição
é o desenvolvedor de cada jogo, um por um.

Somando: pré-instalar exigiria permissão explícita de **cada** autor. Isso não
escala e não é o que o itch.io oferece.

### Modelo B — cliente que baixa no aparelho (legal, mas não resolve arquitetura)

O app da Loja se autentica com a conta do próprio usuário e baixa para `/user`.
Isso **é** a funcionalidade do serviço, e é exatamente o que o app oficial do
itch.io faz. É a única forma legítima de "usar o itch.io" sem pedir licença a
ninguém.

Continua esbarrando em §3: o catálogo não sabe dizer o que roda em ARM.

### Modelo C — lista curada com permissão

Escolher jogo a jogo, pedir autorização e mantê-los nós mesmos. Oneroso, mas é
o caminho de quem quer um catálogo curado. **Não é o itch.io fazendo o
trabalho** — somos nós.

## 3. O dado que decide: o itch.io não tem o conceito de ARM

Esta é a medição central, e ela fecha a questão. O modelo de dados oficial do
itch.io (biblioteca `go-itchio`, usada pelo `butler` e pelo app oficial) define
arquitetura assim:

```go
// Platforms describes which OS/architectures a game or upload is compatible with.
type Platforms struct {
	Windows Architectures `json:"windows,omitempty"`
	Linux   Architectures `json:"linux,omitempty"`
	OSX     Architectures `json:"osx,omitempty"`
}

// Architectures describes a set of processor architectures (mostly 32-bit vs 64-bit)
type Architectures string

const (
	ArchitecturesAll  Architectures = "all"
	Architectures386  Architectures = "386"
	ArchitecturesAmd64 Architectures = "amd64"
)
```

**Os únicos valores são `all`, `386` e `amd64`.** Não existe `arm64`, não existe
`aarch64`. O itch.io é um mundo x86 por modelagem: um desenvolvedor que compile
para ARM só pode marcar "Linux" — a arquitetura é inexpressável.

Consequência prática para a Loja: **não há como filtrar por arquitetura antes do
download.** O app teria que baixar, abrir o binário e ler o cabeçalho ELF
(`e_machine == 0xB7` é `EM_AARCH64`) para descobrir. Baixar para descobrir que
não serve, num aparelho de 2 GB com Wi-Fi, é um custo proibitivo em escala.

Confirmação na interface: na página de "top free games", os ícones por jogo são
`windows8`, `apple`, `tux`, `android` — nenhum de arquitetura. **Há 14 botões
"Play in browser"** na amostra, o que é a única forma de "rodar em qualquer
lugar" oferecida.

### 3.1 Amostra medida (36 jogos únicos, "top free games", 2026-10-31)

| Rótulo | Jogos | Proporção |
|---|---|---|
| Windows | 33 | 92% |
| macOS | 26 | 72% |
| Linux (`tux`) | 23 | 64% |
| "Play in browser" (HTML5) | 14 | 39% |
| Android (APK) | 13 | 36% |

Ou seja: 64% têm build de Linux — e "Linux" no itch.io significa, na prática,
x86_64 (§3). 39% só rodam em navegador.

### 3.2 Por que HTML5 e APK também não salvam

- **HTML5 (39%)**: exigiria um motor de navegador completo (WebKit/CEF) com
  WebGL2. Isso é maior que o orçamento inteiro da UI (700 MB) e não existe
  "navegador leve com WebGL" no Buildroot 2024.02. Fora.
- **Android/APK (36%)**: frequentemente são `arm64-v8a`, o que **seria** o alvo
  certo — mas exigiria runtime Android (Waydroid/container + binder). Num
  aparelho de 1–2 GB, sem systemd, com boot de 3,9 s como meta, é incompatível
  com a premissa do sistema.

### 3.3 Quanta oferta ARM64 nativa existe

Busca textual no catálogo inteiro do itch.io (2026-10-31):

| Termo | Resultados |
|---|---|
| `arm64` | **18** |
| `linux` | ≥ 54 (satura a primeira página) |
| `godot` | ≥ 54 (satura a primeira página) |
| `pixel art` | ≥ 54 (satura a primeira página) |
| `aarch64` | 0 |

O catálogo tem **1.718.158** títulos na classificação "top free games". Dezoito
resultados para `arm64` é ruído estatístico — e ainda são jogos que apenas
*citam* arm64 na descrição, não necessariamente que publiquem um binário ARM.

**Isto é a resposta dura:** mesmo que a Loja fosse perfeita, o estoque de jogos
ARM64 nativos descobríveis no itch.io é da ordem de algumas dezenas.

### 3.4 Se o desenvolvedor publica o código, compilar em arm64 resolve — mas o sujeito não é o itch.io

Este é o contraponto honesto ao §3, e ele é correto: **arquitetura não é
obstáculo quando existe fonte.** O que o §3 prova é mais estreito do que parece
— ele vale para **binários publicados**, não para o jogo.

Três coisas precisam ser separadas:

1. **O itch.io não é dono do código dos jogos.** Ele hospeda. Quem pode liberar
   fonte é cada desenvolvedor, um por um. Não existe um botão "o itch.io libera
   o código". (O que o itch.io tem de aberto é o **cliente** dele,
   `itchio/itch`, MIT — e o `butler`, também MIT. Isso não alcança os jogos.)
2. **Muita gente de fato publica fonte.** A tag `tag-open-source` existe e a
   lista é séria: Mindustry, Battle for Wesnoth, SuperTuxKart, Friday Night
   Funkin' e centenas de outros.
3. **Mas publicar fonte não é o mesmo que poder redistribuir.** É §2 de novo:
   fonte visível sob GPL não autoriza pré-instalar na imagem assinada.

#### O que a fonte muda (medido nos jogos em destaque)

| Jogo | Licença | Motor / linguagem | Compilar p/ aarch64 é… |
|---|---|---|---|
| Mindustry | **GPL-3.0** | Java/libGDX | portar JVM + libGDX |
| Battle for Wesnoth | **GPL-2.0** | C++ | portar ~30 dependências |
| SuperTuxKart | NOASSERTION | C++/Irlicht | portar engine + assets GPL |
| Friday Night Funkin' (Psych) | **Apache-2.0** | Haxe/HaxeFlixel | portar HashLink/Haxe |
| **Jogo em Godot** | depende do autor | **Godot** | **re-exportar (minutos)** |

Repare no padrão: os projetos abertos que aparecem no itch.io são justamente os
**grandes**, escritos sobre engines pesadas, e quase todos **GPL**. Para esses,
"compilar em arm64" não é um `make ARCH=arm64` — é portar engine e dependências
para musl/aarch64, o mesmo trabalho de port que o projeto já faz à mão. E a
licença volta a bater no §6 (anti-tivoization).

#### A exceção que vale ouro: Godot

Se o jogo é Godot e o autor publica o projeto, **não há problema de
arquitetura**: o Godot já tem template de exportação aarch64. Verificado
baixando o template oficial:

```console
$ ... Godot_v4.3-stable_linux.arm64.zip       47,1 MB (103,6 MB descomprimido)
magic: b'\x7fELF'   EI_CLASS: ELF64
e_machine = 0xb7      -> AArch64  (confirmado)
```

**Porém — e este é o elo que faltava:** esse template é **glibc**:

```
GLIBC_2.17  2.18  2.24  2.25  2.27  2.28     (libc.so.6)
ld-musl: ausente      libc.musl: ausente
```

Nosso rootfs é **musl** (`BR2_TOOLCHAIN_BUILDROOT_MUSL=y`, ADR-03). Então o
template oficial **não roda** na imagem. Isso não invalida o caminho: é
exatamente o que o ADR-03 já manda fazer — *"recompilação obrigatória de todos
os binários de runtime a partir do fonte"* — e a própria spec já anota o item:

> `| musl × Godot export template | Recompilação obrigatória (ADR-03) |`

Consequência prática importante: **recompilar o template do Godot contra musl
destrava duas coisas de uma vez** — os jogos do itch.io publicados como projeto
Godot **e** os três jogos padrão do repositório (Arcade Volleyball, Spacewar,
Colossal Cave), que hoje instalam projeto em vez de binário pelo mesmo motivo.
É o item de maior alavancagem de todo esse eixo.

#### Licença para o caso Godot

Mesmo com fonte e com template musl, pré-instalar exige licença permissiva.
Godot publica sob MIT, mas **o jogo tem licença própria** — alguns são MIT/CC0
(e aí servem), muitos não declaram nada.

#### Veredito do caminho "fonte"

| Pergunta | Resposta |
|---|---|
| Compilar fonte em arm64 resolve arquitetura? | **sim** |
| O itch.io pode "liberar" o código? | **não** — é de cada autor |
| Existe oferta real de fonte? | **sim**, mas concentrada em engines pesadas e GPL |
| Serve para a Loja **sem curadoria**? | **não** — cada caso exige licença e trabalho de port |
| Serve para **curadoria** de jogos Godot? | **sim** — e é o caminho mais curto |

Ou seja: o modelo deixa de ser "catálogo automático" e passa a ser **coleção
curada**, jogo a jogo, com duas perguntas por título — *é Godot?* e *a licença
permite distribuir?* Onde as duas respostas são "sim", o trabalho é minutos.
Onde é "não", o trabalho é um port.

## 4. A ferramenta oficial funciona no aparelho (isto é bom)

Nem tudo são obstáculos: o `butler`, CLI oficial do itch.io, é **MIT** e publica
build ARM64:

| Asset (v15.32.0) | Tamanho |
|---|---|
| `butler-linux-arm64.zip` | **9,3 MB** |
| `butler-linux-amd64.zip` | 10,1 MB |

E o login **não precisa de navegador** — o que é essencial num console:

| Variável | Para quê |
|---|---|
| `BUTLER_API_KEY` | usar uma chave de API do itch.io, sem `butler login` |
| `BUTLER_MANUAL_OAUTH=1` | fluxo OAuth de copiar-e-colar, para login headless/remoto |

O caminho de UX que isso permite: o usuário gera a chave em
`itch.io/user/settings/api-keys` no PC e digita/cola no aparelho (ou leva por
USB), e a Loja passa a baixar em nome dele. É viável — mas continua esbarrando
em §3: baixa-se para descobrir que é x86.

**Nota de licença:** `butler` ser MIT não torna os jogos que ele baixa
livres. A ferramenta é livre; o catálogo não é.

## 5. A Loja não existe — e o contrato atual não aceita jogo nativo

`docs/APPS.md` registra que `giochi-store` **não existe**: `layout.json` aponta
`APP:giochi-store` e o `giochi-init` responde `ERR launch_failed`. Também não há
`runtime` para binário nativo:

- [schemas/default-games.schema.json](../schemas/default-games.schema.json):
  `runtime` ∈ `godot` | `retroarch` | `proton`;
- [schemas/metadata.schema.json](../schemas/metadata.schema.json): o
  `src/launcher/metadata.cpp` recusa o que não estiver nesse conjunto.

Então "pôr jogos do itch.io na Loja" hoje significa, no mínimo:

1. escrever o `giochi-store` do zero (UI, rede, download, verificação, espaço
   em `/user`, retomada, cancelamento);
2. acrescentar `runtime: "native"` na spec, nos dois schemas e no Launcher;
3. decidir o que fazer com binários que **não** são `.giochi` assinados — o
   modelo atual é pacote assinado Ed25519 (ADR-01), e um ELF aleatório baixado
   da internet não é isso;
4. resolver arquitetura pós-download (§3).

Nada disso é bloqueio de viabilidade por si só; é escopo. O bloqueio de
viabilidade é §3.

## 6. O que **é** viável (e é o que eu recomendo avaliar em seguida)

> Comparação medida de **todas** as fontes candidatas (PortMaster, Alpine aarch64,
> TIC-80, Buildroot, itch.io): ver [STORE-SOURCES.md](STORE-SOURCES.md).


O problema real do projeto não é "de onde baixar jogos", é **"de onde vêm jogos
ARM64 nativos que caibam em 700 MB"**. Três fontes reais, em ordem de
adequação:

### 6.1 PortMaster — já planejado no próprio repositório

A aba Ports do Launcher aponta `APP:portmaster`, e `docs/APPS.md` registra que
ele também não existe. O `PortsMaster/PortMaster-New` é **MIT**, tem 384
estrelas e existe exatamente para levar jogos a portáteis ARM (ArkOS, JELOS,
muOS e afins): os ports são **recompilados para ARM por padrão**, que é
precisamente o que falta no itch.io.

É a fonte mais alinhada que existe hoje, e o repositório **já a previu**. Vale
uma análise própria, com o mesmo rigor desta.

### 6.2 Projetos Godot publicados como fonte

Uma fração dos jogos do itch.io publica o **projeto Godot**, não só o binário.
Nesse caso não há problema de arquitetura: re-exportar para aarch64 produz um
binário nativo — e o runtime já é o do sistema (ADR-07). Exige o export template
musl que os três jogos padrão também esperam (ADR-03), e exige **licença que
permita recompilar e redistribuir**.

Detalhado em **§3.4**: o template oficial aarch64 existe e é ELF64
`e_machine=0xB7`, mas é **glibc**; o nosso rootfs é **musl**. Recompilar o
template contra musl é o item de maior alavancagem deste eixo, porque resolve
este caso e os três jogos padrão ao mesmo tempo.

### 6.3 Portes próprios

É o que o repositório já faz com Arcade Volleyball, Spacewar e Colossal Cave:
código escrito aqui, licença nossa, orçamento medido, bench próprio. Escala
mal, mas é o único caminho com controle total.

---

## 7. Recomendação

1. **Não** usar o itch.io como catálogo da Loja. O modelo de dados não expressa
   ARM, e a oferta ARM64 descobrível é de dezenas de títulos.
2. **Não** pré-instalar jogos do itch.io na imagem — a licença de cada jogo
   teria que ser negociada uma a uma, e os ToS não cobrem redistribuição fora
   do serviço.
3. **Sim** considerar `butler` (arm64, MIT) se algum dia houver um caso de uso
   pontual de download em nome do usuário — mas por decisão explícita, não como
   base da Loja.
4. **Priorizar a alavancagem máxima: recompilar o template do Godot contra
   musl** (ADR-03). Isso destrava de uma vez os jogos do itch.io publicados como
   projeto Godot **e** os três jogos padrão do repositório, que hoje instalam
   projeto em vez de binário pelo mesmo motivo. Sem isso, "compilar do fonte em
   arm64" não vira binário executável no aparelho (§3.4).
5. **Priorizar PortMaster** como fonte da Loja/Ports. É ARM por construção, é
   MIT, e o repositório já reservou a aba para ele.
6. Se o itch.io entrar, é como **coleção curada**, não catálogo: por jogo,
   responder *"é Godot?"* e *"a licença permite distribuir?"*. Duas respostas
   "sim" = minutos de trabalho; qualquer "não" = um port.
7. Enquanto o `giochi-store` não existir, a aba Loja continua mostrando
   `ERR launch_failed` com log claro — comportamento correto e já testado.

---

## 8. Como reconferir

```sh
# o modelo de dados do itch.io nao tem ARM (a prova)
curl -s https://raw.githubusercontent.com/itchio/go-itchio/master/types.go \
  | grep -A20 "type Platforms struct"
curl -s https://raw.githubusercontent.com/itchio/go-itchio/master/types.go \
  | grep -A8 "type Architectures string"

# butler: licenca e builds por arquitetura
curl -s https://api.github.com/repos/itchio/butler | grep spdx_id
curl -s "https://api.github.com/repos/itchio/butler/releases?per_page=1" \
  | grep -o '"name": "butler[^"]*"'

# oferta de ARM no catalogo
curl -sL "https://itch.io/search?q=arm64" | grep -o 'data-game_id="[0-9]*"' | sort -u | wc -l

# ToS: a clausula "as permitted through the functionality of the Service"
curl -s https://itch.io/docs/legal/terms | grep -o "as permitted through the functionality[^.]*"
```

**Cuidado ao sondar a API do itch.io:** erro de autenticação volta com
**HTTP 200** e corpo `{"errors":["invalid key"]}` (§ armadilha abaixo). Quem
verificar só o código de status conclui que está autenticado.
