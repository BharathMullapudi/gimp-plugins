/* Cubism --- image filter plug-in for The Gimp image manipulation program
 * Copyright (C) 1996 Spencer Kimball, Tracy Scott
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
 *
 * You can contact me at quartic@polloux.fciencias.unam.mx
 * You can contact the original The Gimp authors at gimp@xcf.berkeley.edu
 * Speedups by Elliot Lee
 */
#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


#define SCALE_WIDTH    125
#define BLACK            0
#define BG               1
#define SUPERSAMPLE      4
#define MAX_POINTS       4
#define MIN_ANGLE   -36000
#define MAX_ANGLE    36000
#define RANDOMNESS       5

typedef struct
{
  gint        npts;
  GimpVector2 pts[MAX_POINTS];
} Polygon;

typedef struct
{
  gdouble tile_size;
  gdouble tile_saturation;
  gint    bg_color;
} CubismVals;

typedef struct
{
  gint run;
} CubismInterface;

/* Declare local functions.
 */
static void      query  (void);
static void      run    (gchar         *name,
			 gint           nparams,
			 GimpParam     *param,
			 gint          *nreturn_vals,
			 GimpParam    **return_vals);
static void      cubism (GimpDrawable  *drawable);

static void      render_cubism        (GimpDrawable *drawable);
static void      fill_poly_color      (Polygon      *poly,
				       GimpDrawable *drawable,
				       guchar       *col);
static void      convert_segment      (gint       x1,
				       gint       y1,
				       gint       x2,
				       gint       y2,
				       gint       offset,
				       gint      *min,
				       gint      *max);
static void      randomize_indices    (gint       count,
				       gint      *indices);
static gdouble   fp_rand              (gdouble    val);
static gint      int_rand             (gint       val);
static gdouble   calc_alpha_blend     (gdouble   *vec,
				       gdouble    one_over_dist,
				       gdouble    x,
				       gdouble    y);
static void      polygon_add_point    (Polygon   *poly,
				       gdouble    x,
				       gdouble    y);
static void      polygon_translate    (Polygon   *poly,
				       gdouble    tx,
				       gdouble    ty);
static void      polygon_rotate       (Polygon   *poly,
				       gdouble    theta);
static gint      polygon_extents      (Polygon   *poly,
				       gdouble   *min_x,
				       gdouble   *min_y,
				       gdouble   *max_x,
				       gdouble   *max_y);
static void      polygon_reset        (Polygon   *poly);

static gint      cubism_dialog        (void);
static void      cubism_ok_callback   (GtkWidget *widget,
				       gpointer   data);

/*
 *  Local variables
 */

static guchar bg_col[4];

static CubismVals cvals =
{
  10.0,        /* tile_size */
  2.5,         /* tile_saturation */
  BLACK        /* bg_color */
};

static CubismInterface cint =
{
  FALSE         /* run */
};

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};


/*
 *  Functions
 */

MAIN ()

static void
query (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE, "image", "Input image" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable" },
    { GIMP_PDB_FLOAT, "tile_size", "Average diameter of each tile (in pixels)" },
    { GIMP_PDB_FLOAT, "tile_saturation", "Expand tiles by this amount" },
    { GIMP_PDB_INT32, "bg_color", "Background color: { BLACK (0), BG (1) }" }
  };

  gimp_install_procedure ("plug_in_cubism",
			  "Convert the input drawable into a collection of rotated squares",
			  "Help not yet written for this plug-in",
			  "Spencer Kimball & Tracy Scott",
			  "Spencer Kimball & Tracy Scott",
			  "1996",
			  N_("<Image>/Filters/Artistic/Cubism..."),
			  "RGB*, GRAY*",
			  GIMP_PLUGIN,
			  G_N_ELEMENTS (args), 0,
			  args, NULL);
}

