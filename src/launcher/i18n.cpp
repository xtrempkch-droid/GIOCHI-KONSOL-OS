#include "i18n.hpp"

#include "giochi_json.h"
#include "giochi_log.h"
#include "settings.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace giochi {

namespace {

/* Idiomas que a imagem traz. A ordem importa: o primeiro é o padrão. */
struct KnownLang {
    const char *file;   /* nome do arquivo, sem extensão */
    const char *accept; /* prefixos aceitos, separados por '|' */
};

const KnownLang kKnown[] = {
    {"pt_BR", "pt"},
    {"en", "en"},
};

const char *kDefault = "pt_BR";

}  // namespace

int i18n_normalize(const char *lang, char *out, std::size_t out_size)
{
    if (out == nullptr || out_size == 0) {
        return -1;
    }
    out[0] = '\0';

    if (lang != nullptr) {
        for (const auto &known : kKnown) {
            /* Compara só o prefixo de idioma: "en-US", "en_GB" e "en" caem
             * todos em "en". Sem isso, o settings.json de um aparelho
             * configurado em "en-US" ficaria sem catálogo. */
            const std::size_t len = std::strlen(known.accept);

            if (strncmp(lang, known.accept, len) == 0) {
                std::snprintf(out, out_size, "%s", known.file);
                return 0;
            }
        }
    }

    std::snprintf(out, out_size, "%s", kDefault);
    return lang == nullptr || *lang == '\0' ? 0 : 1;
}

I18n::~I18n()
{
    if (root_ != nullptr) {
        gj_free(root_);
        root_ = nullptr;
    }
}

bool I18n::load(const char *assets_dir, const char *lang)
{
    const std::string base = assets_dir != nullptr ? std::string(assets_dir)
                                                  : std::string("/usr/share/giochi");
    char want[16] = {0};

    (void)i18n_normalize(lang, want, sizeof(want));

    if (root_ != nullptr) {
        gj_free(root_);
        root_ = nullptr;
    }

    auto try_load = [&](const char *tag) -> bool {
        const std::string path = base + "/locales/" + tag + ".json";
        char err[128] = {0};
        gj_value *doc = gj_parse_file(path.c_str(), err, sizeof(err));

        if (doc == nullptr) {
            GLOG_WARN("i18n: '%s': %s", path.c_str(), err);
            return false;
        }
        root_ = doc;
        std::snprintf(lang_, sizeof(lang_), "%s", tag);
        return true;
    };

    if (try_load(want)) {
        if (std::strcmp(want, kDefault) != 0) {
            GLOG_INFO("i18n: idioma '%s' carregado de %s", lang_, base.c_str());
        }
        return true;
    }

    /* Pedido ausente não pode deixar a UI sem rótulo: cai para o padrão. */
    if (std::strcmp(want, kDefault) != 0 && try_load(kDefault)) {
        GLOG_WARN("i18n: idioma '%s' ausente; usando '%s'", want, lang_);
        return true;
    }

    GLOG_ERR("i18n: nenhum catalogo em %s/locales; a UI mostrara' as chaves",
             base.c_str());
    return false;
}

const char *I18n::lookup(const char *key) const
{
    if (root_ == nullptr || key == nullptr) {
        return nullptr;
    }
    const char *value = gj_as_string(gj_object_get(root_, key), nullptr);

    /* String vazia é pior que chave: some da tela sem deixar rastro de que
     * faltou tradução. Trata como ausente. */
    if (value == nullptr || *value == '\0') {
        return nullptr;
    }
    return value;
}

const char *I18n::tr(const char *key) const
{
    const char *value = lookup(key);

    if (value == nullptr) {
        return key != nullptr ? key : "";
    }
    return value;
}

std::string I18n::tr_n(const char *key, long n) const
{
    std::string text = tr(key);
    const std::string token = "{n}";
    const std::size_t pos = text.find(token);

    if (pos != std::string::npos) {
        text.replace(pos, token.size(), std::to_string(n));
    }
    return text;
}

