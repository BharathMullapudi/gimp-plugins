/*
 * This is a plugin for the GIMP.
 *
 * Copyright (C) 1996 Stephen Norris
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
 */

/*
 * This plug-in produces plasma fractal images. The algorithm is losely
 * based on a description of the fractint algorithm, but completely
 * re-implemented because the fractint code was too ugly to read :)
 *
 * Please send any patches or suggestions to me: srn@flibble.cs.su.oz.au.
 */

/*
 * TODO:
 *	- The progress bar sucks.
 *	- It writes some pixels more than once.
 */

/* Version 1.01 */

/*
 * Ported to GIMP Plug-in API 1.0
 *    by Eiichi Takamori <taka@ma1.seikyou.ne.jp>
 *
 * $Id$
 *
 * A few functions names and their order are changed :)
 * Plasma implementation almost hasn't been changed.
 *
 * Feel free to correct my WRONG English, or to modify Plug-in Path,
 * and so on. ;-)
 *
 * Version 1.02 
 *
 * May 2000
 * tim copperfield [timecop@japan.co.jp]
 * Added dynamic preview mode.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>  /* For random seeding */
#include <string.h> /* memcpy */

#ifdef __GNUC__
#warning GTK_DISABLE_DEPRECATED
#endif
#undef GTK_DISABLE_DEPRECATED

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


/* Some useful macros */

#define ENTRY_WIDTH      75
#define SCALE_WIDTH     128
#define TILE_CACHE_SIZE  32
#define PREVIEW_SIZE    128

typedef struct
{
  gint     seed;
  gdouble  turbulence;
  /* Interface only */
  gboolean timeseed;
} PlasmaValues;

typedef struct
{
  gint run;
} PlasmaInterface;

/*
 * Function prototypes.
 */

static void	query	(void);
static void	run	(gchar      *name,
			 gint        nparams,
			 GimpParam  *param,
			 gint       *nreturn_vals,
			 GimpParam **return_vals);

static GtkWidget *preview_widget         (void);
static gint	plasma_dialog            (GimpDrawable *drawable);
static void     plasma_ok_callback       (GtkWidget *widget, 
					  gpointer   data);

static void	plasma	     (GimpDrawable *drawable, 
			      gboolean   preview_mode);
static void     random_rgb   (guchar    *d);
static void     add_random   (guchar    *d,
			      gint       amnt);
static void     init_plasma  (GimpDrawable *drawable, 
			      gboolean   preview_mode);
static void     provide_tile (GimpDrawable *drawable,
			      gint       col,
			      gint       row);
static void     end_plasma   (GimpDrawable *drawable,
			      gboolean   preview_mode);
static void     get_pixel    (GimpDrawable *drawable,
			      gint       x,
			      gint       y,
			      guchar    *pixel,
			      gboolean   preview_mode);
static void     put_pixel    (GimpDrawable *drawable,
			      gint       x,
			      gint       y,
			      guchar    *pixel,
			      gboolean   preview_mode);
static gint     do_plasma    (GimpDrawable *drawable,
			      gint       x1,
			      gint       y1,
			      gint       x2,
			      gint       y2,
			      gint       depth,
			      gint       scale_depth,
			      gboolean   preview_mode);

/***** Local vars *****/

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};

static PlasmaValues pvals =
{
  0,     /* seed       */
  1.0,   /* turbulence */
  TRUE   /* Time seed? */
};

static PlasmaInterface pint =
{
  FALSE     /* run */
};

static guchar *work_buffer;
static GtkWidget *preview;

/***** Functions *****/

MAIN ()

