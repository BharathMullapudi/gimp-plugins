/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * Polarize plug-in --- maps a rectangul to a circle or vice-versa
 * Copyright (C) 1997 Daniel Dunbar
 * Email: ddunbar@diads.com
 * WWW:   http://millennium.diads.com/gimp/
 * Copyright (C) 1997 Federico Mena Quintero
 * federico@nuclecu.unam.mx
 * Copyright (C) 1996 Marc Bless
 * E-mail: bless@ai-lab.fh-furtwangen.de
 * WWW:    www.ai-lab.fh-furtwangen.de/~bless
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


/* Version 1.0:
 * This is the follow-up release.  It contains a few minor changes, the
 * most major being that the first time I released the wrong version of
 * the code, and this time the changes have been fixed.  I also added
 * tooltips to the dialog.
 *
 * Feel free to email me if you have any comments or suggestions on this
 * plugin.
 *               --Daniel Dunbar
 *                 ddunbar@diads.com
 */


/* Version .5:
 * This is the first version publicly released, it will probably be the
 * last also unless i can think of some features i want to add.
 *
 * This plug-in was created with loads of help from quartic (Frederico
 * Mena Quintero), and would surely not have come about without it.
 *
 * The polar algorithms is copied from Marc Bless' polar plug-in for
 * .54, many thanks to him also.
 * 
 * If you can think of a neat addition to this plug-in, or any other
 * info about it, please email me at ddunbar@diads.com.
 *                                     - Daniel Dunbar
 */

#include "config.h"

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


#define WITHIN(a, b, c) ((((a) <= (b)) && ((b) <= (c))) ? 1 : 0)


/***** Magic numbers *****/

#define PLUG_IN_NAME    "plug_in_polar_coords"
#define PLUG_IN_VERSION "July 1997, 0.5"

#define PREVIEW_SIZE 128
#define SCALE_WIDTH  200
#define ENTRY_WIDTH   60

/***** Types *****/

typedef struct
{
  gdouble circle;
  gdouble angle;
  gint backwards;
  gint inverse;
  gint polrec;
} polarize_vals_t;

typedef struct
{
  GtkWidget *preview;
  guchar    *check_row_0;
  guchar    *check_row_1;
  guchar    *image;
  guchar    *dimage;

  gint       run;
} polarize_interface_t;

typedef struct
{
  gint       col, row;
  gint       img_width, img_height, img_bpp, img_has_alpha;
  gint       tile_width, tile_height;
  guchar     bg_color[4];
  GimpDrawable *drawable;
  GimpTile     *tile;
} pixel_fetcher_t;


/***** Prototypes *****/

static void query (void);
static void run   (gchar      *name,
		   gint        nparams,
		   GimpParam  *param,
		   gint       *nreturn_vals,
		   GimpParam **return_vals);

static void   polarize(void);
static int    calc_undistorted_coords(double wx, double wy,
				      double *x, double *y);
static guchar bilinear(double x, double y, guchar *values);

static pixel_fetcher_t *pixel_fetcher_new          (GimpDrawable *drawable);
static void             pixel_fetcher_set_bg_color (pixel_fetcher_t *pf);
static void             pixel_fetcher_get_pixel    (pixel_fetcher_t *pf, int x, int y, guchar *pixel);
static void             pixel_fetcher_destroy      (pixel_fetcher_t *pf);

static void build_preview_source_image(void);

static gint polarize_dialog       (void);
static void dialog_update_preview (void);

static void dialog_scale_update (GtkAdjustment *adjustment, gdouble *value);
static void dialog_ok_callback  (GtkWidget *widget, gpointer data);

static void polar_toggle_callback (GtkWidget *widget, gpointer data);

/***** Variables *****/

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run    /* run_proc   */
};

static polarize_vals_t pcvals =
{
  100.0, /* circle */
  0.0,  /* angle */
  0, /* backwards */
  1,  /* inverse */
  1  /* polar to rectangular? */
};

static polarize_interface_t pcint =
{
  NULL,  /* preview */
  NULL,  /* check_row_0 */
  NULL,  /* check_row_1 */
  NULL,  /* image */
  NULL,  /* dimage */
  FALSE  /* run */
};

static GimpDrawable *drawable;

