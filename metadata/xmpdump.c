/* simple program to test the XMP parser on files given on the command line */

#include <stdio.h>
#include <string.h>
#include <glib/gprintf.h>
#include "xmp-parse.h"

static gpointer
start_schema (XMPParseContext     *context,
              const gchar         *ns_uri,
              const gchar         *ns_prefix,
              gpointer             user_data,
              GError             **error)
{
  printf ("Schema %s = \"%s\"\n", ns_prefix, ns_uri);
  return (gpointer) ns_prefix;
}

static void
end_schema (XMPParseContext     *context,
            gpointer             user_ns_data,
            gpointer             user_data,
            GError             **error)
{
  /* printf ("End of %s\n", user_ns_prefix); */
}

static void
set_property (XMPParseContext     *context,
              const gchar         *name,
              XMPParseType         type,
              const gchar        **value,
              gpointer             user_ns_data,
              gpointer             user_data,
              GError             **error)
{
  const gchar *ns_prefix = user_ns_data;
  int          i;

  switch (type)
    {
    case XMP_PTYPE_TEXT:
      printf ("\t%s:%s = \"%s\"\n", ns_prefix, name,
              value[0]);
      break;

    case XMP_PTYPE_RESOURCE:
      printf ("\t%s:%s @ = \"%s\"\n", ns_prefix, name,
              value[0]);
      break;

    case XMP_PTYPE_ORDERED_LIST:
    case XMP_PTYPE_UNORDERED_LIST:
      printf ("\t%s:%s [] =", ns_prefix, name);
      for (i = 0; value[i] != NULL; i++)
        if (i == 0)
          printf (" \"%s\"", value[i]);
      else
          printf (", \"%s\"", value[i]);
      printf ("\n");
      break;

    case XMP_PTYPE_ALT_LANG:
      for (i = 0; value[i] != NULL; i += 2)
        printf ("\t%s:%s [lang:%s] = \"%s\"\n", ns_prefix, name,
                value[i], value[i + 1]);
      break;

    case XMP_PTYPE_STRUCTURE:
      printf ("\tLocal schema %s = \"%s\"\n", value[0], value[1]);
      for (i = 2; value[i] != NULL; i += 2)
        printf ("\t%s:%s [%s] = \"%s\"\n", ns_prefix, name,
                value[i], value[i + 1]);
      break;

    default:
      printf ("\t%s:%s = ?\n", ns_prefix, name);
      break;
    }
}

static void
print_error (XMPParseContext *context,
             GError          *error,
             gpointer         user_data)
{
  gchar *filename = user_data;

  fprintf (stderr, "While parsing XMP metadata in %s:\n%s\n",
           filename, error->message);
}

static XMPParser xmp_parser = {
  start_schema,
  end_schema,
  set_property,
  print_error
};

static int
scan_file (const gchar *filename)
{
  gchar *contents;
  gsize  length;
  GError *error;
  XMPParseContext *context;

  printf ("\nFile: %s\n", filename);
  error = NULL;
  if (!g_file_get_contents (filename,
                            &contents,
                            &length,
                            &error))
    {
      print_error (NULL, error, (gpointer) filename);
      g_error_free (error);
      return 1;
    }

  context = xmp_parse_context_new (&xmp_parser,
                                   XMP_FLAG_FIND_XPACKET,
                                   (gpointer) filename,
                                   NULL);

  if (! xmp_parse_context_parse (context, contents, length, NULL))
    {
      xmp_parse_context_free (context);
      return 1;
    }

  if (! xmp_parse_context_end_parse (context, NULL))
    {
      xmp_parse_context_free (context);
      return 1;
    }

  xmp_parse_context_free (context);
  return 0;
}

int
main (int   argc,
      char *argv[])
{
  if (argc > 1)
    {
      for (argv++, argc--; argc; argv++, argc--)
        if (scan_file (*argv) != 0)
          return 1;
      return 0;
    }
  else
    {
      fprintf (stderr, "Usage:\n"
               "\txmpdump file [file [...]]\n\n"
               "The file(s) given on the command line will be scanned "
               "for XMP metadata\n");
      return 1;
    }
}
