/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 * HRZ reading and writing code Copyright (C) 1996 Albert Cahalan
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

/* $Id$ */

/*
 * Albert Cahalan <acahalan at cs.uml.edu>, 1997  -  Initial HRZ support.
 * Based on PNM code by Erik Nygren (nygren@mit.edu)
 *
 * Bug reports are wanted. I'd like to remove useless code.
 *
 * The HRZ file is always 256x240 with RGB values from 0 to 63.
 * No compression, no header, just the raw RGB data.
 * It is (was?) used for amatuer radio slow-scan TV.
 * That makes the size 256*240*3 = 184320 bytes.
 */

#include "config.h"

#include <errno.h>
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
#ifdef HAVE_MMAP
#include <sys/mman.h>
#endif

#include <glib.h> /* for G_OS_* */

#ifdef G_OS_WIN32
#include <io.h>
#endif

#ifndef _O_BINARY
#define _O_BINARY 0
#endif

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


/* Declare local data types
 */

typedef struct
{
  gboolean run;
} HRZSaveInterface;


/* Declare some local functions.
 */
static void   query       (void);
static void   run         (const gchar      *name,
			   gint              nparams,
			   const GimpParam  *param,
			   gint             *nreturn_vals,
			   GimpParam       **return_vals);
static gint32 load_image  (const gchar      *filename);
static gint   save_image  (const gchar      *filename,
			   gint32            image_ID,
			   gint32            drawable_ID);

/*
static gint   save_dialog      (void);
static void   save_ok_callback (GtkWidget *widget,
				gpointer   data);
*/

#define hrzscanner_eof(s) ((s)->eof)
#define hrzscanner_fp(s)  ((s)->fp)

/* Checks for a fatal error */
#define CHECK_FOR_ERROR(predicate, jmpbuf, errmsg) \
        if ((predicate)) \
        { /*gimp_message((errmsg));*/ longjmp((jmpbuf),1); }

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};

/*
static HRZSaveInterface psint =
{
  FALSE     / * run * /
};
*/


MAIN ()

