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

/* JPEG loading and saving file filter for the GIMP
 *  -Peter Mattis
 *
 * This filter is heavily based upon the "example.c" file in libjpeg.
 * In fact most of the loading and saving code was simply cut-and-pasted
 *  from that file. The filter, therefore, also uses libjpeg.
 */

/* 11-JAN-99 - Added support for JPEG comments and Progressive saves.
 *  -pete whiting <pwhiting@sprint.net>
 *
 * Comments of size up to 32k can be stored in the header of jpeg
 * files.  (They are not compressed.)  The JPEG specs and libraries
 * support the storing of multiple comments.  The behavior of this
 * code is to merge all comments in a loading image into a single
 * comment (putting \n between each) and attach that string as a
 * parasite - gimp-comment - to the image.  When saving, the image is
 * checked to see if it has the gimp-comment parasite - if so, that is
 * used as the default comment in the save dialog.  Further, the other
 * jpeg parameters (quality, smoothing, compression and progressive)
 * are attached to the image as a parasite.  This allows the
 * parameters to remain consistent between saves.  I was not able to
 * figure out how to determine the quality, smoothing or compression
 * values of an image as it is being loaded, but the code is there to
 * support it if anyone knows how.  Progressive mode is a method of
 * saving the image such that as a browser (or other app supporting
 * progressive loads - gimp doesn't) loads the image it first gets a
 * low res version displayed and then the image is progressively
 * enhanced until you get the final version.  It doesn't add any size
 * to the image (actually it often results in smaller file size) - the
 * only draw backs are that progressive jpegs are not supported by some
 * older viewers/browsers, and some might find it annoying.
 */

/*
 * 21-AUG-99 - Added support for JPEG previews, subsampling,
 *             non-baseline JPEGs, restart markers and DCT method choice
 * - Steinar H. Gunderson <sgunderson@bigfoot.com>
 *
 * A small preview appears and changes according to the changes to the
 * compression options. The image itself works as a much bigger preview.
 * For slower machines, the save operation (not the load operation) is
 * done in the background, with a standard GTK+ idle loop, which turned
 * out to be the most portable way. Win32 porters shouldn't have much
 * difficulty porting my changes (at least I hope so...).
 *
 * Subsampling is a pretty obscure feature, but I thought it might be nice
 * to have it in anyway, for people to play with :-) Does anybody have
 * any better suggestions than the ones I've put in the menu? (See wizard.doc
 * from the libjpeg distribution for a tiny bit of information on subsampling.)
 *
 * A baseline JPEG is often larger and/or of worse quality than a non-baseline
 * one (especially at low quality settings), but all decoders are guaranteed
 * to read baseline JPEGs (I think). Not all will read a non-baseline one.
 *
 * Restart markers are useful if you are transmitting the image over an
 * unreliable network. If a file gets corrupted, it will only be corrupted
 * up to the next restart marker. Of course, if there are no restart markers,
 * the rest of the image will be corrupted. Restart markers take up extra
 * space. The libjpeg developers recommend a restart every 1 row for
 * transmitting images over unreliable networks, such as Usenet.
 *
 * The DCT method is said by the libjpeg docs to _only_ influence quality vs.
 * speed, and nothing else. However, I've found that there _are_ size
 * differences. Fast integer, on the other hand, is faster than integer,
 * which in turn is faster than FP. According to the libjpeg docs (and I
 * believe it's true), FP has only a theoretical advantage in quality, and
 * will be slower than the two other methods, unless you're blessed with
 * very a fast FPU. (In addition, images might not be identical on
 * different types of FP hardware.)
 *
 * ...and thus ends my additions to the JPEG plug-in. I hope. *sigh* :-)
 */

/*
 * 21-AUG-99 - Bunch O' Fixes.
 * - Adam D. Moss <adam@gimp.org>
 *
 * We now correctly create an alpha-padded layer for our preview --
 * having a non-background non-alpha layer is a no-no in GIMP.
 *
 * I've also tweaked the GIMP undo API a little and changed the JPEG
 * plugin to use gimp_image_{freeze,thaw}_undo so that it doesn't store
 * up a whole load of superfluous tile data every time the preview is
 * changed.
 */

/*
 * 22-DEC-99 - volatiles added
 * - Austin Donnelly <austin@gimp.org>
 *
 * When gcc complains a variable may be clobbered by a longjmp or
 * vfork, it means the following: setjmp() was called by the JPEG
 * library for error recovery purposes, but gcc is keeping some
 * variables in registers without updating the memory locations on the
 * stack consistently.  If JPEG error recovery is every invoked, the
 * values of these variable will be inconsistent.  This is almost
 * always a bug, but not one that's commonly seen unless JPEG recovery
 * code is exercised frequently.  The correct solution is to tell gcc
 * to keep the stack version of the affected variables up to date, by
 * using the "volatile" keyword.   Here endeth the lesson.
 */

/*
 * 4-SEP-01 - remove the use of GtkText
 * - DindinX <David@dindinx.org>
 *
 * The new version of GTK+ does not support GtkText anymore.
 * I've just replaced the one used for the image comment by
 * a GtkTextView/GtkTextBuffer couple;
 */

/*
 * 22-JUN-03 - add support for keeping EXIF information
 * - Dave Neary <bolsh@gimp.org>
 *
 * This is little more than a modified version fo a patch from the
 * libexif owner (Lutz Muller) which attaches exif information to
 * a GimpImage, and if there is infoprmation available at save
 * time, writes it out. No modification of the exif data is
 * currently possible.
 */

#include "config.h"   /* configure cares about HAVE_PROGRESSIVE_JPEG */

#include <glib.h>     /* We want glib.h first because of some
		       * pretty obscure Win32 compilation issues.
		       */
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <jpeglib.h>
#include <jerror.h>

#ifdef HAVE_EXIF
#include <libexif/exif-data.h>
#endif /* HAVE_EXIF */

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"

#define SCALE_WIDTH         125

/* if you are not compiling this from inside the gimp tree, you have to  */
/* take care yourself if your JPEG library supports progressive mode     */
/* #undef HAVE_PROGRESSIVE_JPEG   if your library doesn't support it     */
/* #define HAVE_PROGRESSIVE_JPEG  if your library knows how to handle it */

/* See bugs #63610 and #61088 for a discussion about the quality settings */
#define DEFAULT_QUALITY     0.85
#define DEFAULT_SMOOTHING   0.0
#define DEFAULT_OPTIMIZE    1
#define DEFAULT_PROGRESSIVE 0
#define DEFAULT_BASELINE    1
#define DEFAULT_SUBSMP      0
#define DEFAULT_RESTART     0
#define DEFAULT_DCT         0
#define DEFAULT_PREVIEW     0
#define DEFAULT_EXIF        1

/* sg - these should not be global... */
static gint32 volatile  image_ID_global       = -1;
static gint32           orig_image_ID_global  = -1;
static gint32           drawable_ID_global    = -1;
static gint32           layer_ID_global       = -1;
static GtkWidget       *preview_size          = NULL;
static GimpDrawable    *drawable_global       = NULL;
static gboolean         undo_touched          = FALSE;

typedef struct
{
  gdouble quality;
  gdouble smoothing;
  gint    optimize;
  gint    progressive;
  gint    baseline;
  gint    subsmp;
  gint    restart;
  gint    dct;
  gint    preview;
  gint    save_exif;
} JpegSaveVals;

typedef struct
{
  struct jpeg_compress_struct cinfo;
  gint          tile_height;
  FILE         *outfile;
  gboolean      has_alpha;
  gint          rowstride;
  guchar       *temp;
  guchar       *data;
  guchar       *src;
  GimpDrawable *drawable;
  GimpPixelRgn  pixel_rgn;
  const gchar  *file_name;
  gboolean      abort_me;
} PreviewPersistent;

static gboolean *abort_me = NULL;


/* Declare local functions.
 */
static void     query                     (void);
static void     run                       (const gchar      *name,
					   gint              nparams,
					   const GimpParam  *param,
					   gint             *nreturn_vals,
					   GimpParam       **return_vals);
