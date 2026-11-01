# O Launcher como app

A partir de 2026-11-01 o Launcher pode ser **atualizado sem regravar o
sistema**: o binário da UI passou a ser resolvido em tempo de execução, com a
imagem como fallback.

## Por quê

O rootfs é SquashFS assinado (ADR-05): mexer no Launcher significava
recompilar a imagem inteira (~40 min a 3 h), e mexer no sistema reempacotava o
Launcher junto — uma frente de trabalho atrapalhava a outra. Com o Launcher
como app:

| Frente | O que muda |
|---|---|
| Mexer **no sistema** (init, kernel, USB) | o Launcher instalado em `/user` **não** é tocado |
| Mexer **no Launcher** (UI, tema, textos) | copia-se o binário novo para `/user`; o sistema fica como está |
| Trabalhar em Ports/jogos em paralelo | nenhuma das duas frentes depende da outra |

## Como funciona

### 1. Resolução do binário (supervisor)

O `giochi-init` resolve o Launcher nesta ordem
([supervisor.c](../src/init/supervisor.c), `giochi_launcher_program`):

1. `/user/apps/giochi-launcher` — o app, se for executável (`access X_OK`);
2. `/usr/bin/giochi-launcher` — o binário da imagem (fallback, sempre existe).

O teste é `access(X_OK)`, **não** `stat`: um upload via MTP interrompido no
meio (arquivo pela metade, sem bit de execução) não derruba a UI — o fallback
da imagem assume, e o log do boot diz qual binário venceu
(`launcher: /user/apps/giochi-launcher`).

A resolução acontece no boot **e** a cada relançamento do Launcher (volta de
um jogo, crash): instalar o app em runtime vale na próxima volta para a Home,
sem reboot.

### 2. Assets (layout, tema, textos)

Se existir `/user/apps/giochi-launcher/assets/layout.json`, o Launcher usa
`/user/apps/giochi-launcher/assets` como diretório de assets **no lugar** de
`/usr/share/giochi`. Quem mexe em layout/theme/locales coloca os JSONs novos
junto com o binário. `--asset-dir` explícito continua vencendo.

### 3. Instalação/atualização

O MTP expõe `/user` ao PC com leitura e escrita; a árvore de boot já semeia
`/user/apps` ([S10storage](../scripts/init.d/S10storage)). Passo a passo:

1. Compile o Launcher para o alvo (ou pegue o binário do build):
   `dist/x55/build/build/giochi-launcher-1.2/build/target/bin/giochi-launcher`.
2. Conecte o aparelho ao PC (MTP) ou use SSH (Modo Desenvolvedor).
3. Copie o binário para `/user/apps/giochi-launcher` e dê permissão de
   execução:

   ```sh
   # via MTP: arraste o arquivo para a pasta "apps" e depois, por SSH:
   ssh root@10.55.0.2 'chmod +x /user/apps/giochi-launcher'

   # via SSH direto:
   scp giochi-launcher root@10.55.0.2:/user/apps/giochi-launcher
   ssh root@10.55.0.2 'chmod +x /user/apps/giochi-launcher'
   ```

4. (Opcional) assets: `/user/apps/giochi-launcher/assets/{layout.json,
   theme.json, locales/…}`.
5. Reinicie o Launcher (o app não se substitui sozinho em execução). O caminho
   mais simples é desligar e ligar; sem reboot, abra um jogo e feche — a volta
   para a Home relança o Launcher com o binário novo.

Desfazer: apague `/user/apps/giochi-launcher` — o sistema volta a usar a
imagem no relançamento seguinte.

## Orçamento (adendo ao §8)

O Launcher como app não muda o orçamento da §4.1.6: **RAM ≤ 60 MB em repouso,
VRAM de UI ≤ 4 MB**. O binário adicional em `/user` não conta contra o
SquashFS; o fallback da imagem continua lá (uma cópia, ~2 MB).

## Limites e segurança

- **O app não é assinado.** O rootfs é assinado (Ed25519 + SHA-256), mas
  `/user` é a partição gravável do usuário — o Launcher de lá é confiável
  porque **o usuário** o colocou (o mesmo modelo dos ports em `/user/ports`).
  Não há upgrade automático de rede para `/user/apps`: nada entra sem a mão
  do usuário.
- **Um app quebrado não trava o aparelho**: sem `X_OK` (ou sem o arquivo), o
  fallback da imagem assume. O pior caso é o app rodar e cair — e o supervisor
  o relança com backoff (§2.2), como hoje.
- **A/B não é afetado**: o app vive fora das partições de sistema; trocar de
  slot não remove nem altera o Launcher instalado.

## Verificação

- Suíte `primeiro-boot`, classes `LauncherComoApp` (guardas de código) e
  `LauncherComoAppFuncional` (roda o `giochi-init` real num `GIOCHI_ROOT`
  falso e prova **qual** binário executou, nos três casos: app presente, app
  ausente, app sem bit de execução).
- Testado ao contrário: invertida a ordem de preferência no supervisor, o
  teste funcional reprova.
