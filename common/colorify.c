/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * Colorify. Changes the pixel's luminosity to a specified color
 * Copyright (C) 1997 Francisco Bustamante
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

/* Changes: 

   1.1 
   -Corrected small bug when calling color selection dialog 
   -Added LUTs to speed things a little bit up 

   1.0 
   -First release */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


#define PLUG_IN_NAME    "plug_in_colorify"
#define PLUG_IN_VERSION "1.1"

#define COLOR_SIZE 30

static void      query (void);
static void      run   (const gchar      *name,
			gint              nparams,
			const GimpParam  *param,
			gint             *nreturn_vals,
			GimpParam       **return_vals);

static void      colorify                  (GimpDrawable *drawable);
static gboolean  colorify_dialog           (GimpRGB      *color);
static void      colorify_ok_callback      (GtkWidget    *widget,
					    gpointer      data);
static void      predefined_color_callback (GtkWidget    *widget,
					    gpointer      data);

typedef struct
{
  GimpRGB  color;
} ColorifyVals;

typedef struct
{
  gboolean  run;
} ColorifyInterface;

static ColorifyInterface cint =
{
  FALSE
};

static ColorifyVals cvals =
{
  { 1.0, 1.0, 1.0, 1.0 }
};

static GimpRGB button_color[] =
{
  { 1.0, 0.0, 0.0, 1.0 },
  { 1.0, 1.0, 0.0, 1.0 },
  { 0.0, 1.0, 0.0, 1.0 },
  { 0.0, 1.0, 1.0, 1.0 },
  { 0.0, 0.0, 1.0, 1.0 },
  { 1.0, 0.0, 1.0, 1.0 },
  { 1.0, 1.0, 1.0, 1.0 },
};

static GimpRunMode run_mode;

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,
  NULL,
  query,
  run,
};

static GtkWidget *custom_color_button = NULL;

static gint lum_red_lookup[256];
static gint lum_green_lookup[256];
static gint lum_blue_lookup[256];
static gint final_red_lookup[256];
static gint final_green_lookup[256];
static gint final_blue_lookup[256];


MAIN ()

static void
query (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE, "image", "Input image" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable" },
    { GIMP_PDB_COLOR, "color", "Color to apply"}
  };

  gimp_install_procedure (PLUG_IN_NAME,
			  "Similar to the \"Color\" mode for layers.",
			  "Makes an average of the RGB channels and uses it "
			  "to set the color",
			  "Francisco Bustamante",
			  "Francisco Bustamante",
                          PLUG_IN_VERSION,
			  N_("<Image>/Filters/Colors/_Colorify..."), 
			  "RGB*",
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
  GimpPDBStatusType  status;
  static GimpParam   values[1];
  GimpDrawable      *drawable;

  INIT_I18N ();

  status = GIMP_PDB_SUCCESS;
  run_mode = param[0].data.d_int32;

  values[0].type = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  *nreturn_vals = 1;
  *return_vals = values;

  drawable = gimp_drawable_get (param[2].data.d_drawable);

  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE:
      gimp_get_data (PLUG_IN_NAME, &cvals);
      if (!colorify_dialog (&cvals.color))
	return;
      break;

    case GIMP_RUN_NONINTERACTIVE:
      if (nparams != 4)
	status = GIMP_PDB_CALLING_ERROR;

      if (status == GIMP_PDB_SUCCESS)
	cvals.color = param[3].data.d_color;
      break;

    case GIMP_RUN_WITH_LAST_VALS:
      /*  Possibly retrieve data  */
      gimp_get_data (PLUG_IN_NAME, &cvals);
      break;

    default:
      break;
    }

  if (status == GIMP_PDB_SUCCESS)
    {
      gimp_progress_init (_("Colorifying..."));

      colorify (drawable);

      if (run_mode == GIMP_RUN_INTERACTIVE)
	gimp_set_data (PLUG_IN_NAME, &cvals, sizeof (ColorifyVals));

      if (run_mode != GIMP_RUN_NONINTERACTIVE)
	{
	  gimp_displays_flush ();
	}
    }

  gimp_drawable_detach (drawable);

  values[0].data.d_status = status;
}