static gint32   load_image                (const gchar      *filename,
					   GimpRunMode       runmode,
					   gboolean          preview);
static gboolean save_image                (const gchar      *filename,
					   gint32            image_ID,
					   gint32            drawable_ID,
					   gint32            orig_image_ID,
					   gboolean          preview);

static gboolean save_dialog                (void);

static void     save_restart_toggle_update (GtkWidget       *toggle,
					    GtkAdjustment   *adjustment);
static void     save_restart_update        (GtkAdjustment   *adjustment,
					    GtkWidget       *toggle);

static void     make_preview               (void);
static void     destroy_preview            (void);

static void     menu_callback              (GtkWidget       *widget,
					    gpointer         data);


GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};

static JpegSaveVals jsvals =
{
  DEFAULT_QUALITY,
  DEFAULT_SMOOTHING,
  DEFAULT_OPTIMIZE,
  DEFAULT_PROGRESSIVE,
  DEFAULT_BASELINE,
  DEFAULT_SUBSMP,
  DEFAULT_RESTART,
  DEFAULT_DCT,
  DEFAULT_PREVIEW,
  DEFAULT_EXIF
};


static gchar     *image_comment         = NULL;
static GtkWidget *restart_markers_scale = NULL;
static GtkWidget *restart_markers_label = NULL;

#ifdef HAVE_EXIF
static ExifData  *exif_data             = NULL;
#endif /* HAVE_EXIF */

MAIN ()

