#ifndef EFREET_MIME_H
#define EFREET_MIME_H

/**
 * @file Efreet_Mime.h
 * @brief The file that must be included by any project wishing to use
 * @addtogroup Efreet_Mime Efreet_Mime: The XDG Shared Mime Info standard
 * Efreet Mime is a library designed to help apps work with the
 * Freedesktop.org Shared Mime Info standard.
 * Efreet_Mime.h provides all of the necessary headers and
 * includes to work with Efreet_Mime.
 * @{
 */

#ifdef EAPI
# undef EAPI
#endif

#ifdef _WIN32
# ifdef EFL_EFREET_MIME_BUILD
#  ifdef DLL_EXPORT
#   define EAPI __declspec(dllexport)
#  else
#   define EAPI
#  endif /* ! DLL_EXPORT */
# else
#  define EAPI __declspec(dllimport)
# endif /* ! EFL_EFREET_MIME_BUILD */
#else
# ifdef __GNUC__
#  if __GNUC__ >= 4
#   define EAPI __attribute__ ((visibility("default")))
#  else
#   define EAPI
#  endif
# else
#  define EAPI
# endif
#endif

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @return Returns 1 on success or 0 on failure
 * @brief Initializes the efreet mime settings
 */
EAPI int         efreet_mime_init(void);

/**
 * @return Returns no value
 * @brief Cleans up the efreet mime settings system
 */
EAPI int         efreet_mime_shutdown(void);

/**
 * @param file The file to find the mime type
 * @return Returns mime type as a string
 * @brief Retreive the mime type of a file
 */
EAPI const char *efreet_mime_type_get(const char *file);

/**
 * @param file The file to check the mime type
 * @return Returns mime type as a string
 * @brief Retreive the mime type of a file using magic
 */
EAPI const char *efreet_mime_magic_type_get(const char *file);

/**
 * @param file The file to check the mime type
 * @return Returns mime type as a string
 * @brief Retreive the mime type of a file using globs
 */
EAPI const char *efreet_mime_globs_type_get(const char *file);

/**
 * @param file The file to check the mime type
 * @return Returns mime type as a string
 * @brief Retreive the special mime type of a file
 */
EAPI const char *efreet_mime_special_type_get(const char *file);

/**
 * @param file The file to check the mime type
 * @return Returns mime type as a string
 * @brief Retreive the fallback mime type of a file
 */
EAPI const char *efreet_mime_fallback_type_get(const char *file);


/**
 * @param mime The name of the mime type
 * @param theme The name of the theme to search icons in
 * @param size The wanted size of the icon
 * @return Returns mime type icon path as a string
 * @brief Retreive the mime type icon for a file
 */
EAPI const char *efreet_mime_type_icon_get(const char *mime, const char *theme,
                                           unsigned int size);

/**
 * @brief Clear mime icons mapping cache
 */
EAPI void efreet_mime_type_cache_clear(void);

/**
 * @brief Flush mime icons mapping cache
 *
 * Flush timeout is defined at compile time by
 * EFREET_MIME_ICONS_FLUSH_TIMEOUT
 */
EAPI void efreet_mime_type_cache_flush(void);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif
