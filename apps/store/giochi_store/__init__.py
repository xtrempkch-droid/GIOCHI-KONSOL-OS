"""giochi_store — catalogo e instalacao da Loja, do lado do PC (v1.2)."""

from .catalogo import (
    ARCH_OK,
    RUNTIMES_OK,
    Pacote,
    catalogo,
    ler_metadata,
    novidades,
    sha256_de,
    validar,
    varrer,
)

__all__ = [
    "ARCH_OK",
    "RUNTIMES_OK",
    "Pacote",
    "catalogo",
    "ler_metadata",
    "novidades",
    "sha256_de",
    "validar",
    "varrer",
]