static void
query (void)
{
  static GimpParamDef load_args[] =
  {
    { GIMP_PDB_INT32,    "run_mode",     "Interactive, non-interactive" },
    { GIMP_PDB_STRING,   "filename",     "The name of the file to load" },
    { GIMP_PDB_STRING,   "raw_filename", "The name of the file to load" }
  };
  static GimpParamDef load_return_vals[] =
  {
    { GIMP_PDB_IMAGE,   "image",         "Output image" }
  };

  static GimpParamDef save_args[] =
  {
    { GIMP_PDB_INT32,    "run_mode",     "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE,    "image",        "Input image" },
    { GIMP_PDB_DRAWABLE, "drawable",     "Drawable to save" },
    { GIMP_PDB_STRING,   "filename",     "The name of the file to save the image in" },
    { GIMP_PDB_STRING,   "raw_filename", "The name of the file to save the image in" },
    { GIMP_PDB_FLOAT,    "quality",      "Quality of saved image (0 <= quality <= 1)" },
    { GIMP_PDB_FLOAT,    "smoothing",    "Smoothing factor for saved image (0 <= smoothing <= 1)" },
    { GIMP_PDB_INT32,    "optimize",     "Optimization of entropy encoding parameters (0/1)" },
    { GIMP_PDB_INT32,    "progressive",  "Enable progressive jpeg image loading - ignored if not compiled with HAVE_PROGRESSIVE_JPEG (0/1)" },
    { GIMP_PDB_STRING,   "comment",      "Image comment" },
    { GIMP_PDB_INT32,    "subsmp",       "The subsampling option number" },
    { GIMP_PDB_INT32,    "baseline",     "Force creation of a baseline JPEG (non-baseline JPEGs can't be read by all decoders) (0/1)" },
    { GIMP_PDB_INT32,    "restart",      "Frequency of restart markers (in rows, 0 = no restart markers)" },
    { GIMP_PDB_INT32,    "dct",          "DCT algorithm to use (speed/quality tradeoff)" }
  };

  gimp_install_procedure ("file_jpeg_load",
                          "loads files in the JPEG file format",
                          "loads files in the JPEG file format",
                          "Spencer Kimball, Peter Mattis & others",
                          "Spencer Kimball & Peter Mattis",
                          "1995-1999",
			  "<Load>/Jpeg",
			  NULL,
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (load_args),
                          G_N_ELEMENTS (load_return_vals),
                          load_args, load_return_vals);

  gimp_install_procedure ("file_jpeg_save",
                          "saves files in the JPEG file format",
                          "saves files in the lossy, widely supported JPEG format",
                          "Spencer Kimball, Peter Mattis & others",
                          "Spencer Kimball & Peter Mattis",
                          "1995-1999",
                          "<Save>/JPEG",
			  "RGB*, GRAY*",
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (save_args), 0,
                          save_args, NULL);

  gimp_register_magic_load_handler ("file_jpeg_load",
				    "jpg,jpeg",
				    "",
				    "6,string,JFIF,6,string,Exif");
  gimp_register_save_handler       ("file_jpeg_save",
				    "jpg,jpeg",
				    "");
}

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  static GimpParam   values[2];
  GimpRunMode        run_mode;
  GimpPDBStatusType  status = GIMP_PDB_SUCCESS;
  gint32             image_ID;
  gint32             drawable_ID;
  gint32             orig_image_ID;
  gint32             display_ID = -1;
  GimpParasite      *parasite;
  gboolean           err;
  GimpExportReturn   export = GIMP_EXPORT_CANCEL;

  run_mode = param[0].data.d_int32;

  INIT_I18N ();

  *nreturn_vals = 1;
  *return_vals  = values;
  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;

  if (strcmp (name, "file_jpeg_load") == 0)
    {
      image_ID = load_image (param[1].data.d_string, run_mode, FALSE);

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
  else if (strcmp (name, "file_jpeg_save") == 0)
    {
      image_ID = orig_image_ID = param[1].data.d_int32;
      drawable_ID = param[2].data.d_int32;

       /*  eventually export the image */
      switch (run_mode)
	{
	case GIMP_RUN_INTERACTIVE:
	case GIMP_RUN_WITH_LAST_VALS:
	  gimp_ui_init ("jpeg", FALSE);
	  export = gimp_export_image (&image_ID, &drawable_ID, "JPEG",
				      (GIMP_EXPORT_CAN_HANDLE_RGB |
				       GIMP_EXPORT_CAN_HANDLE_GRAY));
	  switch (export)
	    {
	    case GIMP_EXPORT_EXPORT:
	      display_ID = gimp_display_new (image_ID);
	      gimp_image_set_filename (image_ID, _("Export Preview"));
	      gimp_displays_flush ();
	      break;
	    case GIMP_EXPORT_IGNORE:
	      break;
	    case GIMP_EXPORT_CANCEL:
	      values[0].data.d_status = GIMP_PDB_CANCEL;
	      return;
	      break;
	    }
	  break;
	default:
	  break;
	}

#ifdef HAVE_EXIF
      exif_data_unref (exif_data);
      exif_data = NULL;
#endif /* HAVE_EXIF */

      g_free (image_comment);
      image_comment = NULL;

      parasite = gimp_image_parasite_find (orig_image_ID, "gimp-comment");
      if (parasite)
	{
	  image_comment = g_strdup (parasite->data);
	  gimp_parasite_free (parasite);
	}

#ifdef HAVE_EXIF
      parasite = gimp_image_parasite_find (orig_image_ID, "jpeg-exif-data");
      if (parasite)
        {
          exif_data = exif_data_new_from_data (parasite->data, parasite->size);
          gimp_parasite_free (parasite);
        }
#endif /* HAVE_EXIF */

      jsvals.quality     = DEFAULT_QUALITY;
      jsvals.smoothing   = DEFAULT_SMOOTHING;
      jsvals.optimize    = DEFAULT_OPTIMIZE;
      jsvals.progressive = DEFAULT_PROGRESSIVE;
      jsvals.baseline    = DEFAULT_BASELINE;
      jsvals.subsmp      = DEFAULT_SUBSMP;
      jsvals.restart     = DEFAULT_RESTART;
      jsvals.dct         = DEFAULT_DCT;
      jsvals.preview     = DEFAULT_PREVIEW;
      jsvals.save_exif   = DEFAULT_EXIF;

      switch (run_mode)
	{
	case GIMP_RUN_INTERACTIVE:
	  /*  Possibly retrieve data  */
	  gimp_get_data ("file_jpeg_save", &jsvals);

	  /* load up the previously used values */
	  parasite = gimp_image_parasite_find (orig_image_ID,
					       "jpeg-save-options");
	  if (parasite)
	    {
	      jsvals.quality     = ((JpegSaveVals *)parasite->data)->quality;
	      jsvals.smoothing   = ((JpegSaveVals *)parasite->data)->smoothing;
	      jsvals.optimize    = ((JpegSaveVals *)parasite->data)->optimize;
	      jsvals.progressive = ((JpegSaveVals *)parasite->data)->progressive;
	      jsvals.baseline    = ((JpegSaveVals *)parasite->data)->baseline;
	      jsvals.subsmp      = ((JpegSaveVals *)parasite->data)->subsmp;
	      jsvals.restart     = ((JpegSaveVals *)parasite->data)->restart;
	      jsvals.dct         = ((JpegSaveVals *)parasite->data)->dct;
	      jsvals.preview     = ((JpegSaveVals *)parasite->data)->preview;
	      jsvals.save_exif   = ((JpegSaveVals *)parasite->data)->save_exif;
	      gimp_parasite_free (parasite);
	    }

          if (jsvals.preview)
            {
              /* we start an undo_group and immediately freeze undo saving
                 so that we can avoid sucking up tile cache with our unneeded
                 preview steps. */
              gimp_undo_push_group_start (image_ID);
              gimp_image_undo_freeze (image_ID);

              undo_touched = TRUE;
            }

	  /* prepare for the preview */
	  image_ID_global = image_ID;
	  orig_image_ID_global = orig_image_ID;
	  drawable_ID_global = drawable_ID;

	  /*  First acquire information with a dialog  */
	  err = save_dialog ();

          if (undo_touched)
            {
              /* thaw undo saving and end the undo_group. */
              gimp_image_undo_thaw (image_ID);
              gimp_undo_push_group_end (image_ID);
            }

          if (!err)
	    status = GIMP_PDB_CANCEL;
	  break;

	case GIMP_RUN_NONINTERACTIVE:
	  /*  Make sure all the arguments are there!  */
	  /*  pw - added two more progressive and comment */
	  /*  sg - added subsampling, preview, baseline, restarts and DCT */
	  if (nparams != 14)
	    {
	      status = GIMP_PDB_CALLING_ERROR;
	    }
	  else
	    {
	      /* Once the PDB gets default parameters, remove this hack */
	      if (param[5].data.d_float > 0.05)
		{
		  jsvals.quality     = param[5].data.d_float;
		  jsvals.smoothing   = param[6].data.d_float;
		  jsvals.optimize    = param[7].data.d_int32;
#ifdef HAVE_PROGRESSIVE_JPEG
		  jsvals.progressive = param[8].data.d_int32;
#endif /* HAVE_PROGRESSIVE_JPEG */
		  jsvals.baseline    = param[11].data.d_int32;
		  jsvals.subsmp      = param[10].data.d_int32;
		  jsvals.restart     = param[12].data.d_int32;
		  jsvals.dct         = param[13].data.d_int32;

		  /* free up the default -- wasted some effort earlier */
		  g_free (image_comment);
		  image_comment = g_strdup (param[9].data.d_string);
		}

	      jsvals.preview = FALSE;

	      if (jsvals.quality < 0.0 || jsvals.quality > 1.0)
		status = GIMP_PDB_CALLING_ERROR;
	      else if (jsvals.smoothing < 0.0 || jsvals.smoothing > 1.0)
		status = GIMP_PDB_CALLING_ERROR;
	      else if (jsvals.subsmp < 0 || jsvals.subsmp > 2)
		status = GIMP_PDB_CALLING_ERROR;
	      else if (jsvals.dct < 0 || jsvals.dct > 2)
		status = GIMP_PDB_CALLING_ERROR;
	    }
	  break;

	case GIMP_RUN_WITH_LAST_VALS:
	  /*  Possibly retrieve data  */
	  gimp_get_data ("file_jpeg_save", &jsvals);

	  parasite = gimp_image_parasite_find (orig_image_ID,
					       "jpeg-save-options");
	  if (parasite)
	    {
	      jsvals.quality     = ((JpegSaveVals *)parasite->data)->quality;
	      jsvals.smoothing   = ((JpegSaveVals *)parasite->data)->smoothing;
	      jsvals.optimize    = ((JpegSaveVals *)parasite->data)->optimize;
	      jsvals.progressive = ((JpegSaveVals *)parasite->data)->progressive;
	      jsvals.baseline    = ((JpegSaveVals *)parasite->data)->baseline;
	      jsvals.subsmp      = ((JpegSaveVals *)parasite->data)->subsmp;
	      jsvals.restart     = ((JpegSaveVals *)parasite->data)->restart;
	      jsvals.dct         = ((JpegSaveVals *)parasite->data)->dct;
	      jsvals.preview     = FALSE;
	      gimp_parasite_free (parasite);
	    }
	  break;

	default:
	  break;
	}

      if (status == GIMP_PDB_SUCCESS)
	{
	  if (save_image (param[3].data.d_string,
			  image_ID,
			  drawable_ID,
			  orig_image_ID,
			  FALSE))
	    {
	      /*  Store mvals data  */
	      gimp_set_data ("file_jpeg_save", &jsvals, sizeof (JpegSaveVals));
	    }
	  else
	    {
	      status = GIMP_PDB_EXECUTION_ERROR;
	    }
	}

      if (export == GIMP_EXPORT_EXPORT)
	{
	  /* If the image was exported, delete the new display. */
	  /* This also deletes the image.                       */

	  if (display_ID > -1)
	    gimp_display_delete (display_ID);
	  else
	    gimp_image_delete (image_ID);
	}

      /* pw - now we need to change the defaults to be whatever
       * was used to save this image.  Dump the old parasites
       * and add new ones. */

      gimp_image_parasite_detach (orig_image_ID, "gimp-comment");
      if (image_comment && strlen (image_comment))
	{
	  parasite = gimp_parasite_new ("gimp-comment",
					GIMP_PARASITE_PERSISTENT,
					strlen (image_comment) + 1,
					image_comment);
	  gimp_image_parasite_attach (orig_image_ID, parasite);
	  gimp_parasite_free (parasite);
	}
      gimp_image_parasite_detach (orig_image_ID, "jpeg-save-options");

      parasite = gimp_parasite_new ("jpeg-save-options",
				    0, sizeof (jsvals), &jsvals);
      gimp_image_parasite_attach (orig_image_ID, parasite);
      gimp_parasite_free (parasite);
    }
  else
    {
      status = GIMP_PDB_CALLING_ERROR;
    }

  values[0].data.d_status = status;
}

/* Read next byte */
static guint
jpeg_getc (j_decompress_ptr cinfo)
{
  struct jpeg_source_mgr *datasrc = cinfo->src;

  if (datasrc->bytes_in_buffer == 0)
    {
      if (! (*datasrc->fill_input_buffer) (cinfo))
	ERREXIT (cinfo, JERR_CANT_SUSPEND);
    }
  datasrc->bytes_in_buffer--;

  return *datasrc->next_input_byte++;
}

/* We need our own marker parser, since early versions of libjpeg
 * don't keep a conventient list of the marker's that have been
 * seen. */

/*
 * Marker processor for COM markers.
 * This replaces the library's built-in processor, which just skips the marker.
 * Note this code relies on a non-suspending data source.
 */
static GString *local_image_comments = NULL;

static boolean
COM_handler (j_decompress_ptr cinfo)
{
  gint  length;
  guint ch;

  length = jpeg_getc (cinfo) << 8;
  length += jpeg_getc (cinfo);
  if (length < 2)
    return FALSE;
  length -= 2;			/* discount the length word itself */

  if (!local_image_comments)
    local_image_comments = g_string_new (NULL);
  else
    g_string_append_c (local_image_comments, '\n');

  while (length-- > 0)
    {
      ch = jpeg_getc (cinfo);
      g_string_append_c (local_image_comments, ch);
    }

  return TRUE;
}

typedef struct my_error_mgr
{
  struct jpeg_error_mgr pub;            /* "public" fields */

  jmp_buf               setjmp_buffer;  /* for return to caller */
} *my_error_ptr;

/*
 * Here's the routine that will replace the standard error_exit method:
 */

static void
my_error_exit (j_common_ptr cinfo)
{
  /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
  my_error_ptr myerr = (my_error_ptr) cinfo->err;

  /* Always display the message. */
  /* We could postpone this until after returning, if we chose. */
  (*cinfo->err->output_message) (cinfo);

  /* Return control to the setjmp point */
  longjmp (myerr->setjmp_buffer, 1);
}

static gint32
load_image (const gchar *filename,
	    GimpRunMode  runmode,
	    gboolean     preview)
{
  GimpPixelRgn     pixel_rgn;
  GimpDrawable    *drawable;
  gint32 volatile  image_ID;
  gint32           layer_ID;
  struct jpeg_decompress_struct cinfo;
  struct my_error_mgr           jerr;
  FILE    *infile;
  guchar  *buf;
  guchar  * volatile padded_buf = NULL;
  guchar **rowbuf;
  gint     image_type;
  gint     layer_type;
  gint     tile_height;
  gint     scanlines;
  gint     i, start, end;

  GimpParasite * volatile comment_parasite = NULL;

#ifdef HAVE_EXIF
  GimpParasite *exif_parasite = NULL;
  ExifData     *exif_data     = NULL;
#endif

  /* We set up the normal JPEG error routines. */
  cinfo.err = jpeg_std_error (&jerr.pub);
  jerr.pub.error_exit = my_error_exit;

  if ((infile = fopen (filename, "rb")) == NULL)
    {
      g_message (_("Could not open '%s' for reading: %s"),
                 filename, g_strerror (errno));
      gimp_quit ();
    }

  if (!preview)
    {
      gchar *name = g_strdup_printf (_("Opening '%s'..."), filename);

      gimp_progress_init (name);
      g_free (name);
    }

  image_ID = -1;
  /* Establish the setjmp return context for my_error_exit to use. */
  if (setjmp (jerr.setjmp_buffer))
    {
      /* If we get here, the JPEG code has signaled an error.
       * We need to clean up the JPEG object, close the input file, and return.
       */
      jpeg_destroy_decompress (&cinfo);
      if (infile)
	fclose (infile);
      if (image_ID != -1 && !preview)
	gimp_image_delete (image_ID);
      if (preview)
	destroy_preview();
      gimp_quit ();
    }
  /* Now we can initialize the JPEG decompression object. */
  jpeg_create_decompress (&cinfo);

  /* Step 2: specify data source (eg, a file) */

  jpeg_stdio_src (&cinfo, infile);

  /* pw - step 2.1 let the lib know we want the comments. */

  jpeg_set_marker_processor (&cinfo, JPEG_COM, COM_handler);

  /* Step 3: read file parameters with jpeg_read_header() */

  (void) jpeg_read_header (&cinfo, TRUE);
  /* We can ignore the return value from jpeg_read_header since
   *   (a) suspension is not possible with the stdio data source, and
   *   (b) we passed TRUE to reject a tables-only JPEG file as an error.
   * See libjpeg.doc for more info.
   */

  if (!preview)
    {
      /* if we had any comments then make a parasite for them */
      if (local_image_comments && local_image_comments->len)
	{
	  gchar *comment = local_image_comments->str;

	  g_string_free (local_image_comments, FALSE);

	  local_image_comments = NULL;

          if (g_utf8_validate (comment, -1, NULL))
            comment_parasite = gimp_parasite_new ("gimp-comment",
                                                  GIMP_PARASITE_PERSISTENT,
                                                  strlen (comment) + 1,
                                                  comment);
          g_free (comment);
	}

      /* Do not attach the "jpeg-save-options" parasite to the image
       * because this conflics with the global defaults.  See bug #75398:
       * http://bugzilla.gnome.org/show_bug.cgi?id=75398 */
    }

  /* Step 4: set parameters for decompression */

  /* In this example, we don't need to change any of the defaults set by
   * jpeg_read_header(), so we do nothing here.
   */

  /* Step 5: Start decompressor */

  jpeg_start_decompress (&cinfo);

  /* We may need to do some setup of our own at this point before reading
   * the data.  After jpeg_start_decompress() we have the correct scaled
   * output image dimensions available, as well as the output colormap
   * if we asked for color quantization.
   * In this example, we need to make an output work buffer of the right size.
   */
  /* temporary buffer */
  tile_height = gimp_tile_height ();
  buf = g_new (guchar,
	       tile_height * cinfo.output_width * cinfo.output_components);

  rowbuf = g_new (guchar*, tile_height);

  for (i = 0; i < tile_height; i++)
    rowbuf[i] = buf + cinfo.output_width * cinfo.output_components * i;

  /* Create a new image of the proper size and associate the filename with it.

     Preview layers, not being on the bottom of a layer stack, MUST HAVE
     AN ALPHA CHANNEL!
   */
  switch (cinfo.output_components)
    {
    case 1:
      image_type = GIMP_GRAY;
      layer_type = preview ? GIMP_GRAYA_IMAGE : GIMP_GRAY_IMAGE;
      break;

    case 3:
      image_type = GIMP_RGB;
      layer_type = preview ? GIMP_RGBA_IMAGE : GIMP_RGB_IMAGE;
      break;

    case 4:
      if (cinfo.out_color_space == JCS_CMYK)
	{
	  image_type = GIMP_RGB;
	  layer_type = GIMP_RGB_IMAGE;
	  break;
	}
      /*fallthrough*/

    default:
      g_message ("Don't know how to load JPEGs\n"
                 "with %d color channels\n"
                 "using colorspace %d (%d)",
		 cinfo.output_components, cinfo.out_color_space,
		 cinfo.jpeg_color_space);
      gimp_quit ();
      break;
    }

  if (preview)
    padded_buf = g_new (guchar, tile_height * cinfo.output_width *
			(cinfo.output_components + 1));
  else if (cinfo.out_color_space == JCS_CMYK)
    padded_buf = g_new (guchar, tile_height * cinfo.output_width * 3);
  else
    padded_buf = NULL;

  if (preview)
    {
      image_ID = image_ID_global;
    }
  else
    {
      image_ID = gimp_image_new (cinfo.output_width, cinfo.output_height,
				 image_type);
      gimp_image_set_filename (image_ID, filename);
    }

  if (preview)
    {
      layer_ID_global = layer_ID =
	gimp_layer_new (image_ID, _("JPEG preview"),
			cinfo.output_width,
			cinfo.output_height,
			layer_type, 100, GIMP_NORMAL_MODE);
    }
  else
    {
      layer_ID = gimp_layer_new (image_ID, _("Background"),
				 cinfo.output_width,
				 cinfo.output_height,
				 layer_type, 100, GIMP_NORMAL_MODE);
    }

  drawable_global = drawable = gimp_drawable_get (layer_ID);
  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0,
		       drawable->width, drawable->height, TRUE, FALSE);

  /* Step 5.1: if the file had resolution information, set it on the image */
  if (!preview && cinfo.saw_JFIF_marker)
    {
      gdouble xresolution;
      gdouble yresolution;
      gdouble asymmetry;

      xresolution = cinfo.X_density;
      yresolution = cinfo.Y_density;

      switch (cinfo.density_unit)
	{
	case 0: /* unknown -> set the aspect ratio but use the default
		*  image resolution
		*/
	  if (cinfo.Y_density != 0)
	    asymmetry = xresolution / yresolution;
	  else
	    asymmetry = 1.0;

	  gimp_image_get_resolution (image_ID, &xresolution, &yresolution);
	  xresolution *= asymmetry;
	  break;

	case 1: /* dots per inch */
	  break;

	case 2: /* dots per cm */
	  xresolution *= 2.54;
	  yresolution *= 2.54;
	  gimp_image_set_unit (image_ID, GIMP_UNIT_MM);
	  break;

	default:
	  g_message ("Unknown density unit %d\nassuming dots per inch",
		     cinfo.density_unit);
	  break;
	}

      gimp_image_set_resolution (image_ID, xresolution, yresolution);
    }

  /* Step 6: while (scan lines remain to be read) */
  /*           jpeg_read_scanlines(...); */

  /* Here we use the library's state variable cinfo.output_scanline as the
   * loop counter, so that we don't have to keep track ourselves.
   */
  while (cinfo.output_scanline < cinfo.output_height)
    {
      start = cinfo.output_scanline;
      end   = cinfo.output_scanline + tile_height;
      end   = MIN (end, cinfo.output_height);
      scanlines = end - start;

      for (i = 0; i < scanlines; i++)
	jpeg_read_scanlines (&cinfo, (JSAMPARRAY) &rowbuf[i], 1);

      if (preview) /* Add a dummy alpha channel -- convert buf to padded_buf */
	{
	  guchar *dest = padded_buf;
	  guchar *src  = buf;
	  gint    num  = drawable->width * scanlines;

	  switch (cinfo.output_components)
	    {
	    case 1:
	      for (i = 0; i < num; i++)
		{
		  *(dest++) = *(src++);
		  *(dest++) = 255;
		}
	      break;

	    case 3:
	      for (i = 0; i < num; i++)
		{
		  *(dest++) = *(src++);
		  *(dest++) = *(src++);
		  *(dest++) = *(src++);
		  *(dest++) = 255;
		}
	      break;

	    default:
	      g_warning ("JPEG - shouldn't have gotten here.\nReport to http://bugzilla.gnome.org/");
	      break;
	    }
	}
      else if (cinfo.out_color_space == JCS_CMYK) /* buf-> RGB in padded_buf */
	{
	  guchar *dest = padded_buf;
	  guchar *src  = buf;
	  gint    num  = drawable->width * scanlines;

	  for (i = 0; i < num; i++)
	    {
	      guint r_c, g_m, b_y, a_k;

	      r_c = *(src++);
	      g_m = *(src++);
	      b_y = *(src++);
	      a_k = *(src++);
	      *(dest++) = (r_c * a_k) / 255;
	      *(dest++) = (g_m * a_k) / 255;
	      *(dest++) = (b_y * a_k) / 255;
	    }
	}

      gimp_pixel_rgn_set_rect (&pixel_rgn, padded_buf ? padded_buf : buf,
			       0, start, drawable->width, scanlines);

      if (! preview)
        gimp_progress_update ((gdouble) cinfo.output_scanline /
                              (gdouble) cinfo.output_height);
    }

  /* Step 7: Finish decompression */

  jpeg_finish_decompress (&cinfo);
  /* We can ignore the return value since suspension is not possible
   * with the stdio data source.
   */

  /* Step 8: Release JPEG decompression object */

  /* This is an important step since it will release a good deal of memory. */
  jpeg_destroy_decompress (&cinfo);

  /* free up the temporary buffers */
  g_free (rowbuf);
  g_free (buf);
  g_free (padded_buf);

  /* After finish_decompress, we can close the input file.
   * Here we postpone it until after no more JPEG errors are possible,
   * so as to simplify the setjmp error logic above.  (Actually, I don't
   * think that jpeg_destroy can do an error exit, but why assume anything...)
   */
  fclose (infile);

  /* At this point you may want to check to see whether any corrupt-data
   * warnings occurred (test whether jerr.num_warnings is nonzero).
   */

  /* Tell the GIMP to display the image.
   */
  gimp_image_add_layer (image_ID, layer_ID, 0);
  gimp_drawable_flush (drawable);

  /* pw - Last of all, attach the parasites (couldn't do it earlier -
     there was no image. */

  if (!preview)
    {
      if (comment_parasite)
	{
	  gimp_image_parasite_attach (image_ID, comment_parasite);
	  gimp_parasite_free (comment_parasite);

          comment_parasite = NULL;
	}

#ifdef HAVE_EXIF
      exif_data = exif_data_new_from_file (filename);
      if (exif_data)
        {
          guchar *exif_buf;
          guint   exif_buf_len;

          exif_data_save_data (exif_data, &exif_buf, &exif_buf_len);
          exif_data_unref (exif_data);
          exif_parasite = gimp_parasite_new ("jpeg-exif-data",
                                             GIMP_PARASITE_PERSISTENT,
                                             exif_buf_len, exif_buf);
          gimp_image_parasite_attach (image_ID, exif_parasite);
          gimp_parasite_free (exif_parasite);
          free (exif_buf);
        }
#endif

    }

  return image_ID;
}

/*
 * sg - This is the best I can do, I'm afraid... I think it will fail
 * if something bad really happens (but it might not). If you have a
 * better solution, send it ;-)
 */
static void
background_error_exit (j_common_ptr cinfo)
{
  if (abort_me)
    *abort_me = TRUE;
  (*cinfo->err->output_message) (cinfo);
}

static gboolean
background_jpeg_save (PreviewPersistent *pp)
{
  guchar *t;
  guchar *s;
  gint    i, j;
  gint    yend;

  if (pp->abort_me || (pp->cinfo.next_scanline >= pp->cinfo.image_height))
    {
      /* clean up... */
      if (pp->abort_me)
	{
	  jpeg_abort_compress (&(pp->cinfo));
	}
      else
	{
	  jpeg_finish_compress (&(pp->cinfo));
	}

      fclose (pp->outfile);
      jpeg_destroy_compress (&(pp->cinfo));

      g_free (pp->temp);
      g_free (pp->data);

      if (pp->drawable)
	gimp_drawable_detach (pp->drawable);

      /* display the preview stuff */
      if (!pp->abort_me)
	{
	  struct stat buf;
	  gchar       temp[128];

	  stat (pp->file_name, &buf);
	  g_snprintf (temp, sizeof (temp),
		      _("Size: %ld bytes (%02.01f kB)"),
		      (glong) buf.st_size, (gdouble) (buf.st_size) / 1024.0);
	  gtk_label_set_text (GTK_LABEL (preview_size), temp);

	  /* and load the preview */
	  load_image (pp->file_name, GIMP_RUN_NONINTERACTIVE, TRUE);
	}

      /* we cleanup here (load_image doesn't run in the background) */
      unlink (pp->file_name);

      if (abort_me == &(pp->abort_me))
	abort_me = NULL;

      g_free (pp);

      gimp_displays_flush ();
      gdk_flush ();

      return FALSE;
    }
  else
    {
      if ((pp->cinfo.next_scanline % pp->tile_height) == 0)
	{
	  yend = pp->cinfo.next_scanline + pp->tile_height;
	  yend = MIN (yend, pp->cinfo.image_height);
	  gimp_pixel_rgn_get_rect (&pp->pixel_rgn, pp->data, 0,
				   pp->cinfo.next_scanline,
				   pp->cinfo.image_width,
				   (yend - pp->cinfo.next_scanline));
	  pp->src = pp->data;
	}

      t = pp->temp;
      s = pp->src;
      i = pp->cinfo.image_width;

      while (i--)
	{
	  for (j = 0; j < pp->cinfo.input_components; j++)
	    *t++ = *s++;
	  if (pp->has_alpha)  /* ignore alpha channel */
	    s++;
	}

      pp->src += pp->rowstride;
      jpeg_write_scanlines (&(pp->cinfo), (JSAMPARRAY) &(pp->temp), 1);

      return TRUE;
    }
}

