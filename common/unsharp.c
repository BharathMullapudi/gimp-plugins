/* $Id$
 *
 * unsharp.c 0.10 -- This is a plug-in for the GIMP 1.0
 *  http://www.stdout.org/~winston/gimp/unsharp.html
 *  (now out of date)
 *
 * Copyright (C) 1999 Winston Chang
 *                    <winstonc@cs.wisc.edu>
 *                    <winston@stdout.org>
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
#include <string.h>
#include <stdio.h>

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"

#define PLUG_IN_VERSION "0.10"

#define PREVIEW_SIZE  128
#define SCALE_WIDTH   150
#define ENTRY_WIDTH     4



/* to show both pretty unoptimized code and ugly optimized code blocks
   There's really no reason to define this, unless you want to see how
   much pointer aritmetic can speed things up.  I find that it is about
   45% faster with the optimized code. */
/* #define READABLE_CODE */

/* uncomment this line to get a rough feel of how long the
   plug-in takes to run */
/* #define TIMER */

typedef struct
{
  gdouble radius;
  gdouble amount;
  gint    threshold;
} UnsharpMaskParams;

typedef struct
{
  gboolean  run;
} UnsharpMaskInterface;

/* local function prototypes */
static void      query (void);
static void      run   (const gchar      *name,
                        gint              nparams,
                        const GimpParam  *param,
                        gint             *nreturn_vals,
                        GimpParam       **return_vals);

static inline void  blur_line            (gdouble       *ctable,
                                          gdouble       *cmatrix,
                                          gint           cmatrix_length,
                                          guchar        *cur_col,
                                          guchar        *dest_col,
                                          gint           y,
                                          glong          bytes);
static gint      gen_convolve_matrix     (gdouble        std_dev,
                                          gdouble      **cmatrix);
static gdouble * gen_lookup_table        (gdouble       *cmatrix,
                                          gint           cmatrix_length);
static void      unsharp_region          (GimpPixelRgn   srcPTR,
                                          GimpPixelRgn   dstPTR,
                                          gint           width,
                                          gint           height,
                                          gint           bytes,
                                          gdouble        radius,
                                          gdouble        amount,
                                          gint           x1,
                                          gint           x2,
                                          gint           y1,
                                          gint           y2);

static void      unsharp_mask            (GimpDrawable  *drawable,
                                          gdouble        radius,
                                          gdouble        amount);

static gboolean  unsharp_mask_dialog     (void);
static void      preview_update          (void);
static void      preview_toggle_callback (GtkWidget *toggle);


/* create a few globals, set default values */
static UnsharpMaskParams unsharp_params =
  {
    5.0, /* default radius = 5 */
    0.5, /* default amount = .5 */
    0    /* default threshold = 0 */
  };

/* Setting PLUG_IN_INFO */
GimpPlugInInfo PLUG_IN_INFO =
  {
    NULL,  /* init_proc  */
    NULL,  /* quit_proc  */
    query, /* query_proc */
    run,   /* run_proc   */
  };

static  GimpRunMode   run_mode;
static  GtkWidget    *preview        = NULL;   /* Preview widget */
static  gboolean      show_preview   = TRUE;   /* Should we update the preview? */
static  GimpDrawable *drawable       = NULL;   /* Current image  */
static  gint          delta_x        = 0;      /* preview x offset */
static  gint          delta_y        = 0;      /* preview y offset */

MAIN ()
     
static void
query (void)
{
  static GimpParamDef args[] =
    {
      { GIMP_PDB_INT32,    "run_mode",  "Interactive, non-interactive" },
      { GIMP_PDB_IMAGE,    "image",     "(unused)" },
      { GIMP_PDB_DRAWABLE, "drawable",  "Drawable to draw on" },
      { GIMP_PDB_FLOAT,    "radius",    "Radius of gaussian blur (in pixels > 1.0)" },
      { GIMP_PDB_FLOAT,    "amount",    "Strength of effect" },
      { GIMP_PDB_FLOAT,    "threshold", "Threshold" }
    };
  
  gimp_install_procedure ("plug_in_unsharp_mask",
                          "An unsharp mask filter",
                          "The unsharp mask is a sharpening filter that works "
                          "by comparing using the difference of the image and "
                          "a blurred version of the image.  It is commonly "
                          "used on photographic images, and is provides a much "
                          "more pleasing result than the standard sharpen "
                          "filter.",
                          "Winston Chang <winstonc@cs.wisc.edu>",
                          "Winston Chang",
                          "1999",
                          N_("_Unsharp Mask..."),
                          "GRAY*, RGB*",
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (args), 0,
                          args, NULL);
  
  gimp_plugin_menu_register ("plug_in_unsharp_mask",
                             N_("<Image>/Filters/Enhance"));
}

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  static GimpParam   values[1];
  GimpPDBStatusType  status = GIMP_PDB_SUCCESS;
