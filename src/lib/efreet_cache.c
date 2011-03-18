#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

/* TODO: Consider flushing local icons cache after idling.
 *       Icon requests will probably come in batches, f.ex. during menu
 *       browsing.
 * TODO: Retry closing desktop cache on dangling references.
 */

#include <libgen.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <Eet.h>
#include <Ecore.h>
#include <Ecore_File.h>

/* define macros and variable for using the eina logging system  */
#define EFREET_MODULE_LOG_DOM _efreet_cache_log_dom
static int _efreet_cache_log_dom = -1;

#include "Efreet.h"
#include "efreet_private.h"
#include "efreet_cache_private.h"

#define NON_EXISTING (void *)-1

typedef struct _Efreet_Old_Cache Efreet_Old_Cache;

struct _Efreet_Old_Cache
{
    Eina_Hash *hash;
    Eet_File *ef;
};

/**
 * Data for cache files
 */
static Eet_Data_Descriptor *directory_edd = NULL;
static Eet_Data_Descriptor *icon_theme_edd = NULL;
static Eet_Data_Descriptor *icon_theme_directory_edd = NULL;

static Eet_Data_Descriptor *icon_fallback_edd = NULL;
static Eet_Data_Descriptor *icon_element_pointer_edd = NULL;
static Eet_Data_Descriptor *icon_element_edd = NULL;
static Eet_Data_Descriptor *icon_edd = NULL;

static Eet_File            *icon_cache = NULL;
static Eet_File            *fallback_cache = NULL;
static Eet_File            *icon_theme_cache = NULL;

static Eina_Hash           *themes = NULL;
static Eina_Hash           *icons = NULL;
static Eina_Hash           *fallbacks = NULL;

static const char          *icon_theme_cache_file = NULL;

static const char          *theme_name = NULL;

static Eet_Data_Descriptor *version_edd = NULL;
static Eet_Data_Descriptor *desktop_edd = NULL;
static Eet_Data_Descriptor *hash_array_string_edd = NULL;
static Eet_Data_Descriptor *array_string_edd = NULL;
static Eet_Data_Descriptor *hash_string_edd = NULL;

static Eina_Hash           *desktops = NULL;
static Efreet_Cache_Array_String *desktop_dirs = NULL;
static Eina_List           *desktop_dirs_add = NULL;
static Eet_File            *desktop_cache = NULL;
static const char          *desktop_cache_file = NULL;

static Ecore_File_Monitor  *cache_monitor = NULL;

static Ecore_Event_Handler *cache_exe_handler = NULL;
static Ecore_Timer         *icon_cache_timer = NULL;
static Ecore_Exe           *icon_cache_exe = NULL;
static int                  icon_cache_exe_lock = -1;
static Ecore_Timer         *desktop_cache_timer = NULL;
static Ecore_Exe           *desktop_cache_exe = NULL;
static int                  desktop_cache_exe_lock = -1;

static Eina_List           *old_desktop_caches = NULL;

static const char                *util_cache_file = NULL;
static Eet_File                  *util_cache = NULL;
static Efreet_Cache_Hash         *util_cache_hash = NULL;
static const char                *util_cache_hash_key = NULL;
static Efreet_Cache_Array_String *util_cache_names = NULL;
static const char                *util_cache_names_key = NULL;

static void efreet_cache_edd_shutdown(void);
static void efreet_cache_icon_free(Efreet_Cache_Icon *icon);
static void efreet_cache_icon_fallback_free(Efreet_Cache_Fallback_Icon *icon);
static void efreet_cache_icon_theme_free(Efreet_Icon_Theme *theme);

static Eina_Bool efreet_cache_check(Eet_File **ef, const char *path, int major);
static void *efreet_cache_close(Eet_File *ef);

static Eina_Bool cache_exe_cb(void *data, int type, void *event);
static void cache_update_cb(void *data, Ecore_File_Monitor *em,
                            Ecore_File_Event event, const char *path);

static Eina_Bool desktop_cache_update_cache_cb(void *data);
static Eina_Bool icon_cache_update_cache_cb(void *data);
static void desktop_cache_update_free(void *data, void *ev);
static void icon_cache_update_free(void *data, void *ev);

static void *hash_array_string_add(void *hash, const char *key, void *data);

static int strcmplen(const void *data1, const void *data2);

EAPI int EFREET_EVENT_ICON_CACHE_UPDATE = 0;
EAPI int EFREET_EVENT_DESKTOP_CACHE_UPDATE = 0;

int
efreet_cache_init(void)
{
    char buf[PATH_MAX];

    _efreet_cache_log_dom = eina_log_domain_register("efreet_cache", EFREET_DEFAULT_LOG_COLOR);
    if (_efreet_cache_log_dom < 0)
        return 0;

    EFREET_EVENT_ICON_CACHE_UPDATE = ecore_event_type_new();
    EFREET_EVENT_DESKTOP_CACHE_UPDATE = ecore_event_type_new();

    themes = eina_hash_string_superfast_new(EINA_FREE_CB(efreet_cache_icon_theme_free));
    icons = eina_hash_string_superfast_new(EINA_FREE_CB(efreet_cache_icon_free));
    fallbacks = eina_hash_string_superfast_new(EINA_FREE_CB(efreet_cache_icon_fallback_free));
    desktops = eina_hash_string_superfast_new(NULL);

    snprintf(buf, sizeof(buf), "%s/efreet", efreet_cache_home_get());
    if (!ecore_file_exists(buf))
    {
        if (!ecore_file_mkpath(buf)) goto error;
        efreet_setowner(buf);
    }

    if (efreet_cache_update)
    {
        cache_exe_handler = ecore_event_handler_add(ECORE_EXE_EVENT_DEL,
                                                    cache_exe_cb, NULL);
        if (!cache_exe_handler) goto error;

        cache_monitor = ecore_file_monitor_add(buf,
                                               cache_update_cb,
                                               NULL);
        if (!cache_monitor) goto error;

        efreet_cache_icon_update();
        efreet_cache_desktop_update();
    }

    return 1;
error:
    if (themes) eina_hash_free(themes);
    themes = NULL;
    if (icons) eina_hash_free(icons);
    icons = NULL;
    if (fallbacks) eina_hash_free(fallbacks);
    fallbacks = NULL;
    if (desktops) eina_hash_free(desktops);
    desktops = NULL;

    if (cache_exe_handler) ecore_event_handler_del(cache_exe_handler);
    cache_exe_handler = NULL;
    if (cache_monitor) ecore_file_monitor_del(cache_monitor);
    cache_monitor = NULL;
    efreet_cache_edd_shutdown();
    return 0;
}