static gint img_width, img_height, img_bpp, img_has_alpha;
static gint sel_x1, sel_y1, sel_x2, sel_y2;
static gint sel_width, sel_height;
static gint preview_width, preview_height;

static double cen_x, cen_y;
static double scale_x, scale_y;

/***** Functions *****/

MAIN ()

static void
query (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32,    "run_mode",  "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE,    "image",     "Input image" },
    { GIMP_PDB_DRAWABLE, "drawable",  "Input drawable" },
    { GIMP_PDB_FLOAT,    "circle",    "Circle depth in %" },
    { GIMP_PDB_FLOAT,    "angle",     "Offset angle" },
    { GIMP_PDB_INT32,    "backwards",    "Map backwards?" },
    { GIMP_PDB_INT32,    "inverse",     "Map from top?" },
    { GIMP_PDB_INT32,    "polrec",     "Polar to rectangular?" }
  };
  static gint nargs = sizeof (args) / sizeof (args[0]);

  gimp_install_procedure (PLUG_IN_NAME,
			  "Converts and image to and from polar coords",
			  "Remaps and image from rectangular coordinates to polar coordinates "
			  "or vice versa",
			  "Daniel Dunbar and Federico Mena Quintero",
			  "Daniel Dunbar and Federico Mena Quintero",
			  PLUG_IN_VERSION,
			  N_("<Image>/Filters/Distorts/Polar Coords..."),
			  "RGB*, GRAY*",
			  GIMP_PLUGIN,
			  nargs, 0,
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

  GimpRunModeType run_mode;
  GimpPDBStatusType  status;
  double       xhsiz, yhsiz;
  int          pwidth, pheight;

  status   = GIMP_PDB_SUCCESS;
  run_mode = param[0].data.d_int32;

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  *nreturn_vals = 1;
  *return_vals  = values;

  /* Get the active drawable info */

  drawable = gimp_drawable_get (param[2].data.d_drawable);

  img_width     = gimp_drawable_width (drawable->drawable_id);
  img_height    = gimp_drawable_height (drawable->drawable_id);
  img_bpp       = gimp_drawable_bpp (drawable->drawable_id);
  img_has_alpha = gimp_drawable_has_alpha (drawable->drawable_id);

  gimp_drawable_mask_bounds (drawable->drawable_id, &sel_x1, &sel_y1, &sel_x2, &sel_y2);

  /* Calculate scaling parameters */

  sel_width  = sel_x2 - sel_x1;
  sel_height = sel_y2 - sel_y1;

  cen_x = (double) (sel_x1 + sel_x2 - 1) / 2.0;
  cen_y = (double) (sel_y1 + sel_y2 - 1) / 2.0;

  xhsiz = (double) (sel_width - 1) / 2.0;
  yhsiz = (double) (sel_height - 1) / 2.0;

  if (xhsiz < yhsiz)
    {
      scale_x = yhsiz / xhsiz;
      scale_y = 1.0;
    }
  else if (xhsiz > yhsiz)
    {
      scale_x = 1.0;
      scale_y = xhsiz / yhsiz;
    }
  else
    {
      scale_x = 1.0;
      scale_y = 1.0;
    }

  /* Calculate preview size */

  if (sel_width > sel_height)
    {
      pwidth  = MIN (sel_width, PREVIEW_SIZE);
      pheight = sel_height * pwidth / sel_width;
    }
  else
    {
      pheight = MIN (sel_height, PREVIEW_SIZE);
      pwidth  = sel_width * pheight / sel_height;
    }

  preview_width  = MAX (pwidth, 2); /* Min size is 2 */
  preview_height = MAX (pheight, 2);

  /* See how we will run */

  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE:
      INIT_I18N_UI();

      /* Possibly retrieve data */
      gimp_get_data (PLUG_IN_NAME, &pcvals);

      /* Get information from the dialog */
      if (!polarize_dialog ())
	return;

      break;

    case GIMP_RUN_NONINTERACTIVE:
      INIT_I18N();

      /* Make sure all the arguments are present */
      if (nparams != 8)
	{
	  status = GIMP_PDB_CALLING_ERROR;
	}
      else
	{
	  pcvals.circle  = param[3].data.d_float;
	  pcvals.angle  = param[4].data.d_float;
	  pcvals.backwards  = param[5].data.d_int32;
	  pcvals.inverse  = param[6].data.d_int32;
	  pcvals.polrec  = param[7].data.d_int32;
	}
      break;

    case GIMP_RUN_WITH_LAST_VALS:
      INIT_I18N();

      /* Possibly retrieve data */
      gimp_get_data (PLUG_IN_NAME, &pcvals);
      break;

    default:
      break;
    }

  /* Distort the image */
  if ((status == GIMP_PDB_SUCCESS) &&
      (gimp_drawable_is_rgb (drawable->drawable_id) ||
       gimp_drawable_is_gray (drawable->drawable_id)))
    {
      /* Set the tile cache size */
      gimp_tile_cache_ntiles (2 * (drawable->width + gimp_tile_width() - 1) /
			      gimp_tile_width ());

      /* Run! */
      polarize ();

      /* If run mode is interactive, flush displays */
      if (run_mode != GIMP_RUN_NONINTERACTIVE)
	gimp_displays_flush ();

      /* Store data */
      if (run_mode == GIMP_RUN_INTERACTIVE)
	gimp_set_data (PLUG_IN_NAME, &pcvals, sizeof (polarize_vals_t));
    }
  else if (status == GIMP_PDB_SUCCESS)
    status = GIMP_PDB_EXECUTION_ERROR;

  values[0].data.d_status = status;

  gimp_drawable_detach (drawable);
}

