/* vim: set sw=4 ts=4 sts=4 et: */
#ifndef EFREET_PRIVATE_H
#define EFREET_PRIVATE_H

/**
 * @file efreet_private.h
 * @brief Contains methods and defines that are private to the Efreet
 * implementaion
 * @addtogroup Efreet_Private Efreet_Private: Private methods and defines
 *
 * @{
 */

/**
 * @def NEW(x, c)
 * Allocate and zero out c structures of type x
 */
#define NEW(x, c) calloc(c, sizeof(x))

/**
 * @def FREE(x)
 * Free x and set to NULL
 */
#define FREE(x) do { free(x); x = NULL; } while (0)

/**
 * @def IF_FREE(x)
 * If x is set, free x and set to NULL
 */
#define IF_FREE(x) do { if (x) FREE(x); } while (0)

/**
 * @def IF_RELEASE(x)
 * If x is set, eina_stringshare_del x and set to NULL
 */
#define IF_RELEASE(x) do { \
    if (x) { \
        const char *__tmp; __tmp = (x); (x) = NULL; eina_stringshare_del(__tmp); \
    } \
    (x) = NULL; \
} while (0)

/**
 * @def IF_FREE_LIST(x)
 * If x is a valid pointer destroy x and set to NULL
 */
#define IF_FREE_LIST(list, free_cb) do { \
    while (list) \
    { \
        free_cb(eina_list_data_get(list)); \
        list = eina_list_remove_list(list, list); \
    } \
} while (0)

/**
 * @def IF_FREE_HASH(x)
 * If x is a valid pointer destroy x and set to NULL
 */
#define IF_FREE_HASH(x) do { \
    if (x) { \
        Eina_Hash *__tmp; __tmp = (x); (x) = NULL; eina_hash_free(__tmp); \
    } \
    (x) = NULL; \
} while (0)

#ifndef PATH_MAX
/**
 * @def PATH_MAX
 * Convenience define to set the maximim path length
 */
#define PATH_MAX 4096
#endif

/**
 * @def _efree_log_domain_global
 * global log domain for efreet (see eina_log module)
 */

extern int _efreet_log_dom_global;
#ifdef EFREET_DEFAULT_LOG_COLOR
#undef EFREET_DEFAULT_LOG_COLOR
#endif
#define EFREET_DEFAULT_LOG_COLOR "\033[36m"

#define EFREET_MODULE_LOG_DOM _efreet_log_dom_global; /*default log domain for each module. It can redefined inside each module */
#ifdef ERROR
#undef ERROR
#endif
#define ERROR(...) EINA_LOG_DOM_ERR(EFREET_MODULE_LOG_DOM, __VA_ARGS__)
#ifdef DEBUG
#undef DEBUG
#endif
#define DEBUG(...) EINA_LOG_DOM_DBG(EFREET_MODULE_LOG_DOM, __VA_ARGS__)
#ifdef INFO
#undef INFO
#endif
#define INFO(...) EINA_LOG_DOM_INFO(EFREET_MODULE_LOG_DOM, __VA_ARGS__)
#ifdef WARN
#undef WARN
#endif
#define WARN(...) EINA_LOG_DOM_WARN(EFREET_MODULE_LOG_DOM, __VA_ARGS__)

/**
 * macros that are used all around the code for message processing
 * four macros are defined ERR, WRN, DGB, INF. 
 * EFREET_MODULE_LOG_DOM should be defined individually for each module
 */
#define EFREET_MODULE_LOG_DOM _efreet_log_dom_global; /*default log domain for each module. It can redefined inside each module */
#ifdef ERR
#undef ERR
#endif
#define ERR(...) EINA_LOG_DOM_ERR(EFREET_MODULE_LOG_DOM, __VA_ARGS__)
#ifdef DBG
#undef DBG
#endif
#define DBG(...) EINA_LOG_DOM_DBG(EFREET_MODULE_LOG_DOM, __VA_ARGS__)
#ifdef INF
#undef INF
#endif
#define INF(...) EINA_LOG_DOM_INFO(EFREET_MODULE_LOG_DOM, __VA_ARGS__)
#ifdef WRN
#undef WRN
#endif
#define WRN(...) EINA_LOG_DOM_WARN(EFREET_MODULE_LOG_DOM, __VA_ARGS__)
/**
 * @internal
 * The different types of commands in an Exec entry
 */
typedef enum Efreet_Desktop_Command_Flag
{
    EFREET_DESKTOP_EXEC_FLAG_FULLPATH = 0x0001,
    EFREET_DESKTOP_EXEC_FLAG_URI      = 0x0002,
    EFREET_DESKTOP_EXEC_FLAG_DIR      = 0x0004,
    EFREET_DESKTOP_EXEC_FLAG_FILE     = 0x0008
} Efreet_Desktop_Command_Flag;

/**
 * @internal
 * Efreet_Desktop_Command
 */
typedef struct Efreet_Desktop_Command Efreet_Desktop_Command;

/**
 * @internal
 * Holds information on a desktop Exec command entry
 */
struct Efreet_Desktop_Command
{
  Efreet_Desktop *desktop;
  int num_pending;

  Efreet_Desktop_Command_Flag flags;

  Efreet_Desktop_Command_Cb cb_command;
  Efreet_Desktop_Progress_Cb cb_progress;
  void *data;

  Eina_List *files; /**< list of Efreet_Desktop_Command_File */
};

/**
 * @internal
 * Efreet_Desktop_Command_File
 */
typedef struct Efreet_Desktop_Command_File Efreet_Desktop_Command_File;

/**
 * @internal
 * Stores information on a file passed to the desktop Exec command
 */
struct Efreet_Desktop_Command_File
{
  Efreet_Desktop_Command *command;
  char *dir;
  char *file;
  char *fullpath;
  char *uri;

  int pending;
};

int efreet_base_init(void);
void efreet_base_shutdown(void);

int efreet_icon_init(void);
void efreet_icon_shutdown(void);

int efreet_menu_init(void);
void efreet_menu_shutdown(void);
Eina_List *efreet_default_dirs_get(const char *user_dir,
                                    Eina_List *system_dirs,
                                    const char *suffix);

int efreet_ini_init(void);
void efreet_ini_shutdown(void);

int efreet_desktop_init(void);
void efreet_desktop_shutdown(void);

const char *efreet_home_dir_get(void);

EAPI const char *efreet_lang_get(void);
EAPI const char *efreet_lang_country_get(void);
EAPI const char *efreet_lang_modifier_get(void);

size_t efreet_array_cat(char *buffer, size_t size, const char *strs[]);

const char *efreet_desktop_environment_get(void);

#define NON_EXISTING (void *)-1

void efreet_cache_clear(void);
const char *efreet_icon_hash_get(const char *theme_name, const char *icon, int size);
void efreet_icon_hash_put(const char *theme_name, const char *icon, int size, const char *file);

/**
 * @}
 */

#endif