void
efreet_cache_shutdown(void)
{
    Efreet_Old_Cache *d;
    void *data;

    IF_RELEASE(theme_name);

    icon_cache = efreet_cache_close(icon_cache);
    icon_theme_cache = efreet_cache_close(icon_theme_cache);

    IF_FREE_HASH(themes);
    IF_FREE_HASH(icons);
    IF_FREE_HASH(fallbacks);

    IF_FREE_HASH_CB(desktops, EINA_FREE_CB(efreet_cache_desktop_free));
    efreet_cache_array_string_free(desktop_dirs);
    desktop_dirs = NULL;
    EINA_LIST_FREE(desktop_dirs_add, data)
        eina_stringshare_del(data);
    desktop_cache = efreet_cache_close(desktop_cache);
    IF_RELEASE(desktop_cache_file);

    if (cache_exe_handler) ecore_event_handler_del(cache_exe_handler);
    cache_exe_handler = NULL;
    if (cache_monitor) ecore_file_monitor_del(cache_monitor);
    cache_monitor = NULL;

    efreet_cache_edd_shutdown();
    if (desktop_cache_timer)
    {
        ecore_timer_del(desktop_cache_timer);
        desktop_cache_timer = NULL;
    }
    IF_RELEASE(icon_theme_cache_file);
    if (icon_cache_exe_lock > 0)
    {
        close(icon_cache_exe_lock);
        icon_cache_exe_lock = -1;
    }

    if (desktop_cache_exe_lock > 0)
    {
        close(desktop_cache_exe_lock);
        desktop_cache_exe_lock = -1;
    }

    EINA_LIST_FREE(old_desktop_caches, d)
    {
        eina_hash_free(d->hash);
        free(d);
    }

    IF_RELEASE(util_cache_names_key);
    efreet_cache_array_string_free(util_cache_names);
    util_cache_names = NULL;

    IF_RELEASE(util_cache_hash_key);
    if (util_cache_hash)
    {
        eina_hash_free(util_cache_hash->hash);
        free(util_cache_hash);
        util_cache_hash = NULL;
    }

    util_cache = efreet_cache_close(util_cache);
    IF_RELEASE(util_cache_file);

    eina_log_domain_unregister(_efreet_cache_log_dom);
    _efreet_cache_log_dom = -1;
}

/*
 * Needs EAPI because of helper binaries
 */
EAPI const char *
efreet_icon_cache_file(const char *theme)
{
    static char cache_file[PATH_MAX] = { '\0' };
    const char *cache;

    cache = efreet_cache_home_get();

    snprintf(cache_file, sizeof(cache_file), "%s/efreet/icons_%s_%s.eet", cache, theme, efreet_hostname_get());

    return cache_file;
}

/*
 * Needs EAPI because of helper binaries
 */
EAPI const char *
efreet_icon_theme_cache_file(void)
{
    char tmp[PATH_MAX] = { '\0' };

    if (icon_theme_cache_file) return icon_theme_cache_file;

    snprintf(tmp, sizeof(tmp), "%s/efreet/icon_themes_%s.eet",
             efreet_cache_home_get(), efreet_hostname_get());
    icon_theme_cache_file = eina_stringshare_add(tmp);

    return icon_theme_cache_file;
}

/*
 * Needs EAPI because of helper binaries
 */
EAPI const char *
efreet_desktop_util_cache_file(void)
{
    char tmp[PATH_MAX] = { '\0' };
    const char *cache_dir, *lang, *country, *modifier;

    if (util_cache_file) return util_cache_file;

    cache_dir = efreet_cache_home_get();
    lang = efreet_lang_get();
    country = efreet_lang_country_get();
    modifier = efreet_lang_modifier_get();

    if (lang && country && modifier)
        snprintf(tmp, sizeof(tmp), "%s/efreet/desktop_util_%s_%s_%s@%s.eet", cache_dir, efreet_hostname_get(), lang, country, modifier);
    else if (lang && country)
        snprintf(tmp, sizeof(tmp), "%s/efreet/desktop_util_%s_%s_%s.eet", cache_dir, efreet_hostname_get(), lang, country);
    else if (lang)
        snprintf(tmp, sizeof(tmp), "%s/efreet/desktop_util_%s_%s.eet", cache_dir, efreet_hostname_get(), lang);
    else
        snprintf(tmp, sizeof(tmp), "%s/efreet/desktop_util_%s.eet", cache_dir, efreet_hostname_get());

    util_cache_file = eina_stringshare_add(tmp);
    return util_cache_file;
}

/*
 * Needs EAPI because of helper binaries
 */
EAPI Eet_Data_Descriptor *
efreet_version_edd(void)
{
    Eet_Data_Descriptor_Class eddc;

    if (version_edd) return version_edd;

    EET_EINA_FILE_DATA_DESCRIPTOR_CLASS_SET(&eddc, Efreet_Cache_Version);
    version_edd = eet_data_descriptor_file_new(&eddc);
    if (!version_edd) return NULL;

    EET_DATA_DESCRIPTOR_ADD_BASIC(version_edd, Efreet_Cache_Version,
                                  "minor", minor, EET_T_UCHAR);
    EET_DATA_DESCRIPTOR_ADD_BASIC(version_edd, Efreet_Cache_Version,
                                  "major", major, EET_T_UCHAR);

    return version_edd;
}

/*
 * Needs EAPI because of helper binaries
 */
EAPI Eet_Data_Descriptor *
efreet_hash_array_string_edd(void)
{
    Eet_Data_Descriptor_Class eddc;

    if (hash_array_string_edd) return hash_array_string_edd;

    EET_EINA_FILE_DATA_DESCRIPTOR_CLASS_SET(&eddc, Efreet_Cache_Hash);
    eddc.func.hash_add = hash_array_string_add;
    hash_array_string_edd = eet_data_descriptor_file_new(&eddc);
    if (!hash_array_string_edd) return NULL;

    EET_DATA_DESCRIPTOR_ADD_HASH(hash_array_string_edd, Efreet_Cache_Hash,
                                  "hash", hash, efreet_array_string_edd());

    return hash_array_string_edd;
}

/*
 * Needs EAPI because of helper binaries
 */
