/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * The GIMP Help plug-in
 * Copyright (C) 1999-2004 Sven Neumann <sven@gimp.org>
 *                         Michael Natterer <mitch@gimp.org>
 *                         Henrik Brix Andersen <brix@gimp.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <string.h>

#include <glib-object.h>

#include "libgimpbase/gimpbase.h"

#include "domain.h"
#include "help.h"

#include "libgimp/stdplugins-intl.h"


struct _HelpDomain
{
  gchar      *help_domain;
  gchar      *help_uri;
  GHashTable *help_locales;
};

typedef struct _HelpLocale HelpLocale;
struct _HelpLocale
{
  gchar      *locale_id;
  GHashTable *help_id_mapping;
  gchar      *help_missing;
};


/*  local function prototypes  */

static HelpDomain  * domain_new           (const gchar  *domain_name,
                                           const gchar  *domain_uri);
static void          domain_free          (HelpDomain   *domain);

static HelpLocale  * domain_locale_new    (const gchar  *locale_id);
static void          domain_locale_free   (HelpLocale   *locale);

static HelpLocale  * domain_locale_lookup (HelpDomain   *domain,
                                           const gchar  *locale_id);
static const gchar * domain_locale_map    (HelpLocale   *locale,
                                           const gchar  *help_id);

static gboolean      domain_locale_parse  (HelpDomain   *domain,
                                           HelpLocale   *locale,
                                           GError      **error);

/*  private variables  */

static GHashTable  *domain_hash = NULL;


/*  public functions  */

void
domain_register (const gchar *domain_name,
                 const gchar *domain_uri)
{
  g_return_if_fail (domain_name != NULL);
  g_return_if_fail (domain_uri != NULL);

#ifdef GIMP_HELP_DEBUG
  g_printerr ("help: registering help domain \"%s\" with base uri \"%s\"\n",
              domain_name, domain_uri);
#endif

  if (! domain_hash)
    domain_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
                                         g_free, (GDestroyNotify) domain_free);

  g_hash_table_insert (domain_hash,
                       g_strdup (domain_name),
                       domain_new (domain_name, domain_uri));
}

HelpDomain *
domain_lookup (const gchar *domain_name)
{
  g_return_val_if_fail (domain_name, NULL);

  if (domain_hash)
    return g_hash_table_lookup (domain_hash, domain_name);

  return NULL;
}

gchar *
domain_map (HelpDomain   *domain,
            GList        *help_locales,
            const gchar  *help_id)
{
  HelpLocale  *locale = NULL;
  const gchar *ref    = NULL;
  GList       *list;

  g_return_val_if_fail (domain != NULL, NULL);
  g_return_val_if_fail (help_locales != NULL, NULL);
  g_return_val_if_fail (help_id != NULL, NULL);

  /*  first pass: look for a reference matching the help_id  */
  for (list = help_locales; list && !ref; list = list->next)
    {
      locale = domain_locale_lookup (domain, (const gchar *) list->data);
      ref = domain_locale_map (locale, help_id);
    }

  /*  second pass: look for a fallback                 */
  for (list = help_locales; list && !ref; list = list->next)
    {
      locale = domain_locale_lookup (domain, (const gchar *) list->data);
      ref = locale->help_missing;
    }

  if (ref)
    {
      return g_strconcat (domain->help_uri,  "/",
                          locale->locale_id, "/",
                          ref,
                          NULL);
    }
  else  /*  try to assemble a useful error message  */
    {
      GError *error = NULL;

#ifdef GIMP_HELP_DEBUG
      g_printerr ("help: help_id lookup and all fallbacks failed for '%s'\n",
                  help_id);
#endif

      locale = domain_locale_lookup (domain, GIMP_HELP_DEFAULT_LOCALE);

      if (! domain_locale_parse (domain, locale, &error))
        {
          const gchar *msg;

          if (error->code == G_FILE_ERROR_NOENT)
            msg = _("The GIMP help files are not installed.");
          else
            msg = _("There is a problem with the GIMP help files.");

          g_message ("%s\n\n%s\n\n%s",
                     msg,
                     error->message,
                     _("Please check your installation."));

          g_error_free (error);
        }
      else
        {
          g_message (_("Help ID '%s' unknown"), help_id);
        }

      return NULL;
    }
}