std::string I18n::display_name() const
{
    const char *name = lookup("name");

    return name != nullptr ? std::string(name) : std::string(lang_);
}

namespace {

struct HintPair {
    const char *button;
    const char *key;
};

/* Uma linha por contexto. A ordem e' a de leitura na barra. */
const HintPair kHintTabs[] = {{"L/R", "hint.tabs"},
                              {"A", "hint.open"},
                              {"B", "hint.back"},
                              {"Select", "hint.select"}};
/* Home: SEM B. Ele nao faz nada nesta aba (§4.1.2), e anunciar botao morto e'
 * exatamente o que a §4.2 pede para nao fazer. */
const HintPair kHintHome[] = {{"L/R", "hint.tabs"},
                              {"A", "hint.open"},
                              {"Select", "hint.select"}};
const HintPair kHintLibrary[] = {{"A", "hint.open"},
                                 {"B", "hint.back"},
                                 {"Select", "hint.select"}};
const HintPair kHintMusic[] = {{"A", "hint.play"},
                               {"L/R", "hint.tabs"},
                               {"B", "hint.back"}};
const HintPair kHintSettings0[] = {{"A", "hint.open"},
                                   {"B", "hint.back"}};
const HintPair kHintSettings1[] = {{"A", "hint.choose"},
                                   {"B", "hint.back"}};

}  // namespace

std::string ui_hint_text(const I18n &i18n, UiHintContext ctx, int hints_mode)
{
    const HintPair *pares = kHintTabs;
    std::size_t n = sizeof(kHintTabs) / sizeof(kHintTabs[0]);

    switch (ctx) {
    case UI_HINT_HOME:
        pares = kHintHome;
        n = sizeof(kHintHome) / sizeof(kHintHome[0]);
        break;
    case UI_HINT_LIBRARY:
        pares = kHintLibrary;
        n = sizeof(kHintLibrary) / sizeof(kHintLibrary[0]);
        break;
    case UI_HINT_MUSIC:
        pares = kHintMusic;
        n = sizeof(kHintMusic) / sizeof(kHintMusic[0]);
        break;
    case UI_HINT_SETTINGS_0:
        pares = kHintSettings0;
        n = sizeof(kHintSettings0) / sizeof(kHintSettings0[0]);
        break;
    case UI_HINT_SETTINGS_1:
        pares = kHintSettings1;
        n = sizeof(kHintSettings1) / sizeof(kHintSettings1[0]);
        break;
    case UI_HINT_TABS:
    default:
        break;
    }

    std::string out;

    for (std::size_t i = 0; i < n; i++) {
        if (i > 0) {
            out += '|';
        }
        out += pares[i].button;
        out += ':';
        out += i18n.tr(pares[i].key);
    }

    /* Modo `completo` acrescenta o atalho de teclado — o "Use setas do teclado"
     * dos mockups. Vem DEPOIS dos botoes do controle porque no aparelho o
     * controle e' o caminho normal. */
    if (hints_mode != 0) {
        out += "|Setas:";
        out += i18n.tr("hint.keyboard");
    }
    return out;
}

std::string i18n_resolve_language(const char *user_dir)
{
    /* 1. Ambiente: teste de host e depuração no aparelho. */
    if (const char *env = std::getenv("GIOCHI_LANG");
        env != nullptr && *env != '\0') {
        return std::string(env);
    }

    /* 2. settings.json — é o que a UI grava e o que a §4.9.5 define como
     *    propriedade do Launcher. Lido do arquivo, não do struct: quem chama
     *    pode não ter carregado as configurações ainda. */
    if (user_dir != nullptr && *user_dir != '\0') {
        giochi_settings settings;

        if (giochi_settings_load(&settings, user_dir, nullptr, 0) == 0 &&
            settings.language[0] != '\0') {
            return std::string(settings.language);
        }
    }

    return std::string(kDefault);
}

}  // namespace giochi
