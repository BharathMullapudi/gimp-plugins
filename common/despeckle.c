/*
 * "$Id$"
 *
 *   Despeckle (adaptive median) filter for The GIMP -- an image manipulation
 *   program
 *
 *   Copyright 1997-1998 Michael Sweet (mike@easysw.com)
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __GNUC__
#warning GTK_DISABLE_DEPRECATED
#endif
#undef GTK_DISABLE_DEPRECATED

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


/*
 * Constants...
 */

#define PLUG_IN_NAME     "plug_in_despeckle"
#define PLUG_IN_VERSION  "1.3.2 - 17 May 1998"
#define HELP_ID          "plug-in-despeckle"
#define PREVIEW_SIZE     128
#define SCALE_WIDTH      100
#define ENTRY_WIDTH        3
#define MAX_RADIUS        20

#define FILTER_ADAPTIVE  0x01
#define FILTER_RECURSIVE 0x02

#define despeckle_radius (despeckle_vals[0])    /* Radius of filter */
#define filter_type      (despeckle_vals[1])    /* Type of filter */
#define black_level      (despeckle_vals[2])    /* Black level */
#define white_level      (despeckle_vals[3])    /* White level */

/*
 * Local functions...
 */

static void      query (void);
static void      run   (const gchar      *name,
                        gint              nparams,
                        const GimpParam  *param,
                        gint             *nreturn_vals,
                        GimpParam       **return_vals);

static void      despeckle (void);

static gboolean  despeckle_dialog          (void);

static void      dialog_iscale_update      (GtkAdjustment *, gint *);
static void      dialog_adaptive_callback  (GtkWidget *, gpointer);
static void      dialog_recursive_callback (GtkWidget *, gpointer);

static void      preview_init              (void);
static void      preview_exit              (void);
static void      preview_update            (void);
static void      preview_scroll_callback   (void);


/*
 * Globals...
 */

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init  */
  NULL,  /* quit  */
  query, /* query */
  run    /* run   */
};

static GtkWidget      *preview;                 /* Preview widget */
static gint            preview_width,           /* Width of preview widget */
                       preview_height,          /* Height of preview widget */
                       preview_x1,              /* Upper-left X of preview */
                       preview_y1,              /* Upper-left Y of preview */
                       preview_x2,              /* Lower-right X of preview */
                       preview_y2;              /* Lower-right Y of preview */
static guchar         *preview_src = NULL,      /* Source pixel rows */
                      *preview_dst,             /* Destination pixel row */
                      *preview_sort;            /* Pixel value sort array */
static GtkObject      *hscroll_data,            /* Horizontal scrollbar data */
                      *vscroll_data;            /* Vertical scrollbar data */

static GimpDrawable   *drawable = NULL;         /* Current image */
static gint            sel_x1,                  /* Selection bounds */
                       sel_y1,
                       sel_x2,
                       sel_y2;
static gint            sel_width,               /* Selection width */
                       sel_height;              /* Selection height */
static gint            img_bpp;                 /* Bytes-per-pixel in image */

static gint despeckle_vals[4] =
{
  3,
  FILTER_ADAPTIVE,
  7,
  248
};


/*
 * 'main()' - Main entry - just call gimp_main()...
 */

MAIN ()

/*
 * 'query()' - Respond to a plug-in query...
 */

static void
query (void)
{
  static GimpParamDef   args[] =
  {
    { GIMP_PDB_INT32,    "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE,    "image",    "Input image" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable" },
    { GIMP_PDB_INT32,    "radius",   "Filter box radius (default = 3)" },
    { GIMP_PDB_INT32,    "type",     "Filter type (0 = median, 1 = adaptive, 2 = recursive-median, 3 = recursive-adaptive)" },
    { GIMP_PDB_INT32,    "black",    "Black level (-1 to 255)" },
    { GIMP_PDB_INT32,    "white",    "White level (0 to 256)" }
  };

  gimp_install_procedure (PLUG_IN_NAME,
                          "Despeckle filter, typically used to \'despeckle\' "
                          "a photographic image.",
                          "This plug-in selectively performs a median or "
                          "adaptive box filter on an image.",
                          "Michael Sweet <mike@easysw.com>",
                          "Copyright 1997-1998 by Michael Sweet",
                          PLUG_IN_VERSION,
                          N_("Des_peckle..."),
                          "RGB*, GRAY*",
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (args), 0,
                          args, NULL);

  gimp_plugin_menu_register (PLUG_IN_NAME,
                             N_("<Image>/Filters/Enhance"));
}