static void
polarize (void)
{
  GimpPixelRgn  dest_rgn;
  guchar    *dest, *d;
  guchar     pixel[4][4];
  guchar     pixel2[4];
  guchar     values[4];
  gint       progress, max_progress;
  double     cx, cy;
  gint       x1, y1, x2, y2;
  gint       x, y, b;
  gpointer   pr;
  
  pixel_fetcher_t *pft;

  /* Get selection area */
  gimp_drawable_mask_bounds (drawable->drawable_id, &x1, &y1, &x2, &y2);

  /* Initialize pixel region */
  gimp_pixel_rgn_init (&dest_rgn, drawable,
		       x1, y1, (x2 - x1), (y2 - y1), TRUE, TRUE);
  
  pft = pixel_fetcher_new (drawable);

  pixel_fetcher_set_bg_color (pft);

  progress     = 0;
  max_progress = img_width * img_height;

  gimp_progress_init (_("Polarizing..."));

  for (pr = gimp_pixel_rgns_register (1, &dest_rgn);
       pr != NULL;
       pr = gimp_pixel_rgns_process (pr))
    {
      dest = dest_rgn.data;

      for (y = dest_rgn.y; y < (dest_rgn.y + dest_rgn.h); y++)
	{
	  d = dest;

	  for (x = dest_rgn.x; x < (dest_rgn.x + dest_rgn.w); x++)
	    {
	      if (calc_undistorted_coords (x, y, &cx, &cy))
		{
		  pixel_fetcher_get_pixel (pft, cx, cy, pixel[0]);
		  pixel_fetcher_get_pixel (pft, cx + 1, cy, pixel[1]);
		  pixel_fetcher_get_pixel (pft, cx, cy + 1, pixel[2]);
		  pixel_fetcher_get_pixel (pft, cx + 1, cy + 1, pixel[3]);

		  for (b = 0; b < img_bpp; b++)
		    {
		      values[0] = pixel[0][b];
		      values[1] = pixel[1][b];
		      values[2] = pixel[2][b];
		      values[3] = pixel[3][b];
	   
		      d[b] = bilinear (cx, cy, values);
		    }
		}
	      else
		{
		  pixel_fetcher_get_pixel (pft, x, y, pixel2);
		  for (b = 0; b < img_bpp; b++)
		    {
		      d[b] = 255;
		    }	  
		}

	      d += dest_rgn.bpp;
	    }
      
	  dest += dest_rgn.rowstride;
	}
      progress += dest_rgn.w *dest_rgn.h;

      gimp_progress_update ((double) progress / max_progress);
    }
  
  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
  gimp_drawable_update (drawable->drawable_id, x1, y1, (x2 - x1), (y2 - y1));
}

