#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

/* TODO: move eet file handling to eet_cache.c */
/* TODO: add no_display check, as we might want only displayable items */
/* TODO: Consider flushing local cache after a idling a while */
/* TODO: Add special check for glob "*" */

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

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <fnmatch.h>
#include <limits.h>

#include <Eet.h>
#include <Ecore_File.h>

/* define macros and variable for using the eina logging system  */
#define EFREET_MODULE_LOG_DOM _efreet_utils_log_dom
static int _efreet_utils_log_dom = -1;

#include "Efreet.h"
#include "efreet_private.h"
#include "efreet_cache_private.h"

static char *efreet_util_path_in_default(const char *section, const char *path);

static int  efreet_util_glob_match(const char *str, const char *glob);

static Eina_List *efreet_util_menus_find_helper(Eina_List *menus, const char *config_dir);

static Efreet_Desktop *efreet_util_cache_find(const char *search, const char *what1, const char *what2);
static Eina_List *efreet_util_cache_list(const char *search, const char *what);
static Eina_List *efreet_util_cache_glob_list(const char *search, const char *what);

static Efreet_Cache_Hash *efreet_util_cache_hash(Eet_Data_Descriptor *edd, const char *key);
static Efreet_Cache_Array_String *efreet_util_cache_names(Eet_Data_Descriptor *edd, const char *key);

static Eina_Hash *file_id_by_desktop_path = NULL;
static const char *cache_file = NULL;
static Eet_File *cache = NULL;

static Efreet_Cache_Hash *cache_hash = NULL;
static const char *cache_hash_key = NULL;
static Efreet_Cache_Array_String *cache_names = NULL;
static const char *cache_names_key = NULL;

static int init = 0;

int
efreet_util_init(void)
{
    if (init++) return init;
    _efreet_utils_log_dom = eina_log_domain_register
      ("efreet_util", EFREET_DEFAULT_LOG_COLOR);
    if (_efreet_utils_log_dom < 0)
    {
        ERR("Efreet: Could not create a log domain for efreet_util");
        return 0;
    }

    file_id_by_desktop_path = eina_hash_string_superfast_new(EINA_FREE_CB(eina_stringshare_del));

    return init;
}

int
efreet_util_shutdown(void)
{
    if (--init) return init;

    eina_log_domain_unregister(_efreet_utils_log_dom);
    _efreet_utils_log_dom = -1;
    IF_FREE_HASH(file_id_by_desktop_path);

    IF_RELEASE(cache_names_key);
    efreet_cache_array_string_free(cache_names);
    cache_names = NULL;

    IF_RELEASE(cache_hash_key);
    if (cache_hash)
    {
        eina_hash_free(cache_hash->hash);
        free(cache_hash);
        cache_hash = NULL;
    }

    cache = efreet_cache_close(cache);
    IF_RELEASE(cache_file);

    return init;
}

/*
 * Needs EAPI because of helper binaries
 */
EAPI const char *
efreet_desktop_util_cache_file(void)
{
    char tmp[PATH_MAX] = { '\0' };
    const char *cache_dir, *lang, *country, *modifier;

    if (cache_file) return cache_file;

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

    cache_file = eina_stringshare_add(tmp);
    return cache_file;
}

static char *
efreet_util_path_in_default(const char *section, const char *path)
{
    Eina_List *dirs;
    char *ret = NULL;
    char *dir;

    dirs = efreet_default_dirs_get(efreet_data_home_get(), efreet_data_dirs_get(),
                                   section);

    EINA_LIST_FREE(dirs, dir)
    {
        if (!strncmp(path, dir, strlen(dir)))
            ret = dir;
        else
            eina_stringshare_del(dir);
    }

    return ret;
}

/**
 * Returns the fdo file id for a given path. If the file isn't inside
 * a default fdo path it will return NULL.
 *
 * @param path The path to find the file id for
 *
 * @return File id for path or NULL
 */
