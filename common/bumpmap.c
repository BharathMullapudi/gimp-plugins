/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * Bump map plug-in --- emboss an image by using another image as a bump map
 * Copyright (C) 1997 Federico Mena Quintero <federico@nuclecu.unam.mx>
 * Copyright (C) 1997-2000 Jens Lautenbacher <jtl@gimp.org>
 * Copyright (C) 2000 Sven Neumann <sven@gimp.org>
 * 
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


/* This plug-in uses the algorithm described by John Schlag, "Fast
 * Embossing Effects on Raster Image Data" in Graphics Gems IV (ISBN
 * 0-12-336155-9).  It takes a grayscale image to be applied as a
 * bump-map to another image, producing a nice embossing effect.
 */

/* Version 3.0-pre1-ac2:
 *
 * - waterlevel/ambient restricted to 0-255
 * - correctly initialize bumpmap_offsets
 */   

/* Version 3.0-pre1-ac1:
 *
 * - Now able not to tile the bumpmap - this is the default.
 * - Added new PDB call plug_in_bumpmap_tiled.
 * - Added scrollbars for preview.
 * - Fixed slider feedback for bumpmap offset and set initial offsets
 *   from drawable offsets.
 * - Make it work as intended from the very beginning...
 */
  
/* Version 2.04:
 *
 * - The preview is now scrollable via draging with button 1 in the
 * preview area. Thanks to Quartic for helping with gdk event handling.
 *
 * - The bumpmap's offset can alternatively be adjusted by dragging with
 * button 3 in the preview area.
 */
 
/* Version 2.03:
 *
 * - Now transparency in the bumpmap drawable is handled as specified
 * by the waterlevel parameter.  Thanks to Jens for suggesting it!
 *
 * - New cool ambient lighting method.  Thanks to Jens Lautenbacher
 * for creating it!  Something useful actually came out of those IRC
 * sessions ;-)
 *
 * - Added proper rounding wherever it seemed appropriate.  This fixes
 * some minor artifacts in the output.
 */


/* Version 2.02:
 *
 * - Fixed a stupid bug in the preview code (offsets were not wrapped
 * correctly in some situations).  Thanks to Jens Lautenbacher for
 * reporting it!
 */


/* Version 2.01:
 *
 * - For the preview, vertical scrolling and setting the vertical
 * bumpmap offset are now *much* faster.  Instead of calling
 * gimp_pixel_rgn_get_row() a lot of times, I now use an adapted
 * version of gimp_pixel_rgn_get_rect().
 */


/* Version 2.00:
 *
 * - Rewrote from the 0.54 version (well, from the 0.99.9
 * distribution, actually...).  New in this release are the correct
 * handling of all image depths, sizes, and offsets.  Also the
 * different map types, the compensation and map inversion options
 * were added.  The preview widget is new, too.
 */


/* TODO:
 *
 * - Speed-ups
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef __GNUC__
#warning GTK_DISABLE_DEPRECATED
#endif
#undef GTK_DISABLE_DEPRECATED

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


/***** Magic numbers *****/

#define PLUG_IN_VERSION "April 2000, 3.0-pre1-ac2"

#define PREVIEW_SIZE    128
#define SCALE_WIDTH       0

/***** Types *****/

enum
{
  LINEAR = 0,
  SPHERICAL,
  SINUOSIDAL
};

enum
{
  DRAG_NONE = 0,
  DRAG_SCROLL,
  DRAG_BUMPMAP
};

typedef struct
{
  gint32  bumpmap_id;
  gdouble azimuth;
  gdouble elevation;
  gint    depth;
  gint    xofs;
  gint    yofs;
  gint    waterlevel;
  gint    ambient;
  gint    compensate;
  gint    invert;
  gint    type;
  gint    tiled;
} bumpmap_vals_t;

typedef struct
{
  gint    lx, ly;       /* X and Y components of light vector */
  gint    nz2, nzlz;    /* nz^2, nz*lz */
  gint    background;   /* Shade for vertical normals */
  gdouble compensation; /* Background compensation */
  guchar  lut[256];     /* Look-up table for modes */
} bumpmap_params_t;

typedef struct
{
  GtkWidget   *preview;
  GtkObject   *preview_adj_x;
  GtkObject   *preview_adj_y;
  gint         preview_width;
  gint         preview_height;
  gint         mouse_x;
  gint         mouse_y;
  gint         preview_xofs;
  gint         preview_yofs;
  gint         drag_mode;

  GtkObject   *offset_adj_x;
  GtkObject   *offset_adj_y;
  
  guchar      *check_row_0;
  guchar      *check_row_1;

  guchar     **src_rows;
  guchar     **bm_rows;

  gint         src_yofs;
  gint         bm_yofs;

  GimpDrawable   *bm_drawable;
  gint         bm_width;
  gint         bm_height;
  gint         bm_bpp;
  gint         bm_has_alpha;

  GimpPixelRgn    src_rgn;
  GimpPixelRgn    bm_rgn;

  bumpmap_params_t params;

  gint         run;
} bumpmap_interface_t;


/***** Prototypes *****/

static void query (void);
static void run   (gchar   *name,
		   gint     nparams,
		   GimpParam  *param,
		   gint    *nreturn_vals,
		   GimpParam **return_vals);

static void bumpmap             (void);
static void bumpmap_init_params (bumpmap_params_t *params);
static void bumpmap_row         (guchar           *src_row,
				 guchar           *dest_row,
				 gint              width,
				 gint              bpp,
				 gint              has_alpha,
				 guchar           *bm_row1,
				 guchar           *bm_row2,
				 guchar           *bm_row3,
				 gint              bm_width,
				 gint              bm_xofs,
				 gboolean          tiled,
				 gboolean          row_in_bumpmap,       
				 bumpmap_params_t *params);
static void bumpmap_convert_row (guchar           *row,
				 gint              width,
				 gint              bpp,
				 gint              has_alpha,
				 guchar           *lut);

static gint bumpmap_dialog              (void);
static void dialog_init_preview         (void);
static void dialog_new_bumpmap          (gboolean init_offsets);
static void dialog_update_preview       (void);
static gint dialog_preview_events       (GtkWidget *widget, GdkEvent *event);
static void dialog_scroll_src           (void);
static void dialog_scroll_bumpmap       (void);
static void dialog_get_rows             (GimpPixelRgn *pr, guchar **rows,
					 gint x, gint y,
					 gint width, gint height);
static void dialog_fill_src_rows        (gint start, gint how_many, gint yofs);
static void dialog_fill_bumpmap_rows    (gint start, gint how_many, gint yofs);

static void dialog_compensate_callback  (GtkWidget *widget, gpointer data);
static void dialog_invert_callback      (GtkWidget *widget, gpointer data);
static void dialog_tiled_callback       (GtkWidget *widget, gpointer data);
static void dialog_map_type_callback    (GtkWidget *widget, gpointer data);
static gint dialog_constrain            (gint32 image_id, gint32 drawable_id,
					 gpointer data);
static void dialog_bumpmap_callback     (gint32 id, gpointer data);
static void dialog_dscale_update        (GtkAdjustment *adjustment,
					 gdouble *value);
static void dialog_iscale_update_normal (GtkAdjustment *adjustment, gint *value);
static void dialog_iscale_update_full   (GtkAdjustment *adjustment, gint *value);
static void dialog_ok_callback          (GtkWidget *widget, gpointer data);