static void
run (gchar      *name,
     gint        nparams,
     GimpParam  *param,
     gint       *nreturn_vals,
     GimpParam **return_vals)
{
  static GimpParam values[1];
  GimpDrawable *active_drawable;
  GimpRunMode run_mode;
  GimpPDBStatusType status = GIMP_PDB_SUCCESS;

  INIT_I18N_UI();

  run_mode = param[0].data.d_int32;

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_cubism", &cvals);

      /*  First acquire information with a dialog  */
      if (! cubism_dialog ())
	return;
      break;

    case GIMP_RUN_NONINTERACTIVE:
      /*  Make sure all the arguments are there!  */
      if (nparams != 6)
	status = GIMP_PDB_CALLING_ERROR;
      if (status == GIMP_PDB_SUCCESS)
	{
	  cvals.tile_size       = param[3].data.d_float;
	  cvals.tile_saturation = param[4].data.d_float;
	  cvals.bg_color        = param[5].data.d_int32;
	}
      if (status == GIMP_PDB_SUCCESS &&
	  (cvals.bg_color < BLACK || cvals.bg_color > BG))
	status = GIMP_PDB_CALLING_ERROR;
      break;

    case GIMP_RUN_WITH_LAST_VALS:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_cubism", &cvals);
      break;

    default:
      break;
    }

  /*  get the active drawable  */
  active_drawable = gimp_drawable_get (param[2].data.d_drawable);

  /*  Render the cubism effect  */
  if ((status == GIMP_PDB_SUCCESS) &&
      (gimp_drawable_is_rgb (active_drawable->drawable_id) ||
       gimp_drawable_is_gray (active_drawable->drawable_id)))
    {
      /*  set cache size  */
      gimp_tile_cache_ntiles (SQR (4 * cvals.tile_size * cvals.tile_saturation) / SQR (gimp_tile_width ()));

      cubism (active_drawable);

      /*  If the run mode is interactive, flush the displays  */
      if (run_mode != GIMP_RUN_NONINTERACTIVE)
	gimp_displays_flush ();

      /*  Store mvals data  */
      if (run_mode == GIMP_RUN_INTERACTIVE)
	gimp_set_data ("plug_in_cubism", &cvals, sizeof (CubismVals));
    }
  else if (status == GIMP_PDB_SUCCESS)
    {
      /* gimp_message ("cubism: cannot operate on indexed color images"); */
      status = GIMP_PDB_EXECUTION_ERROR;
    }

  values[0].data.d_status = status;

  gimp_drawable_detach (active_drawable);
}

static void
cubism (GimpDrawable *drawable)
{
  GimpRGB background;
  gint    x1, y1, x2, y2;

  /*  find the drawable mask bounds  */
  gimp_drawable_mask_bounds (drawable->drawable_id, &x1, &y1, &x2, &y2);

  /*  determine the background color  */
  if (cvals.bg_color == BLACK)
    {
      bg_col[0] = bg_col[1] = bg_col[2] = bg_col[3] = 0;
    }
  else
    {
      gimp_palette_get_background (&background);
      switch (gimp_drawable_type (drawable->drawable_id))
	{
	case GIMP_RGBA_IMAGE:
	  bg_col[3] = 0;
	case GIMP_RGB_IMAGE:
	  gimp_rgb_get_uchar (&background, 
			      &bg_col[0], &bg_col[1], &bg_col[2]);
	  break;
	case GIMP_GRAYA_IMAGE:
	  bg_col[1] = 0;
	case GIMP_GRAY_IMAGE:
	  bg_col[0] = gimp_rgb_intensity_uchar (&background);

	default:
	  break;
	}
    }

  gimp_progress_init (_("Cubistic Transformation"));

  /*  render the cubism  */
  render_cubism (drawable);

  /*  merge the shadow, update the drawable  */
  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
  gimp_drawable_update (drawable->drawable_id, x1, y1, (x2 - x1), (y2 - y1));
}