static void
query (void)
{
  static GimpParamDef args[]=
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE, "image", "Input image (unused)" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable" },
    { GIMP_PDB_INT32, "seed", "Random seed" },
    { GIMP_PDB_FLOAT, "turbulence", "Turbulence of plasma" }
  };

  gimp_install_procedure ("plug_in_plasma",
			  "Create a plasma cloud like image to the specified drawable",
			  "More help",
			  "Stephen Norris & (ported to 1.0 by) Eiichi Takamori",
			  "Stephen Norris",
			  "May 2000",
			  /* don't translate '<Image>', it's a special
			   * keyword of the gtk toolkit */
			  N_("<Image>/Filters/Render/Clouds/Plasma..."),
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
  static GimpParam   values[1];
  GimpDrawable      *drawable;
  GimpRunMode        run_mode;
  GimpPDBStatusType  status = GIMP_PDB_SUCCESS;

  run_mode = param[0].data.d_int32;

  *nreturn_vals = 1;
  *return_vals  = values;

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  /*  Get the specified drawable  */
  drawable = gimp_drawable_get (param[2].data.d_drawable);

  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE:
      INIT_I18N_UI();
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_plasma", &pvals);

      /*  First acquire information with a dialog  */
      if (! plasma_dialog (drawable))
	{
	  gimp_drawable_detach (drawable);
	  return;
	}
      break;

    case GIMP_RUN_NONINTERACTIVE:
      INIT_I18N();
      /*  Make sure all the arguments are there!  */
      if (nparams != 5)
	{
	  status = GIMP_PDB_CALLING_ERROR;
	}
      else
	{
	  pvals.seed = (gint) param[3].data.d_int32;
	  pvals.turbulence = (gdouble) param[4].data.d_float;
          pvals.timeseed = FALSE;

	  if (pvals.turbulence <= 0)
	    status = GIMP_PDB_CALLING_ERROR;
	}
      break;

    case GIMP_RUN_WITH_LAST_VALS:
      INIT_I18N();
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_plasma", &pvals);
      break;

    default:
      break;
    }

  if (status == GIMP_PDB_SUCCESS)
    {
      /*  Make sure that the drawable is gray or RGB color  */
      if (gimp_drawable_is_rgb (drawable->drawable_id) ||
	  gimp_drawable_is_gray (drawable->drawable_id))
	{
	  gimp_progress_init (_("Plasma..."));
	  gimp_tile_cache_ntiles (TILE_CACHE_SIZE);

	  plasma (drawable, FALSE);

	  if (run_mode != GIMP_RUN_NONINTERACTIVE)
	    gimp_displays_flush ();

	  /*  Store data  */
	  if (run_mode == GIMP_RUN_INTERACTIVE || 
	      (pvals.timeseed && run_mode == GIMP_RUN_WITH_LAST_VALS))
	    gimp_set_data ("plug_in_plasma", &pvals, sizeof (PlasmaValues));
	}
      else
	{
	  /* gimp_message ("plasma: cannot operate on indexed color images"); */
	  status = GIMP_PDB_EXECUTION_ERROR;
	}
    }

  values[0].data.d_status = status;
  gimp_drawable_detach (drawable);
}

static gint
plasma_dialog (GimpDrawable *drawable)
{
  GtkWidget *dlg;
  GtkWidget *main_vbox;
  GtkWidget *abox;
  GtkWidget *frame;
  GtkWidget *table;
  GtkWidget *seed;
  GtkObject *adj;

  gimp_ui_init ("plasma", TRUE);

  dlg = gimp_dialog_new (_("Plasma"), "plasma",
			 gimp_standard_help_func, "filters/plasma.html",
			 GTK_WIN_POS_MOUSE,
			 FALSE, TRUE, FALSE,

			 GTK_STOCK_CANCEL, gtk_widget_destroy,
			 NULL, 1, NULL, FALSE, TRUE,
			 GTK_STOCK_OK, plasma_ok_callback,
			 NULL, NULL, NULL, TRUE, FALSE,

			 NULL);

  g_signal_connect (G_OBJECT (dlg), "destroy",
                    G_CALLBACK (gtk_main_quit),
                    NULL);

  gimp_help_init ();

  main_vbox = gtk_vbox_new (FALSE, 2);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), main_vbox, TRUE, TRUE, 0);
  gtk_widget_show (main_vbox);

  /* make a nice preview frame */
  frame = gtk_frame_new (_("Preview"));
  gtk_container_set_border_width (GTK_CONTAINER (frame), 4);
  gtk_box_pack_start (GTK_BOX (main_vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);
  abox = gtk_alignment_new (0.5, 0.5, 0.0, 0.0);
  gtk_container_set_border_width (GTK_CONTAINER (abox), 4);
  gtk_container_add (GTK_CONTAINER (frame), abox);
  gtk_widget_show (abox);
  frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER (abox), frame);
  gtk_widget_show (frame);
  preview = preview_widget (); /* we are here */
  gtk_container_add (GTK_CONTAINER (frame), preview);
  plasma (drawable, TRUE); /* preview image */
  gtk_widget_show (preview);
  
  /*  parameter settings  */
  frame = gtk_frame_new (_("Parameter Settings"));
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
  gtk_container_set_border_width (GTK_CONTAINER (frame), 6);
  gtk_box_pack_start (GTK_BOX (main_vbox), frame, TRUE, TRUE, 0);

  table = gtk_table_new (2, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_container_set_border_width (GTK_CONTAINER (table), 4);
  gtk_container_add (GTK_CONTAINER (frame), table);

  seed = gimp_random_seed_new (&pvals.seed,
			       &pvals.timeseed,
			       TRUE, FALSE);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 0,
			     _("Random Seed:"), 1.0, 0.5,
			     seed, 1, TRUE);
  g_signal_connect_swapped (G_OBJECT (GIMP_RANDOM_SEED_SPINBUTTON_ADJ (seed)),
                            "value_changed",
                            G_CALLBACK (plasma),
                            drawable);
  g_signal_connect_swapped (G_OBJECT (GIMP_RANDOM_SEED_TOGGLEBUTTON (seed)),
                            "toggled",
                            G_CALLBACK (plasma),
                            drawable);

  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, 1,
			      _("Turbulence:"), SCALE_WIDTH, 0,
			      pvals.turbulence,
			      0.1, 7.0, 0.1, 1.0, 1,
			      TRUE, 0, 0,
			      NULL, NULL);
  g_signal_connect (G_OBJECT (adj), "value_changed",
                    G_CALLBACK (gimp_double_adjustment_update),
                    &pvals.turbulence);
  g_signal_connect_swapped (G_OBJECT (adj), "value_changed",
                            G_CALLBACK (plasma),
                            drawable);

  gtk_widget_show (frame);
  gtk_widget_show (table);
  gtk_widget_show (dlg);

  gtk_main ();
  gimp_help_free ();
  gdk_flush ();

  return pint.run;
}

