/*
 * This is the Glass Tile plug-in for the GIMP 1.2
 * Version 1.02
 *
 * Copyright (C) 1997 Karl-Johan Andersson (t96kja@student.tdb.uu.se)
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
 * This filter divide the image into square "glass"-blocks in which
 * the image is refracted. 
 * 
 * The alpha-channel is left unchanged.
 * 
 * Please send any comments or suggestions to
 * Karl-Johan Andersson (t96kja@student.tdb.uu.se)
 *
 * May 2000 - tim copperfield [timecop@japan.co.jp]
 * Added preview mode.
 * Noticed there is an issue with the algorithm if odd number of rows or
 * columns is requested.  Dunno why.  I am not a graphics expert :(
 *  
 * May 2000 alt@gimp.org Made preview work and removed some boundary 
 * conditions that caused "streaks" to appear when using some tile spaces.
 */ 

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"

/* --- Typedefs --- */
typedef struct
{
  gint xblock;
  gint yblock;
} GlassValues;

typedef struct
{
  gboolean  run;
} GlassInterface;

/* --- Declare local functions --- */
static void query (void);
static void run   (const gchar      *name,
		   gint              nparams,
		   const GimpParam  *param,
		   gint             *nreturn_vals,
		   GimpParam       **return_vals);

static gint glass_dialog               (GimpDrawable  *drawable);
static void glass_ok_callback          (GtkWidget     *widget,
					gpointer       data);

static void glasstile                  (GimpDrawable  *drawable, 
					gboolean       preview_mode);

/* --- Variables --- */
GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,    /* init_proc */
  NULL,    /* quit_proc */
  query,   /* query_proc */
  run,     /* run_proc */
};
static GlassValues gtvals =
{
    20,    /* tile width  */
    20     /* tile height */
};
static GlassInterface gt_int =
{
  FALSE    /* run */
};

static GimpFixMePreview *preview;

/* --- Functions --- */

MAIN ()

static void
query (void) 
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE, "image", "Input image (unused)" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable" },
    { GIMP_PDB_INT32, "tilex", "Tile width (10 - 50)" },
    { GIMP_PDB_INT32, "tiley", "Tile height (10 - 50)" }
  };

  gimp_install_procedure ("plug_in_glasstile",
			  "Divide the image into square glassblocks",
			  "Divide the image into square glassblocks in "
                          "which the image is refracted.",
			  "Karl-Johan Andersson", /* Author */
			  "Karl-Johan Andersson", /* Copyright */
			  "May 2000",
			  N_("<Image>/Filters/Glass Effects/_Glass Tile..."),
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
  static GimpParam   values[1];
  GimpDrawable      *drawable;
  GimpRunMode        run_mode;
  GimpPDBStatusType  status = GIMP_PDB_SUCCESS;
  
  run_mode = param[0].data.d_int32;
  
  INIT_I18N ();

  *nreturn_vals = 1;
  *return_vals  = values;
  
  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  /*  Get the specified drawable  */
  drawable = gimp_drawable_get (param[2].data.d_drawable);
  
  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_glasstile", &gtvals);
      
      /*  First acquire information with a dialog  */
      if (! glass_dialog (drawable))
	{
	  gimp_drawable_detach (drawable);
	  return;
	}
      break;
      
    case GIMP_RUN_NONINTERACTIVE:
      /*  Make sure all the arguments are there!  */
      if (nparams != 5)
	status = GIMP_PDB_CALLING_ERROR;
      if (status == GIMP_PDB_SUCCESS)
	{
	  gtvals.xblock = (gint) param[3].data.d_int32;
	  gtvals.yblock = (gint) param[4].data.d_int32;
	}
      if (gtvals.xblock < 10 || gtvals.xblock > 50) 
	status = GIMP_PDB_CALLING_ERROR;
      if (gtvals.yblock < 10 || gtvals.yblock > 50) 
	status = GIMP_PDB_CALLING_ERROR;
      break;
      
    case GIMP_RUN_WITH_LAST_VALS:
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_glasstile", &gtvals);
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
	  gimp_progress_init (_("Glass Tile..."));
	  gimp_tile_cache_ntiles (2 * 
                                  (drawable->width / gimp_tile_width () + 1));
	  
	  glasstile (drawable, FALSE);
	  
	  if (run_mode != GIMP_RUN_NONINTERACTIVE)
	    gimp_displays_flush (); 
	  /*  Store data  */
	  if (run_mode == GIMP_RUN_INTERACTIVE) 
	    {
	      gimp_set_data ("plug_in_glasstile", &gtvals, 
			     sizeof (GlassValues));
	      gimp_fixme_preview_free (preview);
            }
	}
      else
	{
	  status = GIMP_PDB_EXECUTION_ERROR;
	}
    }
    
  values[0].data.d_status = status;
  
  gimp_drawable_detach (drawable);
}