EAPI Eet_Data_Descriptor *
efreet_hash_string_edd(void)
{
    Eet_Data_Descriptor_Class eddc;

    if (hash_string_edd) return hash_string_edd;

    EET_EINA_FILE_DATA_DESCRIPTOR_CLASS_SET(&eddc, Efreet_Cache_Hash);
    hash_string_edd = eet_data_descriptor_file_new(&eddc);
    if (!hash_string_edd) return NULL;

    EET_DATA_DESCRIPTOR_ADD_HASH_STRING(hash_string_edd, Efreet_Cache_Hash,
                                  "hash", hash);

    return hash_string_edd;
}

/*
 * Needs EAPI because of helper binaries
 */
EAPI Eet_Data_Descriptor *
efreet_array_string_edd(void)
{
    Eet_Data_Descriptor_Class eddc;

    if (array_string_edd) return array_string_edd;

    EET_EINA_FILE_DATA_DESCRIPTOR_CLASS_SET(&eddc, Efreet_Cache_Array_String);
    array_string_edd = eet_data_descriptor_file_new(&eddc);
    if (!array_string_edd) return NULL;
    EET_DATA_DESCRIPTOR_ADD_VAR_ARRAY_STRING(array_string_edd, Efreet_Cache_Array_String,
                                             "array", array);

    return array_string_edd;
}

/*
 * Needs EAPI because of helper binaries
 */
EAPI const char *
efreet_desktop_cache_file(void)
{
    char tmp[PATH_MAX] = { '\0' };
    const char *cache, *lang, *country, *modifier;

    if (desktop_cache_file) return desktop_cache_file;

    cache = efreet_cache_home_get();
    lang = efreet_lang_get();
    country = efreet_lang_country_get();
    modifier = efreet_lang_modifier_get();

    if (lang && country && modifier)
        snprintf(tmp, sizeof(tmp), "%s/efreet/desktop_%s_%s_%s@%s.eet", cache, efreet_hostname_get(), lang, country, modifier);
    else if (lang && country)
        snprintf(tmp, sizeof(tmp), "%s/efreet/desktop_%s_%s_%s.eet", cache, efreet_hostname_get(), lang, country);
    else if (lang)
        snprintf(tmp, sizeof(tmp), "%s/efreet/desktop_%s_%s.eet", cache, efreet_hostname_get(), lang);
    else
        snprintf(tmp, sizeof(tmp), "%s/efreet/desktop_%s.eet", cache, efreet_hostname_get());

    desktop_cache_file = eina_stringshare_add(tmp);
    return desktop_cache_file;
}

#define EDD_SHUTDOWN(Edd)                       \
    if (Edd) eet_data_descriptor_free(Edd);       \
Edd = NULL;

static void
efreet_cache_edd_shutdown(void)
{
    EDD_SHUTDOWN(version_edd);
    EDD_SHUTDOWN(desktop_edd);
    EDD_SHUTDOWN(hash_array_string_edd);
    EDD_SHUTDOWN(array_string_edd);
    EDD_SHUTDOWN(hash_string_edd);
    EDD_SHUTDOWN(icon_theme_edd);
    EDD_SHUTDOWN(icon_theme_directory_edd);
    EDD_SHUTDOWN(directory_edd);
    EDD_SHUTDOWN(icon_fallback_edd);
    EDD_SHUTDOWN(icon_element_pointer_edd);
    EDD_SHUTDOWN(icon_element_edd);
    EDD_SHUTDOWN(icon_edd);
}