static void
plasma_ok_callback (GtkWidget *widget,
		    gpointer   data)
{
  pint.run = TRUE;

  gtk_widget_destroy (GTK_WIDGET (data));
}

#define AVE(n, v1, v2) n[0] = ((gint)v1[0] + (gint)v2[0]) / 2;\
	n[1] = ((gint)v1[1] + (gint)v2[1]) / 2;\
	n[2] = ((gint)v1[2] + (gint)v2[2]) / 2;

/*
 * Some glabals to save passing too many paramaters that don't change.
 */

static gint	ix1, iy1, ix2, iy2;	/* Selected image size. */
static GimpTile   *tile=NULL;
static gint     tile_row, tile_col;
static gint     tile_width, tile_height;
static gint     tile_dirty;
static gint	bpp, has_alpha, alpha;
static gdouble	turbulence;
static glong	max_progress, progress;

/*
 * The setup function.
 */

static void
plasma (GimpDrawable *drawable, 
	gboolean    preview_mode)
{
  gint  depth;

  init_plasma (drawable, preview_mode);

  /*
   * This first time only puts in the seed pixels - one in each
   * corner, and one in the center of each edge, plus one in the
   * center of the image.
   */

  do_plasma (drawable, ix1, iy1, ix2 - 1, iy2 - 1, -1, 0, preview_mode);

  /*
   * Now we recurse through the images, going further each time.
   */
  depth = 1;
  while (!do_plasma (drawable, ix1, iy1, ix2 - 1, iy2 - 1, depth, 0, preview_mode))
    {
      depth ++;
    }
  end_plasma (drawable, preview_mode);
}

static void
init_plasma (GimpDrawable *drawable,
	     gboolean   preview_mode)
{
  if (pvals.timeseed)
    pvals.seed = time(NULL);

  srand (pvals.seed);
  turbulence = pvals.turbulence;

  if (preview_mode) 
    {
      ix1 = iy1 = 0;
      ix2 = GTK_PREVIEW (preview)->buffer_width;
      iy2 = GTK_PREVIEW (preview)->buffer_height;
      bpp = GTK_PREVIEW (preview)->bpp;
      alpha = bpp;
      has_alpha = 0;
      work_buffer = g_malloc (GTK_PREVIEW (preview)->rowstride * iy2);
      memcpy (work_buffer, GTK_PREVIEW (preview)->buffer, GTK_PREVIEW (preview)->rowstride * iy2);
    } 
  else 
    {
      gimp_drawable_mask_bounds (drawable->drawable_id, &ix1, &iy1, &ix2, &iy2);
      bpp = drawable->bpp;
      has_alpha = gimp_drawable_has_alpha (drawable->drawable_id);
      if (has_alpha)
	alpha = bpp-1;
      else
	alpha = bpp;
    }

  max_progress = (ix2 - ix1) * (iy2 - iy1);
  progress = 0;

  tile_width  = gimp_tile_width ();
  tile_height = gimp_tile_height ();

  tile = NULL;
  tile_row = 0; tile_col = 0;
}