static void
query (void)
{
  static GimpParamDef load_args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_STRING, "filename", "The name of the file to load" },
    { GIMP_PDB_STRING, "raw_filename", "The name of the file to load" }
  };
  static GimpParamDef load_return_vals[] =
  {
    { GIMP_PDB_IMAGE, "image", "Output image" }
  };

  static GimpParamDef save_args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE, "image", "Input image" },
    { GIMP_PDB_DRAWABLE, "drawable", "Drawable to save" },
    { GIMP_PDB_STRING, "filename", "The name of the file to save the image in" },
    { GIMP_PDB_STRING, "raw_filename", "The name of the file to save the image in" }
  };

  gimp_install_procedure ("file_hrz_load",
                          "loads files of the hrz file format",
                          "FIXME: write help for hrz_load",
                          "Albert Cahalan",
                          "Albert Cahalan",
                          "1997",
                          "<Load>/HRZ",
			  NULL,
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (load_args),
                          G_N_ELEMENTS (load_return_vals),
                          load_args, load_return_vals);

  gimp_install_procedure ("file_hrz_save",
                          "saves files in the hrz file format",
                          "HRZ saving handles all image types except those with alpha channels.",
                          "Albert Cahalan",
                          "Albert Cahalan",
                          "1997",
                          "<Save>/HRZ",
			  "RGB, GRAY",
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (save_args), 0,
                          save_args, NULL);

  gimp_register_magic_load_handler ("file_hrz_load",
				    "hrz",
				    "",
				    "0,size,184320");
  gimp_register_save_handler       ("file_hrz_save",
				    "hrz",
				    "");
}

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  static GimpParam     values[2];
  GimpRunMode          run_mode;
  GimpPDBStatusType    status = GIMP_PDB_SUCCESS;
  gint32               image_ID;
  gint32               drawable_ID;
  GimpExportReturnType export = GIMP_EXPORT_CANCEL;

  run_mode = param[0].data.d_int32;

  INIT_I18N ();

  *nreturn_vals = 1;
  *return_vals  = values;
  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;

  if (strcmp (name, "file_hrz_load") == 0)
    {
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
  else if (strcmp (name, "file_hrz_save") == 0)
    {
      image_ID    = param[1].data.d_int32;
      drawable_ID = param[2].data.d_int32;

      /*  eventually export the image */ 
      switch (run_mode)
	{
	case GIMP_RUN_INTERACTIVE:
	case GIMP_RUN_WITH_LAST_VALS:
	  gimp_ui_init ("hrz", FALSE);
	  export = gimp_export_image (&image_ID, &drawable_ID, "HRZ", 
				      (GIMP_EXPORT_CAN_HANDLE_RGB |
				       GIMP_EXPORT_CAN_HANDLE_GRAY));
	  if (export == GIMP_EXPORT_CANCEL)
	    {
	      values[0].data.d_status = GIMP_PDB_CANCEL;
	      return;
	    }
	  break;
	default:
	  break;
	}

      switch (run_mode)
	{
	case GIMP_RUN_INTERACTIVE:
	  /*  First acquire information with a dialog  */
	  /*  Save dialog has no options (yet???)
	  if (! save_dialog ())
	    status = GIMP_PDB_CANCEL;
	  */
	  break;

	case GIMP_RUN_NONINTERACTIVE:
	  /*  Make sure all the arguments are there!  */
	  if (nparams != 4)
	    status = GIMP_PDB_CALLING_ERROR;
	  break;

	case GIMP_RUN_WITH_LAST_VALS:
	  break;

	default:
	  break;
	}

      if (status == GIMP_PDB_SUCCESS)
	{
	  if (! save_image (param[3].data.d_string, image_ID, drawable_ID))
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

/************ load HRZ image row *********************/
void
do_hrz_load (void         *mapped,
	     GimpPixelRgn *pixel_rgn)
{
  guchar *data, *d;
  gint    x, y;
  gint    start, end, scanlines;

  data = g_malloc (gimp_tile_height () * 256 * 3);

  for (y = 0; y < 240; )
    {
      start = y;
      end = y + gimp_tile_height ();
      end = MIN (end, 240);
      scanlines = end - start;
      d = data;

      memcpy (d, ((guchar *) mapped) + 256 * 3 * y,
	      256 * 3 * scanlines); /* this is gross */

      /* scale 0..63 into 0..255 properly */
      for (x = 0; x < 256 * 3 * scanlines; x++)
	d[x] = (d[x]>>4) | (d[x]<<2);

      d += 256 * 3 * y;

      gimp_progress_update ((double) y / 240.0);
      gimp_pixel_rgn_set_rect (pixel_rgn, data, 0, y, 256, scanlines);
      y += scanlines;
    }

  g_free (data);
}

/********************* Load HRZ image **********************/
static gint32
load_image (const gchar *filename)
{
  GimpPixelRgn pixel_rgn;
  gint32 image_ID;
  gint32 layer_ID;
  GimpDrawable *drawable;
  gint filedes;
  gchar *temp;
  void *mapped;  /* memory mapped file data */
  struct stat statbuf;  /* must check file size */

  /* open the file */
  filedes = open (filename, O_RDONLY | _O_BINARY);

  if (filedes == -1)
    {
      g_message (_("Can't open '%s':\n%s"), filename, g_strerror (errno));
      return -1;
    }

  temp = g_strdup_printf (_("Opening '%s'..."), filename);
  gimp_progress_init (temp);
  g_free (temp);

  /* stat the file to see if it is the right size */
  fstat (filedes, &statbuf);
  if (statbuf.st_size != 256*240*3)
    {
      g_message (_("'%s' is not a HRZ file"), filename);
      return -1;
    }
#ifdef HAVE_MMAP
  mapped = mmap(NULL, 256*240*3, PROT_READ, MAP_PRIVATE, filedes, 0);
  if (mapped == (void *)(-1))
    {
      g_message ("Could not map file");
      return -1;
    }
#else
  mapped = g_malloc(256*240*3);
  if (read (filedes, mapped, 256*240*3) != 256*240*3)
    {
      g_message ("File read error");
      return -1;
    }
#endif
  close (filedes);  /* not needed anymore, data is memory mapped */

  /* Create new image of proper size; associate filename */
  image_ID = gimp_image_new (256, 240, GIMP_RGB);
  gimp_image_set_filename (image_ID, filename);

  layer_ID = gimp_layer_new (image_ID, _("Background"),
			     256, 240,
			     GIMP_RGB_IMAGE, 100, GIMP_NORMAL_MODE);
  gimp_image_add_layer (image_ID, layer_ID, 0);

  drawable = gimp_drawable_get (layer_ID);
  gimp_pixel_rgn_init (&pixel_rgn, drawable,
		       0, 0, drawable->width, drawable->height, TRUE, FALSE);

  do_hrz_load (mapped, &pixel_rgn);

  /* close the file */
#ifdef HAVE_MMAP
  munmap (mapped, 256*240*3);
#else
  g_free (mapped);
#endif

  /* Tell the GIMP to display the image.
   */
  gimp_drawable_flush (drawable);

  return image_ID;
}

/************** Writes out RGB raw rows ************/
static void
saverow (FILE   *fp,
	 guchar *data)
{
  gint loop = 256*3;
  guchar *walk = data;
  while (loop--)
    {
      *walk = (*walk >> 2);
      walk++;
    }
  fwrite (data, 1, 256 * 3, fp);
}

/********************* save image *********************/
static gint
save_image (const gchar *filename,
	    gint32       image_ID,
	    gint32       drawable_ID)
{
  GimpPixelRgn pixel_rgn;
  GimpDrawable *drawable;
  GimpImageType drawable_type;
  guchar *data;
  guchar *d;          /* FIX */
  guchar *rowbuf;
  gchar *temp;
  gint np = 3;
  gint xres, yres;
  gint ypos, yend;
  FILE *fp;

  /* initialize */

  d = NULL;

  drawable = gimp_drawable_get (drawable_ID);
  drawable_type = gimp_drawable_type (drawable_ID);
  gimp_pixel_rgn_init (&pixel_rgn, drawable,
		       0, 0, drawable->width, drawable->height, FALSE, FALSE);

  /*  Make sure we're not saving an image with an alpha channel  */
  if (gimp_drawable_has_alpha (drawable_ID))
    {
      /* gimp_message ("HRZ save cannot handle images with alpha channels.");  */
      return FALSE;
    }

  /* open the file */
  fp = fopen (filename, "wb");
  if (fp == NULL)
    {
      g_message ("Can't open '%s' for writing:\n%s",
                 filename, g_strerror (errno));
      return FALSE;
    }

  xres = drawable->width;
  yres = drawable->height;

  if ((xres != 256) || (yres != 240))
    {
      g_message (_("Image must be 256x240"));
      return FALSE;
    }
  if (drawable_type == GIMP_INDEXED_IMAGE)
    {
      g_message (_("Image must be RGB or GRAY"));
      return FALSE;
    }

  temp = g_strdup_printf (_("Saving '%s'..."), filename);
  gimp_progress_init (temp);
  g_free (temp);

  /* allocate a buffer for retrieving information from the pixel region  */
  data = (guchar *) g_malloc (gimp_tile_height () * drawable->width *
			      drawable->bpp);

  rowbuf = g_malloc (256 * 3);

  /* Write the body out */
  for (ypos = 0; ypos < yres; ypos++)
    {
      if ((ypos % gimp_tile_height ()) == 0)
	{
	  yend = ypos + gimp_tile_height ();
	  yend = MIN (yend, yres);
	  gimp_pixel_rgn_get_rect (&pixel_rgn, data,
				   0, ypos, xres, (yend - ypos));
	  d = data;
	}

      saverow (fp, d);
      d += xres * np;

      if (!(ypos & 0x0f))
	gimp_progress_update ((double)ypos / 240.0 );
    }

  /* close the file */
  fclose (fp);

  g_free (rowbuf);
  g_free (data);

  gimp_drawable_detach (drawable);

  return TRUE;
}

/*********** Save dialog ************/
/*
static gint
save_dialog (void)
{
  GtkWidget *dlg;

  dlg = gimp_dialog_new (_("Save as HRZ"), "hrz",
			 gimp_standard_help_func, "filters/hrz.html",
			 GTK_WIN_POS_MOUSE,
			 FALSE, TRUE, FALSE,

			 GTK_STOCK_CANCEL, gtk_widget_destroy,
			 NULL, 1, NULL, FALSE, TRUE,
			 GTK_STOCK_OK, save_ok_callback,
			 NULL, NULL, NULL, TRUE, FALSE,

			 NULL);

  g_signal_connect (dlg, "destroy",
                    G_CALLBACK (gtk_main_quit),
		    NULL);

  gtk_main ();
  gdk_flush ();

  return psint.run;
}

static void
save_ok_callback (GtkWidget *widget,
		  gpointer   data)
{
  psint.run = TRUE;

  gtk_widget_destroy (GTK_WIDGET (data));
}
*/