/*
 * 'run()' - Run the filter...
 */

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  GimpRunMode        run_mode;
  GimpPDBStatusType  status;
  GimpParam          *values;

  INIT_I18N ();

  /*
   * Initialize parameter data...
   */

  status   = GIMP_PDB_SUCCESS;
  run_mode = param[0].data.d_int32;

  values = g_new (GimpParam, 1);

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  *nreturn_vals = 1;
  *return_vals  = values;

  /*
   * Get drawable information...
   */

  drawable = gimp_drawable_get (param[2].data.d_drawable);

  gimp_drawable_mask_bounds (drawable->drawable_id,
                             &sel_x1, &sel_y1, &sel_x2, &sel_y2);

  sel_width  = sel_x2 - sel_x1;
  sel_height = sel_y2 - sel_y1;
  img_bpp    = gimp_drawable_bpp (drawable->drawable_id);

  /*
   * See how we will run
   */

  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE :
      /*
       * Possibly retrieve data...
       */

      gimp_get_data (PLUG_IN_NAME, &despeckle_radius);

      /*
       * Get information from the dialog...
       */

      if (!despeckle_dialog ())
        return;
      break;

    case GIMP_RUN_NONINTERACTIVE:
      /*
       * Make sure all the arguments are present...
       */

      if (nparams < 4 || nparams > 7)
        status = GIMP_PDB_CALLING_ERROR;
      else if (nparams == 4)
        {
          despeckle_radius = param[3].data.d_int32;
          filter_type      = FILTER_ADAPTIVE;
          black_level      = 7;
          white_level      = 248;
        }
      else if (nparams == 5)
        {
          despeckle_radius = param[3].data.d_int32;
          filter_type      = param[4].data.d_int32;
          black_level      = 7;
          white_level      = 248;
        }
      else if (nparams == 6)
        {
          despeckle_radius = param[3].data.d_int32;
          filter_type      = param[4].data.d_int32;
          black_level      = param[5].data.d_int32;
          white_level      = 248;
        }
      else
        {
          despeckle_radius = param[3].data.d_int32;
          filter_type      = param[4].data.d_int32;
          black_level      = param[5].data.d_int32;
          white_level      = param[6].data.d_int32;
        };
      break;

    case GIMP_RUN_WITH_LAST_VALS:
      /*
       * Possibly retrieve data...
       */

      INIT_I18N();
      gimp_get_data (PLUG_IN_NAME, despeckle_vals);
        break;
        
    default:
      status = GIMP_PDB_CALLING_ERROR;
      break;
    };

  /*
   * Despeckle the image...
   */

  if (status == GIMP_PDB_SUCCESS)
    {
      if ((gimp_drawable_is_rgb(drawable->drawable_id) ||
           gimp_drawable_is_gray(drawable->drawable_id)))
        {
          /*
           * Set the tile cache size...
           */

          gimp_tile_cache_ntiles (2 * (drawable->width + gimp_tile_width() - 1) /
                                  gimp_tile_width() + 1);

          /*
           * Run!
           */

          despeckle ();

          /*
           * If run mode is interactive, flush displays...
           */

          if (run_mode != GIMP_RUN_NONINTERACTIVE)
            gimp_displays_flush ();

          /*
           * Store data...
           */

          if (run_mode == GIMP_RUN_INTERACTIVE)
            gimp_set_data (PLUG_IN_NAME,
                           despeckle_vals, sizeof (despeckle_vals));
        }
      else
        status = GIMP_PDB_EXECUTION_ERROR;
    };

  /*
   * Reset the current run status...
   */

  values[0].data.d_status = status;

  /*
   * Detach from the drawable...
   */

  gimp_drawable_detach (drawable);
}