/***** Variables *****/

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run    /* run_proc   */
};

static bumpmap_vals_t bmvals =
{
  -1,     /* bumpmap_id */
  135.0,  /* azimuth */
  45.0,   /* elevation */
  3,      /* depth */
  0,      /* xofs */
  0,      /* yofs */
  0,      /* waterlevel */
  0,      /* ambient */
  FALSE,  /* compensate */
  FALSE,  /* invert */
  LINEAR, /* type */
  FALSE   /* tiled */
};

static bumpmap_interface_t bmint =
{
  NULL,      /* preview */
  NULL,      /* preview_adj_x */
  NULL,      /* preview_adj_y */
  0,         /* preview_width */
  0,         /* preview_height */
  0,         /* mouse_x */
  0,         /* mouse_y */
  0,         /* preview_xofs */
  0,         /* preview_yofs */
  DRAG_NONE, /* drag_mode */
  NULL,      /* offset_adj_x */
  NULL,      /* offset_adj_y */
  NULL,      /* check_row_0 */
  NULL,      /* check_row_1 */
  NULL,      /* src_rows */
  NULL,      /* bm_rows */
  0,         /* src_yofs */
  -1,        /* bm_yofs */
  NULL,      /* bm_drawable */
  0,         /* bm_width */
  0,         /* bm_height */
  0,         /* bm_bpp */
  0,         /* bm_has_alpha */
  { 0 },     /* src_rgn */
  { 0 },     /* bm_rgn */
  { 0 },     /* params */
  FALSE      /* run */
};

static GimpDrawable *drawable = NULL;

static gint       sel_x1, sel_y1;
static gint       sel_x2, sel_y2;
static gint       sel_width, sel_height;
static gint       img_bpp;
static gboolean   img_has_alpha;

/***** Functions *****/

MAIN ()

static void
query (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32,    "run_mode",   "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE,    "image",      "Input image" },
    { GIMP_PDB_DRAWABLE, "drawable",   "Input drawable" },
    { GIMP_PDB_DRAWABLE, "bumpmap",    "Bump map drawable" },
    { GIMP_PDB_FLOAT,    "azimuth",    "Azimuth" },
    { GIMP_PDB_FLOAT,    "elevation",  "Elevation" },
    { GIMP_PDB_INT32,    "depth",      "Depth" },
    { GIMP_PDB_INT32,    "xofs",       "X offset" },
    { GIMP_PDB_INT32,    "yofs",       "Y offset" },
    { GIMP_PDB_INT32,    "waterlevel", "Level that full transparency should represent" },
    { GIMP_PDB_INT32,    "ambient",    "Ambient lighting factor" },
    { GIMP_PDB_INT32,    "compensate", "Compensate for darkening" },
    { GIMP_PDB_INT32,    "invert",     "Invert bumpmap" },
    { GIMP_PDB_INT32,    "type",       "Type of map (LINEAR (0), SPHERICAL (1), SINUOSIDAL (2))" }
  };

  gimp_install_procedure ("plug_in_bump_map",
			  "Create an embossing effect using an image as a "
			  "bump map",
			  "This plug-in uses the algorithm described by John "
			  "Schlag, \"Fast Embossing Effects on Raster Image "
			  "Data\" in Graphics GEMS IV (ISBN 0-12-336155-9). "
			  "It takes a drawable to be applied as a bump "
			  "map to another image and produces a nice embossing "
			  "effect.",
			  "Federico Mena Quintero, Jens Lautenbacher & Sven Neumann",
			  "Federico Mena Quintero, Jens Lautenbacher & Sven Neumann",
			  PLUG_IN_VERSION,
			  N_("<Image>/Filters/Map/Bump Map..."),
			  "RGB*, GRAY*",
			  GIMP_PLUGIN,
			  G_N_ELEMENTS (args), 0,
			  args, NULL);

  gimp_install_procedure ("plug_in_bump_map_tiled",
			  "Create an embossing effect using a tiled image "
			  "as a bump map",
			  "This plug-in uses the algorithm described by John "
			  "Schlag, \"Fast Embossing Effects on Raster Image "
			  "Data\" in Graphics GEMS IV (ISBN 0-12-336155-9). "
			  "It takes a drawable to be tiled and applied as a "
			  "bump map to another image and produces a nice "
			  "embossing effect.",
			  "Federico Mena Quintero, Jens Lautenbacher & Sven Neumann",
			  "Federico Mena Quintero, Jens Lautenbacher & Sven Neumann",
			  PLUG_IN_VERSION,
			  NULL,
			  "RGB*, GRAY*",
			  GIMP_PLUGIN,
			  G_N_ELEMENTS (args), 0,
			  args, NULL);
}

static void
run (gchar   *name,
     gint     nparams,
     GimpParam  *param,
     gint    *nreturn_vals,
     GimpParam **return_vals)
{
  static GimpParam values[1];

  GimpRunMode run_mode;
  GimpPDBStatusType  status;

  INIT_I18N_UI();

  status   = GIMP_PDB_SUCCESS;
  run_mode = param[0].data.d_int32;

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  *nreturn_vals = 1;
  *return_vals  = values;

  /* Get drawable information */
  drawable = gimp_drawable_get (param[2].data.d_drawable);

  gimp_drawable_mask_bounds (drawable->drawable_id,
			     &sel_x1, &sel_y1, &sel_x2, &sel_y2);

  sel_width     = sel_x2 - sel_x1;
  sel_height    = sel_y2 - sel_y1;
  img_bpp       = gimp_drawable_bpp (drawable->drawable_id);
  img_has_alpha = gimp_drawable_has_alpha (drawable->drawable_id);

  /* See how we will run */
  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE:
      /* Possibly retrieve data */
      gimp_get_data (name, &bmvals);
  
      /* Get information from the dialog */
      if (!bumpmap_dialog ())
	return;

      break;

    case GIMP_RUN_NONINTERACTIVE:
      /* Make sure all the arguments are present */
      if (nparams != 14)
	{
	  status = GIMP_PDB_CALLING_ERROR;
	}
      else
	{
	  bmvals.bumpmap_id = param[3].data.d_drawable;
	  bmvals.azimuth    = param[4].data.d_float;
	  bmvals.elevation  = param[5].data.d_float;
	  bmvals.depth      = param[6].data.d_int32;
	  bmvals.depth      = param[6].data.d_int32;
	  bmvals.xofs       = param[7].data.d_int32;
	  bmvals.yofs       = param[8].data.d_int32;
	  bmvals.waterlevel = param[9].data.d_int32;
	  bmvals.ambient    = param[10].data.d_int32;
	  bmvals.compensate = param[11].data.d_int32;
	  bmvals.invert     = param[12].data.d_int32;
	  bmvals.type       = param[13].data.d_int32;
	  bmvals.tiled      = strcmp (name, "plug_in_bump_map_tiled") == 0;  
	}
      break;

    case GIMP_RUN_WITH_LAST_VALS:
      /* Possibly retrieve data */
      gimp_get_data (name, &bmvals);
      break;

    default:
      break;
    }

  /* Bumpmap the image */

  if (status == GIMP_PDB_SUCCESS)
    {
      if ((gimp_drawable_is_rgb(drawable->drawable_id) ||
	   gimp_drawable_is_gray(drawable->drawable_id)))
	{
	  /* Run! */
	  bumpmap ();

	  /* If run mode is interactive, flush displays */
	  if (run_mode != GIMP_RUN_NONINTERACTIVE)
	    gimp_displays_flush ();

	  /* Store data */
	  if (run_mode == GIMP_RUN_INTERACTIVE)
	    gimp_set_data (name, &bmvals, sizeof (bumpmap_vals_t));
	}
    }
  else
    status = GIMP_PDB_EXECUTION_ERROR;

  values[0].data.d_status = status;

  gimp_drawable_detach (drawable);
}

