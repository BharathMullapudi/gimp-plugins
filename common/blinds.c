/*
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This is a plug-in for the GIMP.
 *
 * Blinds plug-in. Distort an image as though it was stuck to 
 * window blinds and the blinds where opened/closed.
 *
 * Copyright (C) 1997 Andy Thomas  alt@picnic.demon.co.uk
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
 * A fair proprotion of this code was taken from the Whirl plug-in
 * which was copyrighted by Federico Mena Quintero (as below).
 * 
 * Whirl plug-in --- distort an image into a whirlpool
 * Copyright (C) 1997 Federico Mena Quintero           
 *
 */

/* Change log:-
 * 
 * Version 0.5 10 June 1997.
 * Changes required to work with 0.99.10.
 *
 * Version 0.4 20 May 1997.
 * Fixed problem with using this plugin in GIMP_RUN_NONINTERACTIVE mode
 *
 * Version 0.3 8 May 1997.
 * Make preview work in Quartics words "The Right Way".
 *
 * Allow the background to be transparent.
 *
 * Version 0.2 1 May 1997 (not released).
 * Added patches supplied by Tim Mooney mooney@dogbert.cc.ndsu.NoDak.edu
 * to allow the plug-in to build with Digitals compiler.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


/***** Magic numbers *****/

#define SCALE_WIDTH  150

#define MAX_FANS      10

#define HORIZONTAL     0
#define VERTICAL       1

/* Variables set in dialog box */
typedef struct data
{
  gint angledsp;
  gint numsegs;
  gint orientation;
  gint bg_trans;
} BlindVals;

static GimpFixMePreview *preview;

typedef struct
{
  gboolean   run;
  gint       img_bpp;
} BlindsInterface;

static BlindsInterface bint =
{
  FALSE,         /* run */
  4              /* bpp of drawable */
};

/* Array to hold each size of fans. And no there are not each the
 * same size (rounding errors...)
 */

static gint fanwidths[MAX_FANS];

static GimpDrawable *blindsdrawable;

static void      query  (void);
static void      run    (const gchar      *name,
			 gint              nparams,
			 const GimpParam  *param,
			 gint             *nreturn_vals,
			 GimpParam       **return_vals);

static gint      blinds_dialog       (void);

static void      blinds_ok_callback    (GtkWidget     *widget,
					gpointer       data);
static void      blinds_scale_update   (GtkAdjustment *adjustment,
					gint          *size_val);
static void      blinds_radio_update   (GtkWidget     *widget,
					gpointer       data);
static void      blinds_button_update  (GtkWidget     *widget,
					gpointer       data);
static void      dialog_update_preview (void);
static void	 cache_preview         (void);
static void      apply_blinds          (void);

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};

/* Values when first invoked */
static BlindVals bvals =
{
  30,
  3,
  HORIZONTAL,
  FALSE
};

/* Stuff for the preview bit */
static gint  has_alpha;

MAIN ()

