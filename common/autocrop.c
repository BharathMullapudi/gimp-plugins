/*
 * Autocrop plug-in version 1.00
 * by Tim Newsome <drz@froody.bloke.com>
 * thanks to quartic for finding a nasty bug for me
 */

/* 1999/04/09 -- Sven Neumann <sven@gimp.org>
 * Fixed bad crash that occured when running on an entirely blank image.
 * Cleaned up the code a bit, while I was at it. 
 */ 

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <libgimp/gimp.h>

#include "libgimp/stdplugins-intl.h"


/* Declare local functions. */
static void query         (void);
static void run           (gchar        *name,
			   gint          nparams,
			   GimpParam    *param,
			   gint         *nreturn_vals,
			   GimpParam   **return_vals);

static gint colors_equal  (guchar       *col1,
			   guchar       *col2,
			   gint          bytes);
static gint guess_bgcolor (GimpPixelRgn *pr,
			   gint          width,
			   gint          height,
			   gint          bytes,
			   guchar       *color);

static void doit          (GimpDrawable *drawable,
			   gint32        image_id,
			   gboolean      show_progress);


GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,	  /* init_proc  */
  NULL,   /* quit_proc  */
  query,  /* query_proc */
  run,	  /* run_proc   */
};

static gint bytes;


MAIN ()

static void
query (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE, "image", "Input image" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable" }
  };
  static gint nargs = sizeof (args) / sizeof (args[0]);

  gimp_install_procedure ("plug_in_autocrop",
			  "Automagically crops a picture.",
			  "",
			  "Tim Newsome",
			  "Tim Newsome",
			  "1997",
			  N_("<Image>/Image/Transforms/Autocrop"),
			  "RGB*, GRAY*, INDEXED*",
			  GIMP_PLUGIN,
			  nargs, 0,
			  args, NULL);
}

static void
run (gchar      *name,
     gint        n_params,
     GimpParam  *param,
     gint       *nreturn_vals,
     GimpParam **return_vals)
{
  static GimpParam   values[1];
  GimpDrawable      *drawable;
  GimpRunModeType    run_mode;
  GimpPDBStatusType  status = GIMP_PDB_SUCCESS;
  gint32             image_id;
  gboolean           interactive;

  *nreturn_vals = 1;
  *return_vals = values;
  
  run_mode = param[0].data.d_int32;
  interactive = (run_mode != GIMP_RUN_NONINTERACTIVE);

  INIT_I18N();
  
  if (n_params != 3)
    {
      status = GIMP_PDB_CALLING_ERROR;
      goto out;
    }

  /*  Get the specified drawable  */
  drawable = gimp_drawable_get (param[2].data.d_drawable);
  image_id = param[1].data.d_image;

  /*  Make sure that the drawable is gray or RGB color  */
  if (gimp_drawable_is_rgb (drawable->drawable_id) ||
      gimp_drawable_is_gray (drawable->drawable_id)  ||
      gimp_drawable_is_indexed (drawable->drawable_id)) 
  {
    if (interactive)
      gimp_progress_init (_("Cropping..."));

    gimp_tile_cache_ntiles (2 * (drawable->width / gimp_tile_width() + 1));

    doit (drawable, image_id, interactive);

    if (interactive)
      gimp_displays_flush ();
  } 
  else 
  {
      status = GIMP_PDB_EXECUTION_ERROR;
  }

 out:
  values[0].type = GIMP_PDB_STATUS;
  values[0].data.d_status = status;
}