static void
bumpmap (void)
{
  bumpmap_params_t  params;
  GimpDrawable        *bm_drawable;
  GimpPixelRgn         src_rgn, dest_rgn, bm_rgn;
  gint              bm_width, bm_height, bm_bpp, bm_has_alpha;
  gint              yofs1, yofs2, yofs3;
  guchar           *bm_row1, *bm_row2, *bm_row3, *bm_tmprow;
  guchar           *src_row, *dest_row;
  gint              y;
  gint              progress;
  gint              tmp;
  gint              drawable_tiles_per_row, bm_tiles_per_row;

#if 0
  g_print ("bumpmap: waiting... (pid %d)\n", getpid ());
  kill (getpid (), SIGSTOP);
#endif

  gimp_progress_init (_("Bump-mapping..."));
	
  /* Get the bumpmap drawable */
  if (bmvals.bumpmap_id != -1)
    bm_drawable = gimp_drawable_get (bmvals.bumpmap_id);
  else
    bm_drawable = drawable;

  if (!bm_drawable)
    return;

  /* Get image information */
  bm_width     = gimp_drawable_width (bm_drawable->drawable_id);
  bm_height    = gimp_drawable_height (bm_drawable->drawable_id);
  bm_bpp       = gimp_drawable_bpp (bm_drawable->drawable_id);
  bm_has_alpha = gimp_drawable_has_alpha (bm_drawable->drawable_id);

  /* Set the tile cache size */
  /* Compute number of tiles needed for one row of the drawable */
  drawable_tiles_per_row =
    1
    + (sel_x2 + gimp_tile_width () - 1) / gimp_tile_width ()
    - sel_x1 / gimp_tile_width ();
  /* Compute number of tiles needed for one row of the bitmap */
  bm_tiles_per_row = (bm_width + gimp_tile_width () - 1) / gimp_tile_width ();
  /* Cache one row of source, destination and bitmap */
  gimp_tile_cache_ntiles (bm_tiles_per_row + 2 * drawable_tiles_per_row);

  /* Initialize offsets */
  tmp = bmvals.yofs + sel_y1;
  if (tmp < 0)
    yofs2 = bm_height - (- tmp % bm_height);
  else
    yofs2 = tmp % bm_height;

  yofs1 = (yofs2 + bm_height - 1) % bm_height;
  yofs3 = (yofs2 + 1) % bm_height;

  /* Initialize row buffers */
  bm_row1 = g_new (guchar, bm_width * bm_bpp);
  bm_row2 = g_new (guchar, bm_width * bm_bpp);
  bm_row3 = g_new (guchar, bm_width * bm_bpp);

  src_row  = g_new (guchar, sel_width * img_bpp);
  dest_row = g_new (guchar, sel_width * img_bpp);

  /* Initialize pixel regions */
  gimp_pixel_rgn_init (&src_rgn, drawable,
		       sel_x1, sel_y1, sel_width, sel_height, FALSE, FALSE);
  gimp_pixel_rgn_init (&dest_rgn, drawable,
		       sel_x1, sel_y1, sel_width, sel_height, TRUE, TRUE);
  gimp_pixel_rgn_init (&bm_rgn, bm_drawable,
		       0, 0, bm_width, bm_height, FALSE, FALSE);

  /* Bumpmap */

  bumpmap_init_params (&params);

  gimp_pixel_rgn_get_row (&bm_rgn, bm_row1, 0, yofs1, bm_width);
  gimp_pixel_rgn_get_row (&bm_rgn, bm_row2, 0, yofs2, bm_width);
  gimp_pixel_rgn_get_row (&bm_rgn, bm_row3, 0, yofs3, bm_width);

  bumpmap_convert_row (bm_row1, bm_width, bm_bpp, bm_has_alpha, params.lut);
  bumpmap_convert_row (bm_row2, bm_width, bm_bpp, bm_has_alpha, params.lut);
  bumpmap_convert_row (bm_row3, bm_width, bm_bpp, bm_has_alpha, params.lut);

  progress = 0;

  for (y = sel_y1; y < sel_y2; y++)
    {
      gimp_pixel_rgn_get_row (&src_rgn, src_row, sel_x1, y, sel_width);

      bumpmap_row (src_row, dest_row, sel_width, img_bpp, img_has_alpha,
		   bm_row1, bm_row2, bm_row3, bm_width, bmvals.xofs,
		   bmvals.tiled, 
		   y == CLAMP (y, - bmvals.yofs, - bmvals.yofs + bm_height),
		   &params);

      gimp_pixel_rgn_set_row (&dest_rgn, dest_row, sel_x1, y, sel_width);

      /* Next line */

      bm_tmprow = bm_row1;
      bm_row1   = bm_row2;
      bm_row2   = bm_row3;
      bm_row3   = bm_tmprow;
		
      if (++yofs3 == bm_height)
	yofs3 = 0;

      gimp_pixel_rgn_get_row (&bm_rgn, bm_row3, 0, yofs3, bm_width);
      bumpmap_convert_row (bm_row3, bm_width, bm_bpp, bm_has_alpha, params.lut);

      gimp_progress_update ((double) ++progress / sel_height);
    }

  /* Done */

  g_free (bm_row1);
  g_free (bm_row2);
  g_free (bm_row3);
  g_free (src_row);
  g_free (dest_row);

  if (bm_drawable != drawable)
    gimp_drawable_detach (bm_drawable);

  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
  gimp_drawable_update (drawable->drawable_id, sel_x1, sel_y1, sel_width, sel_height);
}

static void
bumpmap_init_params (bumpmap_params_t *params)
{
  gdouble azimuth;
  gdouble elevation;
  gint    lz, nz;
  gint    i;
  gdouble n;

  /* Convert to radians */
  azimuth   = G_PI * bmvals.azimuth / 180.0;
  elevation = G_PI * bmvals.elevation / 180.0;

  /* Calculate the light vector */
  params->lx = cos(azimuth) * cos(elevation) * 255.0;
  params->ly = sin(azimuth) * cos(elevation) * 255.0;
  lz         = sin(elevation) * 255.0;

  /* Calculate constant Z component of surface normal */
  nz           = (6 * 255) / bmvals.depth;
  params->nz2  = nz * nz;
  params->nzlz = nz * lz;

  /* Optimize for vertical normals */
  params->background = lz;

  /* Calculate darkness compensation factor */
  params->compensation = sin(elevation);

  /* Create look-up table for map type */
  for (i = 0; i < 256; i++)
    {
      switch (bmvals.type)
	{
	case SPHERICAL:
	  n = i / 255.0 - 1.0;
	  params->lut[i] = (int) (255.0 * sqrt(1.0 - n * n) + 0.5);
	  break;

	case SINUOSIDAL:
	  n = i / 255.0;
	  params->lut[i] = (int) (255.0 * (sin((-G_PI / 2.0) + G_PI * n) + 1.0) /
				  2.0 + 0.5);
	  break;

	case LINEAR:
	default:
	  params->lut[i] = i;
	}

      if (bmvals.invert)
	params->lut[i] = 255 - params->lut[i];
    }
}