static gboolean
save_image (const gchar *filename,
	    gint32       image_ID,
	    gint32       drawable_ID,
	    gint32       orig_image_ID,
	    gboolean     preview)
{
  GimpPixelRgn   pixel_rgn;
  GimpDrawable  *drawable;
  GimpImageType  drawable_type;
  struct jpeg_compress_struct cinfo;
  struct my_error_mgr         jerr;
  FILE     * volatile outfile;
  guchar   *temp, *t;
  guchar   *data;
  guchar   *src, *s;
  gchar    *name;
  gboolean  has_alpha;
  gint      rowstride, yend;
  gint      i, j;

  drawable = gimp_drawable_get (drawable_ID);
  drawable_type = gimp_drawable_type (drawable_ID);
  gimp_pixel_rgn_init (&pixel_rgn, drawable,
		       0, 0, drawable->width, drawable->height, FALSE, FALSE);

  if (!preview)
    {
      name = g_strdup_printf (_("Saving '%s'..."), filename);
      gimp_progress_init (name);
      g_free (name);
    }

  /* Step 1: allocate and initialize JPEG compression object */

  /* We have to set up the error handler first, in case the initialization
   * step fails.  (Unlikely, but it could happen if you are out of memory.)
   * This routine fills in the contents of struct jerr, and returns jerr's
   * address which we place into the link field in cinfo.
   */
  cinfo.err = jpeg_std_error (&jerr.pub);
  jerr.pub.error_exit = my_error_exit;

  outfile = NULL;
  /* Establish the setjmp return context for my_error_exit to use. */
  if (setjmp (jerr.setjmp_buffer))
    {
      /* If we get here, the JPEG code has signaled an error.
       * We need to clean up the JPEG object, close the input file, and return.
       */
      jpeg_destroy_compress (&cinfo);
      if (outfile)
	fclose (outfile);
      if (drawable)
	gimp_drawable_detach (drawable);

      return FALSE;
    }

  /* Now we can initialize the JPEG compression object. */
  jpeg_create_compress (&cinfo);

  /* Step 2: specify data destination (eg, a file) */
  /* Note: steps 2 and 3 can be done in either order. */

  /* Here we use the library-supplied code to send compressed data to a
   * stdio stream.  You can also write your own code to do something else.
   * VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
   * requires it in order to write binary files.
   */
  if ((outfile = fopen (filename, "wb")) == NULL)
    {
      g_message (_("Could not open '%s' for writing: %s"),
                 filename, g_strerror (errno));
      return FALSE;
    }
  jpeg_stdio_dest (&cinfo, outfile);

  /* Get the input image and a pointer to its data.
   */
  switch (drawable_type)
    {
    case GIMP_RGB_IMAGE:
    case GIMP_GRAY_IMAGE:
      /* # of color components per pixel */
      cinfo.input_components = drawable->bpp;
      has_alpha = FALSE;
      break;

    case GIMP_RGBA_IMAGE:
    case GIMP_GRAYA_IMAGE:
      /*gimp_message ("jpeg: image contains a-channel info which will be lost");*/
      /* # of color components per pixel (minus the GIMP alpha channel) */
      cinfo.input_components = drawable->bpp - 1;
      has_alpha = TRUE;
      break;

    case GIMP_INDEXED_IMAGE:
      /*gimp_message ("jpeg: cannot operate on indexed color images");*/
      return FALSE;
      break;

    default:
      /*gimp_message ("jpeg: cannot operate on unknown image types");*/
      return FALSE;
      break;
    }

  /* Step 3: set parameters for compression */

  /* First we supply a description of the input image.
   * Four fields of the cinfo struct must be filled in:
   */
  /* image width and height, in pixels */
  cinfo.image_width  = drawable->width;
  cinfo.image_height = drawable->height;
  /* colorspace of input image */
  cinfo.in_color_space = (drawable_type == GIMP_RGB_IMAGE ||
			  drawable_type == GIMP_RGBA_IMAGE)
    ? JCS_RGB : JCS_GRAYSCALE;
  /* Now use the library's routine to set default compression parameters.
   * (You must set at least cinfo.in_color_space before calling this,
   * since the defaults depend on the source color space.)
   */
  jpeg_set_defaults (&cinfo);

  jpeg_set_quality (&cinfo, (gint) (jsvals.quality * 100), jsvals.baseline);
  cinfo.smoothing_factor = (gint) (jsvals.smoothing * 100);
  cinfo.optimize_coding = jsvals.optimize;

#ifdef HAVE_PROGRESSIVE_JPEG
  if (jsvals.progressive)
    {
      jpeg_simple_progression (&cinfo);
    }
#endif /* HAVE_PROGRESSIVE_JPEG */

  switch (jsvals.subsmp)
    {
    case 0:
    default:
      cinfo.comp_info[0].h_samp_factor = 2;
      cinfo.comp_info[0].v_samp_factor = 2;
      cinfo.comp_info[1].h_samp_factor = 1;
      cinfo.comp_info[1].v_samp_factor = 1;
      cinfo.comp_info[2].h_samp_factor = 1;
      cinfo.comp_info[2].v_samp_factor = 1;
      break;

    case 1:
      cinfo.comp_info[0].h_samp_factor = 2;
      cinfo.comp_info[0].v_samp_factor = 1;
      cinfo.comp_info[1].h_samp_factor = 1;
      cinfo.comp_info[1].v_samp_factor = 1;
      cinfo.comp_info[2].h_samp_factor = 1;
      cinfo.comp_info[2].v_samp_factor = 1;
      break;

    case 2:
      cinfo.comp_info[0].h_samp_factor = 1;
      cinfo.comp_info[0].v_samp_factor = 1;
      cinfo.comp_info[1].h_samp_factor = 1;
      cinfo.comp_info[1].v_samp_factor = 1;
      cinfo.comp_info[2].h_samp_factor = 1;
      cinfo.comp_info[2].v_samp_factor = 1;
      break;
    }

  cinfo.restart_interval = 0;
  cinfo.restart_in_rows = jsvals.restart;

  switch (jsvals.dct)
    {
    case 0:
    default:
      cinfo.dct_method = JDCT_ISLOW;
      break;

    case 1:
      cinfo.dct_method = JDCT_IFAST;
      break;

    case 2:
      cinfo.dct_method = JDCT_FLOAT;
      break;
    }

  {
    gdouble xresolution;
    gdouble yresolution;

    gimp_image_get_resolution (orig_image_ID, &xresolution, &yresolution);

    if (xresolution > 1e-5 && yresolution > 1e-5)
      {
	gdouble factor;

	factor = gimp_unit_get_factor (gimp_image_get_unit (orig_image_ID));

	if (factor == 2.54 /* cm */ ||
	    factor == 25.4 /* mm */)
	  {
	    cinfo.density_unit = 2;  /* dots per cm */

	    xresolution /= 2.54;
	    yresolution /= 2.54;
	  }
	else
	  {
	    cinfo.density_unit = 1;  /* dots per inch */
	  }

	cinfo.X_density = xresolution;
	cinfo.Y_density = yresolution;
      }
  }

  /* Step 4: Start compressor */

  /* TRUE ensures that we will write a complete interchange-JPEG file.
   * Pass TRUE unless you are very sure of what you're doing.
   */
  jpeg_start_compress (&cinfo, TRUE);

#ifdef HAVE_EXIF
  if (jsvals.save_exif)
  {
    if (exif_data)
      {
        guchar *exif_buf;
        guint   exif_buf_len;

        exif_data_save_data (exif_data, &exif_buf, &exif_buf_len);
        jpeg_write_marker (&cinfo, 0xe1,
                           exif_buf, exif_buf_len);
        free (exif_buf);
      }
  }
#endif

  /* Step 4.1: Write the comment out - pw */
  if (image_comment && *image_comment)
    {
      jpeg_write_marker (&cinfo, JPEG_COM,
			 (guchar *) image_comment, strlen (image_comment));
    }

  /* Step 5: while (scan lines remain to be written) */
  /*           jpeg_write_scanlines(...); */

  /* Here we use the library's state variable cinfo.next_scanline as the
   * loop counter, so that we don't have to keep track ourselves.
   * To keep things simple, we pass one scanline per call; you can pass
   * more if you wish, though.
   */
  /* JSAMPLEs per row in image_buffer */
  rowstride = drawable->bpp * drawable->width;
  temp = g_new (guchar, cinfo.image_width * cinfo.input_components);
  data = g_new (guchar, rowstride * gimp_tile_height ());

  /* fault if cinfo.next_scanline isn't initially a multiple of
   * gimp_tile_height */
  src = NULL;

  /*
   * sg - if we preview, we want this to happen in the background -- do
   * not duplicate code in the future; for now, it's OK
   */

  if (preview)
    {
      PreviewPersistent *pp = g_new (PreviewPersistent, 1);

      /* pass all the information we need */
      pp->cinfo       = cinfo;
      pp->tile_height = gimp_tile_height();
      pp->data        = data;
      pp->outfile     = outfile;
      pp->has_alpha   = has_alpha;
      pp->rowstride   = rowstride;
      pp->temp        = temp;
      pp->data        = data;
      pp->drawable    = drawable;
      pp->pixel_rgn   = pixel_rgn;
      pp->src         = NULL;
      pp->file_name   = filename;

      pp->abort_me    = FALSE;
      abort_me = &(pp->abort_me);

      jerr.pub.error_exit = background_error_exit;

      g_idle_add ((GSourceFunc) background_jpeg_save, pp);

      /* background_jpeg_save() will cleanup as needed */
      return TRUE;
    }

  while (cinfo.next_scanline < cinfo.image_height)
    {
      if ((cinfo.next_scanline % gimp_tile_height ()) == 0)
	{
	  yend = cinfo.next_scanline + gimp_tile_height ();
	  yend = MIN (yend, cinfo.image_height);
	  gimp_pixel_rgn_get_rect (&pixel_rgn, data,
				   0, cinfo.next_scanline,
				   cinfo.image_width,
				   (yend - cinfo.next_scanline));
	  src = data;
	}

      t = temp;
      s = src;
      i = cinfo.image_width;

      while (i--)
	{
	  for (j = 0; j < cinfo.input_components; j++)
	    *t++ = *s++;
	  if (has_alpha)  /* ignore alpha channel */
	    s++;
	}

      src += rowstride;
      jpeg_write_scanlines (&cinfo, (JSAMPARRAY) &temp, 1);

      if ((cinfo.next_scanline % 5) == 0)
	gimp_progress_update ((gdouble) cinfo.next_scanline /
			      (gdouble) cinfo.image_height);
    }

  /* Step 6: Finish compression */
  jpeg_finish_compress (&cinfo);
  /* After finish_compress, we can close the output file. */
  fclose (outfile);

  /* Step 7: release JPEG compression object */

  /* This is an important step since it will release a good deal of memory. */
  jpeg_destroy_compress (&cinfo);
  /* free the temporary buffer */
  g_free (temp);
  g_free (data);

  /* And we're done! */
  /*gimp_do_progress (1, 1);*/

  gimp_drawable_detach (drawable);

  return TRUE;
}