static void
doit (GimpDrawable *drawable,
      gint32        image_id,
      gboolean      show_progress)
{
  GimpPixelRgn  srcPR;
  gint          width, height;
  gint          x, y, abort;
  gint32        nx, ny, nw, nh;
  guchar       *buffer;
  guchar        color[4] = {0, 0, 0, 0};
  
  width  = drawable->width;
  height = drawable->height;
  bytes  = drawable->bpp;
  
  nx = 0;
  ny = 0;
  nw = width;
  nh = height;
  
  /*  initialize the pixel regions  */
  gimp_pixel_rgn_init (&srcPR, drawable, 0, 0, width, height, FALSE, FALSE);
  
  /* First, let's figure out what exactly to crop. */
  buffer = g_malloc ((width > height ? width : height) * bytes);
  
  guess_bgcolor (&srcPR, width, height, bytes, color);
  
  /* Check how many of the top lines are uniform. */
  abort = 0;
  for (y = 0; y < height && !abort; y++)
    {
      gimp_pixel_rgn_get_row (&srcPR, buffer, 0, y, width);
      for (x = 0; x < width && !abort; x++)
	{
	  abort = !colors_equal (color, buffer + x * bytes, bytes);
	}
    }
  if (y == height && !abort)
    {
      g_free (buffer);
      gimp_drawable_detach (drawable);
      return;
    }
  y--;
  ny = y;
  nh = height - y;

  if (show_progress)
    gimp_progress_update (0.25);
  
  /* Check how many of the bottom lines are uniform. */
  abort = 0;
  for (y = height - 1; y >= 0 && !abort; y--)
    {
      gimp_pixel_rgn_get_row (&srcPR, buffer, 0, y, width);
      for (x = 0; x < width && !abort; x++)
	{
	  abort = !colors_equal (color, buffer + x * bytes, bytes);
	}
    }
  nh = y - ny + 2;
  
  if (show_progress)
    gimp_progress_update (0.5);
  
  /* Check how many of the left lines are uniform. */
  abort = 0;
  for (x = 0; x < width && !abort; x++)
    {
      gimp_pixel_rgn_get_col (&srcPR, buffer, x, ny, nh);
      for (y = 0; y < nh && !abort; y++)
	{
	  abort = !colors_equal (color, buffer + y * bytes, bytes);
	}
    }
  x--;
  nx = x;
  nw = width - x;
  
  if (show_progress)
    gimp_progress_update (0.75);
  
  /* Check how many of the right lines are uniform. */
  abort = 0;
  for (x = width - 1; x >= 0 && !abort; x--)
    {
      gimp_pixel_rgn_get_col (&srcPR, buffer, x, ny, nh);
      for (y = 0; y < nh && !abort; y++)
	{
	  abort = !colors_equal (color, buffer + y * bytes, bytes);
	}
    }
  nw = x - nx + 2;
  
  g_free (buffer);
  
  gimp_drawable_detach (drawable);

  if (nw != width || nh != height)
    gimp_image_crop (image_id, nw, nh, nx, ny);

  if (show_progress)
    gimp_progress_update (1.00);
}

static gint
guess_bgcolor (GimpPixelRgn *pr,
	       gint          width,
	       gint          height,
	       gint          bytes,
	       guchar       *color)
{
  guchar tl[4], tr[4], bl[4], br[4];
  
  gimp_pixel_rgn_get_pixel (pr, tl, 0, 0);
  gimp_pixel_rgn_get_pixel (pr, tr, width - 1, 0);
  gimp_pixel_rgn_get_pixel (pr, bl, 0, height - 1);
  gimp_pixel_rgn_get_pixel (pr, br, width - 1, height - 1);
  
  /* Algorithm pinched from pnmcrop.
   * To guess the background, first see if 3 corners are equal.
   * Then if two are equal.
   * Otherwise average the colors.
   */

  if (colors_equal (tr, bl, bytes) && colors_equal (tr, br, bytes))
    {
      memcpy (color, tr, bytes);
      return 3;
    }
  else if (colors_equal (tl, bl, bytes) && colors_equal (tl, br, bytes))
    {
      memcpy (color, tl, bytes);
      return 3;
    }
  else if (colors_equal (tl, tr, bytes) && colors_equal (tl, br, bytes))
    {
      memcpy (color, tl, bytes);
      return 3;
    }
  else if (colors_equal (tl, tr, bytes) && colors_equal (tl, bl, bytes))
    {
      memcpy (color, tl, bytes);
      return 3;
    }
  else if (colors_equal (tl, tr, bytes) || colors_equal (tl, bl, bytes) ||
	   colors_equal (tl, br, bytes))
    {
      memcpy (color, tl, bytes);
      return 2;
    }
  else if (colors_equal (tr, bl, bytes) || colors_equal (tr, bl, bytes))
    {
      memcpy (color, tr, bytes);
      return 2;
    }
  else if (colors_equal (br, bl, bytes))
    {
      memcpy (color, br, bytes);
      return 2;
    }
  else
    {
      while (bytes--)
	{
	  color[bytes] = (tl[bytes] + tr[bytes] + bl[bytes] + br[bytes]) / 4;
	}
      return 0;
    }
}

static gint 
colors_equal (guchar *col1,
	      guchar *col2,
	      gint    bytes)
{
  gint equal = 1;
  gint b;

  if ((bytes == 2 || bytes == 4) &&	/* HACK! */
      col1[bytes-1] == 0 &&
      col2[bytes-1] == 0)
    {
      return 1;				/* handle zero alpha */
    }
  
  for (b = 0; b < bytes; b++)
    {
      if (col1[b] != col2[b])
	{
	  equal = 0;
	  break;
	}
    }
  
  return equal;
}
