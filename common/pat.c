/*
 * pat plug-in version 1.01
 * Loads/saves version 1 GIMP .pat files, by Tim Newsome <drz@frody.bloke.com>
 * Some bits stolen from the .99.7 source tree.
 * Updated to fix various outstanding problems, brief help -- Nick Lamb
 * njl195@zepler.org.uk, April 2000
 */

#include "config.h"

#include <setjmp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


#ifdef G_OS_WIN32
#include <io.h>
#endif

#ifndef _O_BINARY
#define _O_BINARY 0
#endif

#include "app/core/gimppattern-header.h"


/*  local function prototypes  */

static void       query          (void);
static void       run            (gchar        *name,
				  gint          nparams,
				  GimpParam    *param,
				  gint         *nreturn_vals,
				  GimpParam   **return_vals);
static gint32     load_image     (gchar        *filename);
static gboolean   save_image     (gchar        *filename,
				  gint32        image_ID,
				  gint32        drawable_ID);

static gboolean   save_dialog    (void);
static void       ok_callback    (GtkWidget    *widget,
				  gpointer      data);
static void       entry_callback (GtkWidget    *widget,
				  gpointer      data);


GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};

/*  private variables  */

static gchar    description[256] = "GIMP Pattern";
static gboolean run_flag = FALSE;



MAIN ()

