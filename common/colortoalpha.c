/*
 * Color To Alpha plug-in v1.0 by Seth Burgess, sjburges@gimp.org 1999/05/14
 *  with algorithm by clahey
 */

/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


#define PRV_WIDTH  40
#define PRV_HEIGHT 20

typedef struct
{
  guchar color[3];
} C2AValues;

typedef struct
{
  gint run;
} C2AInterface;

typedef struct
{
  GtkWidget *color_button;
} C2APreview;

/* Declare local functions.
 */
static void      query  (void);
static void      run    (gchar     *name,
			 gint       nparams,
			 GimpParam    *param,
			 gint      *nreturn_vals,
			 GimpParam   **return_vals);

static void      toalpha            (GimpDrawable  *drawable);

static void      toalpha_render_row (const guchar *src_row,
				     guchar       *dest_row,
				     gint          row_width,
				     const gint    bytes);
/* UI stuff */
static gint 	colortoalpha_dialog      (GimpDrawable *drawable);
static void 	colortoalpha_ok_callback (GtkWidget *widget,
					  gpointer   data);

static GimpRunModeType run_mode;

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};

static C2AValues pvals = 
{ 
  { 255, 255, 255 } /* white default */
};

static C2AInterface pint = 
{
  FALSE
};

static C2APreview ppreview = 
{
  NULL
};

MAIN ()

static void
query (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32,    "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE,    "image",    "Input image (unused)" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable" },
    { GIMP_PDB_COLOR,    "color",    "Color to remove" }
  };
  static gint nargs = sizeof (args) / sizeof (args[0]);

  gimp_install_procedure ("plug_in_colortoalpha",
			  "Convert the color in an image to alpha",
			  "This replaces as much of a given color as possible "
			  "in each pixel with a corresponding amount of alpha, "
			  "then readjusts the color accordingly.",
			  "Seth Burgess",
			  "Seth Burgess <sjburges@gimp.org>",
			  "7th Aug 1999",
			  N_("<Image>/Filters/Colors/Color to Alpha..."),
			  "RGB*",
			  GIMP_PLUGIN,
			  nargs, 0,
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
  gint32 image_ID;

  GimpPDBStatusType status = GIMP_PDB_SUCCESS;

  run_mode = param[0].data.d_int32;

  *nreturn_vals = 1;
  *return_vals = values;

  INIT_I18N_UI();

  values[0].type = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  /*  Get the specified drawable  */
  drawable = gimp_drawable_get (param[2].data.d_drawable);
  image_ID = param[1].data.d_image;

  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE:
      gimp_get_data ("plug_in_colortoalpha", &pvals);
      if (! colortoalpha_dialog (drawable ))
	{ 
	  gimp_drawable_detach (drawable);
	  return;
	}
      break;
 
    case GIMP_RUN_NONINTERACTIVE:
      if (nparams != 3)
	status = GIMP_PDB_CALLING_ERROR;
      if (status == GIMP_PDB_SUCCESS)
	{
	  pvals.color[0] = param[3].data.d_color.red;
	  pvals.color[1] = param[3].data.d_color.green;
	  pvals.color[2] = param[3].data.d_color.blue;
	}
      break;

    case GIMP_RUN_WITH_LAST_VALS:
      gimp_get_data ("plug_in_colortoalpha", &pvals);
      break;
    default:
      break;
    }  

  if (status == GIMP_PDB_SUCCESS)
    {
      /*  Add alpha if not present */
      gimp_layer_add_alpha (drawable->id);
      drawable = gimp_drawable_get (drawable->id);

      /*  Make sure that the drawable is RGB color  */
      if (gimp_drawable_is_rgb (drawable->id) && 
	  gimp_drawable_is_layer (drawable->id))  
	{
          if (run_mode != GIMP_RUN_NONINTERACTIVE)
	    gimp_progress_init ("Removing color...");
             
	  toalpha (drawable);
	  gimp_displays_flush ();
	}
    }
  if (run_mode == GIMP_RUN_INTERACTIVE)
    gimp_set_data ("plug_in_colortoalpha", &pvals, sizeof (pvals));

  values[0].data.d_status = status;
  gimp_drawable_detach (drawable);
}