static gint
calc_undistorted_coords (gdouble  wx,
			 gdouble  wy,
			 gdouble *x,
			 gdouble *y)
{
  gint    inside;
  gdouble phi, phi2;
  gdouble xx, xm, ym, yy;
  gint    xdiff, ydiff;
  gdouble r;
  gdouble m;
  gdouble xmax, ymax, rmax;
  gdouble x_calc, y_calc;
  gdouble xi, yi;
  gdouble circle, angl, t, angle;
  gint    x1, x2, y1, y2;

  /* initialize */

  phi = 0.0;
  r = 0.0;

  x1 = 0;
  y1 = 0;
  x2 = img_width;
  y2 = img_height;
  xdiff = x2 - x1;
  ydiff = y2 - y1;
  xm = xdiff / 2.0;
  ym = ydiff / 2.0;
  circle = pcvals.circle;
  angle = pcvals.angle;
  angl = (double)angle / 180.0 * G_PI;

  if (pcvals.polrec)
    {
      if (wx >= cen_x)
	{
	  if (wy > cen_y)
	    {
	      phi = G_PI - atan (((double)(wx - cen_x))/((double)(wy - cen_y)));
	      r   = sqrt (SQR (wx - cen_x) + SQR (wy - cen_y));
	    }
	  else if (wy < cen_y)
	    {
	      phi = atan (((double)(wx - cen_x))/((double)(cen_y - wy)));
	      r   = sqrt (SQR (wx - cen_x) + SQR (cen_y - wy));
	    }
	  else
	    {
	      phi = G_PI / 2;
	      r   = wx - cen_x; /* cen_x - x1; */
	    }
	}
      else if (wx < cen_x)
	{
	  if (wy < cen_y)
	    {
	      phi = 2 * G_PI - atan (((double)(cen_x -wx)) /
				     ((double)(cen_y - wy)));
	      r   = sqrt (SQR (cen_x - wx) + SQR (cen_y - wy));
	    }
	  else if (wy > cen_y)
	    {
	      phi = G_PI + atan (((double)(cen_x - wx))/((double)(wy - cen_y)));
	      r   = sqrt (SQR (cen_x - wx) + SQR (wy - cen_y));
	    }
	  else
	    {
	      phi = 1.5 * G_PI;
	      r   = cen_x - wx; /* cen_x - x1; */
	    }
	}
      if (wx != cen_x)
	{
	  m = fabs (((double)(wy - cen_y)) / ((double)(wx - cen_x)));
	}
      else
	{
	  m = 0;
	}
    
      if (m <= ((double)(y2 - y1) / (double)(x2 - x1)))
	{
	  if (wx == cen_x)
	    {
	      xmax = 0;
	      ymax = cen_y - y1;
	    }
	  else
	    {
	      xmax = cen_x - x1;
	      ymax = m * xmax;
	    }
	}
      else
	{
	  ymax = cen_y - y1;
	  xmax = ymax / m;
	}
    
      rmax = sqrt ( (double)(SQR (xmax) + SQR (ymax)) );
    
      t = ((cen_y - y1) < (cen_x - x1)) ? (cen_y - y1) : (cen_x - x1);
      rmax = (rmax - t) / 100 * (100 - circle) + t;
    
      phi = fmod (phi + angl, 2*G_PI);
    
      if (pcvals.backwards)
	x_calc = x2 - 1 - (x2 - x1 - 1)/(2*G_PI) * phi;
      else
	x_calc = (x2 - x1 - 1)/(2*G_PI) * phi + x1;
    
      if (pcvals.inverse)
	y_calc = (y2 - y1)/rmax   * r   + y1;
      else
	y_calc = y2 - (y2 - y1)/rmax * r;
    
      xi = (int) (x_calc+0.5);
      yi = (int) (y_calc+0.5);
    
      if (WITHIN(0, xi, img_width - 1) && WITHIN(0, yi, img_height - 1))
	{
	  *x = x_calc;
	  *y = y_calc;
      
	  inside = TRUE;
	}
      else
	{
	  inside = FALSE;
	}
    }
  else
    {
      if (pcvals.backwards)
	phi = (2 * G_PI) * (x2 - wx) / xdiff;
      else
	phi = (2 * G_PI) * (wx - x1) / xdiff;
    
      phi = fmod (phi + angl, 2 * G_PI);
    
      if (phi >= 1.5 * G_PI)
	phi2 = 2 * G_PI - phi;
      else
	if (phi >= G_PI)
	  phi2 = phi - G_PI;
	else
	  if (phi >= 0.5 * G_PI)
	    phi2 = G_PI - phi;
	  else
	    phi2 = phi;
    
      xx = tan (phi2);
      if (xx != 0)
	m = (double) 1.0 / xx;
      else
	m = 0;
    
      if (m <= ((double)(ydiff) / (double)(xdiff)))
	{
	  if (phi2 == 0)
	    {
	      xmax = 0;
	      ymax = ym - y1;
	    }
	  else
	    {
	      xmax = xm - x1;
	      ymax = m * xmax;
	    }
	}
      else
	{
	  ymax = ym - y1;
	  xmax = ymax / m;
	}
    
      rmax = sqrt ((double)(SQR (xmax) + SQR (ymax)));
    
      t = ((ym - y1) < (xm - x1)) ? (ym - y1) : (xm - x1);
    
      rmax = (rmax - t) / 100.0 * (100 - circle) + t;
    
      if (pcvals.inverse)
	r = rmax * (double)((wy - y1) / (double)(ydiff));
      else
	r = rmax * (double)((y2 - wy) / (double)(ydiff));
    
      xx = r * sin (phi2);
      yy = r * cos (phi2);
    
      if (phi >= 1.5 * G_PI)
	{
	  x_calc = (double)xm - xx;
	  y_calc = (double)ym - yy;
	}
      else
	if (phi >= G_PI)
	  {
	    x_calc = (double)xm - xx;
	    y_calc = (double)ym + yy;
	  }
	else
	  if (phi >= 0.5 * G_PI)
	    {
	      x_calc = (double)xm + xx;
	      y_calc = (double)ym + yy;
	    }
	  else
	    {
	      x_calc = (double)xm + xx;
	      y_calc = (double)ym - yy;
	    }
    
      xi = (int)(x_calc + 0.5);
      yi = (int)(y_calc + 0.5);
  
      if (WITHIN(0, xi, img_width - 1) && WITHIN(0, yi, img_height - 1)) {
	*x = x_calc;
	*y = y_calc;
      
	inside = TRUE;
      }
      else
	{
	  inside = FALSE;
	}
    }
  
  return inside;
}