static void
provide_tile (GimpDrawable *drawable,
	      gint       col,
	      gint       row)
{
  if (col != tile_col || row != tile_row || !tile)
    {
      if (tile)
	gimp_tile_unref (tile, tile_dirty);

      tile_col = col;
      tile_row = row;
      tile = gimp_drawable_get_tile (drawable, TRUE, tile_row, tile_col);
      tile_dirty = FALSE;
      gimp_tile_ref (tile);
    }
}

static void
end_plasma (GimpDrawable *drawable,
	    gboolean   preview_mode)
{
  if (preview_mode) 
    {
      memcpy (GTK_PREVIEW (preview)->buffer, work_buffer, GTK_PREVIEW (preview)->rowstride * iy2);
      g_free (work_buffer);
      gtk_widget_queue_draw (preview);
    } 
  else 
    {
      if (tile)
	gimp_tile_unref (tile, tile_dirty);
      tile = NULL;

      gimp_drawable_flush (drawable);
      gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
      gimp_drawable_update (drawable->drawable_id,
			    ix1, iy1, (ix2 - ix1), (iy2 - iy1));
    }
}

static void
get_pixel (GimpDrawable *drawable,
	   gint       x,
	   gint       y,
	   guchar    *pixel,
	   gboolean   preview_mode)
{
  gint   row, col;
  gint   offx, offy, i;
  guchar  *ptr;

  if (x < ix1)       x = ix1;
  if (x > ix2 - 1)   x = ix2 - 1;
  if (y < iy1)       y = iy1;
  if (y > iy2 - 1)   y = iy2 - 1;

  if (preview_mode) 
    {
      memcpy (pixel, work_buffer + (y * GTK_PREVIEW (preview)->rowstride) + (x * bpp), bpp);
    }
  else 
    {
      col = x / tile_width;
      row = y / tile_height;
      offx = x % tile_width;
      offy = y % tile_height;
      
      provide_tile (drawable, col, row);
      ptr = tile->data + (offy * tile->ewidth + offx) * bpp;

      for (i = 0; i < alpha; i++)
	pixel[i] = ptr[i];
    }
}

static void
put_pixel (GimpDrawable *drawable,
	   gint       x,
	   gint       y,
	   guchar    *pixel,
	   gboolean   preview_mode)
{
  gint   row, col;
  gint   offx, offy, i;
  guchar  *ptr;

  if (x < ix1)       x = ix1;
  if (x > ix2 - 1)   x = ix2 - 1;
  if (y < iy1)       y = iy1;
  if (y > iy2 - 1)   y = iy2 - 1;

  if (preview_mode)
    memcpy (work_buffer + (y * GTK_PREVIEW (preview)->rowstride) + (x * bpp), pixel, bpp);
  else
    {
      col = x / tile_width;
      row = y / tile_height;
      offx = x % tile_width;
      offy = y % tile_height;

      provide_tile (drawable, col, row);
      ptr = tile->data + (offy * tile->ewidth + offx) * bpp;

      for (i = 0; i < alpha; i++)
	ptr[i] = pixel[i];

      if (has_alpha)
	ptr[alpha] = 255;

      tile_dirty = TRUE;
      progress++;
    }
}

static void
random_rgb (guchar *d)
{
  gint i;

  for(i = 0; i < alpha; i++)
    {
      d[i] = rand() % 256;
    }
}

static void
add_random (guchar *d,
	    gint    amnt)
{
  gint i, tmp;

  for (i = 0; i < alpha; i++)
    {
      if (amnt == 0)
	{
	  amnt = 1;
	}
      tmp = amnt/2 - rand() % amnt;

      if ((gint)d[i] + tmp < 0)
	{
	  d[i] = 0;
	}
      else if ((gint)d[i] + tmp > 255)
	{
	  d[i] = 255;
	}
      else
	{
	  d[i] += tmp;
	}
    }
}