void
colortoalpha (float *a1,
	      float *a2,
	      float *a3,
	      float *a4,
	      float c1,
	      float c2,
	      float c3)
{
  float alpha1, alpha2, alpha3, alpha4;
  
  alpha4 = *a4;
	  
  if ( *a1 > c1 )
    alpha1 = (*a1 - c1)/(255.0-c1);
  else if ( *a1 < c1 )
    alpha1 = (c1 - *a1)/(c1);
  else alpha1 = 0.0;

  if ( *a2 > c2 )
    alpha2 = (*a2 - c2)/(255.0-c2);
  else if ( *a2 < c2 )
    alpha2 = (c2 - *a2)/(c2);
  else alpha2 = 0.0;

  if ( *a3 > c3 )
    alpha3 = (*a3 - c3)/(255.0-c3);
  else if ( *a3 < c3 )
    alpha3 = (c3 - *a3)/(c3);
  else alpha3 = 0.0;

  if ( alpha1 > alpha2 )
    if ( alpha1 > alpha3 )
      {
	*a4 = alpha1;
      }
    else
      {
	*a4 = alpha3;
      }
  else
    if ( alpha2 > alpha3 )
      {
	*a4 = alpha2;
      }
    else
      {
	*a4 = alpha3;
      }
  
  *a4 *= 255.0;
  
  if ( *a4 < 1.0 )
    return;
  *a1 = 255.0 * (*a1-c1)/ *a4 + c1;
  *a2 = 255.0 * (*a2-c2)/ *a4 + c2;
  *a3 = 255.0 * (*a3-c3)/ *a4 + c3;

  *a4 *= alpha4/255.0;
}
/*
<clahey> so if a1 > c1, a2 > c2, and a3 > c2 and a1 - c1 > a2-c2, a3-c3, then a1 = b1 * alpha + c1 * (1-alpha) So, maximizing alpha without taking b1 above 1 gives a1 = alpha + c1(1-alpha) and therefore alpha = (a1-c1)/(1-c1).
<AmJur2d> eek!  math!
> AmJur2d runs and hides behind a library carrel
<sjburges> clahey: btw, the ordering of that a2, a3 in the white->alpha didn't matter
<clahey> sjburges: You mean that it could be either a1, a2, a3 or a1, a3, a2?
<sjburges> yeah
<sjburges> because neither one uses the other
<clahey> sjburges: That's exactly as it should be.  They are both just getting reduced to the same amount, limited by the the darkest color.
<clahey> Then a2 = b2 * alpha + c2 * ( 1- alpha).  Solving for b2 gives b2 = (a1-c2)/alpha + c2.
<sjburges> yeah
<jlb> xachbot, url holy wars
<jlb> xachbot, url wars
<clahey> That gives us are formula for if the background is darker than the foreground? Yep.
<clahey> Next if a1 < c1, a2 < c2, a3 < c3, and c1-a1 > c2-a2, c3-a3, and by our desired result a1 = b1 * alpha + c1 * (1-alpha), we maximize alpha without taking b1 negative gives alpha = 1-a1/c1.
<clahey> And then again, b2 = (a2-c2)/alpha + c2 by the same formula.  (Actually, I think we can use that formula for all cases, though it may possibly introduce rounding error.
<clahey> sjburges: I like the idea of using floats to avoid rounding error.  Good call.
<clahey> It's cool to be able to replace all the black in an image with another color.  It'
*/

static void
toalpha_render_row (const guchar *src_data,
		    guchar       *dest_data,
		    gint          col,               /* row width in pixels */
		    const gint    bytes)
{
  while (col--)
    {
      float v1, v2, v3, v4;

      v1 = (float)src_data[col*bytes   ];
      v2 = (float)src_data[col*bytes +1];
      v3 = (float)src_data[col*bytes +2];
      v4 = (float)src_data[col*bytes +3];
      /* For brighter than the background the rule is to send the
	 farthest above the background as the first address.
	 However, since v1 < COLOR_RED, for example, all of these
	 are negative so we have to invert the operator to reduce
	 the amount of typing to fix the problem.  :) */
      colortoalpha (&v1, &v2, &v3, &v4, 
		    (float)pvals.color[0],
		    (float)pvals.color[1],
		    (float)pvals.color[2]);

      dest_data[col * bytes    ] = (int) v1;
      dest_data[col * bytes + 1] = (int) v2;
      dest_data[col * bytes + 2] = (int) v3;
      dest_data[col * bytes + 3] = (int) v4;
    }
}

static void
toalpha_render_region (const GimpPixelRgn srcPR,
		       const GimpPixelRgn destPR)
{
  gint row;
  guchar* src_ptr  = srcPR.data;
  guchar* dest_ptr = destPR.data;
  
  for (row = 0; row < srcPR.h ; row++)
    {
      if (srcPR.bpp!=4)
	{
	  gimp_message ("Not the proper bpp! \n");
	  exit(1);
	} 
      toalpha_render_row (src_ptr, dest_ptr,
			  srcPR.w,
			  srcPR.bpp) ;

      src_ptr  += srcPR.rowstride;
      dest_ptr += destPR.rowstride;
    }
}