EAPI const char *
efreet_util_path_to_file_id(const char *path)
{
    size_t len, len2;
    char *tmp, *p;
    char *base;
    const char *file_id;

    /* TODO: Check if searching in cache is fast enough */
    if (!path) return NULL;
    file_id = eina_hash_find(file_id_by_desktop_path, path);
    if (file_id) return file_id;

    base = efreet_util_path_in_default("applications", path);
    if (!base) return NULL;

    len = strlen(base);
    if (strlen(path) <= len)
    {
        eina_stringshare_del(base);
        return NULL;
    }
    if (strncmp(path, base, len))
    {
        eina_stringshare_del(base);
        return NULL;
    }

    len2 = strlen(path + len + 1) + 1;
    tmp = alloca(len2);
    memcpy(tmp, path + len + 1, len2);
    p = tmp;
    while (*p)
    {
        if (*p == '/') *p = '-';
        p++;
    }
    eina_stringshare_del(base);
    file_id = eina_stringshare_add(tmp);
    eina_hash_add(file_id_by_desktop_path, path, (void *)file_id);
    return file_id;
}

/**
 * Find all desktops for a given mime type
 *
 * This list must be freed using EINA_LIST_FREE / efreet_desktop_free
 *
 * @param mime the mime type
 * @return a list of desktops
 */
EAPI Eina_List *
efreet_util_desktop_mime_list(const char *mime)
{
    return efreet_util_cache_list("mime_types", mime);
}

/**
 * Find all desktops for a given wm class
 *
 * This list must be freed using EINA_LIST_FREE / efreet_desktop_free
 *
 * @param wmname the wm name
 * @param wmclass the wm class
 * @return a list of desktops
 */
EAPI Efreet_Desktop *
efreet_util_desktop_wm_class_find(const char *wmname, const char *wmclass)
{
    return efreet_util_cache_find("startup_wm_class", wmname, wmclass);
}

/**
 * Find a desktop by file id
 *
 * return value must be freed by efreet_desktop_free
 *
 * @param file_id the file id
 * @return a desktop
 */
EAPI Efreet_Desktop *
efreet_util_desktop_file_id_find(const char *file_id)
{
    Efreet_Cache_Hash *hash;
    Efreet_Desktop *ret = NULL;
    const char *str;

    if (!file_id) return NULL;
    if (!efreet_cache_check(&cache, efreet_desktop_util_cache_file(), EFREET_DESKTOP_UTILS_CACHE_MAJOR)) return NULL;

    hash = efreet_util_cache_hash(efreet_hash_string_edd(), "file_id");
    if (!hash) return NULL;
    str = eina_hash_find(hash->hash, file_id);
    if (str)
        ret = efreet_desktop_get(str);
    return ret;
}

/**
 * Find a desktop by exec
 *
 * return value must be freed by efreet_desktop_free
 *
 * @param exec the exec name
 * @return a desktop
 */
EAPI Efreet_Desktop *
efreet_util_desktop_exec_find(const char *exec)
{
    Efreet_Cache_Hash *hash = NULL;
    Efreet_Desktop *ret = NULL;
    Efreet_Cache_Array_String *names = NULL;
    unsigned int i;

    if (!efreet_cache_check(&cache, efreet_desktop_util_cache_file(), EFREET_DESKTOP_UTILS_CACHE_MAJOR)) return NULL;
    if (!exec) return NULL;

    names = efreet_util_cache_names(efreet_array_string_edd(), "exec_list");
    if (!names) return NULL;
    for (i = 0; i < names->array_count; i++)
    {
        const char *file;
        char *exe;
        unsigned int j;
        Efreet_Cache_Array_String *array;

        exe = ecore_file_app_exe_get(names->array[i]);
        if (!exe) continue;
        file = ecore_file_file_get(exe);
        if (!file) continue;
        if (strcmp(exec, exe) && strcmp(exec, file))
        {
            free(exe);
            continue;
        }
        free(exe);

        if (!hash)
            hash = efreet_util_cache_hash(efreet_hash_array_string_edd(), "exec_hash");
        if (!hash) return NULL;
        array = eina_hash_find(hash->hash, names->array[i]);
        if (!array) continue;
        for (j = 0; j < array->array_count; j++)
        {
            ret = efreet_desktop_get(array->array[j]);
            if (ret) break;
        }
        if (ret) break;
    }
    return ret;
}