static guchar
bilinear (gdouble  x,
	  gdouble  y,
	  guchar *values)
{
  gdouble m0, m1;

  x = fmod (x, 1.0);
  y = fmod (y, 1.0);

  if (x < 0.0)
    x += 1.0;

  if (y < 0.0)
    y += 1.0;

  m0 = (double) values[0] + x * ((double) values[1] - values[0]);
  m1 = (double) values[2] + x * ((double) values[3] - values[2]);

  return (guchar) (m0 + y * (m1 - m0));
}

static pixel_fetcher_t *
pixel_fetcher_new (GimpDrawable *drawable)
{
  pixel_fetcher_t *pf;

  pf = g_new (pixel_fetcher_t, 1);

  pf->col           = -1;
  pf->row           = -1;
  pf->img_width     = gimp_drawable_width (drawable->drawable_id);
  pf->img_height    = gimp_drawable_height (drawable->drawable_id);
  pf->img_bpp       = gimp_drawable_bpp (drawable->drawable_id);
  pf->img_has_alpha = gimp_drawable_has_alpha (drawable->drawable_id);
  pf->tile_width    = gimp_tile_width ();
  pf->tile_height   = gimp_tile_height ();
  pf->bg_color[0]   = 0;
  pf->bg_color[1]   = 0;
  pf->bg_color[2]   = 0;
  pf->bg_color[3]   = 0;

  pf->drawable    = drawable;
  pf->tile        = NULL;

  return pf;
}

static void
pixel_fetcher_set_bg_color (pixel_fetcher_t *pf)
{
  GimpRGB  background;

  gimp_palette_get_background (&background);

  switch (pf->img_bpp)
    {
    case 1:
    case 2:
      pf->bg_color[0] = gimp_rgb_intensity_uchar (&background);
      break;

    case 3:
    case 4:
      gimp_rgb_get_uchar (&background,
			  pf->bg_color, pf->bg_color + 1, pf->bg_color + 2);
      break;
    }
}

