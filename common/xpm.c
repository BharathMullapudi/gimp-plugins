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

/* XPM plugin version 1.2.6 */

/*
1.2.6 fixes crash when saving indexed images (bug #109567)

1.2.5 only creates a "None" color entry if the image has alpha (bug #108034)

1.2.4 displays an error message if saving fails (bug #87588)

1.2.3 fixes bug when running in noninteractive mode
changes alpha_threshold range from [0, 1] to [0,255] for consistency with
the threshold_alpha plugin

1.2.2 fixes bug that generated bad digits on images with more than 20000
colors. (thanks, yanele)
parses gtkrc (thanks, yosh)
doesn't load parameter screen on images that don't have alpha

1.2.1 fixes some minor bugs -- spaces in #XXXXXX strings, small typos in code.

1.2 compute color indexes so that we don't have to use XpmSaveXImage*

Previous...Inherited code from Ray Lehtiniemi, who inherited it from S & P.
*/

#include "config.h"

#include <string.h>

#include <glib/gstdio.h>

#include <gdkconfig.h>		/* For GDK_WINDOWING_WIN32 */

#ifdef GDK_WINDOWING_WIN32
#ifndef XPM_NO_X
#define XPM_NO_X
#endif
#else
#include <X11/Xlib.h>
#endif

#include <X11/xpm.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


static const gchar linenoise [] =
" .+@#$%&*=-;>,')!~{]^/(_:<[}|1234567890abcdefghijklmnopqrstuvwxyz\
ABCDEFGHIJKLMNOPQRSTUVWXYZ`";

#define SCALE_WIDTH 125

/* Structs for the save dialog */
typedef struct
{
  gint threshold;
} XpmSaveVals;

typedef struct
{
  guchar r;
  guchar g;
  guchar b;
} rgbkey;

/*  whether the image is color or not.  global so I only have to pass
 *  one user value to the GHFunc
 */
static gboolean   color;

/*  bytes per pixel.  global so I only have to pass one user value
 *  to the GHFunc
 */
static gint       cpp;

/* Declare local functions */
static void       query               (void);
static void       run                 (const gchar      *name,
                                       gint              nparams,
                                       const GimpParam  *param,
                                       gint             *nreturn_vals,
                                       GimpParam       **return_vals);

static gint32     load_image          (const gchar      *filename);
static guchar   * parse_colors        (XpmImage         *xpm_image);
static void       parse_image         (gint32            image_ID,
                                       XpmImage         *xpm_image,
                                       guchar           *cmap);
static gboolean   save_image          (const gchar      *filename,
                                       gint32            image_ID,
                                       gint32            drawable_ID);

static gboolean   save_dialog         (void);


GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};

static XpmSaveVals xpmvals =
{
  127  /* alpha threshold */
};


MAIN ()