static gint
glass_dialog (GimpDrawable *drawable)
{
  GtkWidget *dlg;
  GtkWidget *main_vbox;
  GtkWidget *table;
  GtkObject *adj;

  gimp_ui_init ("glasstile", TRUE);

  dlg = gimp_dialog_new (_("Glass Tile"), "glasstile",
			 gimp_standard_help_func, "filters/glasstile.html",
			 GTK_WIN_POS_MOUSE,
			 FALSE, TRUE, FALSE,

			 GTK_STOCK_CANCEL, gtk_widget_destroy,
			 NULL, 1, NULL, FALSE, TRUE,
			 GTK_STOCK_OK, glass_ok_callback,
			 NULL, NULL, NULL, TRUE, FALSE,

			 NULL);

  g_signal_connect (dlg, "destroy",
                    G_CALLBACK (gtk_main_quit),
                    NULL);

  main_vbox = gtk_vbox_new (FALSE, 4);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), 
                      main_vbox, TRUE, TRUE, 0);
  gtk_widget_show (main_vbox);

  preview = gimp_fixme_preview_new (drawable, TRUE);
  gtk_box_pack_start (GTK_BOX (main_vbox), preview->frame, FALSE, FALSE, 0);
  gtk_widget_show (preview->widget);
  glasstile (drawable, TRUE); /* filter routine, initial pass */
  
  table = gimp_parameter_settings_new (main_vbox, 2, 3);

  /* Horizontal scale - Width */
  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, 0,
			      _("Tile _Width:"), 150, 0,
			      gtvals.xblock, 10, 50, 2, 10, 0,
			      TRUE, 0, 0,
			      NULL, NULL);

  g_signal_connect (adj, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &gtvals.xblock);
  g_signal_connect_swapped (adj, "value_changed",
                            G_CALLBACK (glasstile),
                            drawable);

  /* Horizontal scale - Height */
  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, 1,
			      _("Tile _Height:"), 150, 0,
			      gtvals.yblock, 10, 50, 2, 10, 0,
			      TRUE, 0, 0,
			      NULL, NULL);

  g_object_set_data (G_OBJECT (adj), "drawable", drawable);

  g_signal_connect (adj, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &gtvals.yblock);
  g_signal_connect_swapped (adj, "value_changed",
                            G_CALLBACK (glasstile),
                            drawable);

  gtk_widget_show (dlg);

  gtk_main ();
  gdk_flush ();

  return gt_int.run;
}

static void
glass_ok_callback (GtkWidget *widget,
		   gpointer   data)
{
  gt_int.run = TRUE;

  gtk_widget_destroy (GTK_WIDGET (data));
}