static void
pixel_fetcher_get_pixel (pixel_fetcher_t *pf,
			 gint             x,
			 gint             y,
			 guchar          *pixel)
{
  gint    col, row;
  gint    coloff, rowoff;
  guchar *p;
  gint    i;

  if ((x < sel_x1) || (x >= sel_x2) ||
      (y < sel_y1) || (y >= sel_y2))
    {
      for (i = 0; i < pf->img_bpp; i++)
	pixel[i] = pf->bg_color[i];

      return;
    }

  col    = x / pf->tile_width;
  coloff = x % pf->tile_width;
  row    = y / pf->tile_height;
  rowoff = y % pf->tile_height;

  if ((col != pf->col) ||
      (row != pf->row) ||
      (pf->tile == NULL))
    {
      if (pf->tile != NULL)
	gimp_tile_unref(pf->tile, FALSE);

      pf->tile = gimp_drawable_get_tile (pf->drawable, FALSE, row, col);
      gimp_tile_ref (pf->tile);

      pf->col = col;
      pf->row = row;
    }

  p = pf->tile->data + pf->img_bpp * (pf->tile->ewidth * rowoff + coloff);

  for (i = pf->img_bpp; i; i--)
    *pixel++ = *p++;
}

static void
pixel_fetcher_destroy (pixel_fetcher_t *pf)
{
  if (pf->tile != NULL)
    gimp_tile_unref (pf->tile, FALSE);

  g_free (pf);
}

static void
build_preview_source_image (void)
{
  gdouble          left, right, bottom, top;
  gdouble          px, py;
  gdouble          dx, dy;
  gint             x, y;
  guchar          *p;
  guchar           pixel[4];
  pixel_fetcher_t *pf;

  pcint.check_row_0 = g_new (guchar, preview_width);
  pcint.check_row_1 = g_new (guchar, preview_width);
  pcint.image       = g_new (guchar, preview_width * preview_height * 4);
  pcint.dimage      = g_new (guchar, preview_width * preview_height * 3);

  left   = sel_x1;
  right  = sel_x2 - 1;
  bottom = sel_y2 - 1;
  top    = sel_y1;

  dx = (right - left) / (preview_width - 1);
  dy = (bottom - top) / (preview_height - 1);

  py = top;

  pf = pixel_fetcher_new(drawable);

  p = pcint.image;

  for (y = 0; y < preview_height; y++)
    {
      px = left;

      for (x = 0; x < preview_width; x++)
	{
	  /* Checks */

	  if ((x / GIMP_CHECK_SIZE) & 1)
	    {
	      pcint.check_row_0[x] = GIMP_CHECK_DARK * 255;
	      pcint.check_row_1[x] = GIMP_CHECK_LIGHT * 255;
	    }
	  else
	    {
	      pcint.check_row_0[x] = GIMP_CHECK_LIGHT * 255;
	      pcint.check_row_1[x] = GIMP_CHECK_DARK * 255;
	    }

	  /* Thumbnail image */

	  pixel_fetcher_get_pixel (pf, (int) px, (int) py, pixel);

	  if (img_bpp < 3)
	    {
	      if (img_has_alpha)
		pixel[3] = pixel[1];
	      else
		pixel[3] = 255;

	      pixel[1] = pixel[0];
	      pixel[2] = pixel[0];
	    }
	  else if (!img_has_alpha)
	    pixel[3] = 255;

	  *p++ = pixel[0];
	  *p++ = pixel[1];
	  *p++ = pixel[2];
	  *p++ = pixel[3];

	  px += dx;
	}

      py += dy;
    }

  pixel_fetcher_destroy (pf);
}