#ifdef TIMER
  GTimer *timer = g_timer_new ();
#endif
  
  run_mode = param[0].data.d_int32;
  
  *return_vals  = values;
  *nreturn_vals = 1;
  
  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = status;
  
  INIT_I18N ();
  
  /*
   * Get drawable information...
   */
  drawable = gimp_drawable_get (param[2].data.d_drawable);
  gimp_tile_cache_ntiles(2 * (drawable->width / gimp_tile_width() + 1));
  
  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE:
      gimp_get_data ("plug_in_unsharp_mask", &unsharp_params);
      /* Reset default values show preview unmodified */
      
      /* initialize pixel regions and buffer */
      if (! unsharp_mask_dialog ())
        return;
      
      break;
      
    case GIMP_RUN_NONINTERACTIVE:
      if (nparams != 6)
        {
          status = GIMP_PDB_CALLING_ERROR;
        }
      else
        {
          unsharp_params.radius = param[3].data.d_float;
          unsharp_params.amount = param[4].data.d_float;
          unsharp_params.threshold = param[5].data.d_int32;

          /* make sure there are legal values */
          if ((unsharp_params.radius < 0.0) ||
              (unsharp_params.amount < 0.0))
            status = GIMP_PDB_CALLING_ERROR;
        }
      break;

    case GIMP_RUN_WITH_LAST_VALS:
      gimp_get_data ("plug_in_unsharp_mask", &unsharp_params);
      break;

    default:
      break;
    }

  if (status == GIMP_PDB_SUCCESS)
    {
      drawable = gimp_drawable_get (param[2].data.d_drawable);

      /* here we go */
      unsharp_mask (drawable, unsharp_params.radius, unsharp_params.amount);

      /* values[0].data.d_status = status; */
      gimp_displays_flush ();

      /* set data for next use of filter */
      gimp_set_data ("plug_in_unsharp_mask", &unsharp_params,
                     sizeof (UnsharpMaskParams));

      /*fprintf(stderr, "%f %f\n", unsharp_params.radius, unsharp_params.amount);*/

      gimp_drawable_detach(drawable);
      values[0].data.d_status = status;
    }

#ifdef TIMER
  g_printerr ("%f seconds\n", g_timer_elapsed (timer));
  g_timer_destroy (timer);
#endif
}

static void
unsharp_mask (GimpDrawable *drawable,
              gdouble       radius,
              gdouble       amount)
{
  GimpPixelRgn srcPR, destPR;
  glong width, height;
  glong bytes;
  gint  x1, y1, x2, y2;

  /* Get the input */
  gimp_drawable_mask_bounds (drawable->drawable_id, &x1, &y1, &x2, &y2);
  if ((run_mode != GIMP_RUN_NONINTERACTIVE))
    gimp_progress_init (_("Blurring..."));

  width = drawable->width;
  height = drawable->height;
  bytes = drawable->bpp;

  /* initialize pixel regions */
  gimp_pixel_rgn_init (&srcPR, drawable, 0, 0, width, height, FALSE, FALSE);
  gimp_pixel_rgn_init (&destPR, drawable, 0, 0, width, height, TRUE, TRUE);

  unsharp_region (srcPR, destPR, width, height, bytes, radius, amount,
                  x1, x2, y1, y2);

  gimp_drawable_flush(drawable);
  gimp_drawable_merge_shadow(drawable->drawable_id, TRUE);
  gimp_drawable_update(drawable->drawable_id, x1, y1, (x2-x1), (y2-y1));
}