/*  -  Filter function  -  I wish all filter functions had a pmode :) */
static void
glasstile (GimpDrawable *drawable, 
	   gboolean      preview_mode)
{
  GimpPixelRgn srcPR, destPR;
  gint    width, height;
  gint    bytes;
  guchar *dest, *d;
  guchar *cur_row;
  gint    row, col, i, iwidth;
  gint    x1, y1, x2, y2;

  /* Translations of variable names from Maswan
   * rutbredd = grid width
   * ruthojd = grid height
   * ymitt = y middle
   * xmitt = x middle
   */
  
  gint rutbredd, xpixel1, xpixel2;
  gint ruthojd , ypixel2;
  gint xhalv, xoffs, xmitt, xplus;
  gint yhalv, yoffs, ymitt, yplus;
  gint cbytes;
      
  if (preview_mode) 
    {
      width  = preview->width;
      height = preview->height;
      bytes  = preview->bpp;

      x1 = y1 = 0;
      x2 = width;
      y2 = height;
    } 
  else 
    {
      gimp_drawable_mask_bounds (drawable->drawable_id, &x1, &y1, &x2, &y2);
      width  = drawable->width;
      height = drawable->height;
      bytes  = drawable->bpp;
    }
  
  cur_row = g_new (guchar, width * bytes);
  dest    = g_new (guchar, width * bytes);

  /* initialize the pixel regions, set grid height/width */
  if (preview_mode) 
    {
      rutbredd = gtvals.xblock * preview->scale_x;
      ruthojd  = gtvals.yblock * preview->scale_y;

      /* Algorithm depends on grid height/width being at least 2 
       * or you'll get extremely bad previews (1/2 size). 
       *
       * Preview isn't really terribly useful for larger images.
       * Might be more useful as full-size window scroll-around type.  
       */
      rutbredd = MAX(rutbredd, 2);
      ruthojd = MAX(ruthojd, 2);
    }
  else
    {
      gimp_pixel_rgn_init (&srcPR, drawable, 0, 0, width, height, FALSE, FALSE);
      gimp_pixel_rgn_init (&destPR, drawable, 0, 0, width, height, TRUE, TRUE);

      rutbredd = gtvals.xblock;
      ruthojd  = gtvals.yblock;
    }

  xhalv = rutbredd / 2;
  yhalv = ruthojd  / 2;
  cbytes = bytes;

  if (! (cbytes & 1)) 
    cbytes--; 

  iwidth = width - x1;
  xplus = rutbredd % 2;
  yplus = ruthojd  % 2;

  ymitt = y1;
  yoffs = 0;

  /*  Loop through the rows */
  for (row = y1; row < y2; row++)
    {
      d = dest;
      
      ypixel2 = ymitt + yoffs * 2;
      ypixel2 = CLAMP (ypixel2, 0, y2 - 1);

      if (preview_mode)
	{
	  memcpy (cur_row, preview->cache + ypixel2 * preview->rowstride, 
		  preview->rowstride);
	}
      else
	{
	  gimp_pixel_rgn_get_row (&srcPR, cur_row, x1, ypixel2, iwidth);
	}
      yoffs++;

      /* if current offset = half, do a displacement next time around */
      if (yoffs == yhalv) 
	{ 
	  ymitt += ruthojd;
	  yoffs = - (yhalv + yplus);
	}
      
      xmitt = 0;
      xoffs = 0;

      for (col = 0; col < x2 - x1; col++) /* one pixel */
	{
	  xpixel1 = (xmitt + xoffs) * bytes;
	  xpixel2 = (xmitt + xoffs * 2) * bytes;

	  if (xpixel2 < ((x2 - x1) * bytes)) 
	    {
	      if(xpixel2 < 0)
		xpixel2 = 0;
	      for (i = 0; i < bytes; i++) 
		d[xpixel1 + i] = cur_row[xpixel2 + i];
	    }
	  else 
	    {
	      for (i = 0; i < bytes; i++)
		d[xpixel1 + i] = cur_row[xpixel1 + i];
	    }

	  xoffs++;

	  if (xoffs == xhalv) 
	    {
	      xmitt += rutbredd;
	      xoffs = - (xhalv + xplus);
	    }
	}

      /*  Store the dest  */
      if (preview_mode)
        {
          gimp_fixme_preview_do_row (preview, row, width, dest);
        }
      else
        {
          gimp_pixel_rgn_set_row (&destPR, dest, x1, row, iwidth);
          
          if ((row % 5) == 0)
            gimp_progress_update ((gdouble) row / (gdouble) (y2 - y1));
        }
    }

  /*  Update region  */
  if (preview_mode) 
    {
      gtk_widget_queue_draw (preview->widget);
    }
  else
    {
      gimp_drawable_flush (drawable);
      gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
      gimp_drawable_update (drawable->drawable_id, 
                            x1, y1, (x2 - x1), (y2 - y1));
    }

  g_free (cur_row);
  g_free (dest);
}