static gint
cubism_dialog (void)
{
  GtkWidget *dlg;
  GtkWidget *toggle;
  GtkWidget *frame;
  GtkWidget *table;
  GtkObject *scale_data;

  gimp_ui_init ("cubism", FALSE);

  dlg = gimp_dialog_new (_("Cubism"), "cubism",
			 gimp_standard_help_func, "filters/cubism.html",
			 GTK_WIN_POS_MOUSE,
			 FALSE, TRUE, FALSE,

			 GTK_STOCK_CANCEL, gtk_widget_destroy,
			 NULL, 1, NULL, FALSE, TRUE,
			 GTK_STOCK_OK, cubism_ok_callback,
			 NULL, NULL, NULL, TRUE, FALSE,

			 NULL);

  g_signal_connect (G_OBJECT (dlg), "destroy",
                    G_CALLBACK (gtk_main_quit),
                    NULL);

  /*  parameter settings  */
  frame = gtk_frame_new (_("Parameter Settings"));
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
  gtk_container_set_border_width (GTK_CONTAINER (frame), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), frame, TRUE, TRUE, 0);

  table = gtk_table_new (3, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_table_set_row_spacing (GTK_TABLE (table), 0, 4);
  gtk_container_set_border_width (GTK_CONTAINER (table), 6);
  gtk_container_add (GTK_CONTAINER (frame), table);

  toggle = gtk_check_button_new_with_mnemonic (_("_Use Background Color"));
  gtk_table_attach (GTK_TABLE (table), toggle, 0, 3, 0, 1,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (toggle);

  g_signal_connect (G_OBJECT (toggle), "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &cvals.bg_color);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle),
				(cvals.bg_color == BG));

  scale_data = gimp_scale_entry_new (GTK_TABLE (table), 0, 1,
				     _("_Tile Size:"), SCALE_WIDTH, 0,
				     cvals.tile_size, 0.0, 100.0, 1.0, 10.0, 1,
				     TRUE, 0, 0,
				     NULL, NULL);
  g_signal_connect (G_OBJECT (scale_data), "value_changed",
                    G_CALLBACK (gimp_double_adjustment_update),
                    &cvals.tile_size);

  scale_data =
    gimp_scale_entry_new (GTK_TABLE (table), 0, 2,
			  _("T_ile Saturation:"), SCALE_WIDTH, 0,
			  cvals.tile_saturation, 0.0, 10.0, 0.1, 1, 1,
			  TRUE, 0, 0,
			  NULL, NULL);
  g_signal_connect (G_OBJECT (scale_data), "value_changed",
                    G_CALLBACK (gimp_double_adjustment_update),
                    &cvals.tile_saturation);

  gtk_widget_show (table);
  gtk_widget_show (frame);

  gtk_widget_show (dlg);

  gtk_main ();
  gdk_flush ();

  return cint.run;
}

static void
cubism_ok_callback (GtkWidget *widget,
		    gpointer   data)
{
  cint.run = TRUE;

  gtk_widget_destroy (GTK_WIDGET (data));
}

static void
render_cubism (GimpDrawable *drawable)
{
  GimpPixelRgn src_rgn;
  gdouble img_area, tile_area;
  gdouble x, y;
  gdouble width, height;
  gdouble theta;
  gint ix, iy;
  gint rows, cols;
  gint i, j, count;
  gint num_tiles;
  gint x1, y1, x2, y2;
  Polygon poly;
  guchar col[4];
  guchar *dest;
  gint bytes;
  gint has_alpha;
  gint *random_indices;
  gpointer pr;

  has_alpha = gimp_drawable_has_alpha (drawable->drawable_id);
  bytes = drawable->bpp;
  gimp_drawable_mask_bounds (drawable->drawable_id, &x1, &y1, &x2, &y2);
  img_area = (x2 - x1) * (y2 - y1);
  tile_area = SQR (cvals.tile_size);

  cols = ((x2 - x1) + cvals.tile_size - 1) / cvals.tile_size;
  rows = ((y2 - y1) + cvals.tile_size - 1) / cvals.tile_size;

  /*  Fill the image with the background color  */
  gimp_pixel_rgn_init (&src_rgn, drawable,
		       x1, y1, (x2 - x1), (y2 - y1), TRUE, TRUE);
  for (pr = gimp_pixel_rgns_register (1, &src_rgn);
       pr != NULL;
       pr = gimp_pixel_rgns_process (pr))
    {
      count = src_rgn.w * src_rgn.h;
      dest = src_rgn.data;

      while (count--)
	for (i = 0; i < bytes; i++)
	  *dest++ = bg_col[i];
    }

  num_tiles = (rows + 1) * (cols + 1);
  random_indices = g_new (gint, num_tiles);
  for (i = 0; i < num_tiles; i++)
    random_indices[i] = i;

  randomize_indices (num_tiles, random_indices);

  count = 0;
  gimp_pixel_rgn_init (&src_rgn, drawable,
		       x1, y1, (x2 - x1), (y2 - y1), FALSE, FALSE);

  while (count < num_tiles)
    {
      i = random_indices[count] / (cols + 1);
      j = random_indices[count] % (cols + 1);
      x = j * cvals.tile_size + (cvals.tile_size / 4.0) 
	- fp_rand (cvals.tile_size/2.0) + x1;
      y = i * cvals.tile_size + (cvals.tile_size / 4.0) 
	- fp_rand (cvals.tile_size/2.0) + y1;
      width = (cvals.tile_size + fp_rand (cvals.tile_size / 4.0) 
	       - cvals.tile_size / 8.0) * cvals.tile_saturation;
      height = (cvals.tile_size + fp_rand (cvals.tile_size / 4.0) 
		- cvals.tile_size / 8.0) * cvals.tile_saturation;
      theta = fp_rand (2 * G_PI);
      polygon_reset (&poly);
      polygon_add_point (&poly, -width / 2.0, -height / 2.0);
      polygon_add_point (&poly, width / 2.0, -height / 2.0);
      polygon_add_point (&poly, width / 2.0, height / 2.0);
      polygon_add_point (&poly, -width / 2.0, height / 2.0);
      polygon_rotate (&poly, theta);
      polygon_translate (&poly, x, y);

      /*  bounds check on x, y  */
      ix = (int) x;
      iy = (int) y;
      if (ix < x1)
	ix = x1;
      if (ix >= x2)
	ix = x2 - 1;
      if (iy < y1)
	iy = y1;
      if (iy >= y2)
	iy = y2 - 1;

      gimp_pixel_rgn_get_pixel (&src_rgn, col, ix, iy);

      if (! has_alpha || (has_alpha && col[bytes - 1] != 0))
	fill_poly_color (&poly, drawable, col);

      count++;
      if ((count % 5) == 0)
	gimp_progress_update ((double) count / (double) num_tiles);
    }

  gimp_progress_update (1.0);
  g_free (random_indices);
}

static inline gdouble
calc_alpha_blend (gdouble *vec,
		  gdouble  one_over_dist,
		  gdouble  x,
		  gdouble  y)
{
  gdouble r;

  if (!one_over_dist)
    return 1.0;
  else
    {
      r = (vec[0] * x + vec[1] * y) * one_over_dist;
      if (r < 0.2)
	r = 0.2;
      else if (r > 1.0)
	r = 1.0;
    }
  return r;
}

static void
fill_poly_color (Polygon   *poly,
		 GimpDrawable *drawable,
		 guchar    *col)
{
  GimpPixelRgn src_rgn;
  gdouble dmin_x, dmin_y;
  gdouble dmax_x, dmax_y;
  gint xs, ys;
  gint xe, ye;
  gint min_x, min_y;
  gint max_x, max_y;
  gint size_x, size_y;
  gint * max_scanlines, *max_scanlines_iter;
  gint * min_scanlines, *min_scanlines_iter;
  gint val;
  gint alpha;
  gint bytes;
  guchar buf[4];
  gint i, j, x, y;
  gdouble sx, sy;
  gdouble ex, ey;
  gdouble xx, yy;
  gdouble vec[2];
  gdouble dist, one_over_dist;
  gint x1, y1, x2, y2;
  gint *vals, *vals_iter, *vals_end;

  sx = poly->pts[0].x;
  sy = poly->pts[0].y;
  ex = poly->pts[1].x;
  ey = poly->pts[1].y;

  dist = sqrt (SQR (ex - sx) + SQR (ey - sy));
  if (dist > 0.0)
    {
      one_over_dist = 1/dist;
      vec[0] = (ex - sx) * one_over_dist;
      vec[1] = (ey - sy) * one_over_dist;
    }
  else
    one_over_dist = 0.0;

  gimp_drawable_mask_bounds (drawable->drawable_id, &x1, &y1, &x2, &y2);
  bytes = drawable->bpp;

  polygon_extents (poly, &dmin_x, &dmin_y, &dmax_x, &dmax_y);
  min_x = (gint) dmin_x;
  min_y = (gint) dmin_y;
  max_x = (gint) dmax_x;
  max_y = (gint) dmax_y;

  size_y = (max_y - min_y) * SUPERSAMPLE;
  size_x = (max_x - min_x) * SUPERSAMPLE;

  min_scanlines = min_scanlines_iter = g_new (gint, size_y);
  max_scanlines = max_scanlines_iter = g_new (gint, size_y);
  for (i = 0; i < size_y; i++)
    {
      min_scanlines[i] = max_x * SUPERSAMPLE;
      max_scanlines[i] = min_x * SUPERSAMPLE;
    }

  if(poly->npts) {
    gint poly_npts = poly->npts;
    GimpVector2 *curptr;

    xs = (gint) (poly->pts[poly_npts-1].x);
    ys = (gint) (poly->pts[poly_npts-1].y);
    xe = (gint) poly->pts[0].x;
    ye = (gint) poly->pts[0].y;

    xs *= SUPERSAMPLE;
    ys *= SUPERSAMPLE;
    xe *= SUPERSAMPLE;
    ye *= SUPERSAMPLE;

    convert_segment (xs, ys, xe, ye, min_y * SUPERSAMPLE,
		     min_scanlines, max_scanlines);

    for (i = 1, curptr = &poly->pts[0]; i < poly_npts; i++)
      {
	xs = (gint) curptr->x;
	ys = (gint) curptr->y;
	curptr++;
	xe = (gint) curptr->x;
	ye = (gint) curptr->y;

	xs *= SUPERSAMPLE;
	ys *= SUPERSAMPLE;
	xe *= SUPERSAMPLE;
	ye *= SUPERSAMPLE;

	convert_segment (xs, ys, xe, ye, min_y * SUPERSAMPLE,
			 min_scanlines, max_scanlines);
      }
  }

  gimp_pixel_rgn_init (&src_rgn, drawable, 0, 0,
		       drawable->width, drawable->height, TRUE, TRUE);

  vals = g_new (gint, size_x);

  for (i = 0; i < size_y; i++, min_scanlines_iter++, max_scanlines_iter++)
    {
      if (! (i % SUPERSAMPLE))
	{
	  memset (vals, 0, sizeof (gint) * size_x);
	}

      yy = (gdouble)i / (gdouble)SUPERSAMPLE + min_y;

      for (j = *min_scanlines_iter; j < *max_scanlines_iter; j++)
	{
	  x = j - min_x * SUPERSAMPLE;
	  vals[x] += 255;
	}

      if (! ((i + 1) % SUPERSAMPLE))
	{
	  y = (i / SUPERSAMPLE) + min_y;

	  if (y >= y1 && y < y2)
	    {
	      for (j = 0; j < size_x; j += SUPERSAMPLE)
		{
		  x = (j / SUPERSAMPLE) + min_x;

		  if (x >= x1 && x < x2)
		    {
		      for (val = 0, vals_iter = &vals[j],
			     vals_end = &vals_iter[SUPERSAMPLE];
			   vals_iter < vals_end;
			   vals_iter++)
			val += *vals_iter;

		      val /= SQR(SUPERSAMPLE);

		      if (val > 0)
			{
			  xx = (gdouble) j / (gdouble) SUPERSAMPLE + min_x;
			  alpha = (gint) (val * calc_alpha_blend (vec, one_over_dist, xx - sx, yy - sy));

			  gimp_pixel_rgn_get_pixel (&src_rgn, buf, x, y);

#ifndef USE_READABLE_BUT_SLOW_CODE
			  {
			    guchar *buf_iter = buf,
			      *col_iter = col,
			      *buf_end = buf+bytes;

			    for(; buf_iter < buf_end; buf_iter++, col_iter++)
			      *buf_iter = ((guint)(*col_iter * alpha)
					   + (((guint)*buf_iter)
					      * (256 - alpha))) >> 8;
			  }
#else /* original, pre-ECL code */
			  for (b = 0; b < bytes; b++)
			    buf[b] = ((col[b] * alpha) + (buf[b] * (255 - alpha))) / 255;

#endif
			  gimp_pixel_rgn_set_pixel (&src_rgn, buf, x, y);
			}
		    }
		}
	    }
	}
    }

  g_free (vals);
  g_free (min_scanlines);
  g_free (max_scanlines);
}

static void
convert_segment (gint  x1,
		 gint  y1,
		 gint  x2,
		 gint  y2,
		 gint  offset,
		 gint *min,
		 gint *max)
{
  gint ydiff, y, tmp;
  gdouble xinc, xstart;

  if (y1 > y2)
    {
      tmp = y2; y2 = y1; y1 = tmp;
      tmp = x2; x2 = x1; x1 = tmp;
    }
  ydiff = (y2 - y1);

  if (ydiff)
    {
      xinc = (gdouble) (x2 - x1) / (gdouble) ydiff;
      xstart = x1 + 0.5 * xinc;
      for (y = y1 ; y < y2; y++)
	{
	  if (xstart < min[y - offset])
	    min[y-offset] = xstart;
	  if (xstart > max[y - offset])
	    max[y-offset] = xstart;

	  xstart += xinc;
	}
    }
}

static void
randomize_indices (gint  count,
		   gint *indices)
{
  gint i;
  gint index1, index2;
  gint tmp;

  for (i = 0; i < count * RANDOMNESS; i++)
    {
      index1 = int_rand (count);
      index2 = int_rand (count);
      tmp = indices[index1];
      indices[index1] = indices[index2];
      indices[index2] = tmp;
    }
}

static gdouble
fp_rand (gdouble val)
{
  gdouble rand_val;

  rand_val = (gdouble) rand () / (gdouble) (G_MAXRAND - 1);
  return rand_val * val;
}

static gint
int_rand (gint val)
{
  gint rand_val;

  rand_val = rand () % val;
  return rand_val;
}

static void
polygon_add_point (Polygon *poly,
		   gdouble  x,
		   gdouble  y)
{
  if (poly->npts < 12)
    {
      poly->pts[poly->npts].x = x;
      poly->pts[poly->npts].y = y;
      poly->npts++;
    }
  else
    g_print ("Unable to add additional point.\n");
}

static void
polygon_rotate (Polygon *poly,
		gdouble  theta)
{
  gint i;
  gdouble ct, st;
  gdouble ox, oy;

  ct = cos (theta);
  st = sin (theta);

  for (i = 0; i < poly->npts; i++)
    {
      ox = poly->pts[i].x;
      oy = poly->pts[i].y;
      poly->pts[i].x = ct * ox - st * oy;
      poly->pts[i].y = st * ox + ct * oy;
    }
}

static void
polygon_translate (Polygon *poly,
		   gdouble  tx,
		   gdouble  ty)
{
  gint i;

  for (i = 0; i < poly->npts; i++)
    {
      poly->pts[i].x += tx;
      poly->pts[i].y += ty;
    }
}

static gint
polygon_extents (Polygon *poly,
		 gdouble *x1,
		 gdouble *y1,
		 gdouble *x2,
		 gdouble *y2)
{
  gint i;

  if (!poly->npts)
    return 0;

  *x1 = *x2 = poly->pts[0].x;
  *y1 = *y2 = poly->pts[0].y;

  for (i = 1; i < poly->npts; i++)
    {
      if (poly->pts[i].x < *x1)
	*x1 = poly->pts[i].x;
      if (poly->pts[i].x > *x2)
	*x2 = poly->pts[i].x;
      if (poly->pts[i].y < *y1)
	*y1 = poly->pts[i].y;
      if (poly->pts[i].y > *y2)
	*y2 = poly->pts[i].y;
    }

  return 1;
}

static void
polygon_reset (Polygon *poly)
{
  poly->npts = 0;
}