/**
 * Find a desktop by name
 *
 * return value must be freed by efreet_desktop_free
 *
 * @param name the name
 * @return a desktop
 */
EAPI Efreet_Desktop *
efreet_util_desktop_name_find(const char *name)
{
    return efreet_util_cache_find("name", name, NULL);
}

/**
 * Find a desktop by generic name
 *
 * return value must be freed by efreet_desktop_free
 *
 * @param generic_name the generic name
 * @return a desktop
 */
EAPI Efreet_Desktop *
efreet_util_desktop_generic_name_find(const char *generic_name)
{
    return efreet_util_cache_find("generic_name", generic_name, NULL);
}

/**
 * Find all desktops where name matches a glob pattern
 *
 * This list must be freed using EINA_LIST_FREE / efreet_desktop_free
 *
 * @param glob the pattern to match
 * @return a list of desktops
 */
EAPI Eina_List *
efreet_util_desktop_name_glob_list(const char *glob)
{
    return efreet_util_cache_glob_list("name", glob);
}

/**
 * Find all desktops where exec matches a glob pattern
 *
 * This list must be freed using EINA_LIST_FREE / efreet_desktop_free
 *
 * @param glob the pattern to match
 * @return a list of desktops
 */
EAPI Eina_List *
efreet_util_desktop_exec_glob_list(const char *glob)
{
    Efreet_Cache_Hash *hash = NULL;
    Eina_List *ret = NULL;
    Efreet_Cache_Array_String *names = NULL;
    unsigned int i;

    if (!efreet_cache_check(&cache, efreet_desktop_util_cache_file(), EFREET_DESKTOP_UTILS_CACHE_MAJOR)) return NULL;
    if (!glob) return NULL;

    names = efreet_util_cache_names(efreet_array_string_edd(), "exec_list");
    if (!names) return NULL;
    for (i = 0; i < names->array_count; i++)
    {
        Efreet_Cache_Array_String *array;
        unsigned int j;
        char *exe;
        Efreet_Desktop *desk;

        exe = ecore_file_app_exe_get(names->array[i]);
        if (!exe) continue;
        if (!efreet_util_glob_match(exe, glob))
        {
            free(exe);
            continue;
        }
        free(exe);

        if (!hash)
            hash = efreet_util_cache_hash(efreet_hash_array_string_edd(), "exec_hash");
        if (!hash) return NULL;

        array = eina_hash_find(hash->hash, names->array[i]);
        if (!array) continue;
        for (j = 0; j < array->array_count; j++)
        {
            desk = efreet_desktop_get(array->array[j]);
            if (desk)
                ret = eina_list_append(ret, desk);
        }
    }
    return ret;
}

/**
 * Find all desktops where generic name matches a glob pattern
 *
 * This list must be freed using EINA_LIST_FREE / efreet_desktop_free
 *
 * @param glob the pattern to match
 * @return a list of desktops
 */
EAPI Eina_List *
efreet_util_desktop_generic_name_glob_list(const char *glob)
{
    return efreet_util_cache_glob_list("generic_name", glob);
}

/**
 * Find all desktops where comment matches a glob pattern
 *
 * This list must be freed using EINA_LIST_FREE / efreet_desktop_free
 *
 * @param glob the pattern to match
 * @return a list of desktops
 */
EAPI Eina_List *
efreet_util_desktop_comment_glob_list(const char *glob)
{
    return efreet_util_cache_glob_list("comment", glob);
}

/**
 * Find all desktop categories
 * This list must be freed using EINA_LIST_FREE
 *
 * @return an Eina_List of category names (const char *)
 */
