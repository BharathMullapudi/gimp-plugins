/* edge filter for the GIMP
 *  -Peter Mattis
 *
 * This filter performs edge detection on the input image.
 *  The code for this filter is based on "pgmedge", a program
 *  that is part of the netpbm package.
 */

/* pgmedge.c - edge-detect a portable graymap
**
** Copyright (C) 1989 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

/*
 *  Ported to GIMP Plug-in API 1.0
 *  version 1.07
 *  This version requires GIMP v0.99.10 or above.
 *
 *  This plug-in performs edge detection. The code is based on edge.c
 *  for GIMP 0.54 by Peter Mattis.
 *
 *	Eiichi Takamori <taka@ma1.seikyou.ne.jp>
 *	http://ha1.seikyou.ne.jp/home/taka/gimp/
 *
 *  Tips: you can enter arbitrary value into entry.
 *	(not bounded between 1.0 and 10.0)
 *
 *  Changes from version 1.06 to version 1.07:
 *  - Added entry
 *  - Cleaned up code a bit
 *
 *  Differences from Peter Mattis's original `edge' plug-in:
 *    - Added Wrapmode. (useful for tilable images)
 *    - Enhanced speed in this version.
 *    - It works with the alpha channel.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


#ifdef RCSID
static gchar rcsid[] = "$Id$";
#endif

/* Some useful macros */

#define TILE_CACHE_SIZE 48

enum
{
  WRAP,
  SMEAR,
  BLACK
};

typedef struct
{
  gdouble amount;
  gint    wrapmode;
} EdgeVals;

typedef struct
{
  gint run;
} EdgeInterface;

typedef struct
{
  GimpTile     *tile;
  gint	     row, col;	/* tile's row, col */
  gint       bpp;
  gint       tile_width, tile_height;
  GimpDrawable *drawable;
  gint       drawable_width, drawable_height;
} TileBuf;

/*
 * Function prototypes.
 */

static void      query  (void);
static void      run    (gchar    *name,
			 gint      nparams,
			 GimpParam   *param,
			 gint     *nreturn_vals,
			 GimpParam  **return_vals);

static void      edge        (GimpDrawable *drawable);
static gint      edge_dialog (GimpDrawable *drawable);

static long      long_sqrt   (long n);

static void   init_tile_buf  (TileBuf   *buf,
			      GimpDrawable *drawable);
static void   get_tile_pixel (TileBuf   *buf,
			      gint       x,
			      gint       y, 
			      guchar    *pixel,
			      gint       wrapmode);
static void   end_tile_buf   (TileBuf   *buf);

/***** Local vars *****/

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init  */
  NULL,  /* quit  */
  query, /* query */
  run,   /* run   */
};

static EdgeVals evals =
{
  2.0,   /* amount   */
  SMEAR  /* wrapmode */
};

static EdgeInterface eint =
{
  FALSE  /* run */
};

/***** Functions *****/

MAIN ()

static void
query (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE, "image", "Input image (unused)" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable" },
    { GIMP_PDB_FLOAT, "amount", "Edge detection amount" },
    { GIMP_PDB_INT32, "wrapmode", "Edge detection behavior: { WRAP (0), SMEAR (1), BLACK (2) }" }
  };
  static gint nargs = sizeof (args) / sizeof (args[0]);

  gchar *help_string =
    "Perform edge detection on the contents of the specified drawable. It "
    "applies, I think, convolution with 3x3 kernel. AMOUNT is an arbitrary "
    "constant, WRAPMODE is like displace plug-in (useful for tilable image).";

  gimp_install_procedure ("plug_in_edge",
			  "Perform edge detection on the contents of the specified drawable",
			  help_string,
			  "Peter Mattis & (ported to 1.0 by) Eiichi Takamori",
			  "Peter Mattis",
			  "1996",
			  N_("<Image>/Filters/Edge-Detect/Edge..."),
			  "RGB*, GRAY*",
			  GIMP_PLUGIN,
			  nargs, 0,
			  args, NULL);
}

