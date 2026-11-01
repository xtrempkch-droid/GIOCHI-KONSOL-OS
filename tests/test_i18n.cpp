/* Internacionalizacao: normalizacao de idioma, catalogo real, e substituicao.
 *
 * O defeito que este teste existe para impedir: a UI escrever rotulo em
 * portugues "na marra" — foi assim que "Musica" ficou sem acento na v1.1,
 * porque o atlas so' tinha ASCII. Agora o texto tem uma fonte so', e esta
 * suite cobra que ela seja coerente.
 */
#include "i18n.hpp"
#include "test_util.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

std::string asset_dir()
{
#ifdef GIOCHI_ASSET_DIR
    return GIOCHI_ASSET_DIR;
#else
    return "assets";
#endif
}

void test_normalize(void)
{
    char out[16];

    CHECK(giochi::i18n_normalize("pt-BR", out, sizeof(out)) == 0);
    CHECK_STR_EQ(out, "pt_BR");
    CHECK(giochi::i18n_normalize("pt_BR", out, sizeof(out)) == 0);
    CHECK_STR_EQ(out, "pt_BR");
    CHECK(giochi::i18n_normalize("pt", out, sizeof(out)) == 0);
    CHECK_STR_EQ(out, "pt_BR");

    /* en-US e en_GB caem em "en": sem isso, um aparelho configurado em
     * "en-US" ficaria sem catalogo. */
    CHECK(giochi::i18n_normalize("en-US", out, sizeof(out)) == 0);
    CHECK_STR_EQ(out, "en");
    CHECK(giochi::i18n_normalize("en_GB", out, sizeof(out)) == 0);
    CHECK_STR_EQ(out, "en");

    /* Desconhecido ou vazio: padrao, nunca string vazia. */
    CHECK(giochi::i18n_normalize("tlh", out, sizeof(out)) == 1);
    CHECK_STR_EQ(out, "pt_BR");
    CHECK(giochi::i18n_normalize("", out, sizeof(out)) == 0);
    CHECK_STR_EQ(out, "pt_BR");
    CHECK(giochi::i18n_normalize(nullptr, out, sizeof(out)) == 0);
    CHECK_STR_EQ(out, "pt_BR");
}

void test_carrega_pt_br(void)
{
    giochi::I18n i18n;

    CHECK(i18n.load(asset_dir().c_str(), "pt-BR") == true);
    CHECK(i18n.loaded());
    CHECK_STR_EQ(i18n.lang(), "pt_BR");

    /* Acento tem de vir do catalogo, nao de ASCII improvisado. */
    CHECK_STR_EQ(i18n.tr("tab.music"), "Música");
    CHECK_STR_EQ(i18n.tr("tab.home"), "Início");
    CHECK_STR_EQ(i18n.tr("settings.title"), "Configurações");
}

void test_carrega_en(void)
{
    giochi::I18n i18n;

    CHECK(i18n.load(asset_dir().c_str(), "en") == true);
    CHECK_STR_EQ(i18n.lang(), "en");
    CHECK_STR_EQ(i18n.tr("tab.library"), "Library");
    CHECK_STR_EQ(i18n.tr("settings.title"), "Settings");
    CHECK(i18n.display_name() == "English");
}

/* Chave ausente devolve a PROPRIA chave: um rotulo estranho na tela e'
 * rastreavel; string vazia some sem deixar rastro. */
void test_chave_ausente(void)
{
    giochi::I18n i18n;

    (void)i18n.load(asset_dir().c_str(), "pt-BR");
    CHECK_STR_EQ(i18n.tr("nao.existe"), "nao.existe");
    CHECK_STR_EQ(i18n.tr(""), "");

    giochi::I18n sem;
    CHECK(sem.loaded() == false);
    CHECK_STR_EQ(sem.tr("tab.music"), "tab.music");
}

/* Idioma pedido que nao existe na imagem cai para o padrao — a UI nunca fica
 * sem rotulo. */
void test_fallback_de_idioma(void)
{
    giochi::I18n i18n;

    if (i18n.load(asset_dir().c_str(), "tlh")) {
        CHECK_STR_EQ(i18n.lang(), "pt_BR");
        CHECK_STR_EQ(i18n.tr("tab.home"), "Início");
    }
}

void test_substituicao(void)
{
    giochi::I18n i18n;

    (void)i18n.load(asset_dir().c_str(), "pt-BR");

    CHECK(i18n.tr_n("library.count", 7) == "7 jogos");
    CHECK(i18n.tr_n("status.battery", 82) == "82%");
    CHECK(i18n.tr_n("settings.level", 80) == "80%");

    /* Chave sem {n} nao pode ganhar texto estranho. */
    CHECK(i18n.tr_n("tab.home", 3) == "Início");
}

/* Os dois catalogos tem de ter AS MESMAS chaves: traducao sobrando e' chave
 * que ninguem usa, faltando e' texto que cai no idioma errado. */
void test_paridade_de_chaves(void)
{
    giochi::I18n pt;
    giochi::I18n en;

    if (!pt.load(asset_dir().c_str(), "pt-BR") ||
        !en.load(asset_dir().c_str(), "en")) {
        return; /* a suite Python cobre a ausencia do arquivo */
    }

    /* Amostra das chaves que a UI realmente usa. Se uma delas faltar em um
     * dos dois, o rótulo aparece cru naquele idioma. */
    const char *const usadas[] = {
        "tab.music",      "tab.library",     "tab.home",
        "tab.store",      "tab.ports",       "library.empty",
        "library.count",  "home.continue",   "home.news",
        "home.recent",    "music.no_decoder", "store.missing",
        "ports.missing",  "settings.title",  "settings.language",
        "settings.on",    "settings.off",    "settings.usb_mode",
        "settings.dev_mode", "hint.open",    "hint.back",
        "hint.select",    "hint.home",       "hint.play",
        "status.battery", "error.title",
    };

    for (const char *key : usadas) {
        CHECK(std::string(pt.tr(key)) != key);
        CHECK(std::string(en.tr(key)) != key);
    }
}

}  // namespace

int main(void)
{
    test_normalize();
    test_carrega_pt_br();
    test_carrega_en();
    test_chave_ausente();
    test_fallback_de_idioma();
    test_substituicao();
    test_paridade_de_chaves();
    TEST_SUMMARY("test_i18n");
}