/*
 * 'despeckle()' - Despeckle an image using a median filter.
 *
 * A median filter basically collects pixel values in a region around the
 * target pixel, sorts them, and uses the median value. This code uses a
 * circular row buffer to improve performance.
 *
 * The adaptive filter is based on the median filter but analizes the histogram
 * of the region around the target pixel and adjusts the despeckle radius
 * accordingly.
 */

static void
despeckle (void)
{
  GimpPixelRgn  src_rgn,        /* Source image region */
                dst_rgn;        /* Destination image region */
  guchar      **src_rows,       /* Source pixel rows */
               *dst_row,        /* Destination pixel row */
               *src_ptr,        /* Source pixel pointer */
               *sort,           /* Pixel value sort array */
               *sort_ptr;       /* Current sort value */
  gint          sort_count,     /* Number of soft values */
                i, j, t, d,     /* Looping vars */
                x, y,           /* Current location in image */
                row,            /* Current row in src_rows */
                rowcount,       /* Number of rows loaded */
                lasty,          /* Last row loaded in src_rows */
                trow,           /* Looping var */
                startrow,       /* Starting row for loop */
                endrow,         /* Ending row for loop */
                max_row,        /* Maximum number of filled src_rows */
                size,           /* Width/height of the filter box */
                width,          /* Byte width of the image */
                xmin, xmax, tx, /* Looping vars */
                radius,         /* Current radius */
                hist0,          /* Histogram count for 0 values */
                hist255;        /* Histogram count for 255 values */

  /*
   * Let the user know what we're doing...
   */

  gimp_progress_init (_("Despeckling..."));

  /*
   * Setup for filter...
   */

  gimp_pixel_rgn_init (&src_rgn, drawable, sel_x1, sel_y1, sel_width, sel_height,
                       FALSE, FALSE);
  gimp_pixel_rgn_init (&dst_rgn, drawable, sel_x1, sel_y1, sel_width, sel_height,
                       TRUE, TRUE);

  size     = despeckle_radius * 2 + 1;
  max_row  = 2 * gimp_tile_height ();
  width    = sel_width * img_bpp;

  src_rows    = g_new (guchar *, max_row);
  src_rows[0] = g_new (guchar, max_row * width);

  for (row = 1; row < max_row; row ++)
    src_rows[row] = src_rows[0] + row * width;

  dst_row = g_new (guchar, width),
  sort    = g_new (guchar, size * size);

  /*
   * Pre-load the first "size" rows for the filter...
   */

  if (sel_height < gimp_tile_height())
    rowcount = sel_height;
  else
    rowcount = gimp_tile_height ();

  gimp_pixel_rgn_get_rect (&src_rgn, src_rows[0], sel_x1, sel_y1, sel_width,
                           rowcount);

  row   = rowcount;
  lasty = sel_y1 + rowcount;

  /*
   * Despeckle...
   */

  for (y = sel_y1 ; y < sel_y2; y ++)
    {
      if ((y + despeckle_radius) >= lasty &&
          lasty < sel_y2)
        {
          /*
           * Load the next block of rows...
           */

          rowcount -= gimp_tile_height();
          if ((i = sel_y2 - lasty) > gimp_tile_height())
            i = gimp_tile_height();

          gimp_pixel_rgn_get_rect (&src_rgn, src_rows[row],
                                   sel_x1, lasty, sel_width, i);

          rowcount += i;
          lasty    += i;
          row      = (row + i) % max_row;
        };

      /*
       * Now find the median pixels and save the results...
       */

      radius = despeckle_radius;

      memcpy (dst_row, src_rows[(row + y - lasty + max_row) % max_row], width);

      if (y >= (sel_y1 + radius) && y < (sel_y2 - radius))
        {
          for (x = 0; x < width; x ++)
            {
              hist0   = 0;
              hist255 = 0;
              xmin    = x - radius * img_bpp;
              xmax    = x + (radius + 1) * img_bpp;

              if (xmin < 0)
                xmin = x % img_bpp;

              if (xmax > width)
                xmax = width;

              startrow = (row + y - lasty - radius + max_row) % max_row;
              endrow   = (row + y - lasty + radius + 1 + max_row) % max_row;

              for (sort_ptr = sort, trow = startrow;
                   trow != endrow;
                   trow = (trow + 1) % max_row)
                for (tx = xmin, src_ptr = src_rows[trow] + xmin;
                     tx < xmax;
                     tx += img_bpp, src_ptr += img_bpp)
                  {
                    if ((*sort_ptr = *src_ptr) <= black_level)
                      hist0 ++;
                    else if (*sort_ptr >= white_level)
                      hist255 ++;

                    if (*sort_ptr < white_level && *sort_ptr > black_level)
                      sort_ptr ++;
                  };

              /*
               * Shell sort the color values...
               */

              sort_count = sort_ptr - sort;

              if (sort_count > 1)
                {
                  for (d = sort_count / 2; d > 0; d = d / 2)
                    for (i = d; i < sort_count; i ++)
                      for (j = i - d, sort_ptr = sort + j;
                           j >= 0 && sort_ptr[0] > sort_ptr[d];
                           j -= d, sort_ptr -= d)
                        {
                          t           = sort_ptr[0];
                          sort_ptr[0] = sort_ptr[d];
                          sort_ptr[d] = t;
                        };

                  /*
                   * Assign the median value...
                   */

                  t = sort_count / 2;

                  if (sort_count & 1)
                    dst_row[x] = (sort[t] + sort[t + 1]) / 2;
                  else
                    dst_row[x] = sort[t];

                  /*
                   * Save the change to the source image too if the user
                   * wants the recursive method...
                   */

                  if (filter_type & FILTER_RECURSIVE)
                    src_rows[(row + y - lasty + max_row) % max_row][x] =
                      dst_row[x];
                };

              /*
               * Check the histogram and adjust the radius accordingly...
               */

              if (filter_type & FILTER_ADAPTIVE)
                {
                  if (hist0 >= radius || hist255 >= radius)
                    {
                      if (radius < despeckle_radius)
                        radius ++;
                    }
                  else if (radius > 1)
                    radius --;
                };
            };
        };

      gimp_pixel_rgn_set_row (&dst_rgn, dst_row, sel_x1, y, sel_width);

      if ((y & 15) == 0)
        gimp_progress_update ((double) (y - sel_y1) / (double) sel_height);
    };

  /*
   * OK, we're done.  Free all memory used...
   */

  g_free (src_rows[0]);
  g_free (src_rows);
  g_free (dst_row);
  g_free (sort);

  /*
   * Update the screen...
   */

  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
  gimp_drawable_update (drawable->drawable_id,
                        sel_x1, sel_y1, sel_width, sel_height);
}