static void
bumpmap_row (guchar           *src,
	     guchar           *dest,
	     gint              width,
	     gint              bpp,
	     gint              has_alpha,
	     guchar           *bm_row1,
	     guchar           *bm_row2,
	     guchar           *bm_row3,
	     gint              bm_width,
	     gint              bm_xofs,
	     gboolean          tiled,
	     gboolean          row_in_bumpmap,       
	     bumpmap_params_t *params)
{
  gint xofs1, xofs2, xofs3;
  gint shade;
  gint ndotl;
  gint nx, ny;
  gint x, k;
  gint pbpp;
  gint result;
  gint tmp;

  if (has_alpha)
    pbpp = bpp - 1;
  else
    pbpp = bpp;

  tmp = bm_xofs + sel_x1;
  if (tmp < 0)
    xofs2 = bm_width - (- tmp % bm_width);
  else
    xofs2 = tmp % bm_width;

  xofs1 = (xofs2 + bm_width - 1) % bm_width;
  xofs3 = (xofs2 + 1) % bm_width;

  for (x = 0; x < width; x++)
    {
      /* Calculate surface normal from bump map */

      if (tiled || (row_in_bumpmap &&
		    x == CLAMP (x, - tmp, - tmp + bm_width)))
	{
	  nx = (bm_row1[xofs1] + bm_row2[xofs1] + bm_row3[xofs1] -
		bm_row1[xofs3] - bm_row2[xofs3] - bm_row3[xofs3]);
	  ny = (bm_row3[xofs1] + bm_row3[xofs2] + bm_row3[xofs3] -
		bm_row1[xofs1] - bm_row1[xofs2] - bm_row1[xofs3]);
	}
       else 
	 {
	   nx = ny = 0;
	 }

      /* Shade */

      if ((nx == 0) && (ny == 0))
	shade = params->background;
      else
	{
	  ndotl = nx * params->lx + ny * params->ly + params->nzlz;

	  if (ndotl < 0)
	    shade = params->compensation * bmvals.ambient;
	  else
	    {
	      shade = ndotl / sqrt(nx * nx + ny * ny + params->nz2);

	      shade = shade + MAX(0, (255 * params->compensation - shade)) *
		bmvals.ambient / 255;
	    }
	}

      /* Paint */

      if (bmvals.compensate)
	for (k = pbpp; k; k--)
	  {
	    result  = (*src++ * shade) / (params->compensation * 255);
	    *dest++ = MIN(255, result);
	  }
      else
	for (k = pbpp; k; k--)
	  *dest++ = *src++ * shade / 255;

      if (has_alpha)
	*dest++ = *src++;

      /* Next pixel */

      if (++xofs1 == bm_width)
	xofs1 = 0;

      if (++xofs2 == bm_width)
	xofs2 = 0;

      if (++xofs3 == bm_width)
	xofs3 = 0;
    }
}

static void
bumpmap_convert_row (guchar *row, 
		     gint    width, 
		     gint    bpp, 
		     gint    has_alpha, 
		     guchar *lut)
{
  guchar *p;

  p = row;

  has_alpha = has_alpha ? 1 : 0;

  if (bpp >= 3)
    for (; width; width--)
      {
	if (has_alpha)
	  *p++ = lut[(int) (bmvals.waterlevel +
			    (((int) (INTENSITY (row[0], row[1], row[2]) + 0.5) - 
			      bmvals.waterlevel) * 
			     row[3]) / 255.0)];
	else
	  *p++ = lut[(int) (INTENSITY (row[0], row[1], row[2]) + 0.5)];

	row += 3 + has_alpha;
      }
  else
    for (; width; width--)
      {
	if (has_alpha)
	  *p++ = lut[bmvals.waterlevel +
		    ((row[0] - bmvals.waterlevel) * row[1]) / 255];
	else
	  *p++ = lut[*row];

	row += 1 + has_alpha;
      }
}