static void 
colorify_func (const guchar *src,
               guchar       *dest,
               gint          bpp,
               gpointer      data)
{
  gint lum;

  lum = (lum_red_lookup[src[0]]   +
         lum_green_lookup[src[1]] +
         lum_blue_lookup[src[2]]);

  dest[0] = final_red_lookup[lum];
  dest[1] = final_green_lookup[lum];
  dest[2] = final_blue_lookup[lum];
  
  if (bpp == 4)
    dest[3] = src[3];
}

static void
colorify (GimpDrawable *drawable)
{
  gint  i;

  for (i = 0; i < 256; i ++)
    {
      lum_red_lookup[i]     = i * INTENSITY_RED;
      lum_green_lookup[i]   = i * INTENSITY_GREEN;
      lum_blue_lookup[i]    = i * INTENSITY_BLUE;
      final_red_lookup[i]   = i * cvals.color.r;
      final_green_lookup[i] = i * cvals.color.g;
      final_blue_lookup[i]  = i * cvals.color.b;
    }

  gimp_rgn_iterate2 (drawable, run_mode, colorify_func, NULL);
}

static gboolean
colorify_dialog (GimpRGB *color)
{
  GtkWidget *dialog;
  GtkWidget *label;
  GtkWidget *button;
  GtkWidget *frame;
  GtkWidget *table;
  GtkWidget *color_area;
  gint       i;

  gimp_ui_init ("colorify", TRUE);

  dialog = gimp_dialog_new (_("Colorify"), "colorify",
			    gimp_standard_help_func, "filters/colorify.html",
			    GTK_WIN_POS_MOUSE,
			    FALSE, TRUE, FALSE,

			    GTK_STOCK_CANCEL, gtk_widget_destroy,
			    NULL, 1, NULL, FALSE, TRUE,

			    GTK_STOCK_OK, colorify_ok_callback,
			    NULL, NULL, NULL, TRUE, FALSE,

			    NULL);

  g_signal_connect (dialog, "destroy",
                    G_CALLBACK (gtk_main_quit),
                    NULL);

  frame = gtk_frame_new (_("Color"));
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
  gtk_container_set_border_width (GTK_CONTAINER (frame), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), 
                      frame, TRUE, TRUE, 0);
  gtk_widget_show (frame);

  table = gtk_table_new (2, 7, TRUE);
  gtk_container_set_border_width (GTK_CONTAINER (table), 4);
  gtk_container_add (GTK_CONTAINER (frame), table);
  gtk_table_set_row_spacings (GTK_TABLE (table), 4);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_widget_show (table);

  label = gtk_label_new (_("Custom Color:"));
  gtk_table_attach (GTK_TABLE (table), label, 4, 6, 0, 1,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (label);

  custom_color_button = gimp_color_button_new (_("Colorify Custom Color"),
					       COLOR_SIZE, COLOR_SIZE,
					       color,
					       GIMP_COLOR_AREA_FLAT);
  g_signal_connect (custom_color_button, "color_changed",
                    G_CALLBACK (gimp_color_button_get_color),
                    color);
  
  gtk_table_attach (GTK_TABLE (table), custom_color_button, 6, 7, 0, 1,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (custom_color_button);

  for (i = 0; i < 7; i++)
    {
      button = gtk_button_new ();
      color_area = gimp_color_area_new (&button_color[i], 
					GIMP_COLOR_AREA_FLAT, 
					GDK_BUTTON2_MASK);
      gtk_widget_set_size_request (GTK_WIDGET (color_area),
				   COLOR_SIZE, COLOR_SIZE);
      gtk_container_add (GTK_CONTAINER (button), color_area);
      g_signal_connect (button, "clicked",
			G_CALLBACK (predefined_color_callback),
                        &button_color[i]);
      gtk_widget_show (color_area);

      gtk_table_attach (GTK_TABLE (table), button, i, i + 1, 1, 2,
			GTK_FILL, GTK_FILL, 0, 0);
      gtk_widget_show (button);
    }

  gtk_widget_show (dialog);

  gtk_main ();
  gdk_flush ();

  return cint.run;
}

static void
colorify_ok_callback (GtkWidget *widget,
		      gpointer   data)
{
  cint.run = TRUE;

  gtk_widget_destroy (GTK_WIDGET (data));
}

static void
predefined_color_callback (GtkWidget *widget,
			   gpointer   data)
{
  gimp_color_button_set_color (GIMP_COLOR_BUTTON (custom_color_button), 
			       (GimpRGB*) data);
}
