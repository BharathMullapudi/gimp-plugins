/* Tiler v0.31
 * 22 May 1997
 * Tim Rowley <tor@cs.brown.edu>
 */

/* TODO:
 * + better basis function
 * + optimize
 */

/* History:
 * v0.1: initial version
 * v0.2: fix edge conditions
 * v0.3: port to 0.99 API
 * v0.31: small bugfix
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libgimp/gimp.h>

#include "libgimp/stdplugins-intl.h"


/* Declare local functions.
 */
static void query (void);
static void run   (gchar   *name,
		   gint     nparams,
		   GimpParam  *param,
		   gint    *nreturn_vals,
		   GimpParam **return_vals);

static void tile  (GimpDrawable *drawable);
static gint scale (gint       width,
		   gint       height,
		   gint       x,
		   gint       y,
		   gint       data);


GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};

MAIN ()

static void
query (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE, "image", "Input image (unused)" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable" }
  };

  gimp_install_procedure ("plug_in_make_seamless",
			  "Seamless tile creation",
			  "This plugin creates a seamless tileable from the input image",
			  "Tim Rowley",
			  "Tim Rowley",
			  "1997",
			  N_("<Image>/Filters/Map/Make Seamless"),
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
  GimpDrawable *drawable;
  GimpRunModeType run_mode;
  GimpPDBStatusType status = GIMP_PDB_SUCCESS;

  run_mode = param[0].data.d_int32;

  INIT_I18N();

  /*  Get the specified drawable  */
  drawable = gimp_drawable_get (param[2].data.d_drawable);

  /*  Make sure that the drawable is gray or RGB color  */
  if (gimp_drawable_is_rgb (drawable->drawable_id) ||
      gimp_drawable_is_gray (drawable->drawable_id))
    {
      gimp_tile_cache_ntiles (2 * (drawable->width / gimp_tile_width () + 1));
      tile(drawable);

      if (run_mode != GIMP_RUN_NONINTERACTIVE)
	gimp_displays_flush ();
    }
  else
    {
      /* gimp_message ("laplace: cannot operate on indexed color images"); */
      status = GIMP_PDB_EXECUTION_ERROR;
    }

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  gimp_drawable_detach (drawable);
}


static gint
scale (gint width,
       gint height,
       gint x,
       gint y,
       gint data)
{
  gint A = width/2-1;
  gint B = height/2-1;
  gint a, b;

  if (x<width/2)
    a = width/2-x-1; 
  else if ((x==width/2) && (width&1))
      return data;
  else
    a = x-width/2-(width&1);

  if (y<height/2)
    b = height/2-y-1; 
  else if ((y==height/2) && (height&1))
    return data;
  else
    b = y-height/2-(height&1);
  
  if ((B*a<A*b) || ((B*a==A*b) && (a&1)))
    {
      a = A-a;
      b = B-b;
      if (a==A)
	return data;
      else
	return data-((data*(A*B-a*B))/(A*b+A*B-a*B));
    }
  else
    {
      if (a==A)
	return 0;
      else 
	return (data*(A*B-a*B))/(A*b+A*B-a*B);
  }
}


static void
tile (GimpDrawable *drawable)
{
  glong      width, height;
  glong      bytes;
  glong      val;
  gint       wodd, hodd;
  GimpPixelRgn  srcPR, destPR;
  gint       x1, y1, x2, y2;
  gint       row, col, x, y, c;
  guchar    *cur_row, *dest_cur, *dest_top, *dest_bot;

  /* Get the input */

  gimp_drawable_mask_bounds (drawable->drawable_id, &x1, &y1, &x2, &y2);
  gimp_progress_init (_("Tiler..."));
  
  width  = drawable->width;
  height = drawable->height;
  bytes  = drawable->bpp;

  /*  allocate row buffers  */
  cur_row  = g_new (guchar, (x2 - x1) * bytes);
  dest_cur = g_new (guchar, (x2 - x1) * bytes);
  dest_top = g_new (guchar, (x2 - x1) * bytes);
  dest_bot = g_new (guchar, (x2 - x1) * bytes);

  gimp_pixel_rgn_init (&srcPR, drawable, 0, 0, width, height, FALSE, FALSE);
  gimp_pixel_rgn_init (&destPR, drawable, 0, 0, width, height, TRUE, TRUE);

  y = y2-y1;
  x = x2-x1;

  wodd = x&1;
  hodd = y&1;

  for (row = 0; row <y; row++)
    {
      gimp_pixel_rgn_get_row (&destPR, dest_cur, x1, y1+row, (x2-x1));
      memset (dest_cur, 0, x*bytes);
      gimp_pixel_rgn_set_row (&destPR, dest_cur, x1, y1+row, (x2-x1));
    }

  for (row = 0; row < y; row++)
    {
      gimp_pixel_rgn_get_row (&srcPR, cur_row, x1, y1+row, (x2-x1));
      gimp_pixel_rgn_get_row (&destPR, dest_cur, x1, y1+row, (x2-x1));
      if (row >= y/2+hodd)
	gimp_pixel_rgn_get_row (&destPR, dest_top,
				x1, y1+(row-y/2-hodd), (x2-x1));
      if (row < y/2)
	gimp_pixel_rgn_get_row (&destPR, dest_bot,
				x1, y1+(row+y/2+hodd), (x2-x1));

      for (col = 0; col < x; col++)
	{
	  for (c=0; c<bytes; c++)
	    {
	      val = scale (x, y, col, row, cur_row[col*bytes+c]);

	      /* Main image */
	      dest_cur[col*bytes+c] += val;
	      /* top left */
	      if ((col>=x/2+wodd) && (row>=y/2+hodd))
		dest_top[(col-x/2-wodd)*bytes+c] += val;
	      /* top right */
	      if ((col<x/2) && (row>=y/2+hodd))
		dest_top[(x/2+col+wodd)*bytes+c] += val;
	      /* bottom left */
	      if ((col>=x/2+wodd) && (row<y/2))
		dest_bot[(col-x/2-wodd)*bytes+c] += val;
	      /* bottom right */
	      if ((col<x/2) && (row<y/2))
		dest_bot[(col+x/2+wodd)*bytes+c] += val;
	    }
	}

      gimp_pixel_rgn_set_row (&destPR, dest_cur, x1, y1+row, (x2-x1));
      if (row >= y/2+hodd)
	gimp_pixel_rgn_set_row (&destPR, dest_top,
				x1, y1+(row-y/2-hodd), (x2-x1));
      if (row < y/2)
	gimp_pixel_rgn_set_row (&destPR, dest_bot,
				x1, y1+(row+y/2+hodd), (x2-x1));

      if ((row % 5) == 0)
	gimp_progress_update ((gdouble) row / (gdouble) (y2 - y1));
    }

  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
  gimp_drawable_update (drawable->drawable_id, x1, y1, (x2-x1), (y2-y1));
}
