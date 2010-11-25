#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#undef alloca
#ifdef HAVE_ALLOCA_H
# include <alloca.h>
#elif defined __GNUC__
# define alloca __builtin_alloca
#elif defined _AIX
# define alloca __alloca
#elif defined _MSC_VER
# include <malloc.h>
# define alloca _alloca
#else
# include <stddef.h>
# ifdef  __cplusplus
extern "C"
# endif
void *alloca (size_t);
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "Efreet.h"
#include "efreet_private.h"
#include "efreet_xml.h"

EAPI int efreet_cache_update = 1;

static int _efreet_init_count = 0;
static int efreet_parsed_locale = 0;
static const char *efreet_lang = NULL;
static const char *efreet_lang_country = NULL;
static const char *efreet_lang_modifier = NULL;
int _efreet_log_domain_global = -1;
static void efreet_parse_locale(void);
static int efreet_parse_locale_setting(const char *env);

/**
 * @return Returns > 0 if the initialization was successful, 0 otherwise
 * @brief Initializes the Efreet system
 */
EAPI int
efreet_init(void)
{
    if (++_efreet_init_count != 1)
        return _efreet_init_count;

    if (!eina_init())
        return --_efreet_init_count;
    if (!eet_init())
        goto shutdown_eina;
    if (!ecore_init())
        goto shutdown_eet;
    if (!ecore_file_init())
        goto shutdown_ecore;
    _efreet_log_domain_global = eina_log_domain_register("efreet", EFREET_DEFAULT_LOG_COLOR);
    if (_efreet_log_domain_global < 0)
    {
       EINA_LOG_ERR("Efreet could create a general log domain.");
        goto shutdown_ecore_file;
    }

    if (!efreet_base_init())
        goto unregister_log_domain;

    if (!efreet_cache_init())
        goto shutdown_efreet_base;

    if (!efreet_xml_init())
        goto shutdown_efreet_cache;

    if (!efreet_icon_init())
        goto shutdown_efreet_xml;

    if (!efreet_ini_init())
        goto shutdown_efreet_icon;

    if (!efreet_desktop_init())
        goto shutdown_efreet_ini;

    if (!efreet_menu_init())
        goto shutdown_efreet_desktop;

    if (!efreet_util_init())
        goto shutdown_efreet_menu;

    return _efreet_init_count;

shutdown_efreet_menu:
    efreet_menu_shutdown();
shutdown_efreet_desktop:
    efreet_desktop_shutdown();
shutdown_efreet_ini:
    efreet_ini_shutdown();
shutdown_efreet_icon:
    efreet_icon_shutdown();
shutdown_efreet_xml:
    efreet_xml_shutdown();
shutdown_efreet_cache:
    efreet_cache_shutdown();
shutdown_efreet_base:
    efreet_base_shutdown();
unregister_log_domain:
    eina_log_domain_unregister(_efreet_log_domain_global);
shutdown_ecore_file:
    ecore_file_shutdown();
shutdown_ecore:
    ecore_shutdown();
shutdown_eet:
    eet_shutdown();
shutdown_eina:
    eina_shutdown();

    return --_efreet_init_count;
}

/**
 * @return Returns the number of times the init function as been called
 * minus the corresponding init call.
 * @brief Shuts down Efreet if a balanced number of init/shutdown calls have
 * been made
 */
EAPI int
efreet_shutdown(void)
{
    if (--_efreet_init_count != 0)
        return _efreet_init_count;

    efreet_util_shutdown();
    efreet_menu_shutdown();
    efreet_desktop_shutdown();
    efreet_ini_shutdown();
    efreet_icon_shutdown();
    efreet_xml_shutdown();
    efreet_cache_shutdown();
    efreet_base_shutdown();
    eina_log_domain_unregister(_efreet_log_domain_global);

    IF_RELEASE(efreet_lang);
    IF_RELEASE(efreet_lang_country);
    IF_RELEASE(efreet_lang_modifier);
    efreet_parsed_locale = 0;  /* reset this in case they init efreet again */

    ecore_file_shutdown();
    ecore_shutdown();
    eet_shutdown();
    eina_shutdown();

    return _efreet_init_count;
}

/**
 * @internal
 * @return Returns the current users language setting or NULL if none set
 * @brief Retrieves the current language setting
 */
const char *
efreet_lang_get(void)
{
    if (efreet_parsed_locale) return efreet_lang;

    efreet_parse_locale();
    return efreet_lang;
}

/**
 * @internal
 * @return Returns the current language country setting or NULL if none set
 * @brief Retrieves the current country setting for the current language or
 */
const char *
efreet_lang_country_get(void)
{
    if (efreet_parsed_locale) return efreet_lang_country;

    efreet_parse_locale();
    return efreet_lang_country;
}

/**
 * @internal
 * @return Returns the current language modifier setting or NULL if none
 * set.
 * @brief Retrieves the modifier setting for the language.
 */
const char *
efreet_lang_modifier_get(void)
{
    if (efreet_parsed_locale) return efreet_lang_modifier;

    efreet_parse_locale();
    return efreet_lang_modifier;
}

/**
 * @internal
 * @return Returns no value
 * @brief Parses out the language, country and modifer setting from the
 * LC_MESSAGES environment variable
 */
static void
efreet_parse_locale(void)
{
    efreet_parsed_locale = 1;

    if (efreet_parse_locale_setting("LC_ALL"))
        return;

    if (efreet_parse_locale_setting("LC_MESSAGES"))
        return;

    efreet_parse_locale_setting("LANG");
}

/**
 * @internal
 * @param env: The environment variable to grab
 * @return Returns 1 if we parsed something of @a env, 0 otherwise
 * @brief Tries to parse the lang settings out of the given environment
 * variable
 */
static int
efreet_parse_locale_setting(const char *env)
{
    int found = 0;
    char *setting;
    char *p;
    size_t len;

    p = getenv(env);
    if (!p) return 0;
    len = strlen(p) + 1;
    setting = alloca(len);
    memcpy(setting, p, len);

    /* pull the modifier off the end */
    p = strrchr(setting, '@');
    if (p)
    {
        *p = '\0';
        efreet_lang_modifier = eina_stringshare_add(p + 1);
        found = 1;
    }

    /* if there is an encoding we ignore it */
    p = strrchr(setting, '.');
    if (p) *p = '\0';

    /* get the country if available */
    p = strrchr(setting, '_');
    if (p)
    {
        *p = '\0';
        efreet_lang_country = eina_stringshare_add(p + 1);
        found = 1;
    }

    if (*setting != '\0')
    {
        efreet_lang = eina_stringshare_add(setting);
        found = 1;
    }

    return found;
}

/**
 * @internal
 * @param buffer: The destination buffer
 * @param size: The destination buffer size
 * @param strs: The strings to concatenate together
 * @return Returns the size of the string in @a buffer
 * @brief Concatenates the strings in @a strs into the given @a buffer not
 * exceeding the given @a size.
 */
size_t
efreet_array_cat(char *buffer, size_t size, const char *strs[])
{
    int i;
    size_t n;
    for (i = 0, n = 0; n < size && strs[i]; i++)
    {
        n += eina_strlcpy(buffer + n, strs[i], size - n);
    }
    return n;
}
