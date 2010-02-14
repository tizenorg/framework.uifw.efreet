/* vim: set sw=4 ts=4 sts=4 et: */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include <Ecore_File.h>

#include "Efreet.h"
#include "efreet_private.h"
#include "efreet_xml.h"

#if 0
static void efreet_xml_dump(Efreet_Xml *xml, int level);
#endif

static Efreet_Xml *efreet_xml_parse(char **data, int *size);
static int efreet_xml_tag_parse(char **data, int *size, const char **tag);
static void efreet_xml_attributes_parse(char **data, int *size,
                                        Efreet_Xml_Attribute ***attributes);
static void efreet_xml_text_parse(char **data, int *size, char **text);

static int efreet_xml_tag_empty(char **data, int *size);
static int efreet_xml_tag_close(char **data, int *size, const char *tag);

static void efreet_xml_cb_attribute_free(void *data);
static void efreet_xml_comment_skip(char **data, int *size);

static int error = 0;

/* define macros and variable for using the eina logging system  */

#ifdef EFREET_MODULE_LOG_DOM 
#undef EFREET_MODULE_LOG_DOM
#endif
#define EFREET_MODULE_LOG_DOM _efreet_xml_log_dom

static int _efreet_xml_init_count = 0;
static int _efreet_xml_log_dom = -1;

/**
 * @internal
 * @return Returns > 0 on success or 0 on failure
 * @brief Initialize the XML parser subsystem
 */
int
efreet_xml_init(void)
{
    _efreet_xml_init_count++;
    if (_efreet_xml_init_count > 1) return _efreet_xml_init_count;
    _efreet_xml_log_dom = eina_log_domain_register("Efreet_xml", EFREET_DEFAULT_LOG_COLOR);
    if (_efreet_xml_log_dom < 0)
    {
        _efreet_xml_init_count--;
        ERROR("Efreet: Could not create a log domain for Efreet_xml.");
        return _efreet_xml_init_count;
    }
    return _efreet_xml_init_count;
}

/**
 * @internal
 * @returns the number of initializations left for this system
 * @brief Attempts to shut down the subsystem if nothing else is using it
 */
void
efreet_xml_shutdown(void)
{
    _efreet_xml_init_count--;
    if (_efreet_xml_init_count > 0) return;
    eina_log_domain_unregister(_efreet_xml_log_dom);
}

/**
 * @internal
 * @param file: The file to parse
 * @return Returns an Efreet_Xml structure for the given file @a file or
 * NULL on failure
 * @brief Parses the given file into an Efreet_Xml structure.
 */
Efreet_Xml *
efreet_xml_new(const char *file)
{
    Efreet_Xml *xml = NULL;
    int size, fd = -1;
    char *data = MAP_FAILED;

    if (!file) return NULL;

    size = ecore_file_size(file);
    if (size <= 0) goto efreet_error;

    fd = open(file, O_RDONLY);
    if (fd == -1) goto efreet_error;

    data = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) goto efreet_error;

    error = 0;
    xml = efreet_xml_parse(&data, &size);
    if (error) goto efreet_error;

    munmap(data, size);
    close(fd);
    return xml;

efreet_error:
    ERR("could not parse xml file");
    if (data != MAP_FAILED) munmap(data, size);
    if (fd != -1) close(fd);
    if (xml) efreet_xml_del(xml);
    return NULL;
}

/**
 * @internal
 * @param xml: The Efree_Xml to free
 * @return Returns no value
 * @brief Frees up the given Efreet_Xml structure
 */
void
efreet_xml_del(Efreet_Xml *xml)
{
    IF_FREE_LIST(xml->children, efreet_xml_cb_attribute_free);

    if (xml->tag) eina_stringshare_del(xml->tag);
    if (xml->attributes)
    {
        Efreet_Xml_Attribute **curr;

        curr = xml->attributes;
        while (*curr)
        {
            eina_stringshare_del((*curr)->key);
            eina_stringshare_del((*curr)->value);

            FREE(*curr);
            curr++;
        }
        FREE(xml->attributes);
    }
    IF_FREE(xml->text);
    FREE(xml);
}

/**
 * @param xml: The xml struct to work with
 * @param key: The attribute key to look for
 * @return Returns the value for the given key, or NULL if none found
 * @brief Retrieves the value for the given attribute key
 */
const char *
efreet_xml_attribute_get(Efreet_Xml *xml, const char *key)
{
    Efreet_Xml_Attribute **curr;

    if (!xml || !key || !xml->attributes) return NULL;

    for (curr = xml->attributes; *curr; curr++)
    {
        if (!strcmp((*curr)->key, key))
            return (*curr)->value;
    }
    return NULL;
}

