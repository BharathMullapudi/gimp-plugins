/*
 * This is a plug-in for the GIMP.
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 */

/*
 * Analyze colorcube.
 * 
 * Author: robert@experimental.net
 */

/*
 * Modified by Manish Singh <yosh@gimp.org> 2003
 */

#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef __GNUC__
#warning GTK_DISABLE_DEPRECATED
#endif
#undef GTK_DISABLE_DEPRECATED

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


/*
 * I found the following implementation of storing a sparse color matrix
 * in Dr. Dobb's Journal #232 (July 1995).
 * 
 * The matrix is build as three linked lists, each representing a color-
 * cube axis. Each node in the matrix contains two pointers: one to its 
 * neighbour and one to the next color-axis. 
 *
 * Each red node contains a pointer to the next red node, and a pointer to
 * the green nodes. Green nodes, in turn, each contain a pointer to the next
 * green node, and a pointer to the blue axis.
 *
 * If we want to find an RGB triplet, we first walk down the red axis, match
 * the red values, from where we start walking down the green axis, etc.
 * If we haven't found our color at the end of the blue axis, it's a new color
 * and we store it in the matrix.
 *
 * For the textual-impaired (number in parentheses are color values):
 *
 *      start of table
 *      |
 *      v
 *         RED(91)  ->  RED(212)  ->  ...
 *      |            |             
 *      |            v 
 *      |            GREEN(81)  ->  GREEN(128)  ->  ...
 *      |            |              |
 *      |            |              v
 *      |            |              BLUE(93)
 *      |            v
 *      |            BLUE(206)  ->  BLUE(93)  ->  ...
 *      v
 *      GREEN(1)  ->  ...
 *      |
 *      v
 *      BLUE(206)  ->  BLUE(12)  ->  ...
 *
 * So, some colors stored are (in RGB triplets): (91, 1, 206), (91, 1, 12),
 * (212, 128, 93), ...
 *
 */
typedef enum
{
  RED,
  GREEN,
  BLUE
} ColorType;


typedef struct _ColorNode ColorNode;

struct _ColorNode
{
  ColorNode *next_neighbour;
  ColorNode *next_axis;

  ColorType  color;

  guchar     r;
  guchar     g;
  guchar     b;

  gint       count;
};


/* lets prototype */
static void query (void);
static void run   (const gchar      *name,
                   gint              n_params,
                   const GimpParam  *param,
                   gint             *nreturn_vals,
                   GimpParam       **return_vals);

static void doDialog (void);
static void analyze  (GimpDrawable *drawable);

static void histogram   (guchar r,
                         guchar g,
                         guchar b);
static void fillPreview (GtkWidget *preview);
static void insertcolor (guchar r,
                         guchar g,
                         guchar b);

static void doLabel (GtkWidget  *table,
                     const char *format,
                     ...) G_GNUC_PRINTF (2, 3);

/* some global variables */
static gchar     *filename = NULL;
static gint       width, height, bpp;
static ColorNode *color_table = NULL;
static gint32     hist_red[256], hist_green[256], hist_blue[256];
static gint       maxred = 0, maxgreen = 0, maxblue = 0;
static gint       uniques = 0;
static gint32     imageID;

/* size of histogram image */
static const int PREWIDTH = 256;
static const int PREHEIGHT = 150;

/* lets declare what we want to do */
GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};

/* run program */
MAIN ()

/* tell GIMP who we are */
static void
query (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32,    "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE,    "image",    "Input image" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable" }
  };

  static GimpParamDef return_vals[] =
  {
    { GIMP_PDB_INT32, "num_colors", "Number of colors in the image" }
  };

  gimp_install_procedure ("plug_in_ccanalyze",
                          "Colorcube analysis",
                          "Analyze colorcube and print some information about "
                          "the current image (also displays a color-histogram)",
                          "robert@experimental.net",
                          "robert@experimental.net",
                          "June 20th, 1997",
                          N_("<Image>/Filters/Colors/Colorcube A_nalysis..."),
                          "RGB*, GRAY*, INDEXED*",
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (args), G_N_ELEMENTS (return_vals),
                          args, return_vals);
}