void
domain_exit (void)
{
  if (domain_hash)
    {
      g_hash_table_destroy (domain_hash);
      domain_hash = NULL;
    }
}


/*  private functions  */

static HelpDomain *
domain_new (const gchar *domain_name,
            const gchar *domain_uri)
{
  HelpDomain *domain = g_new0 (HelpDomain, 1);

  domain->help_domain = g_strdup (domain_name);
  domain->help_uri    = g_strdup (domain_uri);

  return domain;
}

static void
domain_free (HelpDomain *domain)
{
  g_return_if_fail (domain != NULL);

  if (domain->help_locales)
    g_hash_table_destroy (domain->help_locales);

  g_free (domain->help_domain);
  g_free (domain->help_uri);
  g_free (domain);
}


static
HelpLocale *
domain_locale_new (const gchar *locale_id)
{
  HelpLocale *locale = g_new0 (HelpLocale, 1);

  locale->locale_id = g_strdup (locale_id);

  return locale;
}

static void
domain_locale_free (HelpLocale *locale)
{
  if (locale->help_id_mapping)
    g_hash_table_destroy (locale->help_id_mapping);

  g_free (locale->locale_id);
  g_free (locale->help_missing);
}

static HelpLocale *
domain_locale_lookup (HelpDomain  *domain,
                      const gchar *locale_id)
{
  HelpLocale *locale = NULL;

  if (domain->help_locales)
    locale = g_hash_table_lookup (domain->help_locales, locale_id);
  else
    domain->help_locales =
      g_hash_table_new_full (g_str_hash, g_str_equal,
                             g_free,
                             (GDestroyNotify) domain_locale_free);

  if (locale)
    return locale;

  locale = domain_locale_new (locale_id);
  g_hash_table_insert (domain->help_locales, g_strdup (locale_id), locale);

  domain_locale_parse (domain, locale, NULL);

  return locale;
}

static const gchar *
domain_locale_map (HelpLocale  *locale,
                   const gchar *help_id)
{
  if (! locale->help_id_mapping)
    return NULL;

  return g_hash_table_lookup (locale->help_id_mapping, help_id);
}


/*  the domain mapping parser  */

typedef enum
{
  DOMAIN_START,
  DOMAIN_IN_HELP,
  DOMAIN_IN_ITEM,
  DOMAIN_IN_MISSING,
  DOMAIN_IN_UNKNOWN
} DomainParserState;

typedef struct
{
  const gchar       *filename;
  DomainParserState  state;
  DomainParserState  last_known_state;
  gint               markup_depth;
  gint               unknown_depth;
  GString           *value;

  HelpDomain        *domain;
  HelpLocale        *locale;
  gchar             *id_attr_name;
} DomainParser;

static gboolean  domain_parser_parse       (GMarkupParseContext  *context,
                                            GIOChannel           *io,
                                            GError              **error);
static void  domain_parser_start_element   (GMarkupParseContext  *context,
                                            const gchar          *element_name,
                                            const gchar         **attribute_names,
                                            const gchar         **attribute_values,
                                            gpointer              user_data,
                                            GError              **error);
static void  domain_parser_end_element     (GMarkupParseContext  *context,
                                            const gchar          *element_name,
                                            gpointer              user_data,
                                            GError              **error);
static void  domain_parser_error           (GMarkupParseContext  *context,
                                            GError               *error,
                                            gpointer              user_data);
static void  domain_parser_start_unknown   (DomainParser         *parser);
static void  domain_parser_end_unknown     (DomainParser         *parser);
static void  domain_parser_parse_namespace (DomainParser  *parser,
                                            const gchar  **names,
                                            const gchar  **values);
static void  domain_parser_parse_item      (DomainParser         *parser,
                                            const gchar         **names,
                                            const gchar         **values);
static void  domain_parser_parse_missing   (DomainParser         *parser,
                                            const gchar         **names,
                                            const gchar         **values);

static const GMarkupParser markup_parser =
{
  domain_parser_start_element,
  domain_parser_end_element,
  NULL,  /*  characters   */
  NULL,  /*  passthrough  */
  domain_parser_error
};