static void
make_preview (void)
{
  gchar *tn;

  destroy_preview ();

  if (jsvals.preview)
    {
      if (! undo_touched)
        {
          /* we start an undo_group and immediately freeze undo saving
             so that we can avoid sucking up tile cache with our unneeded
             preview steps. */
          gimp_undo_push_group_start (image_ID_global);
          gimp_image_undo_freeze (image_ID_global);

          undo_touched = TRUE;
        }

      tn = gimp_temp_name ("jpeg");
      save_image (tn,
		  image_ID_global,
		  drawable_ID_global,
		  orig_image_ID_global,
		  TRUE);
    }
  else
    {
      gtk_label_set_text (GTK_LABEL (preview_size), _("Size: unknown"));
      gtk_widget_queue_draw (preview_size);

      gimp_displays_flush ();
    }
}

static void
destroy_preview (void)
{
  if (abort_me)
    {
      *abort_me = TRUE;   /* signal the background save to stop */
    }

  if (drawable_global)
    {
      gimp_drawable_detach (drawable_global);
      drawable_global = NULL;
    }

  if (layer_ID_global != -1 && image_ID_global != -1)
    {
      /*  assuming that reference counting is working correctly,
	  we do not need to delete the layer, removing it from
	  the image should be sufficient  */
      gimp_image_remove_layer (image_ID_global, layer_ID_global);

      layer_ID_global = -1;
    }
}

