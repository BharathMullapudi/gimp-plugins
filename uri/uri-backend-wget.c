/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
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

/* Author: Josh MacDonald. */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h>
#include <sys/wait.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


#define TIMEOUT "300"
#define BUFSIZE 1024


gboolean
uri_backend_init (GError **error)
{
  return TRUE;
}

void
uri_backend_shutdown (void)
{
}

const gchar *
uri_backend_get_load_protocols (void)
{
  return "http:,https:,ftp:";
}

const gchar *
uri_backend_get_save_protocols (void)
{
  return NULL;
}

gboolean
uri_backend_load_image (const gchar  *uri,
                        const gchar  *tmpname,
                        GimpRunMode   run_mode,
                        GError      **error)
{
  gint pid;
  gint p[2];

  if (pipe (p) != 0)
    {
      g_set_error (error, 0, 0, "pipe() failed: %s", g_strerror (errno));
      return FALSE;
    }

  if ((pid = fork()) < 0)
    {
      g_set_error (error, 0, 0, "fork() failed: %s", g_strerror (errno));
      return FALSE;
    }
  else if (pid == 0)
    {
      close (p[0]);
      close (2);
      dup (p[1]);
      close (p[1]);

#ifdef HAVE_PUTENV
      /* produce deterministic output */
      putenv ("LANGUAGE=C");
      putenv ("LC_ALL=C");
      putenv ("LANG=C");
#endif

      execlp ("wget", "wget", "-T", TIMEOUT, uri, "-O", tmpname, NULL);
      g_set_error (error, 0, 0, "exec() failed: wget: %s", g_strerror (errno));
      _exit (127);
    }
  else
    {
      FILE     *input;
      gchar     buf[BUFSIZE];
      gboolean  seen_resolve = FALSE;
      gboolean  connected    = FALSE;
      gboolean  file_found   = FALSE;
      gchar     sizestr[32];
      gint      size         = 0;
      gint      i, j;
      gchar     dot;
      gint      kilobytes    = 0;
      gboolean  finished     = FALSE;
      gboolean  debug        = FALSE;
      gchar    *memsize;
      gchar    *message;
      gchar    *timeout_msg;

#define DEBUG(x) if (debug) g_printerr (x)

      close (p[1]);

      input = fdopen (p[0], "r");

      /*  hardcoded and not-really-foolproof scanning of wget putput  */

      if (fgets (buf, sizeof (buf), input) == NULL)
        {
          /*  no message here because failing on the first line means
           *  that wget was not found
           */
          return FALSE;
        }

      DEBUG (buf);

      /*  The second line is the local copy of the file  */
      if (fgets (buf, sizeof (buf), input) == NULL)
        {
          g_set_error (error, 0, 0,
                       _("wget exited abnormally on URI '%s'"), uri);
          return FALSE;
        }

      DEBUG (buf);

      /*  The third line is "Connecting to..."  */

      timeout_msg = g_strdup_printf (_("(timeout is %s seconds)"), TIMEOUT);

      gimp_progress_init (NULL);
      gimp_progress_set_text ("%s %s",
                              _("Connecting to server..."), timeout_msg);

    read_connect:
      if (fgets (buf, sizeof (buf), input) == NULL)
        {
          g_set_error (error, 0, 0,
                       _("wget exited abnormally on URI '%s'"), uri);
          return FALSE;
        }
      else if (strstr (buf, "connected"))
        {
          connected = TRUE;
        }
      /* newer wgets have a "Resolving foo" line, so eat it */
      else if (!seen_resolve && strstr (buf, "Resolving"))
        {
          seen_resolve = TRUE;
          goto read_connect;
        }

      DEBUG (buf);

      /*  The fourth line is either the network request or an error  */

      gimp_progress_set_text ("%s %s", _("Opening URI..."), timeout_msg);

      if (fgets (buf, sizeof (buf), input) == NULL)
        {
          g_set_error (error, 0, 0,
                       _("wget exited abnormally on URI '%s'"), uri);
          return FALSE;
        }
      else if (! connected)
        {
          g_set_error (error, 0, 0,
                       _("A network error occured: %s"), buf);

          DEBUG (buf);

          return FALSE;
        }

      DEBUG (buf);

      /*  The fifth line is either the length of the file or an error  */
      if (fgets (buf, sizeof (buf), input) == NULL)
        {
          g_set_error (error, 0, 0,
                       _("wget exited abnormally on URI '%s'"), uri);
          return FALSE;
        }
      else if (strstr (buf, "Length"))
        {
          file_found = TRUE;
        }
      else
        {
          g_set_error (error, 0, 0,
                       _("A network error occured: %s"), buf);

          DEBUG (buf);

          return FALSE;
        }

      DEBUG (buf);

      if (sscanf (buf, "Length: %31s", sizestr) != 1)
        {
          g_set_error (error, 0, 0,
                       "Could not parse wget's file length message");
          return FALSE;
        }

      /*  strip away commas  */
      for (i = 0, j = 0; i < sizeof (sizestr); i++, j++)
        {
          if (sizestr[i] == ',')
            i++;

          sizestr[j] = sizestr[i];

          if (sizestr[j] == '\0')
            break;
        }

      size = atoi (sizestr);

      /*  Start the actual download...  */
      memsize = gimp_memsize_to_string (size);
      message = g_strdup_printf (_("Downloading %s of image data..."),
                                 memsize);

      gimp_progress_set_text ("%s %s", message, timeout_msg);

      g_free (message);
      g_free (memsize);

      /*  Switch to byte parsing wget's output...  */

      while (1)
        {
          dot = fgetc (input);

          if (feof (input))
            break;

          if (debug)
            {
              fputc (dot, stderr);
              fflush (stderr);
            }

          if (dot == '.')  /* one kilobyte */
            {
              kilobytes++;
              gimp_progress_update ((gdouble) (kilobytes * 1024) /
                                    (gdouble) size);
            }
          else if (dot == ':')  /* the time string contains a ':' */
            {
              fgets (buf, sizeof (buf), input);

              DEBUG (buf);

              if (! strstr (buf, "error"))
                {
                  finished = TRUE;
                  gimp_progress_update (1.0);
                }

              break;
            }
        }

      if (! finished)
        {
          g_set_error (error, 0, 0,
                       "wget exited before finishing downloading URI\n'%s'",
                       uri);
          return FALSE;
        }
    }

  return TRUE;
}

gboolean
uri_backend_save_image (const gchar  *uri,
                        const gchar  *tmpname,
                        GimpRunMode   run_mode,
                        GError      **error)
{
  g_set_error (error, 0, 0, "EEK");

  return FALSE;
}
