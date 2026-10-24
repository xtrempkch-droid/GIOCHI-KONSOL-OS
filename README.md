# GIOCHI-KONSOL-OS


📑 BLUEPRINT TÉCNICO: GIOCHI KONSOL OS

Status do Projeto: Arquitetura Conceitual Homologada (Pronto para Desenvolvimento Futuro)
Foco: Eficiência Radical, Imutabilidade, Portabilidade de Hardware e Facilidade para Desenvolvedores.

1. Arquitetura do Sistema Operacional (Giochi OS)

O sistema foi projetado sob o princípio da compilação minimalista sob medida via Buildroot/Yocto, eliminando qualquer subsistema redundante de PC (sem X11, sem CUPS, com biblioteca musl-libc).
• Boot Instantâneo: Inicialização linear (U-Boot ➔ Kernel Linux Micro ➔ Gamescope ➔ Giochi Launcher) carregando a imagem completa do SO (~150MB) direto para a memória RAM em menos de 5 segundos.
• Linux Imutável: O sistema operacional opera em modo "Apenas Leitura" (Read-Only) em uma partição isolada. Modificações externas são impossíveis, blindando o console contra arquivos corrompidos ou falhas críticas de desligamento.
• Abstração por Camadas (Multi-Chip): A engenharia divide-se em Camada de Aplicação (universal), Distribuição (código compartilhado) e Hardware (perfis BSP/Device Tree independentes). Isso permite portar o Giochi OS para qualquer chip do mercado (Rockchip, Allwinner, Amlogic, x86) apenas alterando o perfil de compilação gráfica aberto (Mesa/Panfrost) e de entradas (SDL2).

2. Gerenciamento Avançado de Memória e Desempenho

A arquitetura de memória do console foi desenhada para extrair performance máxima de hardwares de baixo custo e estender a vida útil dos componentes de armazenamento.
• Linha de Base de Hardware: Projetado com foco em dispositivos de 2 GB de RAM.
• Engine de Interface Reciclável: O menu do console consome apenas entre 40MB e 60MB em repouso. Ao iniciar um jogo, o processo da interface visual é completamente encerrado (Kill), devolvendo quase 100% dos 2 GB de RAM físicos e focando toda a CPU exclusivamente no jogo ativo.
• zRAM como Core de Memória: O sistema operacional inteiro roda compactado dentro da RAM através de zRAM (algoritmo LZ4). Isso expande a capacidade útil para cerca de 3 GB virtuais e reduz o estresse de leitura no cartão SD a quase zero após o boot.
• Inovação: RAID de Swap Paralelo (Dual SD Card): Em sistemas de cartões duplos, se a memória RAM física esgotar em jogos massivos, o Giochi OS cria dois arquivos de Swap de mesma prioridade nos dois slots. Os dados são divididos em canais físicos simultâneos, dobrando a velocidade da memória virtual. O sistema possui uma trava de segurança: se um cartão for 10% mais lento que o outro no teste de estresse do boot, o RAID é desativado para evitar gargalos.

3. Relação com Desenvolvedor (Giochi SDK) e Interface

O ecossistema remove barreiras financeiras e técnicas para atrair criadores de jogos independentes.
• Foco na Godot Engine: O SDK gratuito fornece export templates pré-configurados para Godot e LÖVE2D.
• O Pacote .giochi: Um formato de arquivo único envelopado contendo o binário ARM64, imagens de capa e metadados. O desenvolvedor gera o pacote através de uma ferramenta de linha de comando simples (giochi-cli).
• Menu em Lista Dinâmica: Projetado especificamente para telas de 640x480 sem toque. O lado esquerdo exibe uma lista vertical em texto puro (peso em RAM irrisório) e o lado direito renderiza apenas uma imagem por vez (a capa do jogo sob o cursor), descartando a anterior instantaneamente para economizar VRAM.

4. Distribuição e Atualizações Seguras

• Atualizações Reativas (Manuais): Não existem processos em background buscando updates. O usuário força a busca manualmente na interface (via Wi-Fi) ou insere um arquivo de imagem (giochi_update.img) na raiz do cartão SD.
• Isolamento de Saves: Os dados salvos dos jogos ficam armazenados na partição gravável do cartão SD, totalmente separados da imagem imutável do sistema operacional.
