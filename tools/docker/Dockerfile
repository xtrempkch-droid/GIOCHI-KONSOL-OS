# Giochi Konsol OS v1.2 — ambiente de build das imagens.
#
# Reproduz o runner de CI (ubuntu-24.04 → GCC 13, CMake 3.28) para que o build
# funcione em qualquer host. Sem isso, uma distribuicao mais nova quebra em
# pacotes de host antigos: GCC 15 nao compila host-m4 1.4.19 e CMake 4.x
# rejeita o CMakeLists do host-lzo (suporte a < 3.5 removido).
#
# Uso: tools/docker-build.sh x55
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Mesma lista do .github/workflows/images.yml, mais o que o Buildroot exige
# (bison/flex/dtc) e python3-pil para o bootsplash.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential git wget curl ca-certificates cpio unzip rsync bc \
        file libncurses-dev python3 python3-setuptools python3-pip \
        dosfstools mtools e2fsprogs device-tree-compiler bison flex \
        libssl-dev libelf-dev python3-pil \
    && rm -rf /var/lib/apt/lists/*

# Buildroot LTS. O Buildroot compila o proprio host-cmake quando precisa, entao
# nao instalamos cmake aqui: assim o container nao esconde uma dependencia que
# faltaria no CI.
ARG BUILDROOT_REF=2024.02.x
RUN git clone --depth 1 --branch ${BUILDROOT_REF} \
        https://github.com/buildroot/buildroot.git /opt/buildroot

# O codigo e montado em /work pelo wrapper; o cache de downloads em /dl.
WORKDIR /work
CMD ["/bin/bash"]