static gboolean
save_dialog (void)
{
  GtkWidget     *dlg;
  GtkWidget     *vbox;
  GtkWidget     *main_vbox;
  GtkWidget     *label;
  GtkWidget     *scale;
  GtkWidget     *table;
  GtkWidget     *toggle;
  GtkWidget     *abox;
  GtkObject     *scale_data;
#ifdef HAVE_EXIF
  GtkWidget     *exif_toggle;
#endif

  GtkWidget     *progressive;
  GtkWidget     *baseline;
  GtkWidget     *restart;

  GtkWidget     *preview;
  /* GtkWidget *preview_size; -- global */

  GtkWidget     *menu;

  GtkWidget     *text_view;
  GtkTextBuffer *text_buffer;
  GtkWidget     *com_frame;
  GtkWidget     *scrolled_window;

  GtkWidget     *prv_frame;
  GimpImageType  dtype;
  gboolean       run;

  dlg = gimp_dialog_new (_("Save as JPEG"), "jpeg",
                         NULL, 0,
			 gimp_standard_help_func, "filters/jpeg.html",

			 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			 GTK_STOCK_OK,     GTK_RESPONSE_OK,

			 NULL);

  main_vbox = gtk_vbox_new (FALSE, 4);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), main_vbox,
		      TRUE, TRUE, 0);
  gtk_widget_show (main_vbox);

  /* sg - preview */
  prv_frame = gtk_frame_new (_("Image Preview"));
  gtk_frame_set_shadow_type (GTK_FRAME (prv_frame), GTK_SHADOW_ETCHED_IN);
  gtk_box_pack_start (GTK_BOX (main_vbox), prv_frame, FALSE, FALSE, 0);

  vbox = gtk_vbox_new (FALSE, 2);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 4);
  gtk_container_add (GTK_CONTAINER (prv_frame), vbox);
  gtk_widget_show (vbox);

  preview = gtk_check_button_new_with_label (_("Preview (in image window, will modify image's undo history!)"));
  gtk_box_pack_start (GTK_BOX (vbox), preview, FALSE, FALSE, 0);
  gtk_widget_show (preview);

  g_signal_connect (preview, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &jsvals.preview);
  g_signal_connect (preview, "toggled",
                    G_CALLBACK (make_preview),
                    NULL);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (preview), jsvals.preview);

  preview_size = gtk_label_new (_("Size: unknown"));
  gtk_misc_set_alignment (GTK_MISC (preview_size), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (vbox), preview_size, FALSE, FALSE, 0);
  gtk_widget_show (preview_size);

  gtk_widget_show (prv_frame);

  make_preview ();

  table = gimp_parameter_settings_new (main_vbox, 9, 3);

  label = gtk_label_new (_("Quality:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 1.0);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL | GTK_SHRINK, GTK_FILL, 0, 0);
  gtk_widget_show (label);

  scale_data = gtk_adjustment_new (jsvals.quality, 0.0, 1.0, 0.01, 0.01, 0.0);
  scale = gtk_hscale_new (GTK_ADJUSTMENT (scale_data));
  gtk_widget_set_size_request (scale, SCALE_WIDTH, -1);
  gtk_table_attach (GTK_TABLE (table), scale, 1, 3, 0, 1,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
  gtk_scale_set_value_pos (GTK_SCALE (scale), GTK_POS_TOP);
  gtk_scale_set_digits (GTK_SCALE (scale), 2);
  gtk_range_set_update_policy (GTK_RANGE (scale), GTK_UPDATE_DELAYED);
  gtk_widget_show (scale);

  g_signal_connect (scale_data, "value_changed",
                    G_CALLBACK (gimp_double_adjustment_update),
                    &jsvals.quality);
  g_signal_connect (scale_data, "value_changed",
                    G_CALLBACK (make_preview),
                    NULL);

  label = gtk_label_new (_("Smoothing:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 1.0);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL | GTK_SHRINK, GTK_FILL, 0, 0);
  gtk_widget_show (label);

  scale_data = gtk_adjustment_new (jsvals.smoothing, 0.0, 1.0, 0.01, 0.01, 0.0);
  scale = gtk_hscale_new (GTK_ADJUSTMENT (scale_data));
  gtk_widget_set_size_request (scale, SCALE_WIDTH, -1);
  gtk_table_attach (GTK_TABLE (table), scale, 1, 3, 1, 2,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
  gtk_scale_set_value_pos (GTK_SCALE (scale), GTK_POS_TOP);
  gtk_scale_set_digits (GTK_SCALE (scale), 2);
  gtk_range_set_update_policy (GTK_RANGE (scale), GTK_UPDATE_DELAYED);
  gtk_widget_show (scale);

  g_signal_connect (scale_data, "value_changed",
                    G_CALLBACK (gimp_double_adjustment_update),
                    &jsvals.smoothing);
  g_signal_connect (scale_data, "value_changed",
                    G_CALLBACK (make_preview),
                    NULL);

  /* sg - have to init scale here */
  scale_data = gtk_adjustment_new ((jsvals.restart == 0) ? 1 : jsvals.restart,
				   1, 64, 1, 1, 0.0);
  restart_markers_scale = gtk_hscale_new (GTK_ADJUSTMENT (scale_data));

  restart = gtk_check_button_new_with_label (_("Restart markers"));
  gtk_table_attach (GTK_TABLE (table), restart, 0, 1, 2, 3,
		    GTK_FILL, 0, 0, 0);
  gtk_widget_show (restart);

  g_signal_connect (restart, "toggled",
                    G_CALLBACK (save_restart_toggle_update),
                    scale_data);

  restart_markers_label = gtk_label_new (_("Restart frequency (rows):"));
  gtk_misc_set_alignment (GTK_MISC (restart_markers_label), 1.0, 1.0);
  gtk_table_attach (GTK_TABLE (table), restart_markers_label, 0, 1, 3, 4,
		    GTK_FILL | GTK_SHRINK, GTK_FILL, 0, 0);
  gtk_widget_show (restart_markers_label);

  abox = gtk_alignment_new (0.5, 1.0, 1.0, 0.0);
  gtk_table_attach (GTK_TABLE (table), abox, 1, 3, 2, 4,
		    GTK_EXPAND | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
  gtk_widget_show (abox);

  gtk_widget_set_size_request (restart_markers_scale, SCALE_WIDTH, -1);
  gtk_container_add (GTK_CONTAINER (abox), restart_markers_scale);
  gtk_scale_set_value_pos (GTK_SCALE (restart_markers_scale), GTK_POS_TOP);
  gtk_scale_set_digits (GTK_SCALE (restart_markers_scale), 0);
  gtk_range_set_update_policy (GTK_RANGE (restart_markers_scale),
			       GTK_UPDATE_DELAYED);

  g_signal_connect (scale_data, "value_changed",
                    G_CALLBACK (save_restart_update),
                    restart);

  gtk_widget_set_sensitive (restart_markers_label,
			    jsvals.restart ? TRUE : FALSE);
  gtk_widget_set_sensitive (restart_markers_scale,
			    jsvals.restart ? TRUE : FALSE);

  gtk_widget_show (restart_markers_scale);

  toggle = gtk_check_button_new_with_label (_("Optimize"));
  gtk_table_attach (GTK_TABLE (table), toggle, 0, 3, 4, 5,
		    GTK_FILL, 0, 0, 0);
  gtk_widget_show (toggle);

  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &jsvals.optimize);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (make_preview),
                    NULL);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), jsvals.optimize);

  progressive = gtk_check_button_new_with_label (_("Progressive"));
  gtk_table_attach (GTK_TABLE (table), progressive, 0, 3, 5, 6,
		    GTK_FILL, 0, 0, 0);
  gtk_widget_show (progressive);

  g_signal_connect (progressive, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &jsvals.progressive);
  g_signal_connect (progressive, "toggled",
                    G_CALLBACK (make_preview),
                    NULL);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (progressive),
				jsvals.progressive);

#ifndef HAVE_PROGRESSIVE_JPEG
  gtk_widget_set_sensitive (progressive, FALSE);
#endif

  baseline = gtk_check_button_new_with_label (_("Force baseline JPEG (Readable by all decoders)"));
  gtk_table_attach (GTK_TABLE (table), baseline, 0, 3, 6, 7,
		    GTK_FILL, 0, 0, 0);
  gtk_widget_show (baseline);

  g_signal_connect (baseline, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &jsvals.baseline);
  g_signal_connect (baseline, "toggled",
                    G_CALLBACK (make_preview),
                    NULL);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (baseline),
				jsvals.baseline);

#ifdef HAVE_EXIF
  exif_toggle = gtk_check_button_new_with_label (_("Save EXIF data"));
  gtk_table_attach (GTK_TABLE (table), exif_toggle, 0, 1, 7, 8,
		    GTK_FILL, 0, 0, 0);
  gtk_widget_show (exif_toggle);

  g_signal_connect (exif_toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &jsvals.save_exif);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (exif_toggle),
				jsvals.save_exif);