static gint
polarize_dialog (void)
{
  GtkWidget *dialog;
  GtkWidget *main_vbox;
  GtkWidget *vbox;
  GtkWidget *frame;
  GtkWidget *table;
  GtkWidget *abox;
  GtkWidget *pframe;
  GtkWidget *toggle;
  GtkWidget *hbox;
  GtkObject *adj;

  gimp_ui_init ("polar", TRUE);

  build_preview_source_image ();

  dialog = gimp_dialog_new (_("Polarize"), "polar",
			    gimp_standard_help_func, "filters/polar.html",
			    GTK_WIN_POS_MOUSE,
			    FALSE, TRUE, FALSE,

			    _("OK"), dialog_ok_callback,
			    NULL, NULL, NULL, TRUE, FALSE,
			    _("Cancel"), gtk_widget_destroy,
			    NULL, 1, NULL, FALSE, TRUE,

			    NULL);

  gtk_signal_connect (GTK_OBJECT (dialog), "destroy",
		      GTK_SIGNAL_FUNC (gtk_main_quit),
		      NULL);

  /* Initialize Tooltips */
  gimp_help_init ();
	
  main_vbox = gtk_vbox_new (FALSE, 4);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG(dialog)->vbox), main_vbox,
		      FALSE, FALSE, 0);
  gtk_widget_show (main_vbox);

  /* Preview */
  frame = gtk_frame_new (_("Preview"));
  gtk_box_pack_start (GTK_BOX (main_vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  abox = gtk_alignment_new (0.5, 0.5, 0.0, 0.0);
  gtk_container_add (GTK_CONTAINER (frame), abox);
  gtk_widget_show (abox);

  pframe = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (pframe), GTK_SHADOW_IN);
  gtk_container_set_border_width (GTK_CONTAINER (pframe), 4);
  gtk_container_add (GTK_CONTAINER (abox), pframe);
  gtk_widget_show (pframe);

  pcint.preview = gtk_preview_new (GTK_PREVIEW_COLOR);
  gtk_preview_size (GTK_PREVIEW (pcint.preview), preview_width, preview_height);
  gtk_container_add (GTK_CONTAINER (pframe), pcint.preview);
  gtk_widget_show (pcint.preview);

  /* Controls */
  frame = gtk_frame_new (_("Parameter Settings"));
  gtk_box_pack_start (GTK_BOX (main_vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  vbox = gtk_vbox_new (FALSE, 4);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 4);
  gtk_container_add (GTK_CONTAINER (frame), vbox);
  gtk_widget_show (vbox);

  table = gtk_table_new (2, 3, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, 0,
			      _("Circle Depth in Percent:"), SCALE_WIDTH, 0,
			      pcvals.circle, 0.0, 100.0, 1.0, 10.0, 2,
			      TRUE, 0, 0,
			      NULL, NULL);
  gtk_signal_connect (GTK_OBJECT (adj), "value_changed",
		      GTK_SIGNAL_FUNC (dialog_scale_update),
		      &pcvals.circle);

  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, 1,
			      _("Offset Angle:"), SCALE_WIDTH, 0,
			      pcvals.angle, 0.0, 359.0, 1.0, 15.0, 2,
			      TRUE, 0, 0,
			      NULL, NULL);
  gtk_signal_connect (GTK_OBJECT (adj), "value_changed",
		      GTK_SIGNAL_FUNC (dialog_scale_update),
		      &pcvals.angle);

  /* togglebuttons for backwards, top, polar->rectangular */
  hbox = gtk_hbox_new (TRUE, 4);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  toggle = gtk_check_button_new_with_label (_("Map Backwards"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), pcvals.backwards);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled", 
		      GTK_SIGNAL_FUNC (polar_toggle_callback),
		      &pcvals.backwards);
  gtk_box_pack_start (GTK_BOX (hbox), toggle, TRUE, TRUE, 0);
  gtk_widget_show (toggle);
  gimp_help_set_help_data (toggle,
			   _("If checked the mapping will begin at the right "
			     "side, as opposed to beginning at the left."),
			   NULL);

  toggle = gtk_check_button_new_with_label (_("Map from Top"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), pcvals.inverse);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled", 
		      (GtkSignalFunc) polar_toggle_callback,
		      &pcvals.inverse);
  gtk_box_pack_start (GTK_BOX (hbox), toggle, TRUE, TRUE, 0);
  gtk_widget_show (toggle);
  gimp_help_set_help_data (toggle,
			   _("If unchecked the mapping will put the bottom "
			     "row in the middle and the top row on the "
			     "outside.  If checked it will be the opposite."),
			   NULL);

  toggle = gtk_check_button_new_with_label (_("To Polar"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), pcvals.polrec);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled", 
		      (GtkSignalFunc) polar_toggle_callback,
		      &pcvals.polrec);
  gtk_box_pack_start (GTK_BOX (hbox), toggle, TRUE, TRUE, 0);
  gtk_widget_show (toggle);
  gimp_help_set_help_data (toggle,
			   _("If unchecked the image will be circularly "
			     "mapped onto a rectangle.  If checked the image "
			     "will be mapped onto a circle."),
			   NULL);

  gtk_widget_show (hbox);

  /* Done */

  gtk_widget_show (dialog);
  dialog_update_preview ();

  gtk_main ();
  gimp_help_free ();
  gdk_flush ();

  g_free (pcint.check_row_0);
  g_free (pcint.check_row_1);
  g_free (pcint.image);
  g_free (pcint.dimage);

  return pcint.run;
}