static void
efreet_xml_cb_attribute_free(void *data)
{
    efreet_xml_del(data);
}

#if 0
static void
efreet_xml_dump(Efreet_Xml *xml, int level)
{
    int i;

    for (i = 0; i < level; i++)
        printf("\t");
    printf("<%s", xml->tag);
    if (xml->attributes)
    {
        Efreet_Xml_Attribute **curr;
        for (curr = xml->attributes; *curr; curr++)
            printf(" %s=\"%s\"", (*curr)->key, (*curr)->value);
    }

    if (xml->children)
    {
        Efreet_Xml *child;
        Eina_List *l;

        printf(">");

        EINA_LIST_FOREACH(xml->children, l, child)
            efreet_xml_dump(child, level + 1);

        for (i = 0; i < level; i++)
            printf("\t");
        printf("</%s>", xml->tag);
    }
    else if (xml->text)
        printf(">%s</%s>\n", xml->text, xml->tag);
    else
        printf("/>\n");
}
#endif

static Efreet_Xml *
efreet_xml_parse(char **data, int *size)
{
    Efreet_Xml *xml, *sub_xml;
    const char *tag = NULL;

    /* parse this tag */
    if (!efreet_xml_tag_parse(data, size, &(tag))) return NULL;
    xml = NEW(Efreet_Xml, 1);
    if (!xml)
    {
        eina_stringshare_del(tag);
        return NULL;
    }

    xml->children = NULL;

    xml->tag = tag;
    efreet_xml_attributes_parse(data, size, &(xml->attributes));

    /* Check wether element is empty */
    if (efreet_xml_tag_empty(data, size)) return xml;
    efreet_xml_text_parse(data, size, &(xml->text));

    /* Check wether element is closed */
    if (efreet_xml_tag_close(data, size, xml->tag)) return xml;

    while ((sub_xml = efreet_xml_parse(data, size)))
        xml->children = eina_list_append(xml->children, sub_xml);

    efreet_xml_tag_close(data, size, xml->tag);

    return xml;
}

static int
efreet_xml_tag_parse(char **data, int *size, const char **tag)
{
    const char *start = NULL, *end = NULL;
    char buf[256];
    int buf_size;

    /* Search for tag */
    while (*size > 1)
    {
        /* Check for tag start */
        if (**data == '<')
        {
            /* Check for end tag */
            if (*(*data + 1) == '/') return 0;

            /* skip comments */
            if (*size > 3 && *(*data + 1) == '!' && *(*data + 2) == '-' && *(*data + 3) == '-')
            {
                (*data) += 3;
                (*size) -= 3;
                efreet_xml_comment_skip(data, size);
                continue;
            }

            /* Check for xml directives (and ignore them) */
            else if ((*(*data + 1) != '!') && (*(*data + 1) != '?'))
            {
                (*size)--;
                (*data)++;
                start = *data;
                break;
            }
        }
        (*size)--;
        (*data)++;
    }

    if (!start)
    {
        ERR("missing start tag");
        error = 1;
        return 0;
    }

    while (*size > 0)
    {
        if (!isalpha(**data))
        {
            end = *data;
            break;
        }
        (*size)--;
        (*data)++;
    }

    if (!end)
    {
        ERR("no end of tag");
        error = 1;
        return 0;
    }

    buf_size = end - start + 1;
    if (buf_size <= 1)
    {
        ERR("no tag name");
        error = 1;
        return 0;
    }

    if (buf_size > 256) buf_size = 256;
    memcpy(buf, start, buf_size - 1);
    buf[buf_size - 1] = '\0';
    *tag = eina_stringshare_add(buf);

    return 1;
}

