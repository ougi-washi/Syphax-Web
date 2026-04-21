#include "sw_internal.h"

typedef struct {
    const c8* key;
    const c8* values[SW_LANGUAGES_MAX];
} sw_translation_entry;

static const c8* const sw_default_languages[SW_LANGUAGES_MAX] = {
    "en",
    "ar",
    "fr"
};

static const sw_translation_entry sw_default_translations[] = {
    { "Syphax Web", { "Syphax Web", "صيفاقس ويب", "Syphax Web" } },
    { "English", { "English", "الإنجليزية", "Anglais" } },
    { "French", { "French", "الفرنسية", "Francais" } },
    { "Home", { "Home", "الصفحة الرئيسية", "Accueil" } },
    { "Search", { "Search", "بحث", "Rechercher" } },
    { "Info", { "Info", "معلومات", "Info" } },
    { "Add", { "Add", "إضافة", "Ajouter" } },
    { "Edit", { "Edit", "تعديل", "Editer" } },
    { "Update", { "Update", "تحديث", "Mettre a jour" } },
    { "Delete", { "Delete", "حذف", "Supprimer" } },
    { "Cancel", { "Cancel", "إلغاء", "Annuler" } },
    { "No results found", { "No results found", "لم يتم العثور على نتائج", "Aucun resultat trouve" } },
    { "Login", { "Login", "تسجيل الدخول", "Connexion" } },
    { "Register", { "Register", "التسجيل", "S'inscrire" } },
    { "Logout", { "Logout", "تسجيل الخروج", "Deconnexion" } },
    { "User", { "User", "المستخدم", "Utilisateur" } },
    { "Username", { "Username", "اسم المستخدم", "Nom d'utilisateur" } },
    { "Password", { "Password", "كلمة المرور", "Mot de passe" } },
    { "Language", { "Language", "اللغة", "Langue" } }
};

sw_translator* sw_translator_create(void) {
    sw_translator* translator = (sw_translator*)calloc(1, sizeof(*translator));
    return translator;
}

void sw_translator_destroy(sw_translator* translator) {
    free(translator);
}

b8 sw_translator_set_language(sw_translator* translator, const c8* lang) {
    sz i;

    if (translator == NULL || lang == NULL) {
        return 0;
    }

    for (i = 0; i < SW_LANGUAGES_MAX; ++i) {
        if (strcmp(lang, sw_default_languages[i]) == 0) {
            translator->current_language = i;
            return 1;
        }
    }

    return 0;
}

const c8* sw_translator_get_language(const sw_translator* translator) {
    if (translator == NULL) {
        return sw_default_languages[0];
    }
    return sw_default_languages[translator->current_language];
}

const c8* sw_translate(const sw_translator* translator, const c8* str) {
    sz i;
    sz language_index = 0;

    if (str == NULL) {
        return NULL;
    }

    if (translator != NULL) {
        language_index = translator->current_language;
    }

    for (i = 0; i < (sizeof(sw_default_translations) / sizeof(sw_default_translations[0])); ++i) {
        if (strcmp(str, sw_default_translations[i].key) == 0) {
            return sw_default_translations[i].values[language_index];
        }
    }

    return str;
}