static void
query (void)
{
  static GimpParamDef load_args[] =
  {
    { GIMP_PDB_INT32,  "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_STRING, "filename", "The name of the file to load" },
    { GIMP_PDB_STRING, "raw_filename", "The name of the file to load" }
  };
  static GimpParamDef load_return_vals[] =
  {
    { GIMP_PDB_IMAGE, "image", "Output image" }
  };
  static gint nload_args = sizeof (load_args) / sizeof (load_args[0]);
  static gint nload_return_vals = (sizeof (load_return_vals) /
				   sizeof (load_return_vals[0]));

  static GimpParamDef save_args[] =
  {
    { GIMP_PDB_INT32,    "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE,    "image", "Input image" },
    { GIMP_PDB_DRAWABLE, "drawable", "Drawable to save" },
    { GIMP_PDB_STRING,   "filename", "The name of the file to save the image in" },
    { GIMP_PDB_STRING,   "raw_filename", "The name of the file to save the image in" },
    { GIMP_PDB_STRING,   "description", "Short description of the pattern" },
  };
  static gint nsave_args = sizeof (save_args) / sizeof (save_args[0]);

  gimp_install_procedure ("file_pat_load",
                          "Loads Gimp's .PAT pattern files",
                          "The images in the pattern dialog can be loaded "
			  "directly with this plug-in",
                          "Tim Newsome",
                          "Tim Newsome",
                          "1997",
                          "<Load>/PAT",
                          NULL,
                          GIMP_PLUGIN,
                          nload_args, nload_return_vals,
                          load_args, load_return_vals);

  gimp_install_procedure ("file_pat_save",
                          "Saves Gimp pattern file (.PAT)",
                          "New Gimp patterns can be created by saving them "
			  "in the appropriate place with this plug-in.",
                          "Tim Newsome",
                          "Tim Newsome",
                          "1997",
                          "<Save>/PAT",
                          "RGB, GRAY",
                          GIMP_PLUGIN,
                          nsave_args, 0,
                          save_args, NULL);

  gimp_register_magic_load_handler ("file_pat_load",
				    "pat",
				    "",
				    "20,string,GPAT");
  gimp_register_save_handler       ("file_pat_save",
				    "pat",
				    "");
}

static void
run (gchar      *name,
     gint        nparams,
     GimpParam  *param,
     gint       *nreturn_vals,
     GimpParam **return_vals)
{
  static GimpParam     values[2];
  GimpRunModeType      run_mode;
  GimpPDBStatusType    status = GIMP_PDB_SUCCESS;
  gint32               image_ID;
  gint32               drawable_ID;
  GimpExportReturnType export = GIMP_EXPORT_CANCEL;

  run_mode = param[0].data.d_int32;

  *nreturn_vals = 1;
  *return_vals  = values;
  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;

  if (strcmp (name, "file_pat_load") == 0) 
    {
      INIT_I18N();
      image_ID = load_image (param[1].data.d_string);

      if (image_ID != -1) 
	{
	  *nreturn_vals = 2;
	  values[1].type         = GIMP_PDB_IMAGE;
	  values[1].data.d_image = image_ID;
	}
      else
	{
	  status = GIMP_PDB_EXECUTION_ERROR;
	}
    }
  else if (strcmp (name, "file_pat_save") == 0) 
    {
      image_ID    = param[1].data.d_int32;
      drawable_ID = param[2].data.d_int32;

      /*  eventually export the image */ 
      switch (run_mode)
	{
	case GIMP_RUN_INTERACTIVE:
	case GIMP_RUN_WITH_LAST_VALS:
	  INIT_I18N_UI();
	  gimp_ui_init ("pat", FALSE);
	  export = gimp_export_image (&image_ID, &drawable_ID, "PAT", 
				      (GIMP_EXPORT_CAN_HANDLE_GRAY |
				       GIMP_EXPORT_CAN_HANDLE_RGB));
	  if (export == GIMP_EXPORT_CANCEL)
	    {
	      values[0].data.d_status = GIMP_PDB_CANCEL;
	      return;
	    }
	  break;
	default:
	  INIT_I18N();
	  break;
	}

      switch (run_mode) 
	{
	case GIMP_RUN_INTERACTIVE:
	  /*  Possibly retrieve data  */
	  gimp_get_data ("file_pat_save", description);
	  if (!save_dialog ())
	    status = GIMP_PDB_CANCEL;
	  break;

	case GIMP_RUN_NONINTERACTIVE:
	  if (nparams != 6)
	    {
	      status = GIMP_PDB_CALLING_ERROR;
	    }
	  else
	    {
	      strcpy (description, param[5].data.d_string);
	    }
	  break;

	case GIMP_RUN_WITH_LAST_VALS:
	  gimp_get_data ("file_pat_save", description);
	  break;
	}

      if (status == GIMP_PDB_SUCCESS)
	{
	  if (save_image (param[3].data.d_string, image_ID, drawable_ID))
	    {
	      gimp_set_data ("file_pat_save", description, 256);
	    } 
	  else
	    {
	      status = GIMP_PDB_EXECUTION_ERROR;
	    }
	}

      if (export == GIMP_EXPORT_EXPORT)
	gimp_image_delete (image_ID);
    }
  else
    {
      status = GIMP_PDB_CALLING_ERROR;
    }

  values[0].data.d_status = status;
}

static gint32 
load_image (gchar *filename) 
{
  gchar            *temp;
  gint              fd;
  PatternHeader     ph;
  guchar           *buffer;
  gint32            image_ID;
  gint32            layer_ID;
  GimpDrawable     *drawable;
  gint              line;
  GimpPixelRgn      pixel_rgn;
  GimpImageBaseType base_type;
  GimpImageType     image_type;

  temp = g_strdup_printf (_("Loading %s:"), filename);
  gimp_progress_init (temp);
  g_free (temp);

  fd = open (filename, O_RDONLY | _O_BINARY);

  if (fd == -1)
    return -1;

  if (read (fd, &ph, sizeof (PatternHeader)) != sizeof (PatternHeader)) 
    {
      close (fd);
      return -1;
    }

  /*  rearrange the bytes in each unsigned int  */
  ph.header_size  = g_ntohl (ph.header_size);
  ph.version      = g_ntohl (ph.version);
  ph.width        = g_ntohl (ph.width);
  ph.height       = g_ntohl (ph.height);
  ph.bytes        = g_ntohl (ph.bytes);
  ph.magic_number = g_ntohl (ph.magic_number);

  if (ph.magic_number != GPATTERN_MAGIC ||
      ph.version      != 1 ||
      ph.header_size  <= sizeof (PatternHeader)) 
    {
      close (fd);
      return -1;
    }

  if (lseek (fd, ph.header_size - sizeof (PatternHeader), SEEK_CUR) != ph.header_size) 
    {
      close (fd);
      return -1;
    }

  /* Now there's just raw data left. */
  
  /*
   * Create a new image of the proper size and associate the filename with it.
   */

  switch (ph.bytes)
    {
    case 1:
      base_type = GIMP_GRAY;
      image_type = GIMP_GRAY_IMAGE;
      break;
    case 2:
      base_type = GIMP_GRAY;
      image_type = GIMP_GRAYA_IMAGE;
      g_message ("Your pattern has an aplha channel,\n"
		 "please flatten and save it again to fix this.\n"
		 "Loading it anyway...");
      break;
    case 3:
      base_type = GIMP_RGB;
      image_type = GIMP_RGB_IMAGE;
      break;
    case 4:
      base_type = GIMP_RGB;
      image_type = GIMP_RGBA_IMAGE;
      g_message ("Your pattern has an aplha channel,\n"
		 "please flatten and save it again to fix this.\n"
		 "Loading it anyway...");
      break;
    default:
      g_message ("Unsupported pattern depth: %d\nGIMP Patterns must be GRAY or RGB\n", ph.bytes);
      return -1;
    }

  image_ID = gimp_image_new (ph.width, ph.height, base_type);
  gimp_image_set_filename (image_ID, filename);

  layer_ID = gimp_layer_new (image_ID, _("Background"), ph.width, ph.height,
			    image_type, 100, GIMP_NORMAL_MODE);
  gimp_image_add_layer (image_ID, layer_ID, 0);

  drawable = gimp_drawable_get (layer_ID);
  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0,
		       drawable->width, drawable->height, 
		       TRUE, FALSE);

  buffer = g_malloc (ph.width * ph.bytes);

  for (line = 0; line < ph.height; line++) 
    {
      if (read (fd, buffer, ph.width * ph.bytes) != ph.width * ph.bytes)
	{
	  close (fd);
	  g_free (buffer);
	  return -1;
	}

      gimp_pixel_rgn_set_row (&pixel_rgn, buffer, 0, line, ph.width);

      gimp_progress_update ((gdouble) line / (gdouble) ph.height);
    }

  gimp_drawable_flush (drawable);

  return image_ID;
}

static gboolean
save_image (gchar  *filename, 
	    gint32  image_ID, 
	    gint32  drawable_ID) 
{
  gint          fd;
  PatternHeader ph;
  guchar       *buffer;
  GimpDrawable *drawable;
  gint          line;
  GimpPixelRgn  pixel_rgn;
  gchar        *temp;

  temp = g_strdup_printf (_("Saving %s:"), filename);
  gimp_progress_init (temp);
  g_free (temp);

  drawable = gimp_drawable_get (drawable_ID);
  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, drawable->width,
		       drawable->height, FALSE, FALSE);

  fd = open (filename, O_CREAT | O_TRUNC | O_WRONLY | _O_BINARY, 0644);

  if (fd == -1)
    return FALSE;

  ph.header_size  = g_htonl (sizeof (PatternHeader) + strlen (description) + 1);
  ph.version      = g_htonl (1);
  ph.width        = g_htonl (drawable->width);
  ph.height       = g_htonl (drawable->height);
  ph.bytes        = g_htonl (drawable->bpp);
  ph.magic_number = g_htonl (GPATTERN_MAGIC);

  if (write (fd, &ph, sizeof (PatternHeader)) != sizeof (PatternHeader)) 
    {
      close (fd);
      return FALSE;
    }

  if (write (fd, description, strlen (description) + 1) != strlen (description) + 1) 
    {
      close (fd);
      return FALSE;
    }

  buffer = g_malloc (drawable->width * drawable->bpp);
  if (buffer == NULL) 
    {
      close (fd);
      return FALSE;
    }

  for (line = 0; line < drawable->height; line++) 
    {
      gimp_pixel_rgn_get_row (&pixel_rgn, buffer, 0, line, drawable->width);

      if (write (fd, buffer, drawable->width * drawable->bpp) !=
	  drawable->width * drawable->bpp) 
	{
	  close (fd);
	  return FALSE;
	}

      gimp_progress_update ((gdouble) line / (gdouble) drawable->height);
    }
 
  g_free (buffer);
  close (fd);

  return TRUE;
}