static void
dialog_update_preview (void)
{
  gdouble  left, right, bottom, top;
  gdouble  dx, dy;
  gdouble  px, py;
  gdouble  cx = 0.0, cy = 0.0;
  gint     ix, iy;
  gint     x, y;
  gdouble  scale_x, scale_y;
  guchar  *p_ul, *i, *p;
  guchar  *check_ul;
  gint     check;
  guchar   outside[4];
  GimpRGB  background;

  gimp_palette_get_background (&background);

  switch (img_bpp)
    {
    case 1:
      outside[0] = outside[1] = outside [2] = gimp_rgb_intensity_uchar (&background);
      outside[3] = 255;
      break;

    case 2:
      outside[0] = outside[1] = outside [2] = gimp_rgb_intensity_uchar (&background);
      outside[3] = 0;
      break;

    case 3:
      gimp_rgb_get_uchar (&background,
			  &outside[0], &outside[1], &outside[2]);
      outside[3] = 255;
      break;

    case 4:
      gimp_rgb_get_uchar (&background,
			  &outside[0], &outside[1], &outside[2]);
      outside[3] = 0;
      break;
    }

  left   = sel_x1;
  right  = sel_x2 - 1;
  bottom = sel_y2 - 1;
  top    = sel_y1;

  dx = (right - left) / (preview_width - 1);
  dy = (bottom - top) / (preview_height - 1);

  scale_x = (double) preview_width / (right - left + 1);
  scale_y = (double) preview_height / (bottom - top + 1);

  py = top;

  p_ul = pcint.dimage;
/* p_lr = pcint.dimage + 3 * (preview_width * preview_height - 1);*/

  for (y = 0; y < preview_height; y++)
    {
      px = left;

      if ((y / GIMP_CHECK_SIZE) & 1)
	check_ul = pcint.check_row_0;
      else
	check_ul = pcint.check_row_1;

      for (x = 0; x < preview_width; x++)
	{
	  calc_undistorted_coords (px, py, &cx, &cy);

	  cx = (cx - left) * scale_x;
	  cy = (cy - top) * scale_y;

	  ix = (int) (cx + 0.5);
	  iy = (int) (cy + 0.5);

	  check = check_ul[x];

	  if ((ix >= 0) && (ix < preview_width) &&
	      (iy >= 0) && (iy < preview_height))
	    i = pcint.image + 4 * (preview_width * iy + ix);
	  else
	    i = outside;

	  p_ul[0] = check + ((i[0] - check) * i[3]) / 255;
	  p_ul[1] = check + ((i[1] - check) * i[3]) / 255;
	  p_ul[2] = check + ((i[2] - check) * i[3]) / 255;

	  p_ul += 3;

	  px += dx;
	}

      py += dy;
    }

  p = pcint.dimage;

  for (y = 0; y < img_height; y++)
    {
      gtk_preview_draw_row (GTK_PREVIEW (pcint.preview), p, 0, y, preview_width);

      p += preview_width * 3;
    }

  gtk_widget_draw (pcint.preview, NULL);
  gdk_flush ();
}

static void
dialog_scale_update (GtkAdjustment *adjustment,
		     gdouble       *value)
{
  gimp_double_adjustment_update (adjustment, value);

  dialog_update_preview ();
}

static void
dialog_ok_callback (GtkWidget *widget,
		    gpointer   data)
{
  pcint.run = TRUE;

  gtk_widget_destroy (GTK_WIDGET (data));
}

static void
polar_toggle_callback (GtkWidget *widget,
		       gpointer   data)
{
  gimp_toggle_button_update (widget, data);

  dialog_update_preview ();
}