#define EFREET_POINTER_TYPE(Edd_Dest, Edd_Source, Type)   \
{                                                                     \
    typedef struct _Efreet_##Type##_Pointer Efreet_##Type##_Pointer;   \
    struct _Efreet_##Type##_Pointer                                    \
    {                                                                  \
        Efreet_##Type *pointer;                                         \
    };                                                                 \
    \
    EET_EINA_FILE_DATA_DESCRIPTOR_CLASS_SET(&eddc, Efreet_##Type##_Pointer); \
    Edd_Dest = eet_data_descriptor_file_new(&eddc);                    \
    EET_DATA_DESCRIPTOR_ADD_SUB(Edd_Dest, Efreet_##Type##_Pointer,     \
                                "pointer", pointer, Edd_Source);       \
}

static Eet_Data_Descriptor *
efreet_icon_directory_edd(void)
{
    Eet_Data_Descriptor_Class eddc;

    if (directory_edd) return directory_edd;

    EET_EINA_FILE_DATA_DESCRIPTOR_CLASS_SET(&eddc, Efreet_Cache_Directory);
    directory_edd = eet_data_descriptor_file_new(&eddc);
    if (!directory_edd) return NULL;

    EET_DATA_DESCRIPTOR_ADD_BASIC(directory_edd, Efreet_Cache_Directory,
                                  "modified_time", modified_time, EET_T_LONG_LONG);

    return directory_edd;
}

/*
 * Needs EAPI because of helper binaries
 */
EAPI Eet_Data_Descriptor *
efreet_icon_edd(void)
{
    Eet_Data_Descriptor_Class eddc;

    if (icon_edd) return icon_edd;

    EET_EINA_FILE_DATA_DESCRIPTOR_CLASS_SET(&eddc, Efreet_Cache_Icon_Element);
    icon_element_edd = eet_data_descriptor_file_new(&eddc);
    if (!icon_element_edd) return NULL;

    EET_DATA_DESCRIPTOR_ADD_BASIC(icon_element_edd, Efreet_Cache_Icon_Element,
                                  "type", type, EET_T_USHORT);
    EET_DATA_DESCRIPTOR_ADD_BASIC(icon_element_edd, Efreet_Cache_Icon_Element,
                                  "normal", normal, EET_T_USHORT);
    EET_DATA_DESCRIPTOR_ADD_BASIC(icon_element_edd, Efreet_Cache_Icon_Element,
                                  "normal", normal, EET_T_USHORT);
    EET_DATA_DESCRIPTOR_ADD_BASIC(icon_element_edd, Efreet_Cache_Icon_Element,
                                  "min", min, EET_T_USHORT);
    EET_DATA_DESCRIPTOR_ADD_BASIC(icon_element_edd, Efreet_Cache_Icon_Element,
                                  "max", max, EET_T_USHORT);
    EET_DATA_DESCRIPTOR_ADD_VAR_ARRAY_STRING(icon_element_edd, Efreet_Cache_Icon_Element,
                                             "paths", paths);

    EFREET_POINTER_TYPE(icon_element_pointer_edd, icon_element_edd, Cache_Icon_Element);

    EET_EINA_FILE_DATA_DESCRIPTOR_CLASS_SET(&eddc, Efreet_Cache_Icon);
    icon_edd = eet_data_descriptor_file_new(&eddc);
    if (!icon_edd) return NULL;

    EET_DATA_DESCRIPTOR_ADD_BASIC(icon_edd, Efreet_Cache_Icon,
                                  "theme", theme, EET_T_STRING);
    EET_DATA_DESCRIPTOR_ADD_VAR_ARRAY(icon_edd, Efreet_Cache_Icon,
                                      "icons", icons, icon_element_pointer_edd);

    return icon_edd;
}

/*
 * Needs EAPI because of helper binaries
 */
EAPI Eet_Data_Descriptor *
efreet_icon_theme_edd(Eina_Bool cache)
{
    Eet_Data_Descriptor_Class eddc;

    if (icon_theme_edd) return icon_theme_edd;

    EET_EINA_FILE_DATA_DESCRIPTOR_CLASS_SET(&eddc, Efreet_Icon_Theme_Directory);
    icon_theme_directory_edd = eet_data_descriptor_file_new(&eddc);
    if (!icon_theme_directory_edd) return NULL;

    EET_DATA_DESCRIPTOR_ADD_BASIC(icon_theme_directory_edd, Efreet_Icon_Theme_Directory,
                                  "name", name, EET_T_STRING);
    EET_DATA_DESCRIPTOR_ADD_BASIC(icon_theme_directory_edd, Efreet_Icon_Theme_Directory,
                                  "context", context, EET_T_UCHAR);
    EET_DATA_DESCRIPTOR_ADD_BASIC(icon_theme_directory_edd, Efreet_Icon_Theme_Directory,
                                  "type", type, EET_T_UCHAR);
    EET_DATA_DESCRIPTOR_ADD_BASIC(icon_theme_directory_edd, Efreet_Icon_Theme_Directory,
                                  "size.normal", size.normal, EET_T_UINT);
    EET_DATA_DESCRIPTOR_ADD_BASIC(icon_theme_directory_edd, Efreet_Icon_Theme_Directory,
                                  "size.min", size.min, EET_T_UINT);
    EET_DATA_DESCRIPTOR_ADD_BASIC(icon_theme_directory_edd, Efreet_Icon_Theme_Directory,
                                  "size.max", size.max, EET_T_UINT);
    EET_DATA_DESCRIPTOR_ADD_BASIC(icon_theme_directory_edd, Efreet_Icon_Theme_Directory,
                                  "size.threshold", size.threshold, EET_T_UINT);

    EET_EINA_FILE_DATA_DESCRIPTOR_CLASS_SET(&eddc, Efreet_Cache_Icon_Theme);
    icon_theme_edd = eet_data_descriptor_file_new(&eddc);
    if (!icon_theme_edd) return NULL;

    EET_DATA_DESCRIPTOR_ADD_BASIC(icon_theme_edd, Efreet_Cache_Icon_Theme,
                                  "name.internal", theme.name.internal, EET_T_STRING);
    EET_DATA_DESCRIPTOR_ADD_BASIC(icon_theme_edd, Efreet_Cache_Icon_Theme,
                                  "name.name", theme.name.name, EET_T_STRING);
    EET_DATA_DESCRIPTOR_ADD_BASIC(icon_theme_edd, Efreet_Cache_Icon_Theme,
                                  "comment", theme.comment, EET_T_STRING);
    EET_DATA_DESCRIPTOR_ADD_BASIC(icon_theme_edd, Efreet_Cache_Icon_Theme,
                                  "example_icon", theme.example_icon, EET_T_STRING);

    eet_data_descriptor_element_add(icon_theme_edd, "paths", EET_T_STRING, EET_G_LIST,
                                    offsetof(Efreet_Cache_Icon_Theme, theme.paths), 0, NULL, NULL);
    eet_data_descriptor_element_add(icon_theme_edd, "inherits", EET_T_STRING, EET_G_LIST,
                                    offsetof(Efreet_Cache_Icon_Theme, theme.inherits), 0, NULL, NULL);
    EET_DATA_DESCRIPTOR_ADD_LIST(icon_theme_edd, Efreet_Cache_Icon_Theme,
                                  "directories", theme.directories, icon_theme_directory_edd);

    if (cache)
    {
        EET_DATA_DESCRIPTOR_ADD_BASIC(icon_theme_edd, Efreet_Cache_Icon_Theme,
                                      "last_cache_check", last_cache_check, EET_T_LONG_LONG);

        EET_DATA_DESCRIPTOR_ADD_BASIC(icon_theme_edd, Efreet_Cache_Icon_Theme,
                                      "path", path, EET_T_STRING);

        EET_DATA_DESCRIPTOR_ADD_HASH(icon_theme_edd, Efreet_Cache_Icon_Theme,
                                     "dirs", dirs, efreet_icon_directory_edd());
    }

    return icon_theme_edd;
}

/*
 * Needs EAPI because of helper binaries
 */
EAPI Eet_Data_Descriptor *
efreet_icon_fallback_edd(void)
{
    Eet_Data_Descriptor_Class eddc;

    if (icon_fallback_edd) return icon_fallback_edd;

    EET_EINA_FILE_DATA_DESCRIPTOR_CLASS_SET(&eddc, Efreet_Cache_Fallback_Icon);
    icon_fallback_edd = eet_data_descriptor_file_new(&eddc);
    if (!icon_fallback_edd) return NULL;

    EET_DATA_DESCRIPTOR_ADD_VAR_ARRAY_STRING(icon_fallback_edd,
                                             Efreet_Cache_Fallback_Icon, "icons", icons);

    return icon_fallback_edd;
}

/*
 * Needs EAPI because of helper binaries
 */
EAPI Eet_Data_Descriptor *
efreet_desktop_edd(void)
{
    Eet_Data_Descriptor_Class eddc;

    if (desktop_edd) return desktop_edd;

    EET_EINA_FILE_DATA_DESCRIPTOR_CLASS_SET(&eddc, Efreet_Desktop);
    desktop_edd = eet_data_descriptor_file_new(&eddc);
    if (!desktop_edd) return NULL;

    EET_DATA_DESCRIPTOR_ADD_BASIC(desktop_edd, Efreet_Desktop, "type", type, EET_T_INT);
    EET_DATA_DESCRIPTOR_ADD_BASIC(desktop_edd, Efreet_Desktop, "version", version, EET_T_STRING);
    EET_DATA_DESCRIPTOR_ADD_BASIC(desktop_edd, Efreet_Desktop, "orig_path", orig_path, EET_T_STRING);
    EET_DATA_DESCRIPTOR_ADD_BASIC(desktop_edd, Efreet_Desktop, "load_time", load_time, EET_T_LONG_LONG);
    EET_DATA_DESCRIPTOR_ADD_BASIC(desktop_edd, Efreet_Desktop, "name", name, EET_T_STRING);
    EET_DATA_DESCRIPTOR_ADD_BASIC(desktop_edd, Efreet_Desktop, "generic_name", generic_name, EET_T_STRING);
    EET_DATA_DESCRIPTOR_ADD_BASIC(desktop_edd, Efreet_Desktop, "comment", comment, EET_T_STRING);
    EET_DATA_DESCRIPTOR_ADD_BASIC(desktop_edd, Efreet_Desktop, "icon", icon, EET_T_STRING);
    EET_DATA_DESCRIPTOR_ADD_BASIC(desktop_edd, Efreet_Desktop, "try_exec", try_exec, EET_T_STRING);
    EET_DATA_DESCRIPTOR_ADD_BASIC(desktop_edd, Efreet_Desktop, "exec", exec, EET_T_STRING);
    EET_DATA_DESCRIPTOR_ADD_BASIC(desktop_edd, Efreet_Desktop, "path", path, EET_T_STRING);
    EET_DATA_DESCRIPTOR_ADD_BASIC(desktop_edd, Efreet_Desktop, "startup_wm_class", startup_wm_class, EET_T_STRING);
    EET_DATA_DESCRIPTOR_ADD_BASIC(desktop_edd, Efreet_Desktop, "url", url, EET_T_STRING);
    eet_data_descriptor_element_add(desktop_edd, "only_show_in", EET_T_STRING, EET_G_LIST, offsetof(Efreet_Desktop, only_show_in), 0, NULL, NULL);
    eet_data_descriptor_element_add(desktop_edd, "not_show_in", EET_T_STRING, EET_G_LIST, offsetof(Efreet_Desktop, not_show_in), 0, NULL, NULL);
    eet_data_descriptor_element_add(desktop_edd, "categories", EET_T_STRING, EET_G_LIST, offsetof(Efreet_Desktop, categories), 0, NULL, NULL);
    eet_data_descriptor_element_add(desktop_edd, "mime_types", EET_T_STRING, EET_G_LIST, offsetof(Efreet_Desktop, mime_types), 0, NULL, NULL);
    eet_data_descriptor_element_add(desktop_edd, "x", EET_T_STRING, EET_G_HASH, offsetof(Efreet_Desktop, x), 0, NULL, NULL);
    EET_DATA_DESCRIPTOR_ADD_BASIC(desktop_edd, Efreet_Desktop, "no_display", no_display, EET_T_UCHAR);
    EET_DATA_DESCRIPTOR_ADD_BASIC(desktop_edd, Efreet_Desktop, "hidden", hidden, EET_T_UCHAR);
    EET_DATA_DESCRIPTOR_ADD_BASIC(desktop_edd, Efreet_Desktop, "terminal", terminal, EET_T_UCHAR);
    EET_DATA_DESCRIPTOR_ADD_BASIC(desktop_edd, Efreet_Desktop, "startup_notify", startup_notify, EET_T_UCHAR);

    return desktop_edd;
}

Efreet_Cache_Icon *
efreet_cache_icon_find(Efreet_Icon_Theme *theme, const char *icon)
{
    Efreet_Cache_Icon *cache = NULL;

    if (theme_name && strcmp(theme_name, theme->name.internal))
    {
        /* FIXME: this is bad if people have pointer to this cache, things will go wrong */
        INF("theme_name change from `%s` to `%s`", theme_name, theme->name.internal);
        IF_RELEASE(theme_name);
        icon_cache = efreet_cache_close(icon_cache);
        eina_hash_free(icons);
        icons = eina_hash_string_superfast_new(EINA_FREE_CB(efreet_cache_icon_free));
    }

    if (!efreet_cache_check(&icon_cache, efreet_icon_cache_file(theme->name.internal), EFREET_ICON_CACHE_MAJOR)) return NULL;
    if (!theme_name)
        theme_name = eina_stringshare_add(theme->name.internal);

    cache = eina_hash_find(icons, icon);
    if (cache == NON_EXISTING) return NULL;
    if (cache) return cache;

    cache = eet_data_read(icon_cache, efreet_icon_edd(), icon);
    if (cache)
        eina_hash_add(icons, icon, cache);
    else
        eina_hash_add(icons, icon, NON_EXISTING);
    return cache;
}

Efreet_Cache_Fallback_Icon *
efreet_cache_icon_fallback_find(const char *icon)
{
    Efreet_Cache_Fallback_Icon *cache;

    if (!efreet_cache_check(&fallback_cache, efreet_icon_cache_file(EFREET_CACHE_ICON_FALLBACK), EFREET_ICON_CACHE_MAJOR)) return NULL;

    cache = eina_hash_find(fallbacks, icon);
    if (cache == NON_EXISTING) return NULL;
    if (cache) return cache;

    cache = eet_data_read(fallback_cache, efreet_icon_fallback_edd(), icon);
    if (cache)
        eina_hash_add(fallbacks, icon, cache);
    else
        eina_hash_add(fallbacks, icon, NON_EXISTING);
    return cache;
}

Efreet_Icon_Theme *
efreet_cache_icon_theme_find(const char *theme)
{
    Efreet_Cache_Icon_Theme *cache;

    if (!efreet_cache_check(&icon_theme_cache, efreet_icon_theme_cache_file(), EFREET_ICON_CACHE_MAJOR)) return NULL;

    cache = eina_hash_find(themes, theme);
    if (cache == NON_EXISTING) return NULL;
    if (cache) return &(cache->theme);

    cache = eet_data_read(icon_theme_cache, efreet_icon_theme_edd(EINA_FALSE), theme);
    if (cache)
    {
        eina_hash_add(themes, theme, cache);
        return &(cache->theme);
    }
    else
        eina_hash_add(themes, theme, NON_EXISTING);
    return NULL;
}

static void
efreet_cache_icon_free(Efreet_Cache_Icon *icon)
{
    unsigned int i;

    if (!icon) return;
    if (icon == NON_EXISTING) return;

    for (i = 0; i < icon->icons_count; ++i)
    {
        free(icon->icons[i]->paths);
        free(icon->icons[i]);
    }

    free(icon->icons);
    free(icon);
}

static void
efreet_cache_icon_fallback_free(Efreet_Cache_Fallback_Icon *icon)
{
    if (!icon) return;
    if (icon == NON_EXISTING) return;

    free(icon->icons);
    free(icon);
}

static void
efreet_cache_icon_theme_free(Efreet_Icon_Theme *theme)
{
    void *data;

    if (!theme) return;
    if (theme == NON_EXISTING) return;

    eina_list_free(theme->paths);
    eina_list_free(theme->inherits);
    EINA_LIST_FREE(theme->directories, data)
        free(data);

    free(theme);
}

Eina_List *
efreet_cache_icon_theme_list(void)
{
    Eina_List *ret = NULL;
    char **keys;
    int i, num;

    if (!efreet_cache_check(&icon_theme_cache, efreet_icon_theme_cache_file(), EFREET_ICON_CACHE_MAJOR)) return NULL;
    keys = eet_list(icon_theme_cache, "*", &num);
    for (i = 0; i < num; i++)
    {
        Efreet_Icon_Theme *theme;
        if (!strncmp(keys[i], "__efreet", 8)) continue;

        theme = eina_hash_find(themes, keys[i]);
        if (!theme)
            theme = efreet_cache_icon_theme_find(keys[i]);
        if (theme && theme != NON_EXISTING)
            ret = eina_list_append(ret, theme);
    }
    free(keys);
    return ret;
}

/*
 * Needs EAPI because of helper binaries
 */
EAPI void
efreet_cache_array_string_free(Efreet_Cache_Array_String *array)
{
    if (!array) return;
    free(array->array);
    free(array);
}

Efreet_Desktop *
efreet_cache_desktop_find(const char *file)
{
    Efreet_Desktop *cache;
    char rp[PATH_MAX];

    if (!realpath(file, rp)) return NULL;

    if (!efreet_cache_check(&desktop_cache, efreet_desktop_cache_file(), EFREET_DESKTOP_CACHE_MAJOR)) return NULL;

    cache = eina_hash_find(desktops, rp);
    if (cache == NON_EXISTING) return NULL;
    if (cache) return cache;

    cache = eet_data_read(desktop_cache, efreet_desktop_edd(), rp);
    if (cache)
    {
        cache->eet = 1;
        eina_hash_add(desktops, cache->orig_path, cache);
    }
    else
        eina_hash_add(desktops, rp, NON_EXISTING);
    return cache;
}

void
efreet_cache_desktop_free(Efreet_Desktop *desktop)
{
    Efreet_Old_Cache *d;
    Efreet_Desktop *curr;
    Eina_List *l;

    if (!desktop ||
        desktop == NON_EXISTING ||
        !desktop->eet) return;

    curr = eina_hash_find(desktops, desktop->orig_path);
    if (curr == desktop)
        eina_hash_del_by_key(desktops, desktop->orig_path);

    EINA_LIST_FOREACH(old_desktop_caches, l, d)
    {
        curr = eina_hash_find(d->hash, desktop->orig_path);
        if (curr && curr == desktop)
        {
            eina_hash_del_by_key(d->hash, desktop->orig_path);
            if (eina_hash_population(d->hash) == 0)
            {
                eina_hash_free(d->hash);
                d->hash = NULL;
            }
            break;
        }
    }

    eina_list_free(desktop->only_show_in);
    eina_list_free(desktop->not_show_in);
    eina_list_free(desktop->categories);
    eina_list_free(desktop->mime_types);
    IF_FREE_HASH(desktop->x);
    free(desktop);
}

void
efreet_cache_desktop_add(Efreet_Desktop *desktop)
{
    char buf[PATH_MAX];
    char *dir;
    Efreet_Cache_Array_String *arr;

    /*
     * Read file from disk, save path in cache so it will be included in next
     * cache update
     */
    strncpy(buf, desktop->orig_path, PATH_MAX);
    buf[PATH_MAX - 1] = '\0';
    dir = dirname(buf);
    arr = efreet_cache_desktop_dirs();
    if (arr)
    {
        unsigned int i;

        for (i = 0; i < arr->array_count; i++)
        {
            /* Check if we already have this dir in cache */
            if (!strncmp(dir, arr->array[i], strlen(arr->array[i])))
                return;
        }
    }
    if (!eina_list_search_unsorted_list(desktop_dirs_add, strcmplen, dir))
        desktop_dirs_add = eina_list_append(desktop_dirs_add, eina_stringshare_add(dir));

    efreet_cache_desktop_update();
}

Efreet_Cache_Array_String *
efreet_cache_desktop_dirs(void)
{
    if (desktop_dirs) return desktop_dirs;

    if (!efreet_cache_check(&desktop_cache, efreet_desktop_cache_file(), EFREET_DESKTOP_CACHE_MAJOR)) return NULL;

    desktop_dirs = eet_data_read(desktop_cache, efreet_array_string_edd(), EFREET_CACHE_DESKTOP_DIRS);
    return desktop_dirs;
}

void
efreet_cache_desktop_update(void)
{
    if (!efreet_cache_update) return;

    if (desktop_cache_timer)
        ecore_timer_delay(desktop_cache_timer, 0.2);
    else
        desktop_cache_timer = ecore_timer_add(0.2, desktop_cache_update_cache_cb, NULL);
}

void
efreet_cache_icon_update(void)
{
    if (!efreet_cache_update) return;

    if (icon_cache_timer)
        ecore_timer_delay(icon_cache_timer, 0.2);
    else
        icon_cache_timer = ecore_timer_add(0.2, icon_cache_update_cache_cb, NULL);
}

static Eina_Bool
efreet_cache_check(Eet_File **ef, const char *path, int major)
{
    Efreet_Cache_Version *version;

    if (*ef == NON_EXISTING) return EINA_FALSE;
    if (*ef) return EINA_TRUE;
    if (!*ef)
        *ef = eet_open(path, EET_FILE_MODE_READ);
    if (!*ef)
    {
        *ef = NON_EXISTING;
        return EINA_FALSE;
    }

    version = eet_data_read(*ef, efreet_version_edd(), EFREET_CACHE_VERSION);
    if ((!version) || (version->major != major))
    {
        IF_FREE(version);
        eet_close(*ef);
        *ef = NON_EXISTING;
        return EINA_FALSE;
    }
    free(version);
    return EINA_TRUE;
}

static void *
efreet_cache_close(Eet_File *ef)
{
    if (ef && ef != NON_EXISTING)
        eet_close(ef);
    return NULL;
}

Efreet_Cache_Hash *
efreet_cache_util_hash_string(const char *key)
{
    if (util_cache_hash_key && !strcmp(key, util_cache_hash_key))
        return util_cache_hash;
    if (!efreet_cache_check(&util_cache, efreet_desktop_util_cache_file(), EFREET_DESKTOP_UTILS_CACHE_MAJOR)) return NULL;

    if (util_cache_hash)
    {
        /* free previous util_cache */
        IF_RELEASE(util_cache_hash_key);
        eina_hash_free(util_cache_hash->hash);
        free(util_cache_hash);
    }
    util_cache_hash_key = eina_stringshare_add(key);
    util_cache_hash = eet_data_read(util_cache, efreet_hash_string_edd(), key);
    return util_cache_hash;
}

Efreet_Cache_Hash *
efreet_cache_util_hash_array_string(const char *key)
{
    if (util_cache_hash_key && !strcmp(key, util_cache_hash_key))
        return util_cache_hash;
    if (!efreet_cache_check(&util_cache, efreet_desktop_util_cache_file(), EFREET_DESKTOP_UTILS_CACHE_MAJOR)) return NULL;

    IF_RELEASE(util_cache_hash_key);
    if (util_cache_hash)
    {
        /* free previous cache */
        eina_hash_free(util_cache_hash->hash);
        free(util_cache_hash);
    }
    util_cache_hash_key = eina_stringshare_add(key);
    util_cache_hash = eet_data_read(util_cache, efreet_hash_array_string_edd(), key);
    return util_cache_hash;
}

Efreet_Cache_Array_String *
efreet_cache_util_names(const char *key)
{
    if (util_cache_names_key && !strcmp(key, util_cache_names_key))
        return util_cache_names;
    if (!efreet_cache_check(&util_cache, efreet_desktop_util_cache_file(), EFREET_DESKTOP_UTILS_CACHE_MAJOR)) return NULL;

    if (util_cache_names)
    {
        /* free previous util_cache */
        IF_RELEASE(util_cache_names_key);
        efreet_cache_array_string_free(util_cache_names);
    }
    util_cache_names_key = eina_stringshare_add(key);
    util_cache_names = eet_data_read(util_cache, efreet_array_string_edd(), key);
    return util_cache_names;
}

static Eina_Bool
cache_exe_cb(void *data __UNUSED__, int type __UNUSED__, void *event)
{
    Ecore_Exe_Event_Del *ev;

    ev = event;
    if (ev->exe == desktop_cache_exe)
    {
        if (desktop_cache_exe_lock > 0)
        {
            close(desktop_cache_exe_lock);
            desktop_cache_exe_lock = -1;
        }
        desktop_cache_exe = NULL;
    }
    else if (ev->exe == icon_cache_exe)
    {
        if (icon_cache_exe_lock > 0)
        {
            close(icon_cache_exe_lock);
            icon_cache_exe_lock = -1;
        }
        icon_cache_exe = NULL;
    }
    return ECORE_CALLBACK_RENEW;
}

static void
cache_update_cb(void *data __UNUSED__, Ecore_File_Monitor *em __UNUSED__,
                Ecore_File_Event event, const char *path)
{
    const char *file;
    Efreet_Event_Cache_Update *ev = NULL;
    Efreet_Old_Cache *d = NULL;
    Eina_List *l = NULL;

    if (event != ECORE_FILE_EVENT_CLOSED)
        return;

    file = ecore_file_file_get(path);
    if (!file) return;
    if (!strcmp(file, "desktop_data.update"))
    {
        ev = NEW(Efreet_Event_Cache_Update, 1);
        if (!ev) goto error;

        IF_RELEASE(util_cache_names_key);
        IF_RELEASE(util_cache_hash_key);

        d = NEW(Efreet_Old_Cache, 1);
        if (!d) goto error;
        d->hash = desktops;
        d->ef = desktop_cache;
        old_desktop_caches = eina_list_append(old_desktop_caches, d);

        efreet_cache_array_string_free(desktop_dirs);
        desktop_dirs = NULL;
        desktops = eina_hash_string_superfast_new(NULL);
        desktop_cache = NULL;

        efreet_cache_array_string_free(util_cache_names);
        util_cache_names = NULL;

        if (util_cache_hash)
        {
            eina_hash_free(util_cache_hash->hash);
            free(util_cache_hash);
            util_cache_hash = NULL;
        }

        util_cache = efreet_cache_close(util_cache);

        ecore_event_add(EFREET_EVENT_DESKTOP_CACHE_UPDATE, ev, desktop_cache_update_free, d);
        /* TODO: Check if desktop_dirs_add exists, and rebuild cache if */
    }
    else if (!strcmp(file, "icon_data.update"))
    {
        ev = NEW(Efreet_Event_Cache_Update, 1);
        if (!ev) goto error;

        IF_RELEASE(theme_name);

        /* Save all old caches */
        d = NEW(Efreet_Old_Cache, 1);
        if (!d) goto error;
        d->hash = themes;
        d->ef = icon_theme_cache;
        l = eina_list_append(l, d);

        d = NEW(Efreet_Old_Cache, 1);
        if (!d) goto error;
        d->hash = icons;
        d->ef = icon_cache;
        l = eina_list_append(l, d);

        d = NEW(Efreet_Old_Cache, 1);
        if (!d) goto error;
        d->hash = fallbacks;
        d->ef = fallback_cache;
        l = eina_list_append(l, d);

        /* Create new empty caches */
        themes = eina_hash_string_superfast_new(EINA_FREE_CB(efreet_cache_icon_theme_free));
        icons = eina_hash_string_superfast_new(EINA_FREE_CB(efreet_cache_icon_free));
        fallbacks = eina_hash_string_superfast_new(EINA_FREE_CB(efreet_cache_icon_fallback_free));

        icon_theme_cache = NULL;
        icon_cache = NULL;
        fallback_cache = NULL;

        /* Send event */
        ecore_event_add(EFREET_EVENT_ICON_CACHE_UPDATE, ev, icon_cache_update_free, l);
    }
    return;
error:
    IF_FREE(ev);
    IF_FREE(d);
    EINA_LIST_FREE(l, d)
        free(d);
}

static Eina_Bool
desktop_cache_update_cache_cb(void *data __UNUSED__)
{
    char file[PATH_MAX];
    struct flock fl;
    int prio;

    desktop_cache_timer = NULL;

    /* TODO: Retry update cache later */
    if (desktop_cache_exe_lock > 0) return ECORE_CALLBACK_CANCEL;

    snprintf(file, sizeof(file), "%s/efreet/desktop_exec.lock", efreet_cache_home_get());

    desktop_cache_exe_lock = open(file, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (desktop_cache_exe_lock < 0) goto error;
    efreet_fsetowner(desktop_cache_exe_lock);
    memset(&fl, 0, sizeof(struct flock));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    if (fcntl(desktop_cache_exe_lock, F_SETLK, &fl) < 0) goto error;
    prio = ecore_exe_run_priority_get();
    ecore_exe_run_priority_set(19);
    eina_strlcpy(file, PACKAGE_LIB_DIR "/efreet/efreet_desktop_cache_create", sizeof(file));
    if (desktop_dirs_add)
    {
        const char *str;

        eina_strlcat(file, " -d", sizeof(file));
        EINA_LIST_FREE(desktop_dirs_add, str)
        {
            eina_strlcat(file, " ", sizeof(file));
            eina_strlcat(file, str, sizeof(file));
            eina_stringshare_del(str);
        }
    }
    printf("Run desktop cache creation: %s\n", file);
    desktop_cache_exe = ecore_exe_run(file, NULL);
    ecore_exe_run_priority_set(prio);
    if (!desktop_cache_exe) goto error;

    return ECORE_CALLBACK_CANCEL;
error:
    if (desktop_cache_exe_lock > 0)
    {
        close(desktop_cache_exe_lock);
        desktop_cache_exe_lock = -1;
    }
    return ECORE_CALLBACK_CANCEL;
}

static Eina_Bool
icon_cache_update_cache_cb(void *data __UNUSED__)
{
    char file[PATH_MAX];
    struct flock fl;
    int prio;
    Eina_List **l, *l2;

    icon_cache_timer = NULL;

    /* TODO: Retry update cache later */
    if (icon_cache_exe_lock > 0) return ECORE_CALLBACK_CANCEL;

    snprintf(file, sizeof(file), "%s/efreet/icon_exec.lock", efreet_cache_home_get());

    icon_cache_exe_lock = open(file, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (icon_cache_exe_lock < 0) goto error;
    efreet_fsetowner(icon_cache_exe_lock);
    memset(&fl, 0, sizeof(struct flock));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    if (fcntl(icon_cache_exe_lock, F_SETLK, &fl) < 0) goto error;
    prio = ecore_exe_run_priority_get();
    ecore_exe_run_priority_set(19);
    eina_strlcpy(file, PACKAGE_LIB_DIR "/efreet/efreet_icon_cache_create", sizeof(file));
    l = efreet_icon_extra_list_get();
    if (l && eina_list_count(*l) > 0)
    {
        Eina_List *ll;
        char *p;

        eina_strlcat(file, " -d", sizeof(file));
        EINA_LIST_FOREACH(*l, ll, p)
        {
            eina_strlcat(file, " ", sizeof(file));
            eina_strlcat(file, p, sizeof(file));
        }
    }
    l2 = efreet_icon_extensions_list_get();
    if (eina_list_count(l2) > 0)
    {
        Eina_List *ll;
        char *p;

        eina_strlcat(file, " -e", sizeof(file));
        EINA_LIST_FOREACH(l2, ll, p)
        {
            eina_strlcat(file, " ", sizeof(file));
            eina_strlcat(file, p, sizeof(file));
        }
    }
    icon_cache_exe = ecore_exe_run(file, NULL);
    ecore_exe_run_priority_set(prio);
    if (!icon_cache_exe) goto error;

    return ECORE_CALLBACK_CANCEL;

error:
    if (icon_cache_exe_lock > 0)
    {
        close(icon_cache_exe_lock);
        icon_cache_exe_lock = -1;
    }
    return ECORE_CALLBACK_CANCEL;
}

static void
desktop_cache_update_free(void *data, void *ev)
{
    Efreet_Old_Cache *d;
    int dangling = 0;

    d = data;
    /*
     * All users should now had the chance to update their pointers, so we can now
     * free the old cache
     */
    if (d->hash)
    {
        Eina_Iterator *it;
        Eina_Hash_Tuple *tuple;

        it = eina_hash_iterator_tuple_new(d->hash);
        EINA_ITERATOR_FOREACH(it, tuple)
        {
            if (tuple->data == NON_EXISTING) continue;
            ERR("Efreet: %d:%s still in cache on cache close!",
                   ((Efreet_Desktop *)tuple->data)->ref, (char *)tuple->key);
            dangling++;
        }
        eina_iterator_free(it);

        eina_hash_free(d->hash);
    }
    /*
     * If there are dangling references the eet file won't be closed - to
     * avoid crashes, but this will leak instead.
     */
    if (dangling == 0)
    {
        efreet_cache_close(d->ef);
    }
    else
    {
        /* TODO: Keep in old_desktop_caches, as we might close ref later */
        ERR("Efreet: ERROR. There are still %i desktop files with old\n"
               "dangling references to desktop files. This application\n"
               "has not handled the EFREET_EVENT_DESKTOP_CACHE_UPDATE\n"
               "fully and released its references. Please fix the application\n"
               "so it does this.",
               dangling);
    }
    old_desktop_caches = eina_list_remove(old_desktop_caches, d);
    free(d);
    free(ev);
}

static void
icon_cache_update_free(void *data, void *ev)
{
    Efreet_Old_Cache *d;
    Eina_List *l;

    l = data;
    EINA_LIST_FREE(l, d)
    {
        if (d->hash)
            eina_hash_free(d->hash);
        efreet_cache_close(d->ef);
        free(d);
    }
    free(ev);
}

static void *
hash_array_string_add(void *hash, const char *key, void *data)
{
    if (!hash)
        hash = eina_hash_string_superfast_new(EINA_FREE_CB(efreet_cache_array_string_free));
    if (!hash)
        return NULL;
    eina_hash_add(hash, key, data);
    return hash;
}

static int
strcmplen(const void *data1, const void *data2)
{
    return strncmp(data1, data2, strlen(data1));
}