/* main function */
static void
run (const gchar      *name,
     gint              n_params,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  static GimpParam   values[2];
  GimpRunMode        run_mode;
  GimpPDBStatusType  status = GIMP_PDB_SUCCESS;
  GimpDrawable      *drawable;

  run_mode = param[0].data.d_int32;

  INIT_I18N ();

  *nreturn_vals = 2;
  *return_vals = values;

  if (run_mode == GIMP_RUN_NONINTERACTIVE)
    {
      if (n_params != 3)
        status = GIMP_PDB_CALLING_ERROR;
    }

  if (status == GIMP_PDB_SUCCESS)
    {
      drawable = gimp_drawable_get (param[2].data.d_drawable);
      imageID = param[1].data.d_image;

      if (gimp_drawable_is_rgb (drawable->drawable_id) ||
          gimp_drawable_is_gray (drawable->drawable_id) ||
          gimp_drawable_is_indexed (drawable->drawable_id))
        {
          memset (hist_red, 0, sizeof (hist_red));
          memset (hist_green, 0, sizeof (hist_green));
          memset (hist_blue, 0, sizeof (hist_blue));

          filename = gimp_image_get_filename (imageID);

          gimp_tile_cache_ntiles (2 * (drawable->width / gimp_tile_width () + 1));

          analyze (drawable);

          /* show dialog after we analyzed image */
          if (run_mode != GIMP_RUN_NONINTERACTIVE)
            doDialog ();
        }
      else
        status = GIMP_PDB_EXECUTION_ERROR;

      gimp_drawable_detach (drawable);
    }

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = status;
  values[1].type          = GIMP_PDB_INT32;
  values[1].data.d_int32  = uniques;
}

/* do the analyzing */
static void
analyze (GimpDrawable *drawable)
{
  GimpPixelRgn  srcPR;
  guchar       *src_row, *cmap;
  gint          x, y, numcol;
  gint          x1, y1, x2, y2;
  guchar        r, g, b;
  guchar        idx;
  gboolean      gray;

  gimp_progress_init (_("Colorcube Analysis..."));

  /* 
   * Get the input area. This is the bounding box of the selection in
   * the image (or the entire image if there is no selection). Only
   * operating on the input area is simply an optimization. It doesn't
   * need to be done for correct operation. (It simply makes it go
   * faster, since fewer pixels need to be operated on).
   */
  gimp_drawable_mask_bounds (drawable->drawable_id, &x1, &y1, &x2, &y2);

  /* 
   * Get the size of the input image (this will/must be the same
   * as the size of the output image).
   */
  width = drawable->width;
  height = drawable->height;
  bpp = drawable->bpp;

  /* allocate row buffer */
  src_row = g_malloc ((x2 - x1) * bpp);

  /* initialize the pixel region */
  gimp_pixel_rgn_init (&srcPR, drawable, 0, 0, width, height, FALSE, FALSE);

  cmap = gimp_image_get_cmap (imageID, &numcol);
  gray = gimp_drawable_is_gray (drawable->drawable_id);

  for (y = y1; y < y2; y++)
    {
      gimp_pixel_rgn_get_row (&srcPR, src_row, x1, y, (x2 - x1));

      for (x = 0; x < x2 - x1; x++)
        {
          /* 
           * If the image is indexed, fetch RGB values
           * from colormap.
           */
          if (cmap)
            {
              idx = src_row[x];

              r = cmap[idx * 3];
              g = cmap[idx * 3 + 1];
              b = cmap[idx * 3 + 2];
            }
          else if (gray)
            {
              r = g = b = src_row[x * bpp];
            }
          else
            {
              r = src_row[x * bpp];
              g = src_row[x * bpp + 1];
              b = src_row[x * bpp + 2];
            }

          insertcolor (r, g, b);
        }

      /* tell the user what we're doing */
      if ((y % 10) == 0)
        gimp_progress_update ((double) y / (double) (y2 - y1));
    }

  /* clean up */
  g_free (src_row);
}