static void
run (gchar  *name,
     gint    nparams,
     GimpParam  *param,
     gint   *nreturn_vals,
     GimpParam **return_vals)
{
  static GimpParam values[1];
  GimpDrawable *drawable;
  GimpRunModeType run_mode;
  GimpPDBStatusType status = GIMP_PDB_SUCCESS;

  run_mode = param[0].data.d_int32;

  /*  Get the specified drawable  */
  drawable = gimp_drawable_get (param[2].data.d_drawable);

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE:
      INIT_I18N_UI();
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_edge", &evals);

      /*  First acquire information with a dialog  */
      if (! edge_dialog (drawable))
	return;
      break;

    case GIMP_RUN_NONINTERACTIVE:
      /*  Make sure all the arguments are there!  */
      if (nparams != 5)
	status = GIMP_PDB_CALLING_ERROR;
      if (status == GIMP_PDB_SUCCESS)
	{
	  evals.amount   = param[3].data.d_float;
	  evals.wrapmode = param[4].data.d_int32;
	}
      INIT_I18N();
      break;

    case GIMP_RUN_WITH_LAST_VALS:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_edge", &evals);
      INIT_I18N();
      break;

    default:
      break;
    }

  /* make sure the drawable exist and is not indexed */
  if (gimp_drawable_is_rgb (drawable->drawable_id) ||
      gimp_drawable_is_gray (drawable->drawable_id))
    {
      gimp_progress_init (_("Edge Detection..."));

      /*  set the tile cache size  */
      gimp_tile_cache_ntiles (TILE_CACHE_SIZE);

      /*  run the edge effect  */
      edge (drawable);

      if (run_mode != GIMP_RUN_NONINTERACTIVE)
	gimp_displays_flush ();

      /*  Store data  */
      if (run_mode == GIMP_RUN_INTERACTIVE)
	gimp_set_data ("plug_in_edge", &evals, sizeof (EdgeVals));
    }
  else
     {
      /* gimp_message ("edge: cannot operate on indexed color images"); */
      status = GIMP_PDB_EXECUTION_ERROR;
    }

  values[0].data.d_status = status;

  gimp_drawable_detach (drawable);
}

/*********************************************************************

   TileBuf Util Routines:   Util routines for getting arbitrary pixel
   CAUTION -- the tile is read only !!

 **********************************************************************/

static void
init_tile_buf (TileBuf   *buf,
	       GimpDrawable *drawable)
{
  buf->tile = NULL;
  buf->col = 0;
  buf->row = 0;
  if (gimp_drawable_is_rgb (drawable->drawable_id))
    buf->bpp = 3;
  else
    buf->bpp = 1;
  buf->tile_width = gimp_tile_width();
  buf->tile_height = gimp_tile_height();
  buf->drawable = drawable;
  buf->drawable_width = gimp_drawable_width(drawable->drawable_id);
  buf->drawable_height = gimp_drawable_height(drawable->drawable_id);
}

static void
get_tile_pixel (TileBuf *buf,
		gint     x,
		gint     y,
		guchar  *pixel,
		gint     wrapmode)
{
  gint b;
  gint offx, offy;
  gint row, col;
  guchar *ptr;

  if (x < 0 || x >= buf->drawable_width ||
      y < 0 || y >= buf->drawable_height)
    switch (wrapmode)
      {
      case WRAP:
	if (x < 0 || x >= buf->drawable_width)
	  {
	    x %= buf->drawable_width;
	    if (x < 0)
	      x += buf->drawable_width;
	  }
	if (y < 0 || y >= buf->drawable_height)
	  {
	    y %= buf->drawable_height;
	    if (y < 0)
	      y += buf->drawable_height;
	  }
	break;
      case SMEAR:
	if (x < 0)
	  x = 0;
	if (x >= buf->drawable_width)
	  x = buf->drawable_width - 1;
	if (y < 0)
	  y = 0;
	if (y >= buf->drawable_height)
	  y = buf->drawable_height - 1;
	break;
      case BLACK:
	if (x < 0 || x >= buf->drawable_width || 
	    y < 0 || y >= buf->drawable_height)
	  {
	    for (b = 0; b < buf->bpp; b++)
	      pixel[b] = 0;
	    return;
	  }
	break;
      default:
	return;
      }

  col = x / buf->tile_width;
  offx = x % buf->tile_width;
  row = y / buf->tile_height;
  offy = y % buf->tile_height;

  /* retrieve tile */
  if (!buf->tile || col != buf->col || row != buf->row)
    {
      if(buf->tile)
	gimp_tile_unref (buf->tile, FALSE);
      buf->col = col;
      buf->row = row;
      buf->tile = gimp_drawable_get_tile (buf->drawable, FALSE, row, col);
      gimp_tile_ref (buf->tile);
    }

  /* retrieve target pixel */
  ptr = buf->tile->data + (offy * buf->tile->ewidth + offx) * buf->tile->bpp;
  for(b = 0; b < buf->bpp; b++)
    pixel[b] = ptr[b];
}

static void
end_tile_buf (TileBuf *buf)
{
  if (buf->tile)
    gimp_tile_unref (buf->tile, FALSE);
}

