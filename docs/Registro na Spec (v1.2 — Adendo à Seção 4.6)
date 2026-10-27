📋 Registro na Spec (v1.2 — Adendo à Seção 4.6)

Subseção 4.6.1 — SSH via USB (Modo Desenvolvedor)

    Função: Acesso remoto ao console via SSH sobre USB, sem depender de rede Wi-Fi/Ethernet.

    Ativação: Manual, via Configurações → Sistema → Modo Desenvolvedor. Desativado por padrão.

    Implementação: Gadget USB com função de rede (ecm/rndis) + Dropbear escutando exclusivamente em usb0.

    IP padrão: Console 10.55.0.2/24; PC 10.55.0.1/24.

    Orçamento: +1,5 MB SquashFS; 0 MB RAM em repouso; ≤ 3 MB RAM quando ativo.

    Segurança: Escuta apenas em usb0; autenticação por senha/chave; desativado por padrão.

    Compatibilidade: Linux/macOS (ECM nativo); Windows (RNDIS, pode exigir driver).

    Fora de escopo v1.2: SSH sobre Wi-Fi; autenticação por chave exclusiva (fase 2); túnel reverso.

Impacto no device_profile.json: adicionar "developer": { "ssh_usb": false } (reflete o estado atual, não a capacidade).

Impacto no settings.json: adicionar "developer": { "ssh_usb_enabled": false }.