/* perform an unsharp mask on the region, given a source region, dest.
   region, width and height of the regions, and corner coordinates of
   a subregion to act upon.  Everything outside the subregion is unaffected.
*/
static void
unsharp_region (GimpPixelRgn srcPR,
                GimpPixelRgn destPR,
                gint         width,
                gint         height,
                gint         bytes,
                gdouble      radius,
                gdouble      amount,
                gint         x1,
                gint         x2,
                gint         y1,
                gint         y2)
{
  guchar  *cur_col;
  guchar  *dest_col;
  guchar  *cur_row;
  guchar  *dest_row;
  gint     x;
  gint     y;
  gdouble *cmatrix = NULL;
  gint     cmatrix_length;
  gdouble *ctable;

  gint row, col;  /* these are counters for loops */

  /* these are used for the merging step */
  gint threshold;
  gint diff;
  gint value;
  gint u,v;

  /* find height and width of subregion to act on */
  x = x2-x1;
  y = y2-y1;

  /* generate convolution matrix and make sure it's smaller than each dimension */
  cmatrix_length = gen_convolve_matrix(radius, &cmatrix);
  /* generate lookup table */
  ctable = gen_lookup_table(cmatrix, cmatrix_length);

  /*  allocate row buffers  */
  cur_row  = g_new (guchar, x * bytes);
  dest_row = g_new (guchar, x * bytes);

  /* find height and width of subregion to act on */
  x = x2-x1;
  y = y2-y1;

  /* blank out a region of the destination memory area, I think */
  for (row = 0; row < y; row++)
    {
      gimp_pixel_rgn_get_row(&destPR, dest_row, x1, y1+row, (x2-x1));
      memset(dest_row, 0, x*bytes);
      gimp_pixel_rgn_set_row(&destPR, dest_row, x1, y1+row, (x2-x1));
    }

  /* blur the rows */
  for (row = 0; row < y; row++)
    {
      gimp_pixel_rgn_get_row(&srcPR, cur_row, x1, y1+row, x);
      gimp_pixel_rgn_get_row(&destPR, dest_row, x1, y1+row, x);
      blur_line(ctable, cmatrix, cmatrix_length, cur_row, dest_row, x, bytes);
      gimp_pixel_rgn_set_row(&destPR, dest_row, x1, y1+row, x);

      if (row%5 == 0)
        gimp_progress_update ((gdouble)row/(3*y));
    }

  /* allocate column buffers */
  cur_col  = g_new (guchar, y * bytes);
  dest_col = g_new (guchar, y * bytes);

  /* blur the cols */
  for (col = 0; col < x; col++)
    {
      gimp_pixel_rgn_get_col(&destPR, cur_col, x1+col, y1, y);
      gimp_pixel_rgn_get_col(&destPR, dest_col, x1+col, y1, y);
      blur_line(ctable, cmatrix, cmatrix_length, cur_col, dest_col, y, bytes);
      gimp_pixel_rgn_set_col(&destPR, dest_col, x1+col, y1, y);

      if (col%5 == 0)
        gimp_progress_update ((gdouble)col/(3*x) + 0.33);
    }

  if ((run_mode != GIMP_RUN_NONINTERACTIVE))
    gimp_progress_init (_("Merging..."));

  /* find integer value of threshold */
  threshold = unsharp_params.threshold;

  /* merge the source and destination (which currently contains
     the blurred version) images */
  for (row = 0; row < y; row++)
    {
      value = 0;
      /* get source row */
      gimp_pixel_rgn_get_row(&srcPR, cur_row, x1, y1+row, x);
      /* get dest row */
      gimp_pixel_rgn_get_row(&destPR, dest_row, x1, y1+row, x);
      /* combine the two */
      for (u = 0; u < x; u++)
        {
          for (v = 0; v < bytes; v++)
            {
              diff = (cur_row[u*bytes+v] - dest_row[u*bytes+v]);
              /* do tresholding */
              if (abs (2 * diff) < threshold)
                diff = 0;

              value = cur_row[u*bytes+v] + amount * diff;

              if (value < 0) dest_row[u*bytes+v] =0;
              else if (value > 255) dest_row[u*bytes+v] = 255;
              else  dest_row[u*bytes+v] = value;
            }
        }
      /* update progress bar every five rows */
      if (row%5 == 0)
        gimp_progress_update ((gdouble)row/(3*y) + 0.67);

      gimp_pixel_rgn_set_row(&destPR, dest_row, x1, y1+row, x);
    }

  /* free the memory we took */
  g_free(cur_row);
  g_free(dest_row);
  g_free(cur_col);
  g_free(dest_col);
  g_free(cmatrix);
  g_free(ctable);

}