/* here's were we actually store our color-table */
static void
insertcolor (guchar r,
             guchar g,
             guchar b)
{
  ColorNode *node, *next = NULL, *prev = NULL,
            *newred, *newgreen = NULL, *newblue;
  ColorType  type = RED;

  histogram (r, g, b);

  /* lets walk the tree, and see if it already contains this color */
  for (node = color_table; node != NULL; prev = node, node = next)
    {
      if (node->color == RED)
        {
          if (node->r == r)
            {
              type = GREEN;
              next = node->next_axis;
            }
          else
            {
              type = RED;
              next = node->next_neighbour;
            }
        }
      else if (node->color == GREEN)
        {
          if (node->g == g)
            {
              type = BLUE;
              next = node->next_axis;
            }
          else
            {
              type = GREEN;
              next = node->next_neighbour;
            }
        }
      else if (node->color == BLUE)
        {
          /* found it! */
          if (node->b == b)
            break;
          else
            {
              type = BLUE;
              next = node->next_neighbour;
            }
        }
    }

  /* this color was already stored -> update it's count */
  if (node)
    {
      node->count++;
      return;
    }

  /* New color! */

  /* first, create blue node */
  newblue = g_new0 (ColorNode, 1);
  newblue->color = BLUE;

  /* no neighbours or links to another axis */
  newblue->next_neighbour = NULL;
  newblue->next_axis = NULL;

  /* 
   * At the end of the list, we store the entire triplet.
   * For now, there is no reason whatsoever to do this, but perhaps
   * it might prove useful someday :)
   */
  newblue->r = r;
  newblue->g = g;
  newblue->b = b;
  newblue->count = 1;

  /* previous was green: create link to axis */
  if (prev && prev->color == GREEN && type == BLUE)
    prev->next_axis = newblue;

  /* previous was blue: create link to neighbour */
  if (prev && prev->color == BLUE && type == BLUE)
    prev->next_neighbour = newblue;

  /* green node */
  if (type == GREEN || type == RED)
    {
      newgreen = g_new0 (ColorNode, 1);
      newgreen->color = GREEN;

      newgreen->next_neighbour = NULL;
      newgreen->next_axis = newblue;

      newgreen->g = g;

      /* count doesn't matter here */
      newgreen->count = -1;

      /* previous was red: create link to axis */
      if (prev && prev->color == RED && type == GREEN)
        prev->next_axis = newgreen;

      /* previous was green: create link to neighbour */
      if (prev && prev->color == GREEN && type == GREEN)
        prev->next_neighbour = newgreen;
    }

  /* red node */
  if (type == RED)
    {
      newred = g_new0 (ColorNode, 1);
      newred->color = RED;

      newred->next_neighbour = NULL;
      newred->next_axis = newgreen;

      newred->r = r;

      /* count doesn't matter here */
      newred->count = -1;

      /* previous was red, update its neighbour link */
      if (prev)
        prev->next_neighbour = newred;
      else
        color_table = newred;
    }

  /* increase the number of unique colors */
  uniques++;
}

/* 
 * Update RGB count, and keep track of maximum values (which aren't used 
 * anywhere as of yet, but they might be useful sometime).
 */
static void
histogram (guchar r,
           guchar g,
           guchar b)
{
  if (++hist_red[r] > maxred)
    maxred = hist_red[r];

  if (++hist_green[g] > maxgreen)
    maxgreen = hist_green[g];

  if (++hist_blue[b] > maxblue)
    maxblue = hist_blue[b];
}