static void
query (void)
{
  static GimpParamDef load_args[] =
  {
    { GIMP_PDB_INT32,     "run_mode",     "Interactive, non-interactive" },
    { GIMP_PDB_STRING,    "filename",     "The name of the file to load" },
    { GIMP_PDB_STRING,    "raw_filename", "The name entered" }
  };

  static GimpParamDef load_return_vals[] =
  {
    { GIMP_PDB_IMAGE,    "image",         "Output image" }
  };

  static GimpParamDef save_args[] =
  {
    { GIMP_PDB_INT32,    "run_mode",      "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE,    "image",         "Input image" },
    { GIMP_PDB_DRAWABLE, "drawable",      "Drawable to save" },
    { GIMP_PDB_STRING,   "filename",      "The name of the file to save the image in" },
    { GIMP_PDB_STRING,   "raw_filename",  "The name of the file to save the image in" },
    { GIMP_PDB_INT32,    "threshold",     "Alpha threshold (0-255)" }
  };

  gimp_install_procedure ("file_xpm_load",
                          "Load files in XPM (X11 Pixmap) format.",
                          "Load files in XPM (X11 Pixmap) format. "
                          "XPM is a portable image format designed to be "
                          "included in C source code. XLib provides utility "
                          "functions to read this format. Newer code should "
                          "however be using gdk-pixbuf-csource instead. "
                          "XPM supports colored images, unlike the XBM "
                          "format which XPM was designed to replace.",
                          "Spencer Kimball & Peter Mattis & Ray Lehtiniemi",
                          "Spencer Kimball & Peter Mattis",
                          "1997",
                          N_("X PixMap image"),
                          NULL,
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (load_args),
                          G_N_ELEMENTS (load_return_vals),
                          load_args, load_return_vals);

  gimp_register_file_handler_mime ("file_xpm_load", "image/x-xpixmap");
  gimp_register_magic_load_handler ("file_xpm_load",
                                    "xpm",
                                    "",
                                    "0, string,/*\\040XPM\\040*/");

  gimp_install_procedure ("file_xpm_save",
                          "Save files in XPM (X11 Pixmap) format.",
                          "Save files in XPM (X11 Pixmap) format. "
                          "XPM is a portable image format designed to be "
                          "included in C source code. XLib provides utility "
                          "functions to read this format. Newer code should "
                          "however be using gdk-pixbuf-csource instead. "
                          "XPM supports colored images, unlike the XBM "
                          "format which XPM was designed to replace.",
                          "Spencer Kimball & Peter Mattis & Ray Lehtiniemi & Nathan Summers",
                          "Spencer Kimball & Peter Mattis",
                          "1997",
                          N_("X PixMap image"),
                          "RGB*, GRAY*, INDEXED*",
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (save_args), 0,
                          save_args, NULL);

  gimp_register_file_handler_mime ("file_xpm_save", "image/x-xpixmap");
  gimp_register_save_handler ("file_xpm_save", "xpm", "");
}

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  static GimpParam  values[2];
  GimpRunMode       run_mode;
  GimpPDBStatusType status = GIMP_PDB_SUCCESS;
  gint32            image_ID;
  gint32            drawable_ID;
  GimpExportReturn  export = GIMP_EXPORT_CANCEL;

  run_mode = param[0].data.d_int32;

  INIT_I18N ();

  *nreturn_vals = 1;
  *return_vals  = values;

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;

  if (strcmp (name, "file_xpm_load") == 0)
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
  else if (strcmp (name, "file_xpm_save") == 0)
    {
      gimp_ui_init ("xpm", FALSE);

      image_ID    = param[1].data.d_int32;
      drawable_ID = param[2].data.d_int32;

      /*  eventually export the image */
      switch (run_mode)
        {
        case GIMP_RUN_INTERACTIVE:
        case GIMP_RUN_WITH_LAST_VALS:
          export = gimp_export_image (&image_ID, &drawable_ID, "XPM",
                                      (GIMP_EXPORT_CAN_HANDLE_RGB |
                                       GIMP_EXPORT_CAN_HANDLE_GRAY |
                                       GIMP_EXPORT_CAN_HANDLE_INDEXED |
                                       GIMP_EXPORT_CAN_HANDLE_ALPHA ));
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
          /*  Possibly retrieve data  */
          gimp_get_data ("file_xpm_save", &xpmvals);

          /*  First acquire information with a dialog  */
          if (gimp_drawable_has_alpha (drawable_ID))
            if (! save_dialog ())
              status = GIMP_PDB_CANCEL;
          break;

        case GIMP_RUN_NONINTERACTIVE:
          /*  Make sure all the arguments are there!  */
          if (nparams != 6)
            {
              status = GIMP_PDB_CALLING_ERROR;
            }
          else
            {
              xpmvals.threshold = param[5].data.d_int32;

              if (xpmvals.threshold < 0 ||
                  xpmvals.threshold > 255)
                status = GIMP_PDB_CALLING_ERROR;
            }
          break;

        case GIMP_RUN_WITH_LAST_VALS:
          /*  Possibly retrieve data  */
          gimp_get_data ("file_xpm_save", &xpmvals);
          break;

        default:
          break;
        }

      if (status == GIMP_PDB_SUCCESS)
        {
          if (save_image (param[3].data.d_string,
                          image_ID,
                          drawable_ID))
            {
              gimp_set_data ("file_xpm_save", &xpmvals, sizeof (XpmSaveVals));
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
load_image (const gchar *filename)
{
  XpmImage  xpm_image;
  guchar   *cmap;
  gint32    image_ID;

  gimp_progress_init (NULL);
  gimp_progress_set_text (_("Opening '%s'..."),
                          gimp_filename_to_utf8 (filename));

  /* read the raw file */
  switch (XpmReadFileToXpmImage ((char *) filename, &xpm_image, NULL))
    {
    case XpmSuccess:
      break;

    case XpmOpenFailed:
      g_message (_("Error opening file '%s'"),
                 gimp_filename_to_utf8 (filename));
      return -1;

    case XpmFileInvalid:
      g_message (_("XPM file invalid"));
      return -1;

    default:
      return -1;
    }

  /* parse out the colors into a cmap */
  cmap = parse_colors (&xpm_image);

  /* create the new image */
  image_ID = gimp_image_new (xpm_image.width,
                             xpm_image.height,
                             GIMP_RGB);

  /* name it */
  gimp_image_set_filename (image_ID, filename);

  /* fill it */
  parse_image (image_ID, &xpm_image, cmap);

  /* clean up and exit */
  g_free (cmap);

  return image_ID;
}

static guchar *
parse_colors (XpmImage  *xpm_image)
{
#ifndef XPM_NO_X
  Display  *display;
  Colormap  colormap;
#endif
  gint      i, j;
  guchar   *cmap;

#ifndef XPM_NO_X
  /* open the display and get the default color map */
  display  = XOpenDisplay (NULL);
  colormap = DefaultColormap (display, DefaultScreen (display));
#endif

  /* alloc a buffer to hold the parsed colors */
  cmap = g_new0 (guchar, 4 * xpm_image->ncolors);

  /* parse each color in the file */
  for (i = 0, j = 0; i < xpm_image->ncolors; i++)
    {
      gchar     *colorspec = "None";
      XpmColor *xpm_color;
#ifndef XPM_NO_X
      XColor    xcolor;
#else
      GdkColor  xcolor;
#endif

      xpm_color = &(xpm_image->colorTable[i]);

      /* pick the best spec available */
      if (xpm_color->c_color)
        colorspec = xpm_color->c_color;
      else if (xpm_color->g_color)
        colorspec = xpm_color->g_color;
      else if (xpm_color->g4_color)
        colorspec = xpm_color->g4_color;
      else if (xpm_color->m_color)
        colorspec = xpm_color->m_color;

      /* parse if it's not transparent */
      if (strcmp (colorspec, "None") != 0)
        {
#ifndef XPM_NO_X
          XParseColor (display, colormap, colorspec, &xcolor);
#else
          gdk_color_parse (colorspec, &xcolor);
#endif
          cmap[j++] = xcolor.red >> 8;
          cmap[j++] = xcolor.green >> 8;
          cmap[j++] = xcolor.blue >> 8;
          cmap[j++] = ~0;
        }
      else
        {
          j += 4;
        }
    }
#ifndef XPM_NO_X
  XCloseDisplay (display);
#endif
  return cmap;
}

static void
parse_image (gint32    image_ID,
             XpmImage *xpm_image,
             guchar   *cmap)
{
  gint       tile_height;
  gint       scanlines;
  gint       val;
  guchar    *buf;
  guchar    *dest;
  guint     *src;
  GimpPixelRgn  pixel_rgn;
  GimpDrawable *drawable;
  gint32     layer_ID;
  gint       i, j;

  layer_ID = gimp_layer_new (image_ID,
                             _("Color"),
                             xpm_image->width,
                             xpm_image->height,
                             GIMP_RGBA_IMAGE,
                             100,
                             GIMP_NORMAL_MODE);

  gimp_image_add_layer (image_ID, layer_ID, 0);

  drawable = gimp_drawable_get (layer_ID);

  gimp_pixel_rgn_init (&pixel_rgn, drawable,
                       0, 0,
                       drawable->width, drawable->height,
                       TRUE, FALSE);

  tile_height = gimp_tile_height ();

  buf  = g_new (guchar, tile_height * xpm_image->width * 4);

  src  = xpm_image->data;
  for (i = 0; i < xpm_image->height; i+=tile_height)
    {
      dest = buf;
      scanlines = MIN(tile_height, xpm_image->height - i);
      j = scanlines * xpm_image->width;
      while (j--) {
        {
          val = *(src++) * 4;
          *(dest)   = cmap[val];
          *(dest+1) = cmap[val+1];
          *(dest+2) = cmap[val+2];
          *(dest+3) = cmap[val+3];
          dest += 4;
        }

        if ((j % 100) == 0)
          gimp_progress_update ((double) i / (double) xpm_image->height);
      }

      gimp_pixel_rgn_set_rect (&pixel_rgn, buf,
                               0, i,
                               drawable->width, scanlines);

    }

  g_free(buf);

  gimp_drawable_detach (drawable);
}

static guint
rgbhash (rgbkey *c)
{
  return ((guint)c->r) ^ ((guint)c->g) ^ ((guint)c->b);
}

static guint
compare (rgbkey *c1,
         rgbkey *c2)
{
  return (c1->r == c2->r) && (c1->g == c2->g) && (c1->b == c2->b);
}

static void
set_XpmImage (XpmColor *array,
              guint     index,
              gchar    *colorstring)
{
  gchar *p;
  gint i, charnum, indtemp;

  indtemp=index;
  array[index].string = p = g_new (gchar, cpp+1);

  /*convert the index number to base sizeof(linenoise)-1 */
  for (i=0; i<cpp; ++i)
    {
      charnum = indtemp % (sizeof (linenoise) - 1);
      indtemp = indtemp / (sizeof (linenoise) - 1);
      *p++ = linenoise[charnum];
    }
  /* *p++=linenoise[indtemp]; */

  *p = '\0'; /* C and its stupid null-terminated strings... */

  array[index].symbolic = NULL;
  array[index].m_color  = NULL;
  array[index].g4_color = NULL;

  if (color)
    {
      array[index].g_color = NULL;
      array[index].c_color = colorstring;
    } else {
      array[index].c_color = NULL;
      array[index].g_color = colorstring;
    }
}

static void
create_colormap_from_hash (gpointer gkey,
                           gpointer value,
                           gpointer user_data)
{
  rgbkey *key    = gkey;
  gchar  *string = g_new (gchar, 8);

  sprintf (string, "#%02X%02X%02X", (int)key->r, (int)key->g, (int)key->b);
  set_XpmImage (user_data, *((int *) value), string);
}

static gboolean
save_image (const gchar *filename,
            gint32       image_ID,
            gint32       drawable_ID)
{
  GimpDrawable *drawable;
  GimpPixelRgn  pixel_rgn;

  gint       width;
  gint       height;
  gint       ncolors = 1;
  gint      *indexno;
  gboolean   indexed;
  gboolean   alpha;

  XpmColor  *colormap;
  XpmImage  *image;

  guint     *ibuff   = NULL;
  guchar    *buffer;
  guchar    *data;

  GHashTable *hash = NULL;

  gint       i, j, k;
  gint       threshold = xpmvals.threshold;

  gboolean   rc = FALSE;

  /* get some basic stats about the image */
  alpha   = gimp_drawable_has_alpha (drawable_ID);
  color   = !gimp_drawable_is_gray (drawable_ID);
  indexed = gimp_drawable_is_indexed (drawable_ID);

  drawable = gimp_drawable_get (drawable_ID);
  width    = drawable->width;
  height   = drawable->height;

  /* allocate buffer making the assumption that ibuff is 32 bit aligned... */
  ibuff = g_new (guint, width * height);

  hash = g_hash_table_new ((GHashFunc) rgbhash, (GCompareFunc) compare);

  gimp_progress_init (NULL);
  gimp_progress_set_text (_("Saving '%s'..."),
                          gimp_filename_to_utf8 (filename));

  ncolors = alpha ? 1 : 0;

  /* allocate a pixel region to work with */
  buffer = g_new (guchar, gimp_tile_height() * width * drawable->bpp);

  gimp_pixel_rgn_init (&pixel_rgn, drawable,
                       0, 0,
                       width, height,
                       TRUE, FALSE);

  /* process each row of tiles */
  for (i = 0; i < height; i+=gimp_tile_height())
    {
      gint scanlines;

      /* read the next row of tiles */
      scanlines = MIN (gimp_tile_height(), height - i);
      gimp_pixel_rgn_get_rect (&pixel_rgn, buffer, 0, i, width, scanlines);
      data = buffer;

      /* process each pixel row */
      for (j = 0; j < scanlines; j++)
        {
          /* go to the start of this row in each image */
          guint *idata = ibuff + (i+j) * width;

          /* do each pixel in the row */
          for (k = 0; k < width; k++)
            {
              rgbkey *key = g_new (rgbkey, 1);
              guchar  a;

              /* get pixel data */
              key->r = *(data++);
              key->g = color && !indexed ? *(data++) : key->r;
              key->b = color && !indexed ? *(data++) : key->r;
              a = alpha ? *(data++) : 255;

              if (a < threshold)
                {
                  *(idata++) = 0;
                }
              else
                {
                  if (indexed)
                    {
                      *(idata++) = (key->r) + (alpha ? 1 : 0);
                    }
                  else
                    {
                      indexno = g_hash_table_lookup (hash, key);
                      if (!indexno)
                        {
                          indexno = g_new (gint, 1);
                          *indexno = ncolors++;
                          g_hash_table_insert (hash, key, indexno);
                          key = g_new (rgbkey, 1);
                        }
                      *(idata++) = *indexno;
                    }
                }
            }

          /* kick the progress bar */
          gimp_progress_update ((gdouble) (i+j) / (gdouble) height);
        }
    }
  g_free (buffer);

  if (indexed)
    {
      guchar *cmap = gimp_image_get_colormap (image_ID, &ncolors);
      guchar *c;

      c = cmap;

      if (alpha)
        ncolors++;

      colormap = g_new (XpmColor, ncolors);
      cpp =
        1 + (gdouble) log (ncolors) / (gdouble) log (sizeof (linenoise) - 1.0);

      if (alpha)
        set_XpmImage (colormap, 0, "None");

      for (i = alpha ? 1 : 0; i < ncolors; i++)
        {
          gchar *string;
          guchar r, g, b;

          r = *c++;
          g = *c++;
          b = *c++;

          string = g_new (gchar, 8);
          sprintf (string, "#%02X%02X%02X", (int)r, (int)g, (int)b);
          set_XpmImage (colormap, i, string);
        }

      g_free (cmap);
    }
  else
    {
      colormap = g_new (XpmColor, ncolors);
      cpp =
        1 + (gdouble) log (ncolors) / (gdouble) log (sizeof (linenoise) - 1.0);

      if (alpha)
        set_XpmImage (colormap, 0, "None");

      g_hash_table_foreach (hash, create_colormap_from_hash, colormap);
    }

  image = g_new (XpmImage, 1);

  image->width      = width;
  image->height     = height;
  image->ncolors    = ncolors;
  image->cpp        = cpp;
  image->colorTable = colormap;
  image->data       = ibuff;

  /* do the save */
  rc = (XpmWriteFileFromXpmImage ((char *) filename,
                                  image, NULL) == XpmSuccess);

  /* clean up resources */
  gimp_drawable_detach (drawable);

  g_free (ibuff);

  if (hash)
    g_hash_table_destroy (hash);

  return rc;
}

static gboolean
save_dialog (void)
{
  GtkWidget *dlg;
  GtkWidget *table;
  GtkObject *scale_data;
  gboolean   run;

  dlg = gimp_dialog_new (_("Save as XPM"), "xpm",
                         NULL, 0,
                         gimp_standard_help_func, "file-xpm-save",

                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                         GTK_STOCK_OK,     GTK_RESPONSE_OK,

                         NULL);

  gtk_dialog_set_alternative_button_order (GTK_DIALOG (dlg),
                                              GTK_RESPONSE_OK,
                                              GTK_RESPONSE_CANCEL,
                                              -1);

  table = gtk_table_new (1, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 6);
  gtk_container_set_border_width (GTK_CONTAINER (table), 12);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), table, TRUE, TRUE, 0);
  gtk_widget_show (table);

  scale_data = gimp_scale_entry_new (GTK_TABLE (table), 0, 0,
                                     _("_Alpha threshold:"), SCALE_WIDTH, 0,
                                     xpmvals.threshold, 0, 255, 1, 8, 0,
                                     TRUE, 0, 0,
                                     NULL, NULL);

  g_signal_connect (scale_data, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &xpmvals.threshold);

  gtk_widget_show (dlg);

  run = (gimp_dialog_run (GIMP_DIALOG (dlg)) == GTK_RESPONSE_OK);

  gtk_widget_destroy (dlg);

  return run;
}