/**********************************************************************
   TileBuf Util Routines End
 **********************************************************************/

static long
long_sqrt (long n)
{
#define lsqrt_max4pow (1UL << 30)
  /* lsqrt_max4pow is the (machine-specific) largest power of 4 that can
   * be represented in an unsigned long.
   *
   * Compute the integer square root of the integer argument n
   * Method is to divide n by x computing the quotient x and remainder r
   * Notice that the divisor x is changing as the quotient x changes
   * 
   * Instead of shifting the dividend/remainder left, we shift the
   * quotient/divisor right. The binary point starts at the extreme
   * left, and shifts two bits at a time to the extreme right.
   * 
   * The residue contains n-x^2. (Within these comments, the ^ operator
   * signifies exponentiation rather than exclusive or. Also, the /
   * operator returns fractions, rather than truncating, so 1/4 means
   * one fourth, not zero.)
   * 
   * Since (x + 1/2)^2 == x^2 + x + 1/4,
   * n - (x + 1/2)^2 == (n - x^2) - (x + 1/4)
   * Thus, we can increase x by 1/2 if we decrease (n-x^2) by (x+1/4)
   */

  gulong residue;        /* n - x^2  */
  gulong root;           /* x + 1/4  */
  gulong half;           /* 1/2      */

  residue = n;           /* n - (x = 0)^2, with suitable alignment */

  /*
   * if the correct answer fits in two bits, pull it out of a magic hat
   */
  if (residue <= 12)
    return (0x03FFEA94 >> (residue *= 2)) & 3;

  root = lsqrt_max4pow;         /* x + 1/4, shifted all the way left */
  /* half = root + root; 1/2, shifted likewise */

  /* 
   * Unwind iterations corresponding to leading zero bits 
   */
  while (root > residue)
    root >>= 2;

  /*
   * Unwind the iteration corresponding to the first one bit
   * Operations have been rearranged and combined for efficiency
   * Initialization of half is folded into this iteration
   */
  residue -= root;              /* Decrease (n-x^2) by (0+1/4)             */
  half = root >> 2;             /* 1/4, with binary point shifted right 2  */
  root += half;                 /* x=1. (root is now (x=1)+1/4.)           */
  half += half;                 /* 1/2, properly aligned                   */

  /*
   * Normal loop (there is at least one iteration remaining)
   */
  do
    {
      if (root <= residue)      /* Whenever we can,                          */
        {
          residue -= root;      /* decrease (n-x^2) by (x+1/4)               */
          root += half;         /* increase x by 1/2                         */
        }
      half >>= 2;               /* Shift binary point 2 places right          */
      root -= half;             /* x{ +1/2 } +1/4 - 1/8 == x { +1/2 } 1/8     */
      root >>= 1;               /* 2x{ +1 } +1/4, shifted right 2 places      */
    }
  while (half);                 /* When 1/2 == 0, bin. point is at far right  */

  /* 
   * round up if (x+1/2)^2 < n
   */
  if (root < residue)
    ++root;

  /* 
   * Guaranteed to be correctly rounded (or truncated)
   */
  return root;
}

/********************************************************/
/*              Edge Detection main                     */
/********************************************************/