/* this function is written as if it is blurring a column at a time,
   even though it can operate on rows, too.  There is no difference
   in the processing of the lines, at least to the blur_line function. */
static inline void
blur_line (gdouble *ctable,
           gdouble *cmatrix,
           gint     cmatrix_length,
           guchar  *cur_col,
           guchar  *dest_col,
           gint     y,
           glong    bytes)
{
  gdouble scale;
  gdouble sum;
  gint i=0, j=0;
  gint row;
  gint cmatrix_middle = cmatrix_length/2;

  gdouble *cmatrix_p;
  guchar  *cur_col_p;
  guchar  *cur_col_p1;
  guchar  *dest_col_p;
  gdouble *ctable_p;

  /* this first block is the same as the non-optimized version --
   * it is only used for very small pictures, so speed isn't a
   * big concern.
   */
  if (cmatrix_length > y)
    {
      for (row = 0; row < y ; row++)
        {
          scale=0;
          /* find the scale factor */
          for (j = 0; j < y ; j++)
            {
              /* if the index is in bounds, add it to the scale counter */
              if ((j + cmatrix_length/2 - row >= 0) &&
                  (j + cmatrix_length/2 - row < cmatrix_length))
                scale += cmatrix[j + cmatrix_length/2 - row];
            }
          for (i = 0; i<bytes; i++)
            {
              sum = 0;
              for (j = 0; j < y; j++)
                {
                  if ((j >= row - cmatrix_length/2) &&
                      (j <= row + cmatrix_length/2))
                    sum += cur_col[j*bytes + i] * cmatrix[j];
                }
              dest_col[row*bytes + i] = (guchar) ROUND (sum / scale);
            }
        }
    }
  else
    {
      /* for the edge condition, we only use available info and scale to one */
      for (row = 0; row < cmatrix_middle; row++)
        {
          /* find scale factor */
          scale=0;
          for (j = cmatrix_middle - row; j<cmatrix_length; j++)
            scale += cmatrix[j];
          for (i = 0; i<bytes; i++)
            {
              sum = 0;
              for (j = cmatrix_middle - row; j<cmatrix_length; j++)
                {
                  sum += cur_col[(row + j-cmatrix_middle)*bytes + i] * cmatrix[j];
                }
              dest_col[row*bytes + i] = (guchar) ROUND (sum / scale);
            }
        }
      /* go through each pixel in each col */
      dest_col_p = dest_col + row*bytes;
      for (; row < y-cmatrix_middle; row++)
        {
          cur_col_p = (row - cmatrix_middle) * bytes + cur_col;
          for (i = 0; i<bytes; i++)
            {
              sum = 0;
              cmatrix_p = cmatrix;
              cur_col_p1 = cur_col_p;
              ctable_p = ctable;
              for (j = cmatrix_length; j>0; j--)
                {
                  sum += *(ctable_p + *cur_col_p1);
                  cur_col_p1 += bytes;
                  ctable_p += 256;
                }
              cur_col_p++;
              *(dest_col_p++) = ROUND (sum);
            }
        }

      /* for the edge condition , we only use available info, and scale to one */
      for (; row < y; row++)
        {
          /* find scale factor */
          scale=0;
          for (j = 0; j< y-row + cmatrix_middle; j++)
            scale += cmatrix[j];
          for (i = 0; i<bytes; i++)
            {
              sum = 0;
              for (j = 0; j<y-row + cmatrix_middle; j++)
                {
                  sum += cur_col[(row + j-cmatrix_middle)*bytes + i] * cmatrix[j];
                }
              dest_col[row*bytes + i] = (guchar) ROUND (sum / scale);
            }
        }
    }
}

/* generates a 1-D convolution matrix to be used for each pass of
 * a two-pass gaussian blur.  Returns the length of the matrix.
 */
