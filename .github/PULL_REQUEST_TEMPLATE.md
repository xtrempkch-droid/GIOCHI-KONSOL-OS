## O que muda

<!-- Uma frase. Se implementa um requisito, cite a seção: "B volta um nível (§4.1.2)". -->

## Tipo

- [ ] Bug fix
- [ ] Novo recurso
- [ ] Build/CI/infra
- [ ] Documentação
- [ ] Novo aparelho

## Como foi validado

- [ ] `make lint`
- [ ] `make test` (13 suítes)
- [ ] `tools/build-image.sh --check --all --fallback-dts`
- [ ] Build de imagem real: aparelho `______`
- [ ] Teste em hardware: aparelho `______` (diga o que funcionou)

## Orçamento (se afeta runtime)

| Métrica | Antes | Depois |
|---|---|---|
| RAM em repouso | | |
| VRAM de UI | | |
| Tamanho SquashFS | | |
| Boot até Launcher | | |

## ADRs / decisões

<!-- Viola alguma ADR? Se sim, pare. Se tomou uma decisão não coberta, descreva. -->

- [ ] Não introduz systemd, glibc pré-compilado, Pulse/PipeWire na base,
      Vulkan no RK3326, XWayland ou shell Wayland
- [ ] Defconfigs gerados foram regenerados (`make defconfigs`), não editados à mão

## Riscos e pendências

<!-- O que ficou de fora deste PR e por quê. -->
