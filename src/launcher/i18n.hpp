/* Giochi Konsol OS v1.2 — internacionalização do Launcher (secao 4.9.3).
 *
 * Por que um módulo e não strings espalhadas: o repositório é em português e o
 * produto é para o mundo. Sem uma fronteira, cada `const char *` no render.cpp
 * vira uma decisão de idioma tomada por acidente — foi o que aconteceu com o
 * `paint()` da v1.1, que escrevia "Musica" sem acento porque o atlas só tinha
 * ASCII.
 *
 * O idioma vem nesta ordem: GIOCHI_LANG (ambiente) → settings.json
 * (`system.language`, que é o que a UI grava) → "pt-BR".
 *
 * Os catálogos são JSON plano em /usr/share/giochi/locales/<lingua>.json
 * (`pt_BR.json`, `en.json`). Plano e não aninhado porque a chave é usada
 * literalmente ("settings.language") e `gj_object_get` casa por string: sem
 * percurso de árvore em cada rótulo desenhado a 60 fps.
 *
 * Uma chave ausente devolve a própria chave, nunca NULL e nunca vazio: um
 * rótulo estranho na tela é um defeito visível e rastreável; string vazia é
 * silêncio.
 */
#ifndef GIOCHI_I18N_HPP
#define GIOCHI_I18N_HPP

#include <cstddef>
#include <string>

struct gj_value;

namespace giochi {

/* Normaliza um identificador de idioma para o nome do arquivo sem extensão:
 *   "pt-BR" / "pt_BR" / "pt"  -> "pt_BR"
 *   "en-US" / "en_US" / "en"  -> "en"
 * Devolve 0 em sucesso. Idioma vazio ou desconhecido vira o padrão. */
int i18n_normalize(const char *lang, char *out, std::size_t out_size);

class I18n {
public:
    I18n() = default;
    ~I18n();
    I18n(const I18n &) = delete;
    I18n &operator=(const I18n &) = delete;

    /* Carrega <dir>/locales/<idioma>.json. Se o idioma pedido não existir,
     * cai para o padrão e registra no log — nunca fica sem catálogo se pt_BR
     * estiver na imagem. */
    bool load(const char *assets_dir, const char *lang);

    const char *tr(const char *key) const;

    /* tr() com uma substituição de "{n}". Usado para contadores e "%". */
    std::string tr_n(const char *key, long n) const;

    /* Idioma efetivamente carregado ("pt_BR", "en"). */
    const char *lang() const { return lang_; }

    /* Nome legível do idioma, para o menu de Configurações. */
    std::string display_name() const;

    bool loaded() const { return root_ != nullptr; }

private:
    const char *lookup(const char *key) const;

    gj_value *root_ = nullptr;
    char lang_[16] = "pt_BR";
};

/* Idioma configurado: ambiente → settings.json → padrão. */
std::string i18n_resolve_language(const char *user_dir);

/* --- Hint bar (secao 4.2) ---------------------------------------------
 *
 * A barra de dicas e' uma lista de pares (botao, acao), e a tabela de QUAIS
 * pares cada contexto mostra tem de morar num lugar so'. Ela estava duplicada
 * no main.cpp e no tools/dump-geometry.cpp, e as duas divergiram: o preview
 * mostrava "A Abrir" onde o aparelho mostrava "A Abrir + B Voltar".
 *
 * Item que nao funciona nao aparece (B e' inerte na Home, §4.1.2): botao
 * anunciado que nao faz nada e' ruido.
 *
 * Devolve a string no formato "Botao:acao|Botao:acao", ja' TRADUZIDA.
 */
enum UiHintContext {
    UI_HINT_TABS = 0,   /* navegacao normal entre abas */
    UI_HINT_HOME,
    UI_HINT_LIBRARY,
    UI_HINT_MUSIC,
    UI_HINT_SETTINGS_0, /* menu de Configuracoes, nivel 0 */
    UI_HINT_SETTINGS_1  /* submenu Sistema */
};

std::string ui_hint_text(const I18n &i18n, UiHintContext ctx);

}  // namespace giochi

#endif /* GIOCHI_I18N_HPP */
