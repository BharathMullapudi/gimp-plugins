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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <sys/stat.h>

#include "gdk/gdkkeysyms.h"
#include "gtk/gtk.h"

#include "libgimp/gimp.h"
#include "libgimp/gimpui.h"

#include "script-fu-intl.h"

#include "siod-wrapper.h"
#include "script-fu-console.h"

#include <plug-ins/dbbrowser/dbbrowser_utils.h>

#ifdef G_OS_WIN32
#include <fcntl.h>
#include <io.h>
#endif


#define TEXT_WIDTH  400
#define TEXT_HEIGHT 400
#define ENTRY_WIDTH 400

#define BUFSIZE     256


#define message(string) printf("(%s): %d ::: %s\n", __PRETTY_FUNCTION__, __LINE__, string)


typedef struct
{
  GtkTextBuffer *console;
  GtkWidget     *cc;
  GtkWidget     *text_view;

  gint32         input_id;
} ConsoleInterface;


/*
 *  Local Functions
 */
static void       script_fu_console_interface  (void);
static void       script_fu_close_callback     (GtkWidget    *widget,
						gpointer      data);
static void       script_fu_browse_callback    (GtkWidget    *widget,
						gpointer      data);
static gboolean   script_fu_siod_read          (GIOChannel   *channel,
						GIOCondition  cond,
						gpointer      data);
static gboolean   script_fu_cc_is_empty        (void);
static gboolean   script_fu_cc_key_function    (GtkWidget    *widget,
						GdkEventKey  *event,
						gpointer      data);

static void       script_fu_open_siod_console  (void);
static void       script_fu_close_siod_console (void);


/*
 *  Local variables
 */
static ConsoleInterface cint =
{
  NULL,  /*  console  */
  NULL,  /*  current command  */
  NULL,  /*  text view  */

  -1     /*  input id  */
};

static gchar  read_buffer[BUFSIZE];
static GList *history     = NULL;
static gint   history_len = 0;
static gint   history_cur = 0;
static gint   history_max = 50;

static gint   siod_output_pipe[2];


/*
 *  Function definitions
 */

void
script_fu_console_run (gchar      *name,
		       gint        nparams,
		       GimpParam  *params,
		       gint       *nreturn_vals,
		       GimpParam **return_vals)
{
  static GimpParam  values[1];
  GimpPDBStatusType status = GIMP_PDB_SUCCESS;
  GimpRunModeType   run_mode;

  run_mode = params[0].data.d_int32;

  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE:
      /*  Enable SIOD output  */
      script_fu_open_siod_console ();

      /*  Run the interface  */
      script_fu_console_interface ();

      /*  Clean up  */
      script_fu_close_siod_console ();
      break;

    case GIMP_RUN_WITH_LAST_VALS:
    case GIMP_RUN_NONINTERACTIVE:
      status = GIMP_PDB_CALLING_ERROR;
      g_message (_("Script-Fu console mode allows only interactive invocation"));
      break;

    default:
      break;
    }

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = status;
}