static gint
gen_convolve_matrix (gdouble   radius,
                     gdouble **cmatrix_p)
{
  gint matrix_length;
  gint matrix_midpoint;
  gdouble* cmatrix;
  gint i,j;
  gdouble std_dev;
  gdouble sum;

  /* we want to generate a matrix that goes out a certain radius
   * from the center, so we have to go out ceil(rad-0.5) pixels,
   * inlcuding the center pixel.  Of course, that's only in one direction,
   * so we have to go the same amount in the other direction, but not count
   * the center pixel again.  So we double the previous result and subtract
   * one.
   * The radius parameter that is passed to this function is used as
   * the standard deviation, and the radius of effect is the
   * standard deviation * 2.  It's a little confusing.
   */
  radius = fabs(radius) + 1.0;

  std_dev = radius;
  radius = std_dev * 2;

  /* go out 'radius' in each direction */
  matrix_length = 2 * ceil(radius-0.5) + 1;
  if (matrix_length <= 0) matrix_length = 1;
  matrix_midpoint = matrix_length/2 + 1;
  *cmatrix_p = g_new (gdouble, matrix_length);
  cmatrix = *cmatrix_p;

  /*  Now we fill the matrix by doing a numeric integration approximation
   * from -2*std_dev to 2*std_dev, sampling 50 points per pixel.
   * We do the bottom half, mirror it to the top half, then compute the
   * center point.  Otherwise asymmetric quantization errors will occur.
   *  The formula to integrate is e^-(x^2/2s^2).
   */

  /* first we do the top (right) half of matrix */
  for (i = matrix_length/2 + 1; i < matrix_length; i++)
    {
      double base_x = i - floor(matrix_length/2) - 0.5;
      sum = 0;
      for (j = 1; j <= 50; j++)
        {
          if ( base_x+0.02*j <= radius )
            sum += exp (-(base_x+0.02*j)*(base_x+0.02*j) /
                        (2*std_dev*std_dev));
        }
      cmatrix[i] = sum/50;
    }

  /* mirror the thing to the bottom half */
  for (i=0; i<=matrix_length/2; i++) {
    cmatrix[i] = cmatrix[matrix_length-1-i];
  }

  /* find center val -- calculate an odd number of quanta to make it symmetric,
   * even if the center point is weighted slightly higher than others. */
  sum = 0;
  for (j=0; j<=50; j++)
    {
      sum += exp (-(0.5+0.02*j)*(0.5+0.02*j) /
                  (2*std_dev*std_dev));
    }
  cmatrix[matrix_length/2] = sum/51;

  /* normalize the distribution by scaling the total sum to one */
  sum=0;
  for (i=0; i<matrix_length; i++) sum += cmatrix[i];
  for (i=0; i<matrix_length; i++) cmatrix[i] = cmatrix[i] / sum;

  return matrix_length;
}

/* ----------------------- gen_lookup_table ----------------------- */
/* generates a lookup table for every possible product of 0-255 and
   each value in the convolution matrix.  The returned array is
   indexed first by matrix position, then by input multiplicand (?)
   value.
*/
static gdouble *
gen_lookup_table (gdouble *cmatrix,
                  gint     cmatrix_length)
{
  int i, j;
  gdouble* lookup_table = g_new (gdouble, cmatrix_length * 256);
  gdouble* lookup_table_p = lookup_table;
  gdouble* cmatrix_p      = cmatrix;

  for (i=0; i<cmatrix_length; i++)
    {
      for (j=0; j<256; j++)
        {
          *(lookup_table_p++) = *cmatrix_p * (gdouble)j;
        }
      cmatrix_p++;
    }

  return lookup_table;
}