static gboolean
domain_locale_parse (HelpDomain  *domain,
                     HelpLocale  *locale,
                     GError     **error)
{
  GMarkupParseContext *context;
  DomainParser        *parser;
  gchar               *base_dir;
  gchar               *filename;
  gboolean             success;
  GIOChannel          *io;

  g_return_val_if_fail (domain != NULL, FALSE);
  g_return_val_if_fail (locale != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (locale->help_id_mapping)
    {
      g_hash_table_destroy (locale->help_id_mapping);
      locale->help_id_mapping = NULL;
    }
  if (locale->help_missing)
    {
      g_free (locale->help_missing);
      locale->help_missing = NULL;
    }

  base_dir = g_filename_from_uri (domain->help_uri, NULL, NULL);
  filename = g_build_filename (base_dir,
                               locale->locale_id,
                               "gimp-help.xml",
                               NULL);
  g_free (base_dir);

#ifdef GIMP_HELP_DEBUG
          g_printerr ("help (%s): parsing '%s' for domain \"%s\"\n",
                      locale->locale_id,
                      filename,
                      domain->help_domain);
#endif

  io = g_io_channel_new_file (filename, "r", error);
  if (! io)
    {
      if (error)
        {
          gchar *msg;

          msg = g_strdup_printf (_("Could not open '%s' for reading: %s"),
                                 gimp_filename_to_utf8 (filename),
                                 (*error)->message);
          g_free ((*error)->message);
          (*error)->message = msg;
        }

      g_free (filename);
      return FALSE;
    }

  parser = g_new0 (DomainParser, 1);

  parser->filename     = filename;
  parser->value        = g_string_new (NULL);
  parser->id_attr_name = g_strdup ("id");
  parser->domain       = domain;
  parser->locale       = locale;

  context = g_markup_parse_context_new (&markup_parser, 0, parser, NULL);

  success = domain_parser_parse (context, io, error);

  g_markup_parse_context_free (context);
  g_io_channel_unref (io);

  g_string_free (parser->value, TRUE);
  g_free (parser->id_attr_name);
  g_free (parser);

  if (! success)
    {
      if (error)
        {
          gchar *msg = g_strdup_printf (_("Parse error in '%s':\n%s"),
                                        gimp_filename_to_utf8 (filename),
                                        (*error)->message);
          g_free ((*error)->message);
          (*error)->message = msg;
        }
    }

  g_free (filename);

  return success;
}

static gboolean
domain_parser_parse (GMarkupParseContext  *context,
                     GIOChannel           *io,
                     GError              **error)
{
  GIOStatus  status;
  gsize      len;
  gchar      buffer[4096];

  while (TRUE)
    {
      status = g_io_channel_read_chars (io,
                                        buffer, sizeof (buffer), &len, error);

      switch (status)
        {
        case G_IO_STATUS_ERROR:
          return FALSE;
        case G_IO_STATUS_EOF:
          return g_markup_parse_context_end_parse (context, error);
        case G_IO_STATUS_NORMAL:
          if (! g_markup_parse_context_parse (context, buffer, len, error))
            return FALSE;
          break;
        case G_IO_STATUS_AGAIN:
          break;
        }
    }

  return TRUE;
}

static void
domain_parser_start_element (GMarkupParseContext *context,
                             const gchar         *element_name,
                             const gchar        **attribute_names,
                             const gchar        **attribute_values,
                             gpointer             user_data,
                             GError             **error)
{
  DomainParser *parser = (DomainParser *) user_data;

  switch (parser->state)
    {
    case DOMAIN_START:
      if (strcmp (element_name, "gimp-help") == 0)
        {
          parser->state = DOMAIN_IN_HELP;
          domain_parser_parse_namespace (parser,
                                         attribute_names, attribute_values);
        }
      else
        domain_parser_start_unknown (parser);
      break;

    case DOMAIN_IN_HELP:
      if (strcmp (element_name, "help-item") == 0)
        {
          parser->state = DOMAIN_IN_ITEM;
          domain_parser_parse_item (parser,
                                    attribute_names, attribute_values);
        }
      else if (strcmp (element_name, "help-missing") == 0)
        {
          parser->state = DOMAIN_IN_MISSING;
          domain_parser_parse_missing (parser,
                                       attribute_names, attribute_values);
        }
      else
        domain_parser_start_unknown (parser);
      break;

    case DOMAIN_IN_ITEM:
    case DOMAIN_IN_MISSING:
    case DOMAIN_IN_UNKNOWN:
      domain_parser_start_unknown (parser);
      break;
    }
}

static void
domain_parser_end_element (GMarkupParseContext *context,
                           const gchar         *element_name,
                           gpointer             user_data,
                           GError             **error)
{
  DomainParser *parser = (DomainParser *) user_data;

  switch (parser->state)
    {
    case DOMAIN_START:
      g_warning ("domain_parser: This shouldn't happen.");
      break;

    case DOMAIN_IN_HELP:
      parser->state = DOMAIN_START;
      break;

    case DOMAIN_IN_ITEM:
    case DOMAIN_IN_MISSING:
      parser->state = DOMAIN_IN_HELP;
      break;

    case DOMAIN_IN_UNKNOWN:
      domain_parser_end_unknown (parser);
      break;
    }
}

static void
domain_parser_error (GMarkupParseContext *context,
                     GError              *error,
                     gpointer             user_data)
{
  DomainParser *parser = (DomainParser *) user_data;

  g_printerr ("help (parsing %s): %s", parser->filename, error->message);
}

static void
domain_parser_start_unknown (DomainParser *parser)
{
  if (parser->unknown_depth == 0)
    parser->last_known_state = parser->state;

  parser->state = DOMAIN_IN_UNKNOWN;
  parser->unknown_depth++;
}

static void
domain_parser_end_unknown (DomainParser *parser)
{
  g_assert (parser->unknown_depth > 0 && parser->state == DOMAIN_IN_UNKNOWN);

  parser->unknown_depth--;

  if (parser->unknown_depth == 0)
    parser->state = parser->last_known_state;
}

static void
domain_parser_parse_namespace (DomainParser  *parser,
                               const gchar  **names,
                               const gchar  **values)
{
  for (; *names && *values; names++, values++)
    {
      if (! strncmp (*names, "xmlns:", 6) &&
          ! strcmp (*values, parser->domain->help_domain))
        {
          g_free (parser->id_attr_name);
          parser->id_attr_name = g_strdup_printf ("%s:id", *names + 6);

#ifdef GIMP_HELP_DEBUG
          g_printerr ("help (%s): id attribute name for \"%s\" is \"%s\"\n",
                      parser->locale->locale_id,
                      parser->domain->help_domain,
                      parser->id_attr_name);
#endif
        }
    }
}

static void
domain_parser_parse_item (DomainParser  *parser,
                          const gchar  **names,
                          const gchar  **values)
{
  const gchar *id  = NULL;
  const gchar *ref = NULL;

  for (; *names && *values; names++, values++)
    {
      if (! strcmp (*names, parser->id_attr_name))
        id = *values;

      if (! strcmp (*names, "ref"))
        ref = *values;
    }

  if (id && ref)
    {
      if (! parser->locale->help_id_mapping)
        parser->locale->help_id_mapping = g_hash_table_new_full (g_str_hash,
                                                                 g_str_equal,
                                                                 g_free,
                                                                 g_free);

      g_hash_table_insert (parser->locale->help_id_mapping,
                           g_strdup (id), g_strdup (ref));

#ifdef GIMP_HELP_DEBUG
      g_printerr ("help (%s): added mapping \"%s\" -> \"%s\"\n",
                  parser->locale->locale_id, id, ref);
#endif
    }
}

static void
domain_parser_parse_missing (DomainParser  *parser,
                             const gchar  **names,
                             const gchar  **values)
{
  const gchar *ref = NULL;

  for (; *names && *values; names++, values++)
    {
      if (! strcmp (*names, "ref"))
        ref = *values;
    }

  if (ref &&
      parser->locale->help_missing == NULL)
    {
      parser->locale->help_missing = g_strdup (ref);

#ifdef GIMP_HELP_DEBUG
      g_printerr ("help (%s): added fallback for missing help -> \"%s\"\n",
                  parser->locale->locale_id, ref);
#endif
    }
}