EAPI Eina_List *
efreet_util_desktop_categories_list(void)
{
    Efreet_Cache_Array_String *array;
    Eina_List *ret = NULL;
    unsigned int i;

    if (!efreet_cache_check(&cache, efreet_desktop_util_cache_file(), EFREET_DESKTOP_UTILS_CACHE_MAJOR)) return NULL;

    array = efreet_util_cache_names(efreet_array_string_edd(), "categories_list");
    if (!array) return NULL;
    for (i = 0; i < array->array_count; i++)
        ret = eina_list_append(ret, array->array[i]);
    return ret;
}

/**
 * Find all desktops in a given category
 *
 * This list must be freed using EINA_LIST_FREE / efreet_desktop_free
 *
 * @param category the category name
 * @return a list of desktops
 */
EAPI Eina_List *
efreet_util_desktop_category_list(const char *category)
{
    return efreet_util_cache_list("categories", category);
}

static int
efreet_util_glob_match(const char *str, const char *glob)
{
    if (!str || !glob)
        return 0;
    if (glob[0] == '\0')
    {
        if (str[0] == '\0') return 1;
        return 0;
    }
    if (!strcmp(glob, "*")) return 1;
    if (!fnmatch(glob, str, 0)) return 1;
    return 0;
}

/**
 * Returns a list of .menu files found in the various config dirs.
 * @return An eina list of menu file paths (const char *). This must be freed with EINA_LIST_FREE.
 */
EAPI Eina_List *
efreet_util_menus_find(void)
{
    Eina_List *menus = NULL;
    Eina_List *dirs, *l;
    const char *dir;

    menus = efreet_util_menus_find_helper(menus, efreet_config_home_get());

    dirs = efreet_config_dirs_get();
    EINA_LIST_FOREACH(dirs, l, dir)
        menus = efreet_util_menus_find_helper(menus, dir);

    return menus;
}

static Eina_List *
efreet_util_menus_find_helper(Eina_List *menus, const char *config_dir)
{
    DIR *files = NULL;
    struct dirent *file = NULL;
    char dbuf[PATH_MAX], fbuf[PATH_MAX];

    snprintf(dbuf, sizeof(dbuf), "%s/menus", config_dir);
    files = opendir(dbuf);
    if (!files) return menus;
    while ((file = readdir(files))) {
        const char *exten;
        exten = strrchr(file->d_name, '.');
        if (!exten) continue;
        if (strcmp(".menu", exten)) continue;

        snprintf(fbuf, sizeof(fbuf), "%s/%s", dbuf, file->d_name);
        if (ecore_file_is_dir(fbuf)) continue;

        menus = eina_list_append(menus, strdup(fbuf));
    }
    closedir(files);
    return menus;
}

static Efreet_Desktop *
efreet_util_cache_find(const char *search, const char *what1, const char *what2)
{
    Efreet_Cache_Hash *hash;
    Efreet_Desktop *ret = NULL;
    Efreet_Cache_Array_String *array = NULL;
    char key[256];

    if (!efreet_cache_check(&cache, efreet_desktop_util_cache_file(), EFREET_DESKTOP_UTILS_CACHE_MAJOR)) return NULL;
    if ((!what1) && (!what2)) return NULL;

    snprintf(key, sizeof(key), "%s_hash", search);
    hash = efreet_util_cache_hash(efreet_hash_array_string_edd(), key);
    if (!hash) return NULL;
    if (what1)
        array = eina_hash_find(hash->hash, what1);
    if (!array && what2) array = eina_hash_find(hash->hash, what2);
    if (array)
    {
        unsigned int i;

        for (i = 0; i < array->array_count; i++)
        {
            ret = efreet_desktop_get(array->array[i]);
            if (ret) break;
        }
    }
    return ret;
}