/* show our results */
static void
doDialog (void)
{
  struct stat  st;
  GtkWidget   *dialog;
  GtkWidget   *frame;
  GtkWidget   *xframe;
  GtkWidget   *table;
  GtkWidget   *preview;
  gsize        filesize = 0;

  gimp_ui_init ("ccanalyze", TRUE);

  /* set up the dialog */
  dialog = gimp_dialog_new (_("Colorcube Analysis"), "ccanalyze",
                            gimp_standard_help_func, "filters/ccanalyze.html",
                            GTK_WIN_POS_MOUSE,
                            FALSE, TRUE, FALSE,

                            GTK_STOCK_OK, gtk_widget_destroy,
                            NULL, 1, NULL, FALSE, TRUE,

                            NULL);

  g_signal_connect (dialog, "destroy",
                    G_CALLBACK (gtk_main_quit),
                    NULL);

  /* set up frame */
  frame = gtk_frame_new (_("Results"));
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
  gtk_container_border_width (GTK_CONTAINER (frame), 10);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), frame,
                      TRUE, TRUE, 0);

  table = gtk_table_new (12, 1, FALSE);
  gtk_container_border_width (GTK_CONTAINER (table), 10);
  gtk_container_add (GTK_CONTAINER (frame), table);

  xframe = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type(GTK_FRAME (xframe), GTK_SHADOW_IN);
  gtk_table_attach (GTK_TABLE (table), xframe,
                    0, 1, 0, 1,
                    GTK_EXPAND, GTK_EXPAND, 0, 0);

  /* use preview for histogram window */
  preview = gtk_preview_new (GTK_PREVIEW_COLOR);
  gtk_preview_size (GTK_PREVIEW (preview), PREWIDTH, PREHEIGHT);
  gtk_container_add (GTK_CONTAINER (xframe), preview);

  fillPreview (preview);

  /* output results */
  doLabel (table, _("Image dimensions: %dx%d"), width, height);
  doLabel (table, _("Uncompressed size in bytes: %d"), width * height * bpp);

  if (uniques == 0)
    doLabel (table, _("No colors (?)"));
  else if (uniques == 1)
    doLabel (table, _("Only one unique color"));
  else
    doLabel (table, _("Number of unique colors: %d"), uniques);

  if (filename && !stat (filename, &st) && !gimp_image_is_dirty (imageID))
    {
      doLabel (table, _("Filename: %s"), filename);

      filesize = st.st_size;

      doLabel (table, _("Compressed size in bytes: %u"), filesize);
      doLabel (table, _("Compression ratio (approx.): %d to 1"),
                      (gint) RINT ((double) (width * height * bpp) / filesize));
    }
        
  /* show stuff */
  gtk_widget_show_all (dialog);

  gtk_main ();
  gdk_flush ();
}

/* shortcut */
static void
doLabel (GtkWidget *table,
         const char *format,
         ...)
{
  static gint  idx = 1;
  GtkWidget   *label;
  gchar       *text;
  va_list      args;

  va_start (args, format);
  text = g_strdup_vprintf (format, args);
  va_end (args);

  label = gtk_label_new (text);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_widget_show (label);

  gtk_table_attach (GTK_TABLE (table), label,
                    0, 1, idx, idx + 1,
                    GTK_FILL, 0, 5, 0);

  g_free (text);

  idx += 2;
}

/* fill our preview image with the color-histogram */
static void
fillPreview (GtkWidget *preview)
{
  guchar *image, *pimage, *pixel;
  gint    x, y, rowstride;
  gint    histcount, val;

  image = g_malloc0 (PREWIDTH * PREHEIGHT * 3);

  rowstride = PREWIDTH * 3;

  for (x = 0; x < PREWIDTH; x++)
    {
      /*
       * For every channel, calculate a logarithmic value, scale it,
       * and build a one-pixel bar.
       *  ... in the respective channel, preserving the other ones. --hb
       */
      histcount = hist_red[x] ? hist_red[x] : 1;

      val = (gint) (log ((gdouble) histcount) * (PREHEIGHT / 12));

      if (val > PREHEIGHT)
        val = PREHEIGHT;

      for (y = PREHEIGHT - 1; y > (PREHEIGHT - val); y--)
        {
          pixel = image + (x * 3) + (y * rowstride);
          *pixel = 255;
        }

      histcount = hist_green[x] ? hist_green[x] : 1;

      val = (gint) (log ((gdouble) histcount) * (PREHEIGHT / 12));

      if (val > PREHEIGHT)
        val = PREHEIGHT;

      for (y = PREHEIGHT - 1; y > (PREHEIGHT - val); y--)
        {
          pixel = image + (x * 3) + (y * rowstride);
          *(pixel + 1) = 255;
        }

      histcount = hist_blue[x] ? hist_blue[x] : 1;

      val = (gint) (log ((gdouble) histcount) * (PREHEIGHT / 12));

      if (val > PREHEIGHT)
        val = PREHEIGHT;

      for (y = PREHEIGHT - 1; y > (PREHEIGHT - val); y--)
        {
          pixel = image + (x * 3) + (y * rowstride);
          *(pixel + 2) = 255;
        }
    }

  /* move our data into the preview image */
  for (pimage = image, y = 0; y < PREHEIGHT; y++)
    {
      gtk_preview_draw_row (GTK_PREVIEW (preview), pimage, 0, y, PREWIDTH);
      pimage += 3 * PREWIDTH;
    }

  g_free (image);
}