static gint
bumpmap_dialog (void)
{
  GtkWidget *dialog;
  GtkWidget *top_vbox;
  GtkWidget *hbox;
  GtkWidget *frame;
  GtkWidget *preview;
  GtkWidget *vbox;
  GtkWidget *sep;
  GtkWidget *abox;
  GtkWidget *pframe;
  GtkWidget *ptable;
  GtkWidget *scrollbar;
  GtkWidget *table;
  GtkWidget *right_vbox;
  GtkWidget *option_menu;
  GtkWidget *menu;
  GtkWidget *button;
  GtkObject *adj;
  gint       i;
  gint       row;

  gimp_ui_init ("bumpmap", TRUE);

  dialog = gimp_dialog_new (_("Bump Map"), "bumpmap",
			    gimp_standard_help_func, "filters/bumpmap.html",
			    GTK_WIN_POS_MOUSE,
			    FALSE, TRUE, FALSE,

			    GTK_STOCK_CANCEL, gtk_widget_destroy,
			    NULL, 1, NULL, FALSE, TRUE,
			    GTK_STOCK_OK, dialog_ok_callback,
			    NULL, NULL, NULL, TRUE, FALSE,

			    NULL);

  g_signal_connect (G_OBJECT (dialog), "destroy",
                    G_CALLBACK (gtk_main_quit),
                    NULL);

  top_vbox = gtk_vbox_new (FALSE, 4);
  gtk_container_set_border_width (GTK_CONTAINER (top_vbox), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), top_vbox,
		      FALSE, FALSE, 0);
  gtk_widget_show (top_vbox);

  hbox = gtk_hbox_new (FALSE, 6);
  gtk_box_pack_start (GTK_BOX (top_vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  /* Preview */
  abox = gtk_alignment_new (0.5, 0.5, 0.0, 0.0);
  gtk_box_pack_start (GTK_BOX (hbox), abox, FALSE, FALSE, 0);
  gtk_widget_show (abox);

  ptable = gtk_table_new (2, 2, FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (ptable), 4);
  gtk_container_add (GTK_CONTAINER (abox), ptable);
  gtk_widget_show (ptable);
  
  pframe = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (pframe), GTK_SHADOW_IN);
  gtk_container_set_border_width (GTK_CONTAINER (pframe), 0);
  gtk_table_attach (GTK_TABLE (ptable), pframe, 0, 1, 0, 1, 
		    GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);
  gtk_widget_show (pframe);

  bmint.preview_width  = MIN (sel_width, PREVIEW_SIZE);
  bmint.preview_height = MIN (sel_height, PREVIEW_SIZE);

  bmint.preview = preview = gtk_preview_new (GTK_PREVIEW_COLOR);
  gtk_preview_size (GTK_PREVIEW (bmint.preview),
		    bmint.preview_width, bmint.preview_height);
  gtk_container_add (GTK_CONTAINER (pframe), bmint.preview);
  gtk_widget_show (bmint.preview);
  
  bmint.preview_adj_x = 
    gtk_adjustment_new (0, 0, sel_width, 1, 10, bmint.preview_width);
  if (sel_width > PREVIEW_SIZE)
    {
      scrollbar = gtk_hscrollbar_new (GTK_ADJUSTMENT (bmint.preview_adj_x));
      gtk_table_attach (GTK_TABLE (ptable), scrollbar, 0, 1, 1, 2, 
			GTK_FILL | GTK_EXPAND, 0, 0, 0);
      gtk_widget_show (scrollbar);
    }
  
  bmint.preview_adj_y = 
    gtk_adjustment_new (0, 0, sel_height, 1, 10, bmint.preview_height);
  if (sel_height > PREVIEW_SIZE)
    {
      scrollbar = gtk_vscrollbar_new (GTK_ADJUSTMENT (bmint.preview_adj_y));
      gtk_table_attach (GTK_TABLE (ptable), scrollbar, 1, 2, 0,1, 
			0, GTK_FILL | GTK_EXPAND, 0, 0);
      gtk_widget_show (scrollbar);
    }

  gtk_widget_set_events (bmint.preview, 
			 GDK_BUTTON_PRESS_MASK |
			 GDK_BUTTON_RELEASE_MASK | 
			 GDK_BUTTON_MOTION_MASK |
			 GDK_POINTER_MOTION_HINT_MASK);

  g_signal_connect (G_OBJECT (bmint.preview), "event",
                    G_CALLBACK (dialog_preview_events),
                    NULL);
  g_signal_connect (G_OBJECT (bmint.preview_adj_x), "value_changed",
                    G_CALLBACK (dialog_iscale_update_normal), 
                    &bmint.preview_xofs);
  g_signal_connect (G_OBJECT (bmint.preview_adj_y), "value_changed",
                    G_CALLBACK (dialog_iscale_update_normal), 
                    &bmint.preview_yofs);

  dialog_init_preview ();

  /* Type of map */
  frame =
    gimp_radio_group_new2 (TRUE, _("Map Type"),
			   G_CALLBACK (dialog_map_type_callback),
			   &bmvals.type, (gpointer) bmvals.type,

			   _("_Linear Map"),     (gpointer) LINEAR, NULL,
			   _("_Spherical Map"),  (gpointer) SPHERICAL, NULL,
			   _("S_inuosidal Map"), (gpointer) SINUOSIDAL, NULL,

			   NULL);
  gtk_box_pack_start (GTK_BOX (hbox), frame, TRUE, TRUE, 0);
  gtk_widget_show (frame);

  right_vbox = GTK_BIN (frame)->child;

  sep = gtk_hseparator_new ();
  gtk_box_pack_start (GTK_BOX (right_vbox), sep, FALSE, FALSE, 1);
  gtk_widget_show (sep);

  /* Compensate darkening */
  button = gtk_check_button_new_with_mnemonic (_("Co_mpensate for Darkening"));
  gtk_box_pack_start (GTK_BOX (right_vbox), button, FALSE, FALSE, 0);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
				bmvals.compensate ? TRUE : FALSE);
  gtk_widget_show (button);

  g_signal_connect (G_OBJECT (button), "toggled",
                    G_CALLBACK (dialog_compensate_callback),
                    NULL);

  /* Invert bumpmap */
  button = gtk_check_button_new_with_mnemonic (_("I_nvert Bumpmap"));
  gtk_box_pack_start (GTK_BOX (right_vbox), button, FALSE, FALSE, 0);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
				bmvals.invert ? TRUE : FALSE);
  gtk_widget_show (button);

  g_signal_connect (G_OBJECT (button), "toggled",
                    G_CALLBACK (dialog_invert_callback),
                    NULL);

  /* Tile bumpmap */
  button = gtk_check_button_new_with_mnemonic (_("_Tile Bumpmap"));
  gtk_box_pack_start (GTK_BOX (right_vbox), button, FALSE, FALSE, 0);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
				bmvals.tiled ? TRUE : FALSE);
  gtk_widget_show (button);  

  g_signal_connect (G_OBJECT (button), "toggled",
                    G_CALLBACK (dialog_tiled_callback),
                    NULL);

  frame = gtk_frame_new (_("Parameter Settings"));
  gtk_box_pack_start (GTK_BOX (top_vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  vbox = gtk_vbox_new (FALSE, 2);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 4);
  gtk_container_add (GTK_CONTAINER (frame), vbox);
  gtk_widget_show (vbox);

  /* Bump map menu */
  table = gtk_table_new (1, 2, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  option_menu = gtk_option_menu_new ();
  menu = gimp_drawable_menu_new (dialog_constrain,
				 dialog_bumpmap_callback,
				 NULL,
				 bmvals.bumpmap_id);
  gtk_option_menu_set_menu (GTK_OPTION_MENU (option_menu), menu);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 0,
			     _("_Bump Map:"), 1.0, 0.5,
			     option_menu, 2, TRUE);

  sep = gtk_hseparator_new ();
  gtk_box_pack_start (GTK_BOX (vbox), sep, FALSE, FALSE, 0);
  gtk_widget_show (sep);

  /* Table for bottom controls */

  table = gtk_table_new (7, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  /* Controls */
  row = 0;

  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, row++,
			      _("_Azimuth:"), SCALE_WIDTH, 0,
			      bmvals.azimuth, 0.0, 360.0, 1.0, 15.0, 2,
			      TRUE, 0, 0,
			      NULL, NULL);
  g_signal_connect (G_OBJECT (adj), "value_changed",
                    G_CALLBACK (dialog_dscale_update),
                    &bmvals.azimuth);

  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, row++,
			      _("_Elevation:"), SCALE_WIDTH, 0,
			      bmvals.elevation, 0.5, 90.0, 1.0, 5.0, 2,
			      TRUE, 0, 0,
			      NULL, NULL);
  g_signal_connect (G_OBJECT (adj), "value_changed",
                    G_CALLBACK (dialog_dscale_update),
                    &bmvals.elevation);

  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, row,
			      _("_Depth:"), SCALE_WIDTH, 0,
			      bmvals.depth, 1.0, 65.0, 1.0, 5.0, 0,
			      TRUE, 0, 0,
			      NULL, NULL);
  g_signal_connect (G_OBJECT (adj), "value_changed",
                    G_CALLBACK (dialog_iscale_update_normal),
                    &bmvals.depth);
  gtk_table_set_row_spacing (GTK_TABLE (table), row++, 8);

  bmint.offset_adj_x = adj = 
    gimp_scale_entry_new (GTK_TABLE (table), 0, row++,
			  _("_X Offset:"), SCALE_WIDTH, 0,
			  bmvals.xofs, -1000.0, 1001.0, 1.0, 10.0, 0,
			  TRUE, 0, 0,
			  NULL, NULL);
  g_signal_connect (G_OBJECT (adj), "value_changed",
                    G_CALLBACK (dialog_iscale_update_normal),
                    &bmvals.xofs);

  bmint.offset_adj_y = adj = 
    gimp_scale_entry_new (GTK_TABLE (table), 0, row,
			  _("_Y Offset:"), SCALE_WIDTH, 0,
			  bmvals.yofs, -1000.0, 1001.0, 1.0, 10.0, 0,
			  TRUE, 0, 0,
			  NULL, NULL);
  g_signal_connect (G_OBJECT (adj), "value_changed",
                    G_CALLBACK (dialog_iscale_update_normal),
                    &bmvals.yofs);
  gtk_table_set_row_spacing (GTK_TABLE (table), row++, 8);

  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, row++,
			      _("_Waterlevel:"), SCALE_WIDTH, 0,
			      bmvals.waterlevel, 0.0, 255.0, 1.0, 8.0, 0,
			      TRUE, 0, 0,
			      NULL, NULL);
  g_signal_connect (G_OBJECT (adj), "value_changed",
                    G_CALLBACK (dialog_iscale_update_full),
                    &bmvals.waterlevel);

  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, row++,
			      _("A_mbient:"), SCALE_WIDTH, 0,
			      bmvals.ambient, 0.0, 255.0, 1.0, 8.0, 0,
			      TRUE, 0, 0,
			      NULL, NULL);
  g_signal_connect (G_OBJECT (adj), "value_changed",
                    G_CALLBACK (dialog_iscale_update_normal),
                    &bmvals.ambient);

  /* Done */

  gtk_widget_show (dialog);

  gtk_main ();
  gdk_flush ();

  g_free (bmint.check_row_0);
  g_free (bmint.check_row_1);

  for (i = 0; i < bmint.preview_height; i++)
    g_free (bmint.src_rows[i]);

  g_free (bmint.src_rows);

  for (i = 0; i < (bmint.preview_height + 2); i++)
    g_free (bmint.bm_rows[i]);

  g_free (bmint.bm_rows);

  if (bmint.bm_drawable != drawable)
    gimp_drawable_detach (bmint.bm_drawable);

  return bmint.run;
}