static Eina_List *
efreet_util_cache_list(const char *search, const char *what)
{
    Efreet_Cache_Hash *hash;
    Efreet_Cache_Array_String *array;
    Eina_List *ret = NULL;
    char key[256];

    if (!efreet_cache_check(&cache, efreet_desktop_util_cache_file(), EFREET_DESKTOP_UTILS_CACHE_MAJOR)) return NULL;
    if (!what) return NULL;

    snprintf(key, sizeof(key), "%s_hash", search);
    hash = efreet_util_cache_hash(efreet_hash_array_string_edd(), key);
    if (!hash) return NULL;
    array = eina_hash_find(hash->hash, what);
    if (array)
    {
        unsigned int i;
        Efreet_Desktop *desk;

        for (i = 0; i < array->array_count; i++)
        {
            desk = efreet_desktop_get(array->array[i]);
            if (desk)
                ret = eina_list_append(ret, desk);
        }
    }
    return ret;
}

static Eina_List *
efreet_util_cache_glob_list(const char *search, const char *what)
{
    Efreet_Cache_Hash *hash = NULL;
    Eina_List *ret = NULL;
    Efreet_Cache_Array_String *names = NULL;
    char key[256];
    unsigned int i;

    if (!efreet_cache_check(&cache, efreet_desktop_util_cache_file(), EFREET_DESKTOP_UTILS_CACHE_MAJOR)) return NULL;
    if (!what) return NULL;

    snprintf(key, sizeof(key), "%s_list", search);
    names = efreet_util_cache_names(efreet_array_string_edd(), key);
    if (!names) return NULL;
    for (i = 0; i < names->array_count; i++)
    {
        Efreet_Cache_Array_String *array;
        unsigned int j;
        Efreet_Desktop *desk;

        if (!efreet_util_glob_match(names->array[i], what)) continue;

        if (!hash)
        {
            snprintf(key, sizeof(key), "%s_hash", search);
            hash = efreet_util_cache_hash(efreet_hash_array_string_edd(), key);
        }
        if (!hash) return NULL;

        array = eina_hash_find(hash->hash, names->array[i]);
        if (!array) continue;
        for (j = 0; j < array->array_count; j++)
        {
            desk = efreet_desktop_get(array->array[j]);
            if (desk)
                ret = eina_list_append(ret, desk);
        }
    }
    return ret;
}

static Efreet_Cache_Hash *
efreet_util_cache_hash(Eet_Data_Descriptor *edd, const char *key)
{
    if (cache_hash_key && !strcmp(key, cache_hash_key))
        return cache_hash;
    if (cache_hash)
    {
        /* free previous cache */
        IF_RELEASE(cache_hash_key);
        eina_hash_free(cache_hash->hash);
        free(cache_hash);
    }
    cache_hash_key = eina_stringshare_add(key);
    cache_hash = eet_data_read(cache, edd, key);
    return cache_hash;
}

static Efreet_Cache_Array_String *
efreet_util_cache_names(Eet_Data_Descriptor *edd, const char *key)
{
    if (cache_names_key && !strcmp(key, cache_names_key))
        return cache_names;
    if (cache_names)
    {
        /* free previous cache */
        IF_RELEASE(cache_names_key);
        efreet_cache_array_string_free(cache_names);
    }
    cache_names_key = eina_stringshare_add(key);
    cache_names = eet_data_read(cache, edd, key);
    return cache_names;
}

void
efreet_util_desktop_cache_reload(void)
{
    if (cache_names)
    {
        IF_RELEASE(cache_names_key);
        efreet_cache_array_string_free(cache_names);
        cache_names = NULL;
    }
    if (cache_hash)
    {
        IF_RELEASE(cache_hash_key);
        eina_hash_free(cache_hash->hash);
        free(cache_hash);
        cache_hash = NULL;
    }

    cache = efreet_cache_close(cache);
}

/*
 * Needs EAPI because of helper binaries
 */
EAPI void
efreet_hash_free(Eina_Hash *hash, Eina_Free_Cb free_cb)
{
    Eina_Iterator *it;
    void *data;

    it = eina_hash_iterator_data_new(hash);
    EINA_ITERATOR_FOREACH(it, data)
        (*free_cb)(data);
    eina_iterator_free(it);
    eina_hash_free(hash);
}