#endif

  /* Subsampling */
  menu =
    gimp_int_option_menu_new (FALSE,
			      G_CALLBACK (menu_callback),
			      &jsvals.subsmp, jsvals.subsmp,

			      "2x2,1x1,1x1",         0, NULL,
			      "2x1,1x1,1x1 (4:2:2)", 1, NULL,
			      "1x1,1x1,1x1",         2, NULL,

			      NULL);

  gimp_table_attach_aligned (GTK_TABLE (table), 1, 7,
			     _("Subsampling:"),
			     1.0, 0.5,
			     menu, 1, FALSE);

  /* DCT method */
  menu =
    gimp_int_option_menu_new (FALSE,
			      G_CALLBACK (menu_callback),
			      &jsvals.dct, jsvals.dct,

			      _("Fast Integer"),   1, NULL,
			      _("Integer"),        0, NULL,
			      _("Floating-Point"), 2, NULL,
			      
			      NULL);

  gimp_table_attach_aligned (GTK_TABLE (table), 1, 8,
			     _("DCT method (Speed/quality tradeoff):"),
			     1.0, 0.5,
			     menu, 1, FALSE);

  dtype = gimp_drawable_type (drawable_ID_global);
  if (dtype != GIMP_RGB_IMAGE && dtype != GIMP_RGBA_IMAGE)
    gtk_widget_set_sensitive (menu, FALSE);

  com_frame = gtk_frame_new (_("Image comments"));
  gtk_frame_set_shadow_type (GTK_FRAME (com_frame), GTK_SHADOW_ETCHED_IN);
  gtk_box_pack_start (GTK_BOX (main_vbox), com_frame, TRUE, TRUE, 0);

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  gtk_container_set_border_width (GTK_CONTAINER (scrolled_window), 4);
  gtk_container_add (GTK_CONTAINER (com_frame), scrolled_window);
  gtk_widget_show (scrolled_window);

  text_buffer = gtk_text_buffer_new (NULL);
  if (image_comment)
    gtk_text_buffer_set_text (text_buffer, image_comment, -1);

  text_view = gtk_text_view_new_with_buffer (text_buffer);
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (text_view), GTK_WRAP_WORD);

  gtk_container_add (GTK_CONTAINER (scrolled_window), text_view);
  gtk_widget_show (text_view);

  g_object_unref (text_buffer);

  gtk_widget_show (com_frame);

  gtk_widget_show (table);
  gtk_widget_show (dlg);

  run = (gimp_dialog_run (GIMP_DIALOG (dlg)) == GTK_RESPONSE_OK);

  if (run)
    {
      GtkTextIter start_iter;
      GtkTextIter end_iter;

      gtk_text_buffer_get_bounds (text_buffer, &start_iter, &end_iter);
      image_comment = gtk_text_buffer_get_text (text_buffer,
                                                &start_iter, &end_iter, FALSE);
    }

  gtk_widget_destroy (dlg);
  destroy_preview ();
  gimp_displays_flush ();

  return run;
}

static void
save_restart_toggle_update (GtkWidget     *widget,
			    GtkAdjustment *adjustment)
{
  save_restart_update (adjustment, widget);
}

static void
save_restart_update (GtkAdjustment *adjustment,
		     GtkWidget     *toggle)
{
  jsvals.restart = GTK_TOGGLE_BUTTON (toggle)->active ? adjustment->value : 0;

  gtk_widget_set_sensitive (restart_markers_label,
			    jsvals.restart ? TRUE : FALSE);
  gtk_widget_set_sensitive (restart_markers_scale,
			    jsvals.restart ? TRUE : FALSE);

  make_preview ();
}

static void
menu_callback (GtkWidget *widget,
	       gpointer   data)
{
  gimp_menu_item_update (widget, data);
  make_preview ();
}