static void
query (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE, "image", "Input image (unused)" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable" },
    { GIMP_PDB_INT32, "angle_dsp", "Angle of Displacement " },
    { GIMP_PDB_INT32, "number_of_segments", "Number of segments in blinds" },
    { GIMP_PDB_INT32, "orientation", "orientation; 0 = Horizontal, 1 = Vertical" },
    { GIMP_PDB_INT32, "backgndg_trans", "background transparent; FALSE,TRUE" }
  };

  gimp_install_procedure ("plug_in_blinds",
			  "Adds a blinds effect to the image. Rather like "
			  "putting the image on a set of window blinds and "
			  "the closing or opening the blinds",
			  "More here later",
			  "Andy Thomas",
			  "Andy Thomas",
			  "1997",
			  N_("<Image>/Filters/Distorts/_Blinds..."),
			  "RGB*, GRAY*",
			  GIMP_PLUGIN,
			  G_N_ELEMENTS (args), 0,
			  args, NULL);
}

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  static GimpParam values[1];
  GimpDrawable *drawable;
  GimpRunMode run_mode;
  GimpPDBStatusType status = GIMP_PDB_SUCCESS;

  run_mode = param[0].data.d_int32;

  INIT_I18N ();

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  blindsdrawable = drawable = 
    gimp_drawable_get (param[2].data.d_drawable);

  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE:
      gimp_get_data ("plug_in_blinds", &bvals);
      if (! blinds_dialog())
	{
	  gimp_drawable_detach (drawable);
	  return;
	}
      break;

    case GIMP_RUN_NONINTERACTIVE:
      if (nparams != 7)
	status = GIMP_PDB_CALLING_ERROR;
      if (status == GIMP_PDB_SUCCESS)
	{
	  bvals.angledsp = param[3].data.d_int32;
	  bvals.numsegs = param[4].data.d_int32;
	  bvals.orientation = param[5].data.d_int32;
	  bvals.bg_trans = param[6].data.d_int32;
	}
      break;

    case GIMP_RUN_WITH_LAST_VALS:
      gimp_get_data ("plug_in_blinds", &bvals);
      break;

    default:
      break;
    }

  if (gimp_drawable_is_rgb (drawable->drawable_id) ||
      gimp_drawable_is_gray (drawable->drawable_id))
    {
      gimp_progress_init ( _("Adding Blinds..."));

      apply_blinds ();
   
      if (run_mode != GIMP_RUN_NONINTERACTIVE)
	gimp_displays_flush ();

      if (run_mode == GIMP_RUN_INTERACTIVE)
	gimp_set_data ("plug_in_blinds", &bvals, sizeof (BlindVals));
    }
  else
    {
      status = GIMP_PDB_EXECUTION_ERROR;
    }

  values[0].data.d_status = status;

  gimp_drawable_detach (drawable);
}


/* Build the dialog up. This was the hard part! */
static gint
blinds_dialog (void)
{
  GtkWidget *dlg;
  GtkWidget *main_vbox;
  GtkWidget *hbox;
  GtkWidget *vbox;
  GtkWidget *frame;
  GtkWidget *toggle_vbox;
  GtkWidget *table;
  GtkObject *size_data;
  GtkWidget *toggle;

  gimp_ui_init ("blinds", TRUE);

  cache_preview (); /* Get the preview image and store it also set has_alpha */

  dlg = gimp_dialog_new (_("Blinds"), "blinds",
			 gimp_standard_help_func, "filters/blinds.html",
			 GTK_WIN_POS_MOUSE,
			 FALSE, TRUE, FALSE,

			 GTK_STOCK_CANCEL, gtk_widget_destroy,
			 NULL, 1, NULL, FALSE, TRUE,
			 GTK_STOCK_OK, blinds_ok_callback,
			 NULL, NULL, NULL, TRUE, FALSE,

			 NULL);

  g_signal_connect (dlg, "destroy",
                    G_CALLBACK (gtk_main_quit),
                    NULL);

  main_vbox = gtk_vbox_new (FALSE, 4);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 6);
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dlg)->vbox), main_vbox);
  gtk_widget_show (main_vbox);

  hbox = gtk_hbox_new (FALSE, 6);
  gtk_box_pack_start (GTK_BOX (main_vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  preview = gimp_fixme_preview_new (NULL, TRUE);
  gimp_fixme_preview_fill_scaled (preview, blindsdrawable);
  gtk_box_pack_start (GTK_BOX (hbox), preview->frame, FALSE, FALSE, 0);
  gtk_widget_show (preview->widget);

  vbox = gtk_vbox_new (FALSE, 4);
  gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);
  gtk_widget_show (vbox);

  frame =
    gimp_radio_group_new2 (TRUE, _("Orientation"),
			   G_CALLBACK (blinds_radio_update),
			   &bvals.orientation, (gpointer) bvals.orientation,

			   _("_Horizontal"), (gpointer) HORIZONTAL, NULL,
			   _("_Vertical"),   (gpointer) VERTICAL, NULL,

			   NULL);
  gtk_box_pack_start (GTK_BOX (vbox), frame, TRUE, TRUE, 0);
  gtk_widget_show (frame);

  frame = gtk_frame_new (_("Background"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, TRUE, TRUE, 0);
  gtk_widget_show (frame);

  toggle_vbox = gtk_vbox_new (FALSE, 2);
  gtk_container_set_border_width (GTK_CONTAINER (toggle_vbox), 2);
  gtk_container_add (GTK_CONTAINER (frame), toggle_vbox);
  gtk_widget_show (toggle_vbox);

  toggle = gtk_check_button_new_with_mnemonic (_("_Transparent"));
  gtk_box_pack_start (GTK_BOX (toggle_vbox), toggle, FALSE, FALSE, 0);
  gtk_widget_show (toggle);

  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (blinds_button_update),
                    &bvals.bg_trans);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), bvals.bg_trans);

  if (!has_alpha)
    {
      gtk_widget_set_sensitive (toggle, FALSE);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), FALSE);
    }

  table = gimp_parameter_settings_new (main_vbox, 2, 3);

  size_data = gimp_scale_entry_new (GTK_TABLE (table), 0, 0,
				    _("_Displacement:"), SCALE_WIDTH, 0,
				    bvals.angledsp, 1, 90, 1, 15, 0,
				    TRUE, 0, 0,
				    NULL, NULL);
  g_signal_connect (size_data, "value_changed",
                    G_CALLBACK (blinds_scale_update),
                    &bvals.angledsp);

  size_data = gimp_scale_entry_new (GTK_TABLE (table), 0, 1,
				    _("_Num Segments:"), SCALE_WIDTH, 0,
				    bvals.numsegs, 1, MAX_FANS, 1, 2, 0,
				    TRUE, 0, 0,
				    NULL, NULL);
  g_signal_connect (size_data, "value_changed",
		    G_CALLBACK (blinds_scale_update),
		    &bvals.numsegs);

  gtk_widget_show (dlg);

  dialog_update_preview ();

  gtk_main ();
  gdk_flush ();

  return bint.run;
}