static GtkWidget *
unsharp_preview_new (void)
{
  GtkWidget *table;
  GtkWidget *frame;
  GtkObject *adj;
  GtkWidget *scrollbar;
  GtkWidget *preview_toggle;
  
  gint       sel_width;
  gint       sel_height;
  gint       x1, y1, x2, y2;
  gint       preview_width;
  gint       preview_height;

  gimp_drawable_mask_bounds (drawable->drawable_id, &x1, &y1, &x2, &y2);
  sel_width     = x2 - x1;
  sel_height    = y2 - y1;
  preview_width  = MIN (sel_width, PREVIEW_SIZE);
  preview_height = MIN (sel_height, PREVIEW_SIZE);

  table = gtk_table_new (3, 2, FALSE);

  frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
  gtk_table_attach (GTK_TABLE (table), frame, 0, 1, 0, 1, 0, 0, 0, 0);
  gtk_widget_show (frame);

  if (gimp_drawable_type (drawable->drawable_id) == GIMP_GRAY_IMAGE ||
      gimp_drawable_type (drawable->drawable_id) == GIMP_GRAYA_IMAGE)
    preview = gtk_preview_new (GTK_PREVIEW_GRAYSCALE);
  else
    preview = gtk_preview_new (GTK_PREVIEW_COLOR);
  gtk_preview_size (GTK_PREVIEW (preview), preview_width, preview_height);
  gtk_container_add (GTK_CONTAINER (frame), preview);
  gtk_widget_show (preview);

  adj = gtk_adjustment_new (0, 0, sel_width - 1, 1.0,
                            MIN (preview_width, sel_width),
                            MIN (preview_width, sel_width));

  g_signal_connect (adj, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &delta_x);
  g_signal_connect (adj, "value_changed",
                    G_CALLBACK (preview_update),
                    NULL);
  scrollbar = gtk_hscrollbar_new (GTK_ADJUSTMENT (adj));
  gtk_range_set_update_policy (GTK_RANGE (scrollbar),
                               GTK_UPDATE_DISCONTINUOUS);
  gtk_table_attach (GTK_TABLE (table), scrollbar, 0, 1, 1, 2,
                    GTK_FILL, 0, 0, 0);
  gtk_widget_show (scrollbar);

  adj = gtk_adjustment_new (0, 0, sel_height - 1, 1.0,
                            MIN (preview_height, sel_height),
                            MIN (preview_height, sel_height));

  g_signal_connect (adj, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &delta_y);
  g_signal_connect (adj, "value_changed",
                    G_CALLBACK (preview_update),
                    NULL);

  scrollbar = gtk_vscrollbar_new (GTK_ADJUSTMENT (adj));
  gtk_range_set_update_policy (GTK_RANGE (scrollbar),
                               GTK_UPDATE_DISCONTINUOUS);
  gtk_table_attach (GTK_TABLE (table), scrollbar, 1, 2, 0, 1,
                    0, GTK_FILL, 0, 0);
  gtk_widget_show (scrollbar);

  preview_toggle = gtk_check_button_new_with_mnemonic (_("_Preview"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (preview_toggle),
                                show_preview);
  gtk_table_attach (GTK_TABLE (table), preview_toggle,
                    0, 1, 2, 3, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (preview_toggle);
  g_signal_connect (preview_toggle, "toggled",
                    G_CALLBACK (preview_toggle_callback), NULL);

  return table;
}

static gboolean
unsharp_mask_dialog (void)
{
  GtkWidget *dialog;
  GtkWidget *table;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *scrollbar;
  GtkObject *adj;
  gboolean   run;

  gimp_ui_init ("unsharp", TRUE);

  dialog = gimp_dialog_new (_("Unsharp Mask"), "unsharp",
                            NULL, 0,
                            gimp_standard_help_func, "plug-in-unsharp-mask",

                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                            GTK_STOCK_OK,     GTK_RESPONSE_OK,

                            NULL);

  vbox = gtk_vbox_new (FALSE, 12);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), vbox,
                      FALSE, FALSE, 0);
  gtk_widget_show (vbox);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  table = unsharp_preview_new ();
  gtk_box_pack_start (GTK_BOX (hbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  table = gtk_table_new (3, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 6);
  gtk_table_set_row_spacings (GTK_TABLE (table), 6);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, 0,
                              _("_Radius:"), SCALE_WIDTH, ENTRY_WIDTH,
                              unsharp_params.radius, 0.1, 120.0, 0.1, 1.0, 1,
                              TRUE, 0, 0,
                              NULL, NULL);

  g_signal_connect (adj, "value_changed",
                    G_CALLBACK (gimp_double_adjustment_update),
                    &unsharp_params.radius);
  g_signal_connect (adj, "value_changed",
                    G_CALLBACK (preview_update),
                    NULL);

  scrollbar = GIMP_SCALE_ENTRY_SCALE (adj);
  gtk_range_set_update_policy (GTK_RANGE (scrollbar),
                               GTK_UPDATE_DISCONTINUOUS);

  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, 1,
                              _("_Amount:"), SCALE_WIDTH, ENTRY_WIDTH,
                              unsharp_params.amount, 0.0, 5.0, 0.01, 0.1, 2,
                              TRUE, 0, 0,
                              NULL, NULL);
  scrollbar = GIMP_SCALE_ENTRY_SCALE(adj);
  gtk_range_set_update_policy (GTK_RANGE (scrollbar), GTK_UPDATE_DISCONTINUOUS);


  g_signal_connect (adj, "value_changed",
                    G_CALLBACK (gimp_double_adjustment_update),
                    &unsharp_params.amount);
  g_signal_connect (adj, "value_changed",
                    G_CALLBACK (preview_update),
                    NULL);

  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, 2,
                              _("_Threshold:"), SCALE_WIDTH, ENTRY_WIDTH,
                              unsharp_params.threshold, 0.0, 255.0, 1.0, 10.0, 0,
                              TRUE, 0, 0,
                              NULL, NULL);
  scrollbar = GIMP_SCALE_ENTRY_SCALE(adj);
  gtk_range_set_update_policy (GTK_RANGE (scrollbar), GTK_UPDATE_DISCONTINUOUS);

  g_signal_connect (adj, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &unsharp_params.threshold);
  g_signal_connect_after (adj, "value_changed",
                          G_CALLBACK (preview_update),
                          NULL);



  gtk_widget_show (dialog);

  preview_update ();

  run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

  gtk_widget_destroy (dialog);

  return run;
}