static void
toalpha (GimpDrawable *drawable)
{
  GimpPixelRgn srcPR, destPR;
  gint x1, y1, x2, y2;
  gpointer pr;
  gint total_area, area_so_far;
  gint progress_skip;

  /* Get the input area. This is the bounding box of the selection in
   *  the image (or the entire image if there is no selection). Only
   *  operating on the input area is simply an optimization. It doesn't
   *  need to be done for correct operation. (It simply makes it go
   *  faster, since fewer pixels need to be operated on).
   */
  gimp_drawable_mask_bounds (drawable->id, &x1, &y1, &x2, &y2);

  total_area = (x2 - x1) * (y2 - y1);
  area_so_far = 0;
  progress_skip = 0;

  /* Initialize the pixel regions. */
  gimp_pixel_rgn_init (&srcPR, drawable, x1, y1, (x2 - x1), (y2 - y1),
		       FALSE, FALSE);
  gimp_pixel_rgn_init (&destPR, drawable, x1, y1, (x2 - x1), (y2 - y1),
		       TRUE, TRUE);
  
  for (pr = gimp_pixel_rgns_register (2, &srcPR, &destPR);
       pr != NULL;
       pr = gimp_pixel_rgns_process (pr))
    {
      toalpha_render_region (srcPR, destPR);

      if ((run_mode != GIMP_RUN_NONINTERACTIVE))
	{
	  area_so_far += srcPR.w * srcPR.h;
	  if (((progress_skip++)%10) == 0)
	    gimp_progress_update ((double) area_so_far / (double) total_area);
	}
    }

  /*  update the processed region  */
  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->id, TRUE);
  gimp_drawable_update (drawable->id, x1, y1, (x2 - x1), (y2 - y1));
}

static gint 
colortoalpha_dialog (GimpDrawable *drawable)
{
  GtkWidget *dlg;
  GtkWidget *frame;
  GtkWidget *table;
  GtkWidget *button;
  GtkWidget *label;
  GimpRGB    color;

  gimp_ui_init ("colortoalpha", TRUE);

  dlg = gimp_dialog_new (_("Color to Alpha"), "colortoalpha",
			 gimp_standard_help_func, "filters/colortoalpha.html",
			 GTK_WIN_POS_MOUSE,
			 FALSE, TRUE, FALSE,

			 _("OK"), colortoalpha_ok_callback,
			 NULL, NULL, NULL, TRUE, FALSE,
			 _("Cancel"), gtk_widget_destroy,
			 NULL, 1, NULL, FALSE, TRUE,

			 NULL);

  gtk_signal_connect (GTK_OBJECT (dlg), "destroy",
                      GTK_SIGNAL_FUNC (gtk_main_quit),
                      NULL);

  frame = gtk_frame_new (_("Color"));
  gtk_container_set_border_width (GTK_CONTAINER (frame), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  table = gtk_table_new (1, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 6);
  gtk_container_set_border_width (GTK_CONTAINER (table), 4);
  gtk_container_add (GTK_CONTAINER (frame), table);
  gtk_widget_show (table);
   
  label = gtk_label_new (_("From:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach_defaults (GTK_TABLE(table), label, 0, 1, 0, 1);
  gtk_widget_show (label);

  gimp_rgb_set (&color,
		(gdouble) pvals.color[0] / 255.0,
		(gdouble) pvals.color[1] / 255.0,
		(gdouble) pvals.color[2] / 255.0);		
  button = gimp_color_button_new (_("Color to Alpha Color Picker"), 
				  PRV_WIDTH, PRV_HEIGHT,
				  &color, FALSE);
  gtk_signal_connect (GTK_OBJECT (button), "color_changed",
		      GTK_SIGNAL_FUNC (gimp_color_update_uchar),
		      pvals.color);
  gtk_table_attach (GTK_TABLE (table), button, 1, 2, 0, 1, 
		    GTK_FILL, GTK_SHRINK, 0, 0) ; 
  gtk_widget_show (button);
  ppreview.color_button = button;

  label = gtk_label_new (_("to Alpha"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach_defaults (GTK_TABLE(table), label, 2, 3, 0, 1);
  gtk_widget_show (label);

  gtk_widget_show (dlg);

  gtk_main ();
  gdk_flush ();

  return pint.run;
}

static void
colortoalpha_ok_callback (GtkWidget *widget, 
			  gpointer   data)
{
  pint.run = TRUE;

  gtk_widget_destroy (GTK_WIDGET (data));
}