static void
blinds_ok_callback (GtkWidget *widget,
		    gpointer   data)
{
  bint.run = TRUE;

  gtk_widget_destroy (GTK_WIDGET (data));
}

static void
blinds_radio_update (GtkWidget *widget,
		     gpointer   data)
{
  gimp_radio_button_update (widget, data);

  if (GTK_TOGGLE_BUTTON (widget)->active)
    dialog_update_preview ();
}                  

static void
blinds_button_update (GtkWidget *widget,
		      gpointer   data)
{
  gimp_toggle_button_update (widget, data);

  dialog_update_preview ();
}                  

static void
blinds_scale_update (GtkAdjustment *adjustment,
		     gint          *value)
{
  gimp_int_adjustment_update (adjustment, value);

  dialog_update_preview ();
} 

/* Cache the preview image - updates are a lot faster. */
/* The preview_cache will contain the small image */

static void
cache_preview (void)
{
  gboolean has_alpha;

  bint.img_bpp = gimp_drawable_bpp (blindsdrawable->drawable_id);   

  has_alpha = gimp_drawable_has_alpha (blindsdrawable->drawable_id);

  if (bint.img_bpp < 3)
    {
      bint.img_bpp = 3 + has_alpha;
    }
}

static void 
blindsapply (guchar *srow,
	     guchar *drow,
	     gint    width,
	     gint    bpp,
	     guchar *bg)
{
  guchar *src;
  guchar *dst;
  gint i,j,k;
  gdouble ang;
  gint available;

  /* Make the row 'shrink' around points along its length */
  /* The bvals.numsegs determins how many segments to slip it in to */
  /* The angle is the conceptual 'rotation' of each of these segments */

  /* Note the row is considered to be made up of a two dim array actual
   * pixel locations and the RGB colour at these locations.
   */

  /* In the process copy the src row to the destination row */

  /* Fill in with background color ? */
  for (i = 0 ; i < width ; i++)
    {
      dst = &drow[i*bpp];

      for (j = 0 ; j < bpp; j++)
	{
	  dst[j] = bg[j];
	}
    }

  /* Apply it */

  available = width;
  for (i = 0; i < bvals.numsegs; i++)
    {
      /* Width of segs are variable */
      fanwidths[i] = available / (bvals.numsegs - i);
      available -= fanwidths[i];
    }

  /* do center points  first - just for fun...*/
  available = 0;
  for (k = 1; k <= bvals.numsegs; k++)
    {
      int point;

      point = available + fanwidths[k - 1] / 2;

      available += fanwidths[k - 1];

      src = &srow[point * bpp];
      dst = &drow[point * bpp];
  
      /* Copy pixels across */
      for (j = 0 ; j < bpp; j++)
	{
	  dst[j] = src[j];
	}
    }

  /* Disp for each point */
  ang = (bvals.angledsp * 2 * G_PI) / 360; /* Angle in rads */
  ang = (1 - fabs (cos (ang)));

  available = 0;
  for (k = 0 ; k < bvals.numsegs; k++)
    {
      int dx; /* Amount to move by */
      int fw;

      for (i = 0 ; i < (fanwidths[k]/2) ; i++)
	{
	  /* Copy pixels across of left half of fan */
	  fw = fanwidths[k] / 2;
	  dx = (int) (ang * ((double) (fw - (double)(i % fw))));

	  src = &srow[(available + i) * bpp];      
	  dst = &drow[(available + i + dx) * bpp];

	  for (j = 0; j < bpp; j++)
	    {
	      dst[j] = src[j];
	    }

	  /* Right side */
	  j = i + 1;
	  src = &srow[(available + fanwidths[k] - j
		       - (fanwidths[k] % 2)) * bpp];      
	  dst = &drow[(available + fanwidths[k] - j
		       - (fanwidths[k] % 2) - dx) * bpp];

	  for (j = 0; j < bpp; j++)
	    {
	      dst[j] = src[j];
	    }
	}

      available += fanwidths[k];
    }
}

