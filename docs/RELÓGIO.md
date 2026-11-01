# Relógio e data

O aparelho **não tem relógio de hardware** (RTC): `grep -ci rtc` no `dmesg`
completo do boot da X55 devolve **zero**, e não existe `/dev/rtc0`. O RK817
tem RTC, mas sem bateria de backup e sem `CONFIG_RTC_DRV_RK808` no
`linux.fragment` ele nunca aparece.

## O que acontece sem RTC

O kernel acorda com uma época fixa. Medido, console contra PC no mesmo
instante:

```
console: Sat Aug  5 09:03:57 UTC 2017   (epoch 1501923837)
PC:      dom 01 nov 2026 15:10          (epoch 1793556647)
```

E isso não é cosmético:

| Efeito | Por quê |
|---|---|
| `resize2fs` recusa o crescimento de `/user` | `s_lastcheck (2017) < s_mtime (2026)`, gravado pelo PC — AGENTS.md §3.111 |
| Certificado TLS da Loja "ainda não válido" | erro de **data**, não de rede |
| Logs com data de 9 anos atrás | comparar dois boots deixa de fazer sentido |

## Como o sistema se acerta sozinho

`S01relogio` (roda antes de tudo no `rcS`) restaura a hora em três degraus, do
mais preciso ao mais fraco:

1. **Pedido do usuário** — `/user/.giochi/relogio.pedido` (ver abaixo);
2. **Época salva** — `/user/.giochi/relogio`, gravada pelo `rcK` no
   desligamento anterior;
3. **Data do cartão** — o maior `mtime` dos arquivos da partição de boot,
   escritos pelo PC ao gerar a imagem (vale no primeiro boot).

Todas as fontes passam pela mesma faixa de sanidade (2020 a 2100). Época fora
da faixa é **recusada**, não ajustada: salvar um relógio errado perpetuaria o
erro.

## Quando a bateria do RTC é trocada

A época salva fica velha (é a hora do último desligamento antes da troca) e o
relógio volta a errar — este é o cenário que o mantenedor apontou. Há dois
caminhos para corrigir:

### 1. Pelo MTP (já funciona)

O MTP expõe `/user` ao PC. Crie um arquivo de texto chamado
`relogio.pedido` dentro da pasta `.giochi` (a pasta oculta `.giochi` aparece
na raiz do dispositivo MTP) com **um** destes conteúdos:

```
1793556647
```

(época — segundos desde 1970) **ou**

```
2026-11-01 15:30
```

(data legível, aceita `YYYY-MM-DD HH:MM[:SS]`).

Desligue e ligue o aparelho: o `S01relogio` aplica o pedido, atualiza a fonte
persistente e **remove** o arquivo. Se o pedido for inválido (texto que não é
data, época fora de 2020..2100, resultado fora da faixa), ele **fica** no
lugar e o boot registra `pedido de data invalido` — o aviso a cada boot é
proposital: lembra que existe um pedido pendente para corrigir.

### 2. Pelo `SET_DATE` no control.sock (SSH hoje, Loja no futuro)

O `giochi-init` aceita `SET_DATE <epoch>` no `control.sock`
([APP-SDK.md](APP-SDK.md)). Ele valida a faixa, acerta o relógio e persiste em
`/user/.giochi/relogio` — a mesma fonte do `rcK`.

- **Por SSH** (Modo Desenvolvedor):

  ```sh
  tools/giochi-ctl.py data          # manda a hora do PC, sem calcular nada
  # ou manualmente:
  tools/giochi-ctl.py ctl "SET_DATE $(date +%s)"
  ```

- **No console**, direto: `giochi-ctl SET_DATE $(date +%s)`.

- **Pelo app da Loja (futuro)**: é este mesmo comando que a Loja vai enviar
  quando detectar data implausível — o canal já existe e é coberto por teste
  de contrato; falta só a UI da Loja.

## Limite honesto

Sem RTC não existe fonte de verdade **entre** boots: a melhor hora disponível
é a do último desligamento, e o erro máximo é o tempo em que o aparelho ficou
desligado. No primeiro boot após gravar o cartão vale a data do cartão (a hora
em que a imagem foi gerada no PC). Se um dia a placa ganhar RTC com bateria, o
`S01relogio` passa a `hwclock -s` e os arquivos viram só fallback.

## Verificação

- Suíte `relogio` (`tests/test_relogio.py`, 21 testes): restauração, faixa de
  sanidade, primeiro boot, pedido via MTP (época, data legível, CRLF, lixo,
  fora de faixa, ano implausível, prioridade sobre a época salva) e a
  gravação pelo `rcK`.
- Suíte `control.sock` (`tests/test_control_protocol.py`): contrato do
  `SET_DATE` (resposta exata e persistência), com `GIOCHI_DRY_RUN=1` — o
  relógio do host nunca é tocado nos testes.
- Guardas testadas ao contrário: sem a validação de ano o teste reprova 2;
  sem remover o pedido aplicado, 4; sem a faixa da época, 2.