static void
dialog_init_preview (void)
{
  gint x;
	
  /* Create checkerboard rows */

  bmint.check_row_0 = g_new (guchar, bmint.preview_width);
  bmint.check_row_1 = g_new (guchar, bmint.preview_width);

  for (x = 0; x < bmint.preview_width; x++)
    if ((x / GIMP_CHECK_SIZE) & 1)
      {
	bmint.check_row_0[x] = GIMP_CHECK_DARK  * 255;
	bmint.check_row_1[x] = GIMP_CHECK_LIGHT * 255;
      }
    else
      {
	bmint.check_row_0[x] = GIMP_CHECK_LIGHT * 255;
	bmint.check_row_1[x] = GIMP_CHECK_DARK  * 255;
      }

  /* Initialize source rows */

  gimp_pixel_rgn_init (&bmint.src_rgn, drawable,
		       sel_x1, sel_y1, sel_width, sel_height, FALSE, FALSE);

  bmint.src_rows = g_new (guchar *, bmint.preview_height);

  for (x = 0; x < bmint.preview_height; x++)
    bmint.src_rows[x]  = g_new (guchar, sel_width * 4);

  dialog_fill_src_rows (0,
			bmint.preview_height,
			sel_y1 + bmint.preview_yofs);

  /* Initialize bumpmap rows */

  bmint.bm_rows = g_new (guchar *, bmint.preview_height + 2);

  for (x = 0; x < (bmint.preview_height + 2); x++)
    bmint.bm_rows[x] = NULL;
}

static gint
dialog_preview_events (GtkWidget *widget, 
		       GdkEvent  *event)
{
  gint            x, y;
  gint            dx, dy;
  GdkEventButton *bevent;
	
  gtk_widget_get_pointer (widget, &x, &y);

  bevent = (GdkEventButton *) event;

  switch (event->type)
    {
    case GDK_BUTTON_PRESS:
      switch (bevent->button)
	{
	case 1:
	case 2:  
	  if (bevent->state & GDK_SHIFT_MASK)
	    bmint.drag_mode = DRAG_BUMPMAP;
	  else
	    bmint.drag_mode = DRAG_SCROLL;
	  break;

	case 3:
	  bmint.drag_mode = DRAG_BUMPMAP;
	  break;

	default:
	  return FALSE;
	}

      bmint.mouse_x = x;
      bmint.mouse_y = y;

      gtk_grab_add (widget);

      break;

    case GDK_BUTTON_RELEASE:
      if (bmint.drag_mode != DRAG_NONE)
	{
	  gtk_grab_remove (widget);
	  bmint.drag_mode = DRAG_NONE;
	  dialog_update_preview ();
	}

      break;

    case GDK_MOTION_NOTIFY:
      dx = x - bmint.mouse_x;
      dy = y - bmint.mouse_y;

      bmint.mouse_x = x;
      bmint.mouse_y = y;

      if ((dx == 0) && (dy == 0))
	break;

      switch (bmint.drag_mode)
	{
	case DRAG_SCROLL:
	  bmint.preview_xofs = CLAMP (bmint.preview_xofs - dx,
				      0,
				      sel_width - bmint.preview_width);
	  gtk_signal_handler_block_by_data (GTK_OBJECT (bmint.preview_adj_x), 
					    &bmint.preview_xofs);
	  gtk_adjustment_set_value (GTK_ADJUSTMENT (bmint.preview_adj_x), 
				    bmint.preview_xofs);
	  gtk_signal_handler_unblock_by_data (GTK_OBJECT (bmint.preview_adj_x), 
					      &bmint.preview_xofs);
	  bmint.preview_yofs = CLAMP (bmint.preview_yofs - dy,
				      0,
				      sel_height - bmint.preview_height);
	  gtk_signal_handler_block_by_data (GTK_OBJECT (bmint.preview_adj_y), 
					    &bmint.preview_yofs);
	  gtk_adjustment_set_value (GTK_ADJUSTMENT (bmint.preview_adj_y), 
				    bmint.preview_yofs);
	  gtk_signal_handler_unblock_by_data (GTK_OBJECT (bmint.preview_adj_y), 
					      &bmint.preview_yofs);
	  
	  break;

	case DRAG_BUMPMAP:
	  bmvals.xofs = CLAMP (bmvals.xofs - dx, -1000, 1000);
	  gtk_signal_handler_block_by_data (GTK_OBJECT (bmint.offset_adj_x), 
					    &bmvals.xofs);
	  gtk_adjustment_set_value (GTK_ADJUSTMENT (bmint.offset_adj_x), 
				    bmvals.xofs);
	  gtk_signal_handler_unblock_by_data (GTK_OBJECT (bmint.offset_adj_x), 
					      &bmvals.xofs);

	  bmvals.yofs = CLAMP (bmvals.yofs - dy, -1000, 1000);
	  gtk_signal_handler_block_by_data (GTK_OBJECT (bmint.offset_adj_y), 
					    &bmvals.yofs);
	  gtk_adjustment_set_value (GTK_ADJUSTMENT (bmint.offset_adj_y), 
				    bmvals.yofs);
	  gtk_signal_handler_unblock_by_data (GTK_OBJECT (bmint.offset_adj_y), 
					      &bmvals.yofs);

	  break;

	default:
	  return FALSE;
	}

      dialog_update_preview ();

      break; 

    default:
      break;
    }

  return FALSE;
}