static void
dialog_update_preview (void)
{
  gint    y;
  guchar *p, *buffer;
  guchar  bg[4];
  
  p = preview->cache;

  gimp_get_bg_guchar (blindsdrawable, bvals.bg_trans, bg);

  buffer = (guchar*) g_malloc (preview->rowstride);

  if (bvals.orientation)
    {
      for (y = 0; y < preview->height; y++)
	{
	  blindsapply (p, buffer, preview->width, bint.img_bpp, bg);
	  gimp_fixme_preview_do_row (preview, y, preview->width, buffer);
	  p += preview->width * bint.img_bpp;
	} 
    }
  else
    {
      /* Horizontal blinds */
      /* Apply the blinds algo to a single column -
       * this act as a transfomation matrix for the 
       * rows. Make row 0 invalid so we can find it again!
       */
      gint i;
      guchar *sr = g_new (guchar, preview->height * 4);
      guchar *dr = g_new0 (guchar, preview->height * 4);
      guchar dummybg[4] = {0, 0, 0, 0};

      /* Fill in with background color ? */
      for (i = 0 ; i < preview->width ; i++)
	{
	  gint j;
	  gint bd = bint.img_bpp;
	  guchar *dst;
	  dst = &buffer[i * bd];
	  
	  for (j = 0 ; j < bd; j++)
	    {
	      dst[j] = bg[j];
	    }
	}

      for ( y = 0 ; y < preview->height; y++)
	{
	  sr[y] = y+1;
	}

      /* Bit of a fiddle since blindsapply really works on an image
       * row not a set of bytes. - preview can't be > 255
       * or must make dr sr int rows. 
       */
      blindsapply (sr, dr, preview->height, 1, dummybg);

      for (y = 0; y < preview->height; y++)
	{
	  if (dr[y] == 0)
	    {
	      /* Draw background line */
	      p = buffer;
	    }
	  else
	    {
	      /* Draw line from src */
	      p = preview->cache + 
		(preview->width * bint.img_bpp * (dr[y] - 1));
	    }

	  gimp_fixme_preview_do_row (preview, y, preview->width, p);
	} 
      g_free (sr);
      g_free (dr);
    }

  g_free (buffer);

  gtk_widget_queue_draw (preview->widget);
  gdk_flush ();
}

/* STEP tells us how many rows/columns to gulp down in one go... */
/* Note all the "4" literals around here are to do with the depths
 * of the images. Makes it easier to deal with for my small brain.
 */

#define STEP 40