static void
efreet_xml_attributes_parse(char **data, int *size,
        Efreet_Xml_Attribute ***attributes)
{
    Efreet_Xml_Attribute attr[10];
    int i, count = 0;

    while (*size > 0)
    {
        if (**data == '>')
        {
            (*size)++;
            (*data)--;
            break;
        }
        else if ((count < 10) && (isalpha(**data)))
        {
            /* beginning of key */
            const char *start = NULL, *end = NULL;
            char buf[256];
            int buf_size;

            attr[count].key = NULL;
            attr[count].value = NULL;

            start = *data;
            while ((*size > 0) && ((isalpha(**data)) || (**data == '_')))
            {
                (*size)--;
                (*data)++;
            }

            end = *data;
            buf_size = end - start + 1;
            if (buf_size <= 1)
            {
                ERR("zero length key");
                goto efreet_error;
            }

            if (buf_size > 256) buf_size = 256;
            memcpy(buf, start, buf_size - 1);
            buf[buf_size - 1] = '\0';
            attr[count].key = eina_stringshare_add(buf);

            /* search for '=', key/value seperator */
            start = NULL;
            while (*size > 0)
            {
                if (**data == '=')
                {
                    start = *data;
                    break;
                }
                (*size)--;
                (*data)++;
            }

            if (!start)
            {
                ERR("missing value for attribute!");
                goto efreet_error;
            }

            /* search for '"', beginning of value */
            start = NULL;
            while (*size > 0)
            {
                if (**data == '"')
                {
                    start = *data;
                    break;
                }
                (*size)--;
                (*data)++;
            }

            if (!start)
            {
                ERR("erroneous value for attribute!");
                goto efreet_error;
            }

            /* skip '"' */
            start++;
            (*size)--;
            (*data)++;

            /* search for '"', end of value */
            end = NULL;
            while (*size > 0)
            {
                if (**data == '"')
                {
                    end = *data;
                    break;
                }
                (*size)--;
                (*data)++;
            }

            if (!end)
            {
                ERR("erroneous value for attribute!");
                goto efreet_error;
            }

            buf_size = end - start + 1;
            if (buf_size <= 1)
            {
                ERR("zero length value");
                goto efreet_error;
            }

            if (buf_size > 256) buf_size = 256;
            memcpy(buf, start, buf_size - 1);
            buf[buf_size - 1] = '\0';
            attr[count].value = eina_stringshare_add(buf);

            count++;
        }

        (*size)--;
        (*data)++;
    }

    *attributes = NEW(Efreet_Xml_Attribute *, count + 1);
    for (i = 0; i < count; i++)
    {
        (*attributes)[i] = malloc(sizeof(Efreet_Xml_Attribute));
        (*attributes)[i]->key = attr[i].key;
        (*attributes)[i]->value = attr[i].value;
    }
    return;

efreet_error:
    while (count >= 0)
    {
        if (attr[count].key) eina_stringshare_del(attr[count].key);
        if (attr[count].value) eina_stringshare_del(attr[count].value);
        count--;
    }
    error = 1;
    return;
}

static void
efreet_xml_text_parse(char **data, int *size, char **text)
{
    const char *start = NULL, *end = NULL;
    int buf_size;

    /* skip leading whitespace */
    while (*size > 0)
    {
        if (!isspace(**data))
        {
            start = *data;
            break;
        }
        (*size)--;
        (*data)++;
    }

    if (!start) return;

    /* find next tag */
    while (*size > 0)
    {
        if (**data == '<')
        {
            end = *data;
            break;
        }
        (*size)--;
        (*data)++;
    }
    if (!end) return;

    /* skip trailing whitespace */
    while (isspace(*(end - 1))) end--;

    /* copy text */
    buf_size = end - start + 1;
    if (buf_size <= 1) return;

    *text = malloc(buf_size);
    memcpy(*text, start, buf_size - 1);
    (*text)[buf_size - 1] = '\0';
}

static int
efreet_xml_tag_empty(char **data, int *size)
{
    while (*size > 1)
    {
        if (**data == '/')
        {
            (*size)--;
            (*data)++;
            if (**data == '>')
            {
                (*size)--;
                (*data)++;
                return 1;
            }
        }
        else if (**data == '>')
        {
            (*size)--;
            (*data)++;
            return 0;
        }
        (*size)--;
        (*data)++;
    }
    ERR("missing end of tag");
    error = 1;

    return 1;
}

static int
efreet_xml_tag_close(char **data, int *size, const char *tag)
{
    while (*size > 1)
    {
        if (**data == '<')
        {
            if (*(*data + 1) == '/')
            {
                (*size) -= 2;
                (*data) += 2;
                if ((int)strlen(tag) > *size)
                {
                    ERR("wrong end tag");
                    error = 1;
                    return 1;
                }
                else
                {
                    char *tmp;
                    tmp = *data;
                    while ((*tag) && (*tmp == *tag))
                    {
                        tmp++;
                        tag++;
                    }

                    if (*tag)
                    {
                        ERR("wrong end tag");
                        error = 1;
                        return 1;
                    }
                }
                return 1;
            }
            else return 0;
        }
        (*size)--;
        (*data)++;
    }
    return 0;
}

static void
efreet_xml_comment_skip(char **data, int *size)
{
    while (*size > 2)
    {
        if (**data == '-' && *(*data + 1) == '-' && *(*data + 2) == '>')
        {
            (*data) += 3;
            (*size) -= 3;
            return;
        }
        (*data)++;
        (*size)--;
    }
}