static void
dialog_new_bumpmap (gboolean init_offsets)
{
  GtkAdjustment   *adj;
  gint             i;
  gint             yofs;
  gint             bump_offset_x;
  gint             bump_offset_y;
  gint             draw_offset_y;
  gint             draw_offset_x;

  /* Get drawable */
  if (bmint.bm_drawable && (bmint.bm_drawable != drawable))
    gimp_drawable_detach (bmint.bm_drawable);

  if (bmvals.bumpmap_id != -1)
    bmint.bm_drawable = gimp_drawable_get (bmvals.bumpmap_id);
  else
    bmint.bm_drawable = drawable;

  if (!bmint.bm_drawable)
    return;

  /* Get sizes */
  bmint.bm_width     = gimp_drawable_width (bmint.bm_drawable->drawable_id);
  bmint.bm_height    = gimp_drawable_height (bmint.bm_drawable->drawable_id);
  bmint.bm_bpp       = gimp_drawable_bpp (bmint.bm_drawable->drawable_id);
  bmint.bm_has_alpha = gimp_drawable_has_alpha (bmint.bm_drawable->drawable_id);

  if (init_offsets)
    {      
      gimp_drawable_offsets (bmint.bm_drawable->drawable_id,
			     &bump_offset_x, &bump_offset_y);
      gimp_drawable_offsets (drawable->drawable_id,
			     &draw_offset_x, &draw_offset_y);
      
      bmvals.xofs = draw_offset_x - bump_offset_x;
      bmvals.yofs = draw_offset_y - bump_offset_y;
    }

  adj = (GtkAdjustment *) bmint.offset_adj_x;
  if (adj)
    {
      adj->value = bmvals.xofs;
      gtk_signal_handler_block_by_data (GTK_OBJECT (adj), &bmvals.xofs);
      gtk_adjustment_value_changed (adj);
      gtk_signal_handler_unblock_by_data (GTK_OBJECT (adj), &bmvals.xofs);
    }
  
  adj = (GtkAdjustment *) bmint.offset_adj_y;
  if (adj)
    {
      adj->value = bmvals.yofs;
      gtk_signal_handler_block_by_data (GTK_OBJECT (adj), &bmvals.yofs);
      gtk_adjustment_value_changed (adj);
      gtk_signal_handler_unblock_by_data (GTK_OBJECT (adj), &bmvals.yofs);
    }
  
  /* Initialize pixel region */

  gimp_pixel_rgn_init (&bmint.bm_rgn, bmint.bm_drawable,
		       0, 0, bmint.bm_width, bmint.bm_height, FALSE, FALSE);

  /* Initialize row buffers */

  yofs = bmvals.yofs + bmint.preview_yofs - 1; /* Minus 1 for conv. matrix */

  if (yofs < 0)
    yofs = bmint.bm_height - (-yofs % bmint.bm_height);
  else
    yofs = yofs % bmint.bm_height;

  bmint.bm_yofs = yofs;

  for (i = 0; i < (bmint.preview_height + 2); i++)
    {
      g_free (bmint.bm_rows[i]);
      bmint.bm_rows[i] = g_new (guchar, bmint.bm_width * bmint.bm_bpp);
    }

  bumpmap_init_params (&bmint.params);
  dialog_fill_bumpmap_rows (0, bmint.preview_height + 2, yofs);
}

static void
dialog_update_preview (void)
{
  static guchar dest_row[PREVIEW_SIZE * 4];
  static guchar preview_row[PREVIEW_SIZE * 3];

  guchar *check_row;
  guchar  check;
  gint    xofs;
  gint    x, y;
  guchar *sp, *p;

  bumpmap_init_params (&bmint.params);

  /* Scroll the row buffers */

  dialog_scroll_src ();
  dialog_scroll_bumpmap ();

  /* Bumpmap */

  xofs = bmint.preview_xofs;

  for (y = 0; y < bmint.preview_height; y++)
    {
      bumpmap_row (bmint.src_rows[y] + 4 * xofs, dest_row,
		   bmint.preview_width, 4, TRUE,
		   bmint.bm_rows[y], 
		   bmint.bm_rows[y + 1],
		   bmint.bm_rows[y + 2],
		   bmint.bm_width, xofs + bmvals.xofs,
		   bmvals.tiled, 
		   y == CLAMP (y, 
			       - bmvals.yofs - bmint.preview_yofs - sel_y1 ,
			       - bmvals.yofs - bmint.preview_yofs - sel_y1 + bmint.bm_height),
		   &bmint.params);

      /* Paint row */

      sp = dest_row;
      p  = preview_row;

      if ((y / GIMP_CHECK_SIZE) & 1)
	check_row = bmint.check_row_0;
      else
	check_row = bmint.check_row_1;

      for (x = 0; x < bmint.preview_width; x++)
	{
	  check = check_row[x];

	  p[0] = check + ((sp[0] - check) * sp[3]) / 255;
	  p[1] = check + ((sp[1] - check) * sp[3]) / 255;
	  p[2] = check + ((sp[2] - check) * sp[3]) / 255;

	  sp += 4;
	  p  += 3;
	}

      gtk_preview_draw_row (GTK_PREVIEW(bmint.preview),
			    preview_row, 0, y, bmint.preview_width);
    }

  gtk_widget_queue_draw (bmint.preview);
  gdk_flush ();
}

#define SWAP_ROWS(a, b, t) { t = a; a = b; b = t; }

static void
dialog_scroll_src (void)
{
  gint    yofs;
  gint    y, ofs;
  guchar *tmp;

  yofs = bmint.preview_yofs;

  if (yofs == bmint.src_yofs)
    return;

  if (yofs < bmint.src_yofs)
    {
      ofs = bmint.src_yofs - yofs;

      /* Scroll useful rows... */

      if (ofs < bmint.preview_height)
	for (y = (bmint.preview_height - 1); y >= ofs; y--)
	  SWAP_ROWS (bmint.src_rows[y], bmint.src_rows[y - ofs], tmp);

      /* ... and get the new ones */

      dialog_fill_src_rows (0, MIN (ofs, bmint.preview_height), sel_y1 + yofs);
    }
  else
    {
      ofs = yofs - bmint.src_yofs;

      /* Scroll useful rows... */

      if (ofs < bmint.preview_height)
	for (y = 0; y < (bmint.preview_height - ofs); y++)
	  SWAP_ROWS (bmint.src_rows[y], bmint.src_rows[y + ofs], tmp);

      /* ... and get the new ones */

      dialog_fill_src_rows ((bmint.preview_height -
			     MIN (ofs, bmint.preview_height)),
			    MIN (ofs, bmint.preview_height),
			    (sel_y1 + yofs + bmint.preview_height -
			     MIN (ofs, bmint.preview_height)));
    }

  bmint.src_yofs = yofs;
}

static void
dialog_scroll_bumpmap (void)
{
  gint    yofs;
  gint    y, ofs;
  guchar *tmp;

  yofs = bmvals.yofs + bmint.preview_yofs - 1; /* Minus 1 for conv. matrix */

  if (yofs < 0)
    yofs = bmint.bm_height - (-yofs % bmint.bm_height);
  else
    yofs %= bmint.bm_height;

  if (yofs == bmint.bm_yofs)
    return;

  if (yofs < bmint.bm_yofs)
    {
      ofs = bmint.bm_yofs - yofs;

      /* Scroll useful rows... */

      if (ofs < (bmint.preview_height + 2))
	for (y = (bmint.preview_height + 1); y >= ofs; y--)
	  SWAP_ROWS (bmint.bm_rows[y], bmint.bm_rows[y - ofs], tmp);

      /* ... and get the new ones */

      dialog_fill_bumpmap_rows (0,
				MIN (ofs, bmint.preview_height + 2),
				yofs);
    }
  else
    {
      ofs = yofs - bmint.bm_yofs;

      /* Scroll useful rows... */

      if (ofs < (bmint.preview_height + 2))
	for (y = 0; y < (bmint.preview_height + 2 - ofs); y++)
	  SWAP_ROWS (bmint.bm_rows[y], bmint.bm_rows[y + ofs], tmp);

      /* ... and get the new ones */

      dialog_fill_bumpmap_rows ((bmint.preview_height + 2 -
				 MIN (ofs, bmint.preview_height + 2)),
				MIN (ofs, bmint.preview_height + 2),
				(yofs + bmint.preview_height + 2 -
				 MIN (ofs, bmint.preview_height + 2)) %
				bmint.bm_height);
    }

  bmint.bm_yofs = yofs;
}