static void
apply_blinds (void)
{
  GimpPixelRgn des_rgn;
  GimpPixelRgn src_rgn;
  guchar *src_rows, *des_rows;
  gint x,y;
  guchar bg[4];
  gint sel_x1, sel_y1, sel_x2, sel_y2;
  gint sel_width, sel_height;

  gimp_get_bg_guchar (blindsdrawable, bvals.bg_trans, bg);

  gimp_drawable_mask_bounds (blindsdrawable->drawable_id, &sel_x1, &sel_y1, 
			     &sel_x2, &sel_y2);

  sel_width  = sel_x2 - sel_x1;
  sel_height = sel_y2 - sel_y1;

  gimp_pixel_rgn_init (&src_rgn, blindsdrawable,
		       sel_x1, sel_y1, sel_width, sel_height, FALSE, FALSE);
  gimp_pixel_rgn_init (&des_rgn, blindsdrawable,
		       sel_x1, sel_y1, sel_width, sel_height, TRUE, TRUE);

  src_rows = g_new (guchar, MAX (sel_width, sel_height) * 4 * STEP); 
  des_rows = g_new (guchar, MAX (sel_width, sel_height) * 4 * STEP); 

  if (bvals.orientation)
    {
      for (y = 0; y < sel_height; y += STEP) 
	{
	  int rr;
	  int step;

	  if((y + STEP) > sel_height)
	    step = sel_height - y;
	  else
	    step = STEP;

	  gimp_pixel_rgn_get_rect (&src_rgn,
				   src_rows,
				   sel_x1,
				   sel_y1 + y,
				   sel_width,
				   step);

	  /* OK I could make this better */
	  for (rr = 0; rr < STEP; rr++)
	    blindsapply (src_rows + (sel_width * rr * src_rgn.bpp),
			 des_rows + (sel_width * rr * src_rgn.bpp),
			 sel_width, src_rgn.bpp, bg);

	  gimp_pixel_rgn_set_rect (&des_rgn,
				   des_rows,
				   sel_x1,
				   sel_y1 + y,
				   sel_width,
				   step);

	  gimp_progress_update ((double) y / (double) sel_height);
	}
    }
  else
    {
      /* Horizontal blinds */
      /* Apply the blinds algo to a single column -
       * this act as a transfomation matrix for the 
       * rows. Make row 0 invalid so we can find it again!
       */
      int i;
      gint *sr = g_new (gint, sel_height * 4);
      gint *dr = g_new (gint, sel_height * 4);
      guchar *dst = g_new (guchar, STEP * 4);
      guchar dummybg[4];

      memset (dummybg, 0, 4);
      memset (dr, 0, sel_height * 4); /* all dr rows are background rows */
      for (y = 0; y < sel_height; y++)
	{
	  sr[y] = y+1;
	}

      /* Hmmm. does this work portably? */
      /* This "swaps the intergers around that are held in in the
       * sr & dr arrays. 
       */
      blindsapply ((guchar *) sr, (guchar *) dr,
		   sel_height, sizeof (gint), dummybg);

      /* Fill in with background color ? */
      for (i = 0 ; i < STEP ; i++)
	{
	  int j;
	  guchar *bgdst;
	  bgdst = &dst[i * src_rgn.bpp];

	  for (j = 0 ; j < src_rgn.bpp; j++)
	    {
	      bgdst[j] = bg[j];
	    }
	}

      for (x = 0; x < sel_width; x += STEP) 
	{
	  int rr;
	  int step;
	  guchar *p;

	  if((x + STEP) > sel_width)
	    step = sel_width - x;
	  else
	    step = STEP;

	  gimp_pixel_rgn_get_rect (&src_rgn,
				   src_rows,
				   sel_x1 + x,
				   sel_y1,
				   step,
				   sel_height);

	  /* OK I could make this better */
	  for (rr = 0; rr < sel_height; rr++)
	    {
	      if(dr[rr] == 0)
	  	{
		  /* Draw background line */
		  p = dst;
	  	}
	      else
	  	{
		  /* Draw line from src */
		  p = src_rows + (step * src_rgn.bpp * (dr[rr] - 1));
	  	}
	      memcpy (des_rows + (rr * step * src_rgn.bpp), p,
		      step * src_rgn.bpp);
	    }

	  gimp_pixel_rgn_set_rect (&des_rgn,
				   des_rows,
				   sel_x1 + x,
				   sel_y1,
				   step,
				   sel_height);

	  gimp_progress_update ((double) x / (double) sel_width);
	}

      g_free (dst);
      g_free (sr);
      g_free (dr);
    }

  g_free (src_rows);
  g_free (des_rows);

  gimp_drawable_flush (blindsdrawable);
  gimp_drawable_merge_shadow (blindsdrawable->drawable_id, TRUE);
  gimp_drawable_update (blindsdrawable->drawable_id,
			sel_x1, sel_y1, sel_width, sel_height);  
  
}