/*
 * 'despeckle_dialog()' - Popup a dialog window for the filter box size...
 */

static gint
despeckle_dialog (void)
{
  GtkWidget *dialog;
  GtkWidget *main_vbox;
  GtkWidget *hbox;
  GtkWidget *vbox;
  GtkWidget *table;
  GtkWidget *ptable;
  GtkWidget *frame;
  GtkWidget *scrollbar;
  GtkWidget *button;
  GtkObject *adj;
  gboolean   run;

  gimp_ui_init ("despeckle", TRUE);

  dialog = gimp_dialog_new (_("Despeckle"), "despeckle",
                            NULL, 0,
                            gimp_standard_help_func, HELP_ID,

                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                            GTK_STOCK_OK,     GTK_RESPONSE_OK,

                            NULL);

  main_vbox = gtk_vbox_new (FALSE, 12);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 12);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), main_vbox,
                      TRUE, TRUE, 0);
  gtk_widget_show (main_vbox);

  hbox = gtk_hbox_new (FALSE, 12);
  gtk_box_pack_start (GTK_BOX (main_vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  /*
   * Preview window...
   */

  ptable = gtk_table_new (2, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (hbox), ptable, FALSE, FALSE, 0);
  gtk_widget_show(ptable);

  frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
  gtk_table_attach (GTK_TABLE (ptable), frame, 0, 1, 0, 1, 0, 0, 0, 0);
  gtk_widget_show (frame);

  preview_width  = MIN (sel_width, PREVIEW_SIZE);
  preview_height = MIN (sel_height, PREVIEW_SIZE);

  preview = gimp_preview_area_new ();
  gtk_widget_set_size_request (preview, preview_width, preview_height);
  gtk_container_add (GTK_CONTAINER (frame), preview);
  gtk_widget_show (preview);

  hscroll_data = gtk_adjustment_new (0, 0, sel_width - 1, 1.0,
                                     MIN (preview_width, sel_width),
                                     MIN (preview_width, sel_width));

  g_signal_connect (hscroll_data, "value_changed",
                    G_CALLBACK (preview_scroll_callback),
                    NULL);

  scrollbar = gtk_hscrollbar_new (GTK_ADJUSTMENT (hscroll_data));
  gtk_range_set_update_policy (GTK_RANGE (scrollbar), GTK_UPDATE_CONTINUOUS);
  gtk_table_attach (GTK_TABLE (ptable), scrollbar,
                    0, 1, 1, 2, GTK_FILL, 0, 0, 0);
  gtk_widget_show (scrollbar);

  vscroll_data = gtk_adjustment_new (0, 0, sel_height - 1, 1.0,
                                     MIN (preview_height, sel_height),
                                     MIN (preview_height, sel_height));

  g_signal_connect (vscroll_data, "value_changed",
                    G_CALLBACK (preview_scroll_callback),
                    NULL);

  scrollbar = gtk_vscrollbar_new (GTK_ADJUSTMENT (vscroll_data));
  gtk_range_set_update_policy (GTK_RANGE (scrollbar), GTK_UPDATE_CONTINUOUS);
  gtk_table_attach (GTK_TABLE (ptable), scrollbar, 1, 2, 0, 1, 0,
                    GTK_FILL, 0, 0);
  gtk_widget_show (scrollbar);

  preview_init ();

  preview_x1 = sel_x1;
  preview_y1 = sel_y1;
  preview_x2 = preview_x1 + MIN (preview_width, sel_width);
  preview_y2 = preview_y1 + MIN (preview_height, sel_height);

  /*
   * Filter type controls...
   */

  frame = gimp_frame_new (_("Type"));
  gtk_box_pack_start (GTK_BOX (hbox), frame, TRUE, TRUE, 0);
  gtk_widget_show (frame);

  vbox = gtk_vbox_new (FALSE, 6);
  gtk_container_add (GTK_CONTAINER (frame), vbox);
  gtk_widget_show (vbox);

  button = gtk_check_button_new_with_mnemonic (_("_Adaptive"));
  gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
                                (filter_type & FILTER_ADAPTIVE) ? TRUE : FALSE);
  gtk_widget_show (button);

  g_signal_connect (button, "toggled",
                    G_CALLBACK (dialog_adaptive_callback),
                    NULL);

  button = gtk_check_button_new_with_mnemonic (_("R_ecursive"));
  gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
                                (filter_type & FILTER_RECURSIVE) ? TRUE : FALSE);
  gtk_widget_show (button);

  g_signal_connect (button, "toggled",
                    G_CALLBACK (dialog_recursive_callback),
                    NULL);

  table = gtk_table_new (3, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 6);
  gtk_table_set_row_spacings (GTK_TABLE (table), 6);
  gtk_box_pack_start (GTK_BOX (main_vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  /*
   * Box size (radius) control...
   */

  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, 0,
                              _("_Radius:"), SCALE_WIDTH, ENTRY_WIDTH,
                              despeckle_radius, 1, MAX_RADIUS, 1, 5, 0,
                              TRUE, 0, 0,
                              NULL, NULL);
  g_signal_connect (adj, "value_changed",
                    G_CALLBACK (dialog_iscale_update),
                    &despeckle_radius);

  /*
   * Black level control...
   */

  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, 1,
                              _("_Black level:"), SCALE_WIDTH, ENTRY_WIDTH,
                              black_level, -1, 255, 1, 8, 0,
                              TRUE, 0, 0,
                              NULL, NULL);
  g_signal_connect (adj, "value_changed",
                    G_CALLBACK (dialog_iscale_update),
                    &black_level);

  /*
   * White level control...
   */

  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, 2,
                              _("_White level:"), SCALE_WIDTH, ENTRY_WIDTH,
                              white_level, 0, 256, 1, 8, 0,
                              TRUE, 0, 0,
                              NULL, NULL);
  g_signal_connect (adj, "value_changed",
                    G_CALLBACK (dialog_iscale_update),
                    &white_level);

  /*
   * Show it and wait for the user to do something...
   */

  gtk_widget_show (dialog);

  preview_update ();

  run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

  gtk_widget_destroy (dialog);

  /*
   * Free the preview data...
   */

  preview_exit ();

  /*
   * Return ok/cancel...
   */

  return run;
}