static gboolean
save_dialog (void)
{
  GtkWidget *dlg;
  GtkWidget *table;
  GtkWidget *entry;

  dlg = gimp_dialog_new (_("Save as Pattern"), "pat",
			 gimp_standard_help_func, "filters/pat.html",
			 GTK_WIN_POS_MOUSE,
			 FALSE, TRUE, FALSE,

			 GTK_STOCK_CANCEL, gtk_widget_destroy,
			 NULL, 1, NULL, FALSE, TRUE,
			 GTK_STOCK_OK, ok_callback,
			 NULL, NULL, NULL, TRUE, FALSE,

			 NULL);

  gtk_signal_connect (GTK_OBJECT (dlg), "destroy",
		      GTK_SIGNAL_FUNC (gtk_main_quit),
		      NULL);

  /* The main table */
  table = gtk_table_new (1, 2, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_container_set_border_width (GTK_CONTAINER (table), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), table, TRUE, TRUE, 0);
  gtk_widget_show (table);

  entry = gtk_entry_new ();
  gtk_widget_set_size_request (entry, 200, -1);
  gtk_entry_set_text (GTK_ENTRY (entry), description);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 0,
			     _("Description:"), 1.0, 0.5,
			     entry, 1, FALSE);
  gtk_signal_connect (GTK_OBJECT (entry), "changed",
		      GTK_SIGNAL_FUNC (entry_callback),
		      description);
  gtk_widget_show (entry);

  gtk_widget_show (dlg);

  gtk_main ();
  gdk_flush ();

  return run_flag;
}

static void 
ok_callback (GtkWidget *widget, 
	     gpointer   data)
{
  run_flag = TRUE;

  gtk_widget_destroy (GTK_WIDGET (data));
}

static void 
entry_callback (GtkWidget *widget, 
		gpointer   data)
{
  if (data == description)
    strncpy (description, gtk_entry_get_text (GTK_ENTRY (widget)), 256);
}