static void
edge (GimpDrawable *drawable)
{
  /*
   * this function is too long, so I must split this into a few
   * functions later ...  -- taka
   */
  GimpPixelRgn src_rgn, dest_rgn;
  gpointer pr;
  TileBuf buf;
  guchar *srcrow, *src;
  guchar *destrow, *dest;
  guchar pix00[3], pix01[3], pix02[3];
  guchar pix10[3],/*pix11[3],*/ pix12[3];
  guchar pix20[3], pix21[3], pix22[3];
  glong width, height;
  gint alpha, has_alpha, chan;
  gint x, y;
  gint x1, y1, x2, y2;
  glong sum1, sum2;
  glong sum, scale;
  gint maxval;
  gint cur_progress;
  gint max_progress;
  gint wrapmode;

  if (evals.amount < 1.0)
    evals.amount = 1.0;

  init_tile_buf (&buf, drawable);

  gimp_drawable_mask_bounds (drawable->drawable_id, &x1, &y1, &x2, &y2);

  width = gimp_drawable_width (drawable->drawable_id);
  height = gimp_drawable_height (drawable->drawable_id);
  alpha = gimp_drawable_bpp (drawable->drawable_id);
  has_alpha = gimp_drawable_has_alpha (drawable->drawable_id);
  if (has_alpha)
    alpha--;

  maxval = 255;
  scale = (10 << 16) / evals.amount;
  wrapmode = evals.wrapmode;

  cur_progress = 0;
  max_progress = (x2 - x1) * (y2 - y1);

  gimp_pixel_rgn_init (&src_rgn, drawable, x1, y1, x2-x1, y2-y1, FALSE, FALSE);
  gimp_pixel_rgn_init (&dest_rgn, drawable, x1, y1, x2-x1, y2-y1, TRUE, TRUE);

  for (pr = gimp_pixel_rgns_register (2, &src_rgn, &dest_rgn); 
       pr != NULL;
       pr = gimp_pixel_rgns_process (pr))
    {
      srcrow = src_rgn.data;
      destrow = dest_rgn.data;
      for (y = dest_rgn.y;
	    y < (dest_rgn.y + dest_rgn.h);
	    y++, srcrow += src_rgn.rowstride, destrow += dest_rgn.rowstride)
	{
	  src = srcrow;
	  dest = destrow;
	  for (x = dest_rgn.x;
	       x < (dest_rgn.x + dest_rgn.w);
	       x++,  src += src_rgn.bpp, dest += dest_rgn.bpp)
	    {
	      if(dest_rgn.x < x &&  x < dest_rgn.x + dest_rgn.w - 1 &&
		 dest_rgn.y < y &&  y < dest_rgn.y + dest_rgn.h - 1)
		{
		  /*
		  ** 3x3 kernel is inside of the tile -- do fast
		  ** version
		  */
		  for (chan = 0; chan < alpha; chan++)
		    {
		      /*
		       * PIX(1,1) is the current pixel, so
		       * e.g. PIX(0,0) means 1 above and 1 left pixel.
		       *
		       * There were casting to `long' in GIMP 0.54
		       * edge code, but I think `guchar' should be
		       * extended to `int' with minus operators, so
		       * there's no need to cast to `long'. Both sum1
		       * and sum2 will be between -4*255 to 4*255
		       *
		       *    -- taka
		       */
#define PIX(X,Y)  src[ (Y-1)*(int)src_rgn.rowstride + (X-1)*(int)src_rgn.bpp + chan ]
		      /* make convolution */
		      sum1 = (PIX(2,0) - PIX(0,0)) +
			 2 * (PIX(2,1) - PIX(0,1)) +
			     (PIX(2,2) - PIX(2,0));
		      sum2 = (PIX(0,2) - PIX(0,0)) +
			 2 * (PIX(1,2) - PIX(1,0)) +
			     (PIX(2,2) - PIX(2,0));
#undef  PIX
		      /* common job ... */
		      sum = long_sqrt ((long) sum1 * sum1 + (long) sum2 * sum2);
		      sum = (sum * scale) >> 16;    /* arbitrary scaling factor */
		      if (sum > maxval)
			sum = maxval;
		      dest[chan] = sum;
		    }
		}
	      else
		{
		  /*
		  ** The kernel is not inside of the tile -- do slow
		  ** version
		  */
		  /*
		   * When the kernel intersects the boundary of the
		   * image, get_tile_pixel() will (should) do the
		   * right work with `wrapmode'.
		   */
		  get_tile_pixel (&buf, x-1, y-1, pix00, wrapmode);
		  get_tile_pixel (&buf, x  , y-1, pix10, wrapmode);
		  get_tile_pixel (&buf, x+1, y-1, pix20, wrapmode);
		  get_tile_pixel (&buf, x-1, y  , pix01, wrapmode);
		  get_tile_pixel (&buf, x+1, y  , pix21, wrapmode);
		  get_tile_pixel (&buf, x-1, y+1, pix02, wrapmode);
		  get_tile_pixel (&buf, x  , y+1, pix12, wrapmode);
		  get_tile_pixel (&buf, x+1, y+1, pix22, wrapmode);

		  for (chan = 0; chan < alpha; chan++)
		    {
		      /* make convolution */
		      sum1 = (pix20[chan] - pix00[chan]) +
			 2 * (pix21[chan] - pix01[chan]) +
			     (pix22[chan] - pix20[chan]);
		      sum2 = (pix02[chan] - pix00[chan]) +
			 2 * (pix12[chan] - pix10[chan]) +
			     (pix22[chan] - pix20[chan]);
		      /* common job ... */
		      sum = long_sqrt ((long) sum1 * sum1 + (long) sum2 * sum2);
		      sum = (sum * scale) >> 16;  /* arbitrary scaling factor */
		      if (sum > maxval) sum = maxval;
		      dest[chan] = sum;
		    }
		}
	      if (has_alpha)
		dest[alpha] = src[alpha];
	    }
        }
      cur_progress += dest_rgn.w * dest_rgn.h;
      gimp_progress_update ((double) cur_progress / (double) max_progress);
    }

  end_tile_buf (&buf);
  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
  gimp_drawable_update (drawable->drawable_id, x1, y1, (x2 - x1), (y2 - y1));
}