static void
dialog_get_rows (GimpPixelRgn  *pr, 
		 guchar    **rows, 
		 gint        x, 
		 gint        y, 
		 gint        width, 
		 gint        height)
{
  /* This is shamelessly ripped off from gimp_pixel_rgn_get_rect().
   * Its function is exactly the same, but it can fetch an image
   * rectangle to a sparse buffer which is defined as separate
   * rows instead of one big linear region.
   */

  GimpTile  *tile;
  guchar *src, *dest;
  gint    xstart, ystart;
  gint    xend, yend;
  gint    xboundary;
  gint    yboundary;
  gint    xstep, ystep;
  gint    b, bpp;
  gint    tx, ty;
  gint    tile_width, tile_height;

  tile_width  = gimp_tile_width();
  tile_height = gimp_tile_height();

  bpp = pr->bpp;

  xstart = x;
  ystart = y;
  xend   = x + width;
  yend   = y + height;
  ystep  = 0; /* Shut up -Wall */

  while (y < yend)
    {
      x = xstart;

      while (x < xend)
	{
	  tile = gimp_drawable_get_tile2 (pr->drawable, pr->shadow, x, y);
	  gimp_tile_ref (tile);

	  xstep     = tile->ewidth - (x % tile_width);
	  ystep     = tile->eheight - (y % tile_height);
	  xboundary = x + xstep;
	  yboundary = y + ystep;
	  xboundary = MIN (xboundary, xend);
	  yboundary = MIN (yboundary, yend);

	  for (ty = y; ty < yboundary; ty++)
	    {
	      src  = tile->data + tile->bpp * (tile->ewidth * (ty % tile_height) +
					       (x % tile_width));
	      dest = rows[ty - ystart] + bpp * (x - xstart);

	      for (tx = x; tx < xboundary; tx++)
		for (b = bpp; b; b--)
		  *dest++ = *src++;
	    }

	  gimp_tile_unref (tile, FALSE);

	  x += xstep;
	}

      y += ystep;
    }
}

static void
dialog_fill_src_rows (gint start, 
		      gint how_many, 
		      gint yofs)
{
  gint    x;
  gint    y;
  guchar *sp;
  guchar *p;

  dialog_get_rows (&bmint.src_rgn,
		   bmint.src_rows + start,
		   sel_x1,
		   yofs,
		   sel_width,
		   how_many);

  /* Convert to RGBA.  We move backwards! */

  for (y = start; y < (start + how_many); y++)
    {
      sp = bmint.src_rows[y] + img_bpp * sel_width - 1;
      p  = bmint.src_rows[y] + 4 * sel_width - 1;

      for (x = 0; x < sel_width; x++)
	{
	  if (img_has_alpha)
	    *p-- = *sp--;
	  else
	    *p-- = 255;

	  if (img_bpp < 3)
	    {
	      *p-- = *sp;
	      *p-- = *sp;
	      *p-- = *sp--;
	    }
	  else
	    {
	      *p-- = *sp--;
	      *p-- = *sp--;
	      *p-- = *sp--;
	    }
	}
    }
}

static void
dialog_fill_bumpmap_rows (gint start, 
			  gint how_many, 
			  gint yofs)
{
  gint buf_row_ofs;
  gint remaining;
  gint this_pass;

  /* Adapt to offset of selection */
  yofs += sel_y1;
  if (yofs < 0)
    yofs = bmint.bm_height - (-yofs % bmint.bm_height);
  else
    yofs %= bmint.bm_height;

  buf_row_ofs = start;
  remaining   = how_many;

  while (remaining > 0)
    {
      this_pass = MIN (remaining, bmint.bm_height - yofs);

      dialog_get_rows (&bmint.bm_rgn,
		       bmint.bm_rows + buf_row_ofs,
		       0,
		       yofs,
		       bmint.bm_width,
		       this_pass);

      yofs         = (yofs + this_pass) % bmint.bm_height;
      remaining   -= this_pass;
      buf_row_ofs += this_pass;
    }

  /* Convert rows */

  for (; how_many; how_many--)
    {
      bumpmap_convert_row (bmint.bm_rows[start],
			   bmint.bm_width,
			   bmint.bm_bpp,
			   bmint.bm_has_alpha,
			   bmint.params.lut);

      start++;
    }
}

static void
dialog_compensate_callback (GtkWidget *widget, 
			    gpointer   data)
{
  bmvals.compensate = GTK_TOGGLE_BUTTON (widget)->active;

  dialog_update_preview ();
}

static void
dialog_invert_callback (GtkWidget *widget, 
			gpointer   data)
{
  bmvals.invert = GTK_TOGGLE_BUTTON (widget)->active;

  bumpmap_init_params (&bmint.params);
  dialog_fill_bumpmap_rows (0, bmint.preview_height + 2, bmint.bm_yofs);
  dialog_update_preview ();
}

static void
dialog_tiled_callback (GtkWidget *widget, 
			gpointer   data)
{
  bmvals.tiled = GTK_TOGGLE_BUTTON (widget)->active;

  bumpmap_init_params (&bmint.params);
  dialog_fill_bumpmap_rows (0, bmint.preview_height + 2, bmint.bm_yofs);
  dialog_update_preview ();
}

static void
dialog_map_type_callback (GtkWidget *widget, 
			  gpointer   data)
{
  gimp_radio_button_update (widget, data);

  if (GTK_TOGGLE_BUTTON (widget)->active)
    {
      bumpmap_init_params (&bmint.params);
      dialog_fill_bumpmap_rows (0, bmint.preview_height + 2, bmint.bm_yofs);
      dialog_update_preview ();
    }
}

static gint
dialog_constrain (gint32   image_id, 
		  gint32   drawable_id, 
		  gpointer data)
{
  if (drawable_id == -1)
    return TRUE;

  return (gimp_drawable_is_rgb (drawable_id) ||
	  gimp_drawable_is_gray (drawable_id));
}

static void
dialog_bumpmap_callback (gint32   id, 
			 gpointer data)
{
  if (bmvals.bumpmap_id == id)
    {
      dialog_new_bumpmap (FALSE);
    }
  else
    {
      bmvals.bumpmap_id = id;
      dialog_new_bumpmap (TRUE);
    }
  dialog_update_preview ();
}

static void
dialog_dscale_update (GtkAdjustment *adjustment, 
		      gdouble       *value)
{
  gimp_double_adjustment_update (adjustment, value);

  dialog_update_preview ();
}

static void
dialog_iscale_update_normal (GtkAdjustment *adjustment, 
			     gint          *value)
{
  gimp_int_adjustment_update (adjustment, value);

  dialog_update_preview ();
}

static void
dialog_iscale_update_full (GtkAdjustment *adjustment, 
			   gint          *value)
{
  gimp_int_adjustment_update (adjustment, value);

  bumpmap_init_params (&bmint.params);
  dialog_fill_bumpmap_rows (0, bmint.preview_height + 2, bmint.bm_yofs);
  dialog_update_preview ();
}

static void
dialog_ok_callback (GtkWidget *widget, 
		    gpointer   data)
{
  bmint.run = TRUE;

  gtk_widget_destroy (GTK_WIDGET (data));
}
