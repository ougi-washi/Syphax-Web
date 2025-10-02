// Syphax-Web - Ougi Washi

#include "sw_translator.h"
#include "sw_types.h"
#include "string.h"

static sz current_language = 0;

// TODO move to a file (csv or json) and load the dictionary on startup
c8* sw_translations[][SW_LANGUAGES_MAX] = {
    // Title
    { "Syphax Web", "صيفاقس ويب", "Syphax Web" },
    // Languages
    { "English", "الإنجليزية", "Anglais" },
    { "French", "الفرنسية", "Français" },
    
    // Tabs
    { "Home", "الصفحة الرئيسية", "Accueil" },
        
    // General
    { "Search", "بحث", "Rechercher" },
    { "Info", "معلومات", "Info" },
    { "Add", "إضافة", "Ajouter" },
    { "Edit", "تعديل", "Editer" },
    { "Update", "تحديث", "Mettre à jour" },
    { "Delete", "حذف", "Supprimer" },
    { "Cancel", "إلغاء", "Annuler" },
    { "No results found", "لم يتم العثور على نتائج", "Aucun résultat trouvé" },
    
    // User
    { "Login", "تسجيل الدخول", "Connexion" },
    { "Register", "التسجيل", "S'inscrire" },
    { "Logout", "تسجيل الخروج", "Déconnexion" },
    { "User", "المستخدم", "Utilisateur" },
    { "Username", "اسم المستخدم", "Nom d'utilisateur" },
    { "Password", "كلمة المرور", "Mot de passe" },
    { "Language", "اللغة", "Langue" },
    { "Logout", "تسجيل الخروج", "Déconnexion" },
};

#define SW_TRANSLATIONS_SIZE sizeof(sw_translations) / sizeof(sw_translations[0])

void sw_set_language(c8* lang) {
    for (int i = 0; i < SW_LANGUAGES_MAX; i++) {
        if (strcmp(lang, language[i]) == 0) {
            current_language = i;
            break;
        }
    }
}

const c8* sw_translate(const c8* str) {
    for (int i = 0; i < SW_TRANSLATIONS_SIZE; i++) {
        if (strcmp(str, sw_translations[i][0]) == 0) {
            return sw_translations[i][current_language];
        }
    }
    return str;
}