/*******************************************************/
/*                    Dialog                           */
/*******************************************************/

static void
edge_ok_callback (GtkWidget *widget,
		  gpointer   data)
{
  eint.run = TRUE;

  gtk_widget_destroy (GTK_WIDGET (data));
}

static gint
edge_dialog (GimpDrawable *drawable)
{
  GtkWidget *dlg;
  GtkWidget *frame;
  GtkWidget *table;
  GtkWidget *hbox;
  GtkWidget *toggle;
  GtkObject *scale_data;
  GSList *group = NULL;

  gint	use_wrap  = (evals.wrapmode == WRAP);
  gint	use_smear = (evals.wrapmode == SMEAR);
  gint	use_black = (evals.wrapmode == BLACK);

  gimp_ui_init ("edge", FALSE);

  dlg = gimp_dialog_new (_("Edge Detection"), "edge",
			 gimp_standard_help_func, "filters/edge.html",
			 GTK_WIN_POS_MOUSE,
			 FALSE, TRUE, FALSE,

			 _("OK"), edge_ok_callback,
			 NULL, NULL, NULL, TRUE, FALSE,
			 _("Cancel"), gtk_widget_destroy,
			 NULL, 1, NULL, FALSE, TRUE,

			 NULL);

  gtk_signal_connect (GTK_OBJECT (dlg), "destroy",
		      GTK_SIGNAL_FUNC (gtk_main_quit),
		      NULL);

  /*  parameter settings  */
  frame = gtk_frame_new (_("Parameter Settings"));
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
  gtk_container_set_border_width (GTK_CONTAINER (frame), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), frame, TRUE, TRUE, 0);

  table = gtk_table_new (2, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_table_set_row_spacings (GTK_TABLE (table), 4);
  gtk_container_set_border_width (GTK_CONTAINER (table), 4);
  gtk_container_add (GTK_CONTAINER (frame), table);

  /*  Label, scale, entry for evals.amount  */
  scale_data = gimp_scale_entry_new (GTK_TABLE (table), 0, 0,
				     _("Amount:"), 100, 0,
				     evals.amount, 1.0, 10.0, 0.1, 1.0, 1,
				     TRUE, 0, 0,
				     NULL, NULL);

  gtk_signal_connect (GTK_OBJECT (scale_data), "value_changed",
		      GTK_SIGNAL_FUNC (gimp_double_adjustment_update),
		      &evals.amount);

  /*  Radio buttons WRAP, SMEAR, BLACK  */

  hbox = gtk_hbox_new (FALSE, 4);
  gtk_table_attach (GTK_TABLE (table), hbox, 0, 3, 1, 2,
		    GTK_FILL, GTK_FILL, 0, 0);

  toggle = gtk_radio_button_new_with_label (group, _("Wrap"));
  group = gtk_radio_button_group (GTK_RADIO_BUTTON (toggle));
  gtk_box_pack_start (GTK_BOX (hbox), toggle, TRUE, TRUE, 0);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &use_wrap);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), use_wrap);
  gtk_widget_show (toggle);

  toggle = gtk_radio_button_new_with_label (group, _("Smear"));
  group = gtk_radio_button_group (GTK_RADIO_BUTTON (toggle));
  gtk_box_pack_start (GTK_BOX (hbox), toggle, TRUE, TRUE, 0);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &use_smear);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), use_smear);
  gtk_widget_show (toggle);

  toggle = gtk_radio_button_new_with_label (group, _("Black"));
  group = gtk_radio_button_group (GTK_RADIO_BUTTON (toggle));
  gtk_box_pack_start (GTK_BOX (hbox), toggle, TRUE, TRUE, 0);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &use_black);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), use_black);
  gtk_widget_show (toggle);
  gtk_widget_show (hbox);

  gtk_widget_show (table);
  gtk_widget_show (frame);
  gtk_widget_show (dlg);

  gtk_main ();
  gdk_flush ();

  if (use_wrap)
    evals.wrapmode = WRAP;
  else if (use_smear)
    evals.wrapmode = SMEAR;
  else if (use_black)
    evals.wrapmode = BLACK;

  return eint.run;
}