static gint
do_plasma (GimpDrawable *drawable,
	   gint       x1,
	   gint       y1,
	   gint       x2,
	   gint       y2,
	   gint       depth,
	   gint       scale_depth,
	   gboolean   preview_mode)
{
  guchar  tl[3], ml[3], bl[3], mt[3], mm[3], mb[3], tr[3], mr[3], br[3];
  guchar  tmp[3];
  gint    ran;
  gint    xm, ym;

  static gint count = 0;

  /* Initial pass through - no averaging. */

  if (depth == -1)
    {
      random_rgb (tl);
      put_pixel (drawable, x1, y1, tl, preview_mode);
      random_rgb (tr);
      put_pixel (drawable, x2, y1, tr, preview_mode);
      random_rgb (bl);
      put_pixel (drawable, x1, y2, bl, preview_mode);
      random_rgb (br);
      put_pixel (drawable, x2, y2, br, preview_mode);
      random_rgb (mm);
      put_pixel (drawable, (x1 + x2) / 2, (y1 + y2) / 2, mm, preview_mode);
      random_rgb (ml);
      put_pixel (drawable, x1, (y1 + y2) / 2, ml, preview_mode);
      random_rgb (mr);
      put_pixel (drawable, x2, (y1 + y2) / 2, mr, preview_mode);
      random_rgb (mt);
      put_pixel (drawable, (x1 + x2) / 2, y1, mt, preview_mode);
      random_rgb (ml);
      put_pixel (drawable, (x1 + x2) / 2, y2, ml, preview_mode);

      return 0;
    }

  /*
   * Some later pass, at the bottom of this pass,
   * with averaging at this depth.
   */
  if (depth == 0)
    {
      gdouble	rnd;
      gint	xave, yave;

      get_pixel (drawable, x1, y1, tl, preview_mode);
      get_pixel (drawable, x1, y2, bl, preview_mode);
      get_pixel (drawable, x2, y1, tr, preview_mode);
      get_pixel (drawable, x2, y2, br, preview_mode);

      rnd = (256.0 / (2.0 * (gdouble)scale_depth)) * turbulence;
      ran = rnd;

      xave = (x1 + x2) / 2;
      yave = (y1 + y2) / 2;

      if (xave == x1 && xave == x2 && yave == y1 && yave == y2)
	{
	  return 0;
	}

      if (xave != x1 || xave != x2)
	{
	  /* Left. */
	  AVE (ml, tl, bl);
	  add_random (ml, ran);
	  put_pixel (drawable, x1, yave, ml, preview_mode);

	  if (x1 != x2)
	    {
				/* Right. */
	      AVE (mr, tr, br);
	      add_random (mr, ran);
	      put_pixel (drawable, x2, yave, mr, preview_mode);
	    }
	}

      if (yave != y1 || yave != y2)
	{
	  if (x1 != xave || yave != y2)
	    {
	      /* Bottom. */
	      AVE (mb, bl, br);
	      add_random (mb, ran);
	      put_pixel (drawable, xave, y2, mb, preview_mode);
	    }

	  if (y1 != y2)
	    {
	      /* Top. */
	      AVE (mt, tl, tr);
	      add_random (mt, ran);
	      put_pixel (drawable, xave, y1, mt, preview_mode);
	    }
	}

      if (y1 != y2 || x1 != x2)
	{
	  /* Middle pixel. */
	  AVE (mm, tl, br);
	  AVE (tmp, bl, tr);
	  AVE (mm, mm, tmp);

	  add_random (mm, ran);
	  put_pixel (drawable, xave, yave, mm, preview_mode);
	}

      count ++;

      if (!(count % 2000) && !preview_mode)
	{
	  gimp_progress_update ((double) progress / (double) max_progress);
	}

      if ((x2 - x1) < 3 && (y2 - y1) < 3)
	{
	  return 1;
	}
      return 0;
    }

  xm = (x1 + x2) >> 1;
  ym = (y1 + y2) >> 1;

  /* Top left. */
  do_plasma (drawable, x1, y1, xm, ym, depth - 1, scale_depth + 1, preview_mode);
  /* Bottom left. */
  do_plasma (drawable, x1, ym, xm ,y2, depth - 1, scale_depth + 1, preview_mode);
  /* Top right. */
  do_plasma (drawable, xm, y1, x2 , ym, depth - 1, scale_depth + 1, preview_mode);
  /* Bottom right. */
  return do_plasma (drawable, xm, ym, x2, y2, depth - 1, scale_depth + 1, preview_mode);
}


/* preview library */


static GtkWidget *
preview_widget (void)
{
  GtkWidget *preview;
  guchar    *buf;
  gint       y;

  preview = gtk_preview_new (GTK_PREVIEW_COLOR);
  gtk_preview_size (GTK_PREVIEW (preview), PREVIEW_SIZE, PREVIEW_SIZE);

  buf = g_malloc0 (PREVIEW_SIZE * 3);
  for (y = 0; y < PREVIEW_SIZE; y++) 
    gtk_preview_draw_row (GTK_PREVIEW (preview), buf, 0, y, PREVIEW_SIZE);
  g_free (buf);

  return preview;
}