/*
 * 'preview_init()' - Initialize the preview window...
 */

static void
preview_init (void)
{
  gint  size,           /* Size of filter box */
        width;          /* Byte width of the image */

  /*
   * Setup for preview filter...
   */

  size  = despeckle_radius * 2 + 1;
  width = preview_width * img_bpp;

  if (preview_src != NULL)
    {
      g_free (preview_src);
      g_free (preview_dst);
      g_free (preview_sort);
    }

  preview_src  = g_new (guchar, width * preview_height);
  preview_dst  = g_new (guchar, width);
  preview_sort = g_new (guchar, size * size);
}


/*
 * 'preview_scroll_callback()' - Update the preview when a scrollbar is moved.
 */

static void
preview_scroll_callback (void)
{
  preview_x1 = sel_x1 + GTK_ADJUSTMENT (hscroll_data)->value;
  preview_y1 = sel_y1 + GTK_ADJUSTMENT (vscroll_data)->value;
  preview_x2 = preview_x1 + MIN (preview_width, sel_width);
  preview_y2 = preview_y1 + MIN (preview_height, sel_height);

  preview_update ();
}


/*
 * 'preview_update()' - Update the preview window.
 */

static void
preview_update (void)
{
  GimpPixelRgn  src_rgn;        /* Source image region */
  guchar       *sort_ptr,       /* Current preview_sort value */
               *src_ptr,        /* Current source pixel */
               *dst_ptr;        /* Current destination pixel */
  gint          sort_count,     /* Number of soft values */
                i, j, t, d,     /* Looping vars */
                x, y,           /* Current location in image */
                size,           /* Width/height of the filter box */
                width,          /* Byte width of the image */
                xmin, xmax, tx, /* Looping vars */
                radius,         /* Current radius */
                hist0,          /* Histogram count for 0 values */
                hist255;        /* Histogram count for 255 values */
  guchar        rgba[PREVIEW_SIZE * PREVIEW_SIZE * 4], /* Output image */
               *rgba_ptr;        /* Pixel pointer for output */

 /*
  * Setup for filter...
  */

  gimp_pixel_rgn_init (&src_rgn, drawable,
                       preview_x1, preview_y1, preview_width, preview_height,
                       FALSE, FALSE);

  /*
   * Pre-load the preview rectangle...
   */

  size  = despeckle_radius * 2 + 1;
  width = preview_width * img_bpp;

  gimp_pixel_rgn_get_rect (&src_rgn, preview_src, preview_x1, preview_y1,
                           preview_width, preview_height);

  /*
   * Despeckle...
   */

  for (y = 0; y < preview_height; y ++)
    {
      /*
       * Now find the median pixels and save the results...
       */

      radius = despeckle_radius;

      memcpy (preview_dst, preview_src + y * width, width);

      if (y >= radius && y < (preview_height - radius))
        {
          for (x = 0, dst_ptr = preview_dst; x < width; x ++, dst_ptr ++)
            {
              hist0   = 0;
              hist255 = 0;
              xmin    = x - radius * img_bpp;
              xmax    = x + (radius + 1) * img_bpp;

              if (xmin < 0)
                xmin = x % img_bpp;

              if (xmax > width)
                xmax = width;

              for (i = -radius, sort_ptr = preview_sort,
                     src_ptr = preview_src + width * (y - radius);
                   i <= radius;
                   i ++, src_ptr += width)
                for (tx = xmin; tx < xmax; tx += img_bpp)
                  {
                    if ((*sort_ptr = src_ptr[tx]) <= black_level)
                      hist0 ++;
                    else if (*sort_ptr >= white_level)
                      hist255 ++;

                    if (*sort_ptr < white_level && *sort_ptr > black_level)
                      sort_ptr ++;
                  };

              /*
               * Shell preview_sort the color values...
               */

              sort_count = sort_ptr - preview_sort;

              if (sort_count > 1)
                {
                  for (d = sort_count / 2; d > 0; d = d / 2)
                    for (i = d; i < sort_count; i ++)
                      for (j = i - d, sort_ptr = preview_sort + j;
                           j >= 0 && sort_ptr[0] > sort_ptr[d];
                           j -= d, sort_ptr -= d)
                        {
                          t           = sort_ptr[0];
                          sort_ptr[0] = sort_ptr[d];
                          sort_ptr[d] = t;
                        };

                  /*
                   * Assign the median value...
                   */

                  t = sort_count / 2;

                  if (sort_count & 1)
                    *dst_ptr = (preview_sort[t] + preview_sort[t + 1]) / 2;
                  else
                    *dst_ptr = preview_sort[t];

                  /*
                   * Save the change to the source image too if the user
                   * wants the recursive method...
                   */

                  if (filter_type & FILTER_RECURSIVE)
                    preview_src[y * width + x] = *dst_ptr;
                };

              /*
               * Check the histogram and adjust the radius accordingly...
               */

              if (filter_type & FILTER_ADAPTIVE)
                {
                  if (hist0 >= radius || hist255 >= radius)
                    {
                      if (radius < despeckle_radius)
                        radius ++;
                    }
                  else if (radius > 1)
                    radius --;
                };
            };
        };

      /*
       * Draw this row...
       */

      rgba_ptr = rgba + y * preview_width * 4;

      switch (img_bpp)
        {
        case 1:
          for (x = preview_width, dst_ptr = preview_dst;
               x > 0;
               x --, dst_ptr ++, rgba_ptr += 4)
          {
            rgba_ptr[0] = rgba_ptr[1] = rgba_ptr[2] = *dst_ptr;
            rgba_ptr[3] = 255;
          }
          break;

        case 2:
          for (x = preview_width, dst_ptr = preview_dst;
               x > 0;
               x --, dst_ptr += 2, rgba_ptr += 4)
          {
            rgba_ptr[0] = rgba_ptr[1] = rgba_ptr[2] = dst_ptr[0];
            rgba_ptr[3] = dst_ptr[1];
          }
          break;

        case 3:
          for (x = preview_width, dst_ptr = preview_dst;
               x > 0;
               x --, dst_ptr += 3, rgba_ptr += 4)
          {
            rgba_ptr[0] = dst_ptr[0];
            rgba_ptr[1] = dst_ptr[1];
            rgba_ptr[2] = dst_ptr[2];
            rgba_ptr[3] = 255;
          }
          break;

        case 4:
          memcpy (rgba_ptr, preview_dst, preview_width * 4);
          break;
        };
    };

  /*
   * Update the screen...
   */

  gimp_preview_area_draw (GIMP_PREVIEW_AREA (preview),
                          0, 0, preview_width, preview_height,
                          GIMP_RGBA_IMAGE,
                          rgba,
                          preview_height * 4);
}

static void
preview_exit (void)
{
  g_free (preview_src);
  g_free (preview_dst);
  g_free (preview_sort);
}

static void
dialog_iscale_update (GtkAdjustment *adjustment,
                      gint          *value)
{
  *value = adjustment->value;

  if (value == &despeckle_radius)
    preview_init ();

  preview_update ();
}

static void
dialog_adaptive_callback (GtkWidget *widget,
                          gpointer   data)
{
  if (GTK_TOGGLE_BUTTON (widget)->active)
    filter_type |= FILTER_ADAPTIVE;
  else
    filter_type &= ~FILTER_ADAPTIVE;

  preview_update ();
}

static void
dialog_recursive_callback (GtkWidget *widget,
                           gpointer  data)
{
  if (GTK_TOGGLE_BUTTON (widget)->active)
    filter_type |= FILTER_RECURSIVE;
  else
    filter_type &= ~FILTER_RECURSIVE;

  preview_update ();
}