static void
script_fu_console_interface (void)
{
  GtkWidget  *dialog;
  GtkWidget  *main_vbox;
  GtkWidget  *button;
  GtkWidget  *label;
  GtkWidget  *scrolled_window;
  GtkWidget  *hbox;
  GIOChannel *input_channel;

  gimp_ui_init ("script-fu", FALSE);

  dialog = gimp_dialog_new (_("Script-Fu Console"), "script-fu-console",
			    gimp_standard_help_func, "filters/script-fu.html",
			    GTK_WIN_POS_MOUSE,
			    FALSE, TRUE, FALSE,

			    GTK_STOCK_CLOSE, gtk_widget_destroy, NULL,
			    1, NULL, FALSE, TRUE,

			    NULL);

  g_signal_connect (G_OBJECT (dialog), "destroy",
		    G_CALLBACK (script_fu_close_callback),
		    NULL);
  g_signal_connect (G_OBJECT (dialog), "destroy",
		    G_CALLBACK (gtk_widget_destroyed),
		    &dialog);

  /*  The main vbox  */
  main_vbox = gtk_vbox_new (FALSE, 4);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 4);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), main_vbox,
		      TRUE, TRUE, 0);
  gtk_widget_show (main_vbox);

  label = gtk_label_new (_("SIOD Output"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (main_vbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  /*  The output text widget  */
  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_ALWAYS);
  gtk_box_pack_start (GTK_BOX (main_vbox), scrolled_window, TRUE, TRUE, 0);
  gtk_widget_show (scrolled_window);

  cint.console = gtk_text_buffer_new (NULL);
  cint.text_view = gtk_text_view_new_with_buffer (cint.console);
  g_object_unref (G_OBJECT (cint.console));

  gtk_text_view_set_editable (GTK_TEXT_VIEW (cint.text_view), FALSE);
  gtk_widget_set_usize (cint.text_view, TEXT_WIDTH, TEXT_HEIGHT);
  gtk_container_add (GTK_CONTAINER (scrolled_window), cint.text_view);
  gtk_widget_show (cint.text_view);

  gtk_text_buffer_create_tag (cint.console, "strong",
			      "weight", PANGO_WEIGHT_BOLD,
			      "size",   12 * PANGO_SCALE,
			      NULL);
  gtk_text_buffer_create_tag (cint.console, "emphasis",
			      "style",  PANGO_STYLE_OBLIQUE,
			      "size",   10 * PANGO_SCALE,
			      NULL);
  gtk_text_buffer_create_tag (cint.console, "weak",
			      "size",   10 * PANGO_SCALE,
			      NULL);

  {
    const gchar *greeting_texts[] =
    {
      "strong",   "The GIMP - GNU Image Manipulation Program\n\n",
      "emphasis", "Copyright (C) 1995-2001\n",
      "emphasis", "Spencer Kimball, Peter Mattis and the GIMP Development Team\n",
      "weak",     "\nThis program is free software; you can redistribute it and/or modify\n",
      "weak",     "it under the terms of the GNU General Public License as published by\n",
      "weak",     "the Free Software Foundation; either version 2 of the License, or\n",
      "weak",     "(at your option) any later version.\n\n",
      "weak",     "This program is distributed in the hope that it will be useful,\n",
      "weak",     "but WITHOUT ANY WARRANTY; without even the implied warranty of\n",
      "weak",     "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n",
      "weak",     "See the GNU General Public License for more details.\n\n",
      "weak",     "You should have received a copy of the GNU General Public License\n",
      "weak",     "along with this program; if not, write to the Free Software\n",
      "weak",     "Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.\n\n\n",
      "strong",   "Script-Fu Console - FIXME(\\n)\n",
      "emphasis", "Interactive Scheme Development\n\n",
      NULL
    };

    GtkTextIter cursor;
    gint        i;

    gtk_text_buffer_get_end_iter (cint.console, &cursor);

    for (i = 0; greeting_texts[i]; i += 2)
      {
	gtk_text_buffer_insert_with_tags_by_name (cint.console, &cursor,
						  greeting_texts[i + 1], -1,
						  greeting_texts[i],
						  NULL);
      }
  }

  /*  The current command  */
  label = gtk_label_new (_("Current Command"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (main_vbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  hbox = gtk_hbox_new (FALSE, 2);
  gtk_widget_set_usize (hbox, ENTRY_WIDTH, 0);
  gtk_box_pack_start (GTK_BOX (main_vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  cint.cc = gtk_entry_new ();
  gtk_box_pack_start (GTK_BOX (hbox), cint.cc, TRUE, TRUE, 0);
  GTK_WIDGET_SET_FLAGS (cint.cc, GTK_CAN_DEFAULT);
  gtk_widget_grab_default (cint.cc);
  gtk_widget_show (cint.cc);

  g_signal_connect (G_OBJECT (cint.cc), "key_press_event",
		    G_CALLBACK (script_fu_cc_key_function),
		    NULL);

  button = gtk_button_new_with_label (_("Browse..."));
  gtk_misc_set_padding (GTK_MISC (GTK_BIN (button)->child), 2, 0);
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, TRUE, 0);
  gtk_widget_show (button);

  g_signal_connect (G_OBJECT (button), "clicked",
		    G_CALLBACK (script_fu_browse_callback),
		    NULL);

  input_channel = g_io_channel_unix_new (siod_output_pipe[0]);
  cint.input_id = g_io_add_watch (input_channel,
				  G_IO_IN,
				  script_fu_siod_read,
				  NULL);

  /*  Initialize the history  */
  history     = g_list_append (history, NULL);
  history_len = 1;

  gtk_widget_show (dialog);

  gtk_main ();

  g_source_remove (cint.input_id);

  if (dialog)
    gtk_widget_destroy (dialog);
}

static void
script_fu_close_callback (GtkWidget *widget,
			  gpointer   data)
{
  gtk_main_quit ();
}

void 
apply_callback (gchar           *proc_name,
		gchar           *scheme_proc_name,
		gchar           *proc_blurb,
		gchar           *proc_help,
		gchar           *proc_author,
		gchar           *proc_copyright,
		gchar           *proc_date,
		GimpPDBProcType  proc_type,
		gint             nparams,
		gint             nreturn_vals,
		GimpParamDef    *params,
		GimpParamDef    *return_vals)
{
  gint     i;
  GString *text;

  if (proc_name == NULL) 
    return;
  
  text = g_string_new ("(");
  text = g_string_append (text, scheme_proc_name);
  for (i=0; i<nparams; i++) 
    {
      text = g_string_append_c (text, ' ');
      text = g_string_append (text, params[i].name);
    }
  text = g_string_append_c (text, ')');

  gtk_entry_set_text (GTK_ENTRY (cint.cc), text->str);
  g_string_free (text, TRUE);
}

static void
script_fu_browse_callback (GtkWidget *widget,
			   gpointer   data)
{
  gtk_quit_add_destroy (1, (GtkObject *) gimp_db_browser (apply_callback));
}

static gboolean
script_fu_console_idle_scroll_end (gpointer data)
{
  GtkAdjustment *adj;

  adj = GTK_ADJUSTMENT (data);

  gtk_adjustment_set_value (adj, adj->upper - adj->page_size);

  return FALSE;
}

static void
script_fu_console_scroll_end (void)
{
  GtkTextView *view;

  view = GTK_TEXT_VIEW (cint.text_view);

  /*  the text view idle updates so we need to idle scroll too
   */
  g_idle_add (script_fu_console_idle_scroll_end, view->vadjustment);
}

static gboolean
script_fu_siod_read (GIOChannel  *channel,
		     GIOCondition cond,
		     gpointer     data)
{
  gint         count;
  GIOStatus    status;
  GError      *error;
  GtkTextIter  cursor;

  count = 0;

  do
    {
      status = g_io_channel_read_chars (channel, read_buffer,
					BUFSIZE - 1,
					&count,
					&error);
    }
  while (status == G_IO_STATUS_AGAIN);

  if (status == G_IO_STATUS_NORMAL)
    {
      read_buffer[count] = '\0';

      gtk_text_buffer_get_end_iter (cint.console, &cursor);
      gtk_text_buffer_insert_with_tags_by_name (cint.console, &cursor,
						read_buffer, -1,
						"weak",
						NULL);

      script_fu_console_scroll_end ();
    }

  return TRUE;
}

static gboolean
script_fu_cc_is_empty (void)
{
  const gchar *str;

  if ((str = gtk_entry_get_text (GTK_ENTRY (cint.cc))) == NULL)
    return TRUE;

  while (*str)
    {
      if (*str != ' ' && *str != '\t' && *str != '\n')
	return FALSE;

      str ++;
    }

  return TRUE;
}

static gboolean
script_fu_cc_key_function (GtkWidget   *widget,
			   GdkEventKey *event,
			   gpointer     data)
{
  GList       *list;
  gint         direction = 0;
  GtkTextIter  cursor;

  switch (event->keyval)
    {
    case GDK_Return:
      gtk_signal_emit_stop_by_name (GTK_OBJECT (widget), "key_press_event");

      if (script_fu_cc_is_empty ())
	return TRUE;

      list = g_list_nth (history, (g_list_length (history) - 1));
      if (list->data)
	g_free (list->data);
      list->data = g_strdup (gtk_entry_get_text (GTK_ENTRY (cint.cc)));

      gtk_text_buffer_get_end_iter (cint.console, &cursor);

      gtk_text_buffer_insert_with_tags_by_name (cint.console, &cursor,
						"=> FIXME(\\n)\n", -1,
						"strong",
						NULL);
      {
	gchar *eek;

	eek = g_strdup_printf ("%s\n\n",
			       gtk_entry_get_text (GTK_ENTRY (cint.cc)));

	gtk_text_buffer_insert_with_tags_by_name (cint.console, &cursor,
						  eek, -1,
						  "weak",
						  NULL);

	g_free (eek);
      }

      /*
      gtk_text_buffer_insert_with_tags_by_name (cint.console, &cursor,
						"\n\n", -1,
						"weak",
						NULL);
      */

      script_fu_console_scroll_end ();

      gtk_entry_set_text (GTK_ENTRY (cint.cc), "");

      siod_interpret_string ((char *) list->data);
      gimp_displays_flush ();

      history = g_list_append (history, NULL);
      if (history_len == history_max)
	{
	  history = g_list_remove (history, history->data);
	  if (history->data)
	    g_free (history->data);
	}
      else
	history_len++;
      history_cur = g_list_length (history) - 1;

      return TRUE;
      break;

    case GDK_KP_Up:
    case GDK_Up:
      gtk_signal_emit_stop_by_name (GTK_OBJECT (widget), "key_press_event");
      direction = -1;
      break;

    case GDK_KP_Down:
    case GDK_Down:
      gtk_signal_emit_stop_by_name (GTK_OBJECT (widget), "key_press_event");
      direction = 1;
      break;

    case GDK_P:
    case GDK_p:
      if (event->state & GDK_CONTROL_MASK)
	{
	  gtk_signal_emit_stop_by_name (GTK_OBJECT (widget), "key_press_event");
	  direction = -1;
	}
      break;

    case GDK_N:
    case GDK_n:
      if (event->state & GDK_CONTROL_MASK)
	{
	  gtk_signal_emit_stop_by_name (GTK_OBJECT (widget), "key_press_event");
	  direction = 1;
	}
      break;

    default:
      break;
    }

  if (direction)
    {
      /*  Make sure we keep track of the current one  */
      if (history_cur == g_list_length (history) - 1)
	{
	  list = g_list_nth (history, history_cur);
	  if (list->data)
	    g_free (list->data);
	  list->data = g_strdup (gtk_entry_get_text (GTK_ENTRY (cint.cc)));
	}

      history_cur += direction;
      if (history_cur < 0)
	history_cur = 0;
      if (history_cur >= history_len)
	history_cur = history_len - 1;

      gtk_entry_set_text (GTK_ENTRY (cint.cc), 
			  (gchar *) (g_list_nth (history, history_cur))->data);

      return TRUE;
    }

  return FALSE;
}

static void
script_fu_open_siod_console (void)
{
  FILE *siod_output;

  siod_output = siod_get_output_file ();

  if (siod_output == stdout)
    {
      if (pipe (siod_output_pipe) == 0)
        {
          siod_output = fdopen (siod_output_pipe [1], "w");
          if (siod_output != NULL)
            {
              siod_set_verbose_level (2);
              siod_print_welcome ();
            }
          else 
            {
              g_message (_("Unable to open a stream on the SIOD output pipe"));
              siod_output = stdout;
            }
        } 
      else
        {
          g_message (_("Unable to open the SIOD output pipe"));
          siod_output = stdout;
        }
    }

  siod_set_output_file (siod_output);
}

static void
script_fu_close_siod_console (void)
{
  FILE *siod_output;

  siod_output = siod_get_output_file ();

  if (siod_output != stdout)
    fclose (siod_output);

  close (siod_output_pipe[0]);
  close (siod_output_pipe[1]);
}

void
script_fu_eval_run (gchar      *name,
		    gint        nparams,
		    GimpParam  *params,
		    gint       *nreturn_vals,
		    GimpParam **return_vals)
{
  static GimpParam  values[1];
  GimpPDBStatusType status = GIMP_PDB_SUCCESS;
  GimpRunModeType   run_mode;

  run_mode = params[0].data.d_int32;

  switch (run_mode)
    {
    case GIMP_RUN_NONINTERACTIVE:
      if (siod_interpret_string (params[1].data.d_string) != 0)
	status = GIMP_PDB_EXECUTION_ERROR;
      break;

    case GIMP_RUN_INTERACTIVE:
    case GIMP_RUN_WITH_LAST_VALS:
      status = GIMP_PDB_CALLING_ERROR;
      g_message (_("Script-Fu evaluate mode allows only noninteractive invocation"));
      break;

    default:
      break;
    }

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = status;
}