/*  preview functions  */
static void
preview_toggle_callback (GtkWidget *toggle)
{
   if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (toggle)))
    {
      show_preview = TRUE;
      preview_update ();
    }
  else
    show_preview = FALSE;
}

static void
preview_update (void)
{
  /* drawable */
  glong width, height;
  glong bytes;
  gint  x1, y1, x2, y2;

  /* preview */
  guchar    *render_buffer = NULL; /* Buffer to hold rendered image */
  guchar    *row_buffer    = NULL;
  gint       preview_width;        /* Width of preview widget */
  gint       preview_height;       /* Height of preview widget */
  gint       preview_x1;           /* Upper-left X of preview */
  gint       preview_y1;           /* Upper-left Y of preview */
  gint       preview_x2;           /* Lower-right X of preview */
  gint       preview_y2;           /* Lower-right Y of preview */
  /* preview buffer */
  gint       preview_buf_width;    /* Width of preview widget */
  gint       preview_buf_height;   /* Height of preview widget */
  gint       preview_buf_x1;       /* Upper-left X of preview */
  gint       preview_buf_y1;       /* Upper-left Y of preview */
  gint       preview_buf_x2;       /* Lower-right X of preview */
  gint       preview_buf_y2;       /* Lower-right Y of preview */

  const guchar check_light = GIMP_CHECK_LIGHT * 255;
  const guchar check_dark  = GIMP_CHECK_DARK  * 255;
  guchar       check;

  GimpPixelRgn srcPR, destPR;      /* Pixel regions */
  gint         x, y;               /* Current location in image */

  gint row,buf_y, offset;          /* Preview loop control      */

  if (!show_preview)
    return;

  /* Get drawable info */
  gimp_drawable_mask_bounds (drawable->drawable_id, &x1, &y1, &x2, &y2);
  width  = drawable->width;
  height = drawable->height;
  bytes  = drawable->bpp;

  /*
   * Setup for filter...
   */
  preview_x1     = x1 + delta_x;
  preview_y1     = y1 + delta_y;
  preview_x2     = preview_x1 + MIN (PREVIEW_SIZE, x2 - x1);
  preview_y2     = preview_y1 + MIN (PREVIEW_SIZE, y2 - y1);
  preview_width  = preview_x2 - preview_x1;
  preview_height = preview_y2 - preview_y1;

  /* Make buffer large enough to minimize disturbence */
  preview_buf_x1     = MAX (0, preview_x1 - unsharp_params.radius);
  preview_buf_y1     = MAX (0, preview_y1 - unsharp_params.radius);
  preview_buf_x2     = MIN (x2, preview_x2 + unsharp_params.radius);
  preview_buf_y2     = MIN (y2, preview_y2 + unsharp_params.radius);
  preview_buf_width  = preview_buf_x2 - preview_buf_x1;
  preview_buf_height = preview_buf_y2 - preview_buf_y1;

  /* If radius/amount/treshold changed render*/
  if ((run_mode != GIMP_RUN_NONINTERACTIVE))
    gimp_progress_init (_("Blurring..."));
  /* initialize pixel regions */
  gimp_pixel_rgn_init (&srcPR, drawable,
                       preview_buf_x1,preview_buf_y1,
                       preview_buf_width, preview_buf_height, FALSE, FALSE);
  gimp_pixel_rgn_init (&destPR, drawable,
                       preview_buf_x1,preview_buf_y1,
                       preview_buf_width, preview_buf_height,  TRUE, TRUE);

  /* render image */
  unsharp_region (srcPR, destPR, preview_buf_width, preview_buf_height, bytes,
                  unsharp_params.radius, unsharp_params.amount,
                  preview_buf_x1, preview_buf_x2, preview_buf_y1, preview_buf_y2);

  render_buffer = g_new (guchar, preview_buf_width * preview_buf_height * bytes);

  gimp_pixel_rgn_get_rect(&destPR, render_buffer,
                          preview_buf_x1, preview_buf_y1,
                          preview_buf_width, preview_buf_height);

  /*
   * Draw the preview image on the screen...
   */
  y     = preview_y1 - preview_buf_y1;
  x     = preview_x1 - preview_buf_x1;
  row   = 0;
  buf_y = y + preview_height;

  if (gimp_drawable_type (drawable->drawable_id) == GIMP_GRAY_IMAGE ||
      gimp_drawable_type (drawable->drawable_id) == GIMP_GRAYA_IMAGE)
    row_buffer = g_new (guchar, preview_width);
  else
    row_buffer = g_new (guchar, preview_width * 3);

  for ( ; y < buf_y ; y++ )
    {
      offset = (x * bytes) + (y * preview_buf_width * bytes);

      switch (gimp_drawable_type (drawable->drawable_id))
        {
        case GIMP_GRAY_IMAGE:
          memcpy (row_buffer, render_buffer + offset, preview_width);
          break;

        case GIMP_GRAYA_IMAGE:
          {
            gint    j;
            guchar *rowptr = row_buffer;
            guchar *bufptr = render_buffer + offset;

            for (j = 0; j < preview_width; j++)
              {
                if (bufptr[1] == 255)
                  *rowptr = bufptr[0];
                else
                  {
                    if ((row & GIMP_CHECK_SIZE) ^ (j & GIMP_CHECK_SIZE))
                      check = check_light;
                    else
                      check = check_dark;

                    if (bufptr[1] == 0)
                      *rowptr = check;
                    else
                      *rowptr = check + (((gint) (bufptr[0] - check) * bufptr[1]) >> 8);
                  }

                rowptr ++;
                bufptr += 2;
              }
          }
          break;

        default:
        case GIMP_RGB_IMAGE:
          memcpy (row_buffer, render_buffer + offset, preview_width * 3);
          break;

        case GIMP_RGBA_IMAGE:
          {
            gint    j;
            guchar *rowptr = row_buffer;
            guchar *bufptr = render_buffer + offset;

            for (j = 0; j < preview_width; j++)
              {
                if (bufptr[3] == 255)
                  {
                    rowptr[0] = bufptr[0];
                    rowptr[1] = bufptr[1];
                    rowptr[2] = bufptr[2];
                  }
                else
                  {
                    if ((row & GIMP_CHECK_SIZE) ^ (j & GIMP_CHECK_SIZE))
                      check = check_light;
                    else
                      check = check_dark;

                    if (bufptr[3] == 0)
                      {
                        rowptr[0] = check;
                        rowptr[1] = check;
                        rowptr[2] = check;
                      }
                    else
                      {
                        rowptr[0] = check + (((gint) (bufptr[0] - check) * bufptr[3]) >> 8);
                        rowptr[1] = check + (((gint) (bufptr[1] - check) * bufptr[3]) >> 8);
                        rowptr[2] = check + (((gint) (bufptr[2] - check) * bufptr[3]) >> 8);
                      }
                  }

                rowptr += 3;
                bufptr += 4;
              }
          }
          break;
        }

      gtk_preview_draw_row (GTK_PREVIEW (preview), row_buffer, 0,
                            row++, preview_width);
    }

  gtk_widget_queue_draw (preview);

  g_free (row_buffer);
  g_free (render_buffer);
}

