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
#include <string.h>

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


typedef struct
{
  gdouble  radius;
  gboolean horizontal;
  gboolean vertical;
} BlurValues;

typedef struct
{
  gdouble horizontal;
  gdouble vertical;
} Blur2Values;


/* Declare local functions.
 */
static void      query  (void);
static void      run    (const gchar      *name,
			 gint              nparams,
			 const GimpParam  *param,
			 gint             *nreturn_vals,
			 GimpParam       **return_vals);

static void      gauss_rle (GimpDrawable  *drawable,
			    gdouble        horizontal,
			    gdouble        vertical);

/*
 * Gaussian blur interface
 */
static gint      gauss_rle_dialog  (void);
static gint      gauss_rle2_dialog (gint32        image_ID,
				    GimpDrawable *drawable);

/*
 * Gaussian blur helper functions
 */
static gint *    make_curve        (gdouble    sigma,
				    gint      *length);
static void      run_length_encode (guchar    *src,
				    gint      *dest,
				    gint       bytes,
				    gint       width);


GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};

static BlurValues bvals =
{
  5.0,  /*  radius           */
  TRUE, /*  horizontal blur  */
  TRUE  /*  vertical blur    */
};

static Blur2Values b2vals =
{
  5.0,  /*  x radius  */
  5.0   /*  y radius  */
};


MAIN ()

static void
query (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE, "image", "Input image (unused)" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable" },
    { GIMP_PDB_FLOAT, "radius", "Radius of gaussian blur (in pixels, > 0.0)" },
    { GIMP_PDB_INT32, "horizontal", "Blur in horizontal direction" },
    { GIMP_PDB_INT32, "vertical", "Blur in vertical direction" }
  };

  static GimpParamDef args2[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE, "image", "Input image" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable" },
    { GIMP_PDB_FLOAT, "horizontal", "Horizontal radius of gaussian blur (in pixels, > 0.0)" },
    { GIMP_PDB_FLOAT, "vertical",   "Vertical radius of gaussian blur (in pixels, > 0.0)" }
  };

  gimp_install_procedure ("plug_in_gauss_rle",
			  "Applies a gaussian blur to the specified drawable.",
			  "Applies a gaussian blur to the drawable, with "
			  "specified radius of affect.  The standard deviation "
			  "of the normal distribution used to modify pixel "
			  "values is calculated based on the supplied radius.  "
			  "Horizontal and vertical blurring can be "
			  "independently invoked by specifying only one to "
			  "run.  The RLE gaussian blurring performs most "
			  "efficiently on computer-generated images or images "
			  "with large areas of constant intensity.",
			  "Spencer Kimball & Peter Mattis",
			  "Spencer Kimball & Peter Mattis",
			  "1995-1996",
			  NULL,
			  "RGB*, GRAY*",
			  GIMP_PLUGIN,
			  G_N_ELEMENTS (args), 0,
			  args, NULL);

  gimp_install_procedure ("plug_in_gauss_rle2",
			  "Applies a gaussian blur to the specified drawable.",
			  "Applies a gaussian blur to the drawable, with "
			  "specified radius of affect.  The standard deviation "
			  "of the normal distribution used to modify pixel "
			  "values is calculated based on the supplied radius.  "
			  "This radius can be specified indepently on for the "
			  "horizontal and the vertical direction. The RLE "
			  "gaussian blurring performs most efficiently on "
			  "computer-generated images or images with large "
			  "areas of constant intensity.",
			  "Spencer Kimball, Peter Mattis & Sven Neumann",
			  "Spencer Kimball, Peter Mattis & Sven Neumann",
			  "1995-2000",
			  N_("<Image>/Filters/Blur/Gaussian Blur (_RLE)..."),
			  "RGB*, GRAY*",
			  GIMP_PLUGIN,
			  G_N_ELEMENTS (args2), 0,
			  args2, NULL);
}

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  static GimpParam   values[1];
  gint32             image_ID;
  GimpDrawable      *drawable;
  GimpRunMode        run_mode;
  GimpPDBStatusType  status = GIMP_PDB_SUCCESS;

  run_mode = param[0].data.d_int32;

  INIT_I18N ();

  *nreturn_vals = 1;
  *return_vals  = values;

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  /*  Get the specified image and drawable  */
  image_ID = param[1].data.d_image;
  drawable = gimp_drawable_get (param[2].data.d_drawable);

  if (strcmp (name, "plug_in_gauss_rle") == 0)   /* the old-fashioned way of calling it */
    {
      switch (run_mode)
	{
	case GIMP_RUN_INTERACTIVE:
	  /*  Possibly retrieve data  */
	  gimp_get_data ("plug_in_gauss_rle", &bvals);

	  /*  First acquire information with a dialog  */
	  if (! gauss_rle_dialog ())
	    return;
	  break;
	case GIMP_RUN_NONINTERACTIVE:
	  /*  Make sure all the arguments are there!  */
	  if (nparams != 6)
	    status = GIMP_PDB_CALLING_ERROR;
	  if (status == GIMP_PDB_SUCCESS)
	    {
	      bvals.radius = param[3].data.d_float;
	      bvals.horizontal = (param[4].data.d_int32) ? TRUE : FALSE;
	      bvals.vertical = (param[5].data.d_int32) ? TRUE : FALSE;
	    }
	  if (status == GIMP_PDB_SUCCESS && (bvals.radius <= 0.0))
	    status = GIMP_PDB_CALLING_ERROR;
	  break;

	case GIMP_RUN_WITH_LAST_VALS:
	  /*  Possibly retrieve data  */
	  gimp_get_data ("plug_in_gauss_rle", &bvals);
	  break;

	default:
	  break;
	}

      if (!(bvals.horizontal || bvals.vertical))
	{
	  g_message (_("You must specify either horizontal "
                       "or vertical (or both)"));
	  status = GIMP_PDB_CALLING_ERROR;
	}

    }
  else if (strcmp (name, "plug_in_gauss_rle2") == 0)
    {
      switch (run_mode)
	{
	case GIMP_RUN_INTERACTIVE:
	  /*  Possibly retrieve data  */
	  gimp_get_data ("plug_in_gauss_rle2", &b2vals);

	  /*  First acquire information with a dialog  */
	  if (! gauss_rle2_dialog (image_ID, drawable))
	    return;
	  break;
	case GIMP_RUN_NONINTERACTIVE:
	  /*  Make sure all the arguments are there!  */
	  if (nparams != 5)
	    status = GIMP_PDB_CALLING_ERROR;
	  if (status == GIMP_PDB_SUCCESS)
	    {
	      b2vals.horizontal = param[3].data.d_float;
	      b2vals.vertical   = param[4].data.d_float;
	    }
	  if (status == GIMP_PDB_SUCCESS &&
              (b2vals.horizontal <= 0.0 && b2vals.vertical <= 0.0))
	    status = GIMP_PDB_CALLING_ERROR;
	  break;

	case GIMP_RUN_WITH_LAST_VALS:
	  /*  Possibly retrieve data  */
	  gimp_get_data ("plug_in_gauss_rle2", &b2vals);
	  break;

	default:
	  break;
	}
    }
  else
    status = GIMP_PDB_CALLING_ERROR;

  if (status == GIMP_PDB_SUCCESS)
    {
      /*  Make sure that the drawable is gray or RGB color  */
      if (gimp_drawable_is_rgb (drawable->drawable_id) ||
          gimp_drawable_is_gray (drawable->drawable_id))
        {
          gimp_progress_init (_("RLE Gaussian Blur"));

          /*  set the tile cache size so that the gaussian blur works well  */
          gimp_tile_cache_ntiles (2 *
                                  (MAX (drawable->width, drawable->height) /
				  gimp_tile_width () + 1));

          /*  run the gaussian blur  */
	  if (strcmp (name, "plug_in_gauss_rle") == 0)
	    {
	      gauss_rle (drawable, (bvals.horizontal ? bvals.radius : 0.0),
                                   (bvals.vertical ? bvals.radius : 0.0));

	      /*  Store data  */
	      if (run_mode == GIMP_RUN_INTERACTIVE)
		gimp_set_data ("plug_in_gauss_rle",
                               &bvals, sizeof (BlurValues));
	    }
	  else
	    {
	      gauss_rle (drawable, b2vals.horizontal, b2vals.vertical);

	      /*  Store data  */
	      if (run_mode == GIMP_RUN_INTERACTIVE)
		gimp_set_data ("plug_in_gauss_rle2",
                               &b2vals, sizeof (Blur2Values));
	    }

          if (run_mode != GIMP_RUN_NONINTERACTIVE)
	    gimp_displays_flush ();
        }
      else
        {
          g_message (_("Cannot operate on indexed color images."));
          status = GIMP_PDB_EXECUTION_ERROR;
        }

    gimp_drawable_detach (drawable);
  }

  values[0].data.d_status = status;
}

static gint
gauss_rle_dialog (void)
{
  GtkWidget *dlg;
  GtkWidget *label;
  GtkWidget *spinbutton;
  GtkObject *adj;
  GtkWidget *toggle;
  GtkWidget *frame;
  GtkWidget *vbox;
  GtkWidget *hbox;
  gboolean   run;

  gimp_ui_init ("gauss_rle", FALSE);

  dlg = gimp_dialog_new (_("RLE Gaussian Blur"), "gauss_rle",
                         NULL, 0,
			 gimp_standard_help_func, "plug-in-gauss-rle",

			 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			 GTK_STOCK_OK,     GTK_RESPONSE_OK,

			 NULL);

  /*  parameter settings  */
  frame = gtk_frame_new (_("Parameter Settings"));
  gtk_container_set_border_width (GTK_CONTAINER (frame), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), frame, TRUE, TRUE, 0);
  gtk_widget_show (frame);

  vbox = gtk_vbox_new (FALSE, 2);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 4);
  gtk_container_add (GTK_CONTAINER (frame), vbox);
  gtk_widget_show (vbox);

  toggle = gtk_check_button_new_with_label (_("Blur Horizontally"));
  gtk_box_pack_start (GTK_BOX (vbox), toggle, FALSE, FALSE, 0);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), bvals.horizontal);
  gtk_widget_show (toggle);

  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &bvals.horizontal);

  toggle = gtk_check_button_new_with_label (_("Blur Vertically"));
  gtk_box_pack_start (GTK_BOX (vbox), toggle, FALSE, FALSE, 0);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), bvals.vertical);
  gtk_widget_show (toggle);

  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &bvals.vertical);

  hbox = gtk_hbox_new (FALSE, 4);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  label = gtk_label_new (_("Blur Radius:"));
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  spinbutton = gimp_spin_button_new (&adj,
				     bvals.radius, 0.0, GIMP_MAX_IMAGE_SIZE,
				     0.0, 5.0, 0, 1, 2);
  gtk_box_pack_start (GTK_BOX (hbox), spinbutton, TRUE, TRUE, 0);
  gtk_widget_show (spinbutton);

  g_signal_connect (adj, "value_changed",
                    G_CALLBACK (gimp_double_adjustment_update),
                    &bvals.radius);

  gtk_widget_show (hbox);
  gtk_widget_show (dlg);

  run = (gimp_dialog_run (GIMP_DIALOG (dlg)) == GTK_RESPONSE_OK);

  gtk_widget_destroy (dlg);

  return run;
}


static gint
gauss_rle2_dialog (gint32        image_ID,
		   GimpDrawable *drawable)
{
  GtkWidget *dlg;
  GtkWidget *frame;
  GtkWidget *size;
  GimpUnit   unit;
  gdouble    xres;
  gdouble    yres;
  gboolean   run;

  gimp_ui_init ("gauss_rle2", FALSE);

  dlg = gimp_dialog_new (_("RLE Gaussian Blur"), "gauss_rle",
                         NULL, 0,
			 gimp_standard_help_func, "plug-in-gauss-rle2",

			 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			 GTK_STOCK_OK,     GTK_RESPONSE_OK,

			 NULL);

  /*  parameter settings  */
  frame = gtk_frame_new (_("Blur Radius"));
  gtk_container_set_border_width (GTK_CONTAINER (frame), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), frame, TRUE, TRUE, 0);

  /*  Get the image resolution and unit  */
  gimp_image_get_resolution (image_ID, &xres, &yres);
  unit = gimp_image_get_unit (image_ID);

  size = gimp_coordinates_new (unit, "%a", TRUE, FALSE, -1,
			       GIMP_SIZE_ENTRY_UPDATE_SIZE,

			       b2vals.horizontal == b2vals.vertical,
			       FALSE,

			       _("_Horizontal:"), b2vals.horizontal, xres,
			       0, 8 * MAX (drawable->width, drawable->height),
			       0, 0,

			       _("_Vertical:"), b2vals.vertical, yres,
			       0, 8 * MAX (drawable->width, drawable->height),
			       0, 0);
  gtk_container_set_border_width (GTK_CONTAINER (size), 4);
  gtk_container_add (GTK_CONTAINER (frame), size);

  gimp_size_entry_set_pixel_digits (GIMP_SIZE_ENTRY (size), 1);

  gtk_widget_show (size);
  gtk_widget_show (frame);
  gtk_widget_show (dlg);

  run = (gimp_dialog_run (GIMP_DIALOG (dlg)) == GTK_RESPONSE_OK);

  if (run)
    {
      b2vals.horizontal = gimp_size_entry_get_refval (GIMP_SIZE_ENTRY (size), 0);
      b2vals.vertical   = gimp_size_entry_get_refval (GIMP_SIZE_ENTRY (size), 1);
    }

  gtk_widget_destroy (dlg);

  return run;
}

/* Convert from separated to premultiplied alpha, on a single scan line. */
static void
multiply_alpha (guchar *buf,
		gint    width,
		gint    bytes)
{
  gint    i, j;
  gdouble alpha;

  for (i = 0; i < width * bytes; i += bytes)
    {
      alpha = buf[i + bytes - 1] * (1.0 / 255.0);
      for (j = 0; j < bytes - 1; j++)
	buf[i + j] *= alpha;
    }
}

/* Convert from premultiplied to separated alpha, on a single scan
   line. */
static void
separate_alpha (guchar *buf,
		gint    width,
		gint    bytes)
{
  gint    i, j;
  guchar  alpha;
  gdouble recip_alpha;
  gint    new_val;

  for (i = 0; i < width * bytes; i += bytes)
    {
      alpha = buf[i + bytes - 1];
      if (alpha != 0 && alpha != 255)
	{
	  recip_alpha = 255.0 / alpha;
	  for (j = 0; j < bytes - 1; j++)
	    {
	      new_val = buf[i + j] * recip_alpha;
	      buf[i + j] = MIN (255, new_val);
	    }
	}
    }
}

static void
gauss_rle (GimpDrawable *drawable,
	   gdouble       horz,
	   gdouble       vert)
{
  GimpPixelRgn src_rgn, dest_rgn;
  gint     width, height;
  gint     bytes;
  gint     has_alpha;
  guchar  *dest, *dp;
  guchar  *src, *sp;
  gint    *buf, *bb;
  gint     pixels;
  gint     total = 1;
  gint     x1, y1, x2, y2;
  gint     i, row, col, b;
  gint     start, end;
  gdouble  progress, max_progress;
  gint    *curve;
  gint    *sum = NULL;
  gint     val;
  gint     length;
  gint     initial_p, initial_m;
  gdouble  std_dev;

  if (horz <= 0.0 && vert <= 0.0)
    return;

  gimp_drawable_mask_bounds (drawable->drawable_id, &x1, &y1, &x2, &y2);

  width  = (x2 - x1);
  height = (y2 - y1);

  if (width < 1 || height < 1)
    return;

  bytes = drawable->bpp;
  has_alpha = gimp_drawable_has_alpha(drawable->drawable_id);

  buf = g_new (gint, MAX (width, height) * 2);

  /*  allocate buffers for source and destination pixels  */
  src = g_new (guchar, MAX (width, height) * bytes);
  dest = g_new (guchar, MAX (width, height) * bytes);

  gimp_pixel_rgn_init (&src_rgn,
                       drawable, 0, 0, drawable->width, drawable->height,
                       FALSE, FALSE);
  gimp_pixel_rgn_init (&dest_rgn,
                       drawable, 0, 0, drawable->width, drawable->height,
                       TRUE, TRUE);

  progress = 0.0;
  max_progress  = (horz <= 0.0 ) ? 0 : width * height * horz;
  max_progress += (vert <= 0.0 ) ? 0 : width * height * vert;


  /*  First the vertical pass  */
  if (vert > 0.0)
    {
      vert = fabs (vert) + 1.0;
      std_dev = sqrt (-(vert * vert) / (2 * log (1.0 / 255.0)));

      curve = make_curve (std_dev, &length);
      sum = g_new (gint, 2 * length + 1);

      sum[0] = 0;

      for (i = 1; i <= length*2; i++)
	sum[i] = curve[i-length-1] + sum[i-1];
      sum += length;

      total = sum[length] - sum[-length];

       for (col = 0; col < width; col++)
	{
	  gimp_pixel_rgn_get_col (&src_rgn, src, col + x1, y1, (y2 - y1));
          if (has_alpha)
              multiply_alpha (src, height, bytes);

	  sp = src;
	  dp = dest;

	  for (b = 0; b < bytes; b++)
	    {
	      initial_p = sp[b];
	      initial_m = sp[(height-1) * bytes + b];

	      /*  Determine a run-length encoded version of the row  */
	      run_length_encode (sp + b, buf, bytes, height);

	      for (row = 0; row < height; row++)
		{
		  start = (row < length) ? -row : -length;
		  end = (height <= (row + length) ?
                         (height - row - 1) : length);

		  val = 0;
		  i = start;
		  bb = buf + (row + i) * 2;

		  if (start != -length)
		    val += initial_p * (sum[start] - sum[-length]);

		  while (i < end)
		    {
		      pixels = bb[0];
		      i += pixels;
		      if (i > end)
			i = end;
		      val += bb[1] * (sum[i] - sum[start]);
		      bb += (pixels * 2);
		      start = i;
		    }

		  if (end != length)
		    val += initial_m * (sum[length] - sum[end]);

		  dp[row * bytes + b] = val / total;
		}
	    }
          if (has_alpha)
              separate_alpha (dest, height, bytes);

	  gimp_pixel_rgn_set_col (&dest_rgn, dest, col + x1, y1, (y2 - y1));
	  progress += height * vert;
	  if ((col % 5) == 0)
	    gimp_progress_update (progress / max_progress);
	}

      /*  prepare for the horizontal pass  */
      gimp_pixel_rgn_init (&src_rgn,
                           drawable, 0, 0, drawable->width, drawable->height,
                           FALSE, TRUE);
    }

  /*  Now the horizontal pass  */
  if (horz > 0.0)
    {
      horz = fabs (horz) + 1.0;

      if (horz != vert)
	{
	  std_dev = sqrt (-(horz * horz) / (2 * log (1.0 / 255.0)));

	  curve = make_curve (std_dev, &length);
	  sum = g_new (gint, 2 * length + 1);

	  sum[0] = 0;

	  for (i = 1; i <= length*2; i++)
	    sum[i] = curve[i-length-1] + sum[i-1];
	  sum += length;

	  total = sum[length] - sum[-length];
	}

      for (row = 0; row < height; row++)
	{
	  gimp_pixel_rgn_get_row (&src_rgn, src, x1, row + y1, (x2 - x1));
          if (has_alpha)
              multiply_alpha (src, width, bytes);

	  sp = src;
	  dp = dest;

	  for (b = 0; b < bytes; b++)
	    {
	      initial_p = sp[b];
	      initial_m = sp[(width-1) * bytes + b];

	      /*  Determine a run-length encoded version of the row  */
	      run_length_encode (sp + b, buf, bytes, width);

	      for (col = 0; col < width; col++)
		{
		  start = (col < length) ? -col : -length;
		  end = (width <= (col + length)) ? (width - col - 1) : length;

		  val = 0;
		  i = start;
		  bb = buf + (col + i) * 2;

		  if (start != -length)
		    val += initial_p * (sum[start] - sum[-length]);

		  while (i < end)
		    {
		      pixels = bb[0];
		      i += pixels;
		      if (i > end)
			i = end;
		      val += bb[1] * (sum[i] - sum[start]);
		      bb += (pixels * 2);
		      start = i;
		    }

		  if (end != length)
		    val += initial_m * (sum[length] - sum[end]);

		  dp[col * bytes + b] = val / total;
		}
	    }
          if (has_alpha)
              separate_alpha (dest, width, bytes);

	  gimp_pixel_rgn_set_row (&dest_rgn, dest, x1, row + y1, (x2 - x1));
	  progress += width * horz;
	  if ((row % 5) == 0)
	    gimp_progress_update (progress / max_progress);
	}
    }


  /*  merge the shadow, update the drawable  */
  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
  gimp_drawable_update (drawable->drawable_id, x1, y1, (x2 - x1), (y2 - y1));

  /*  free buffers  */
  g_free (buf);
  g_free (src);
  g_free (dest);
}

/*
 * The equations: g(r) = exp (- r^2 / (2 * sigma^2))
 *                   r = sqrt (x^2 + y ^2)
 */

static gint *
make_curve (gdouble  sigma,
	    gint    *length)
{
  gint *curve;
  gdouble sigma2;
  gdouble l;
  gint temp;
  gint i, n;

  sigma2 = 2 * sigma * sigma;
  l = sqrt (-sigma2 * log (1.0 / 255.0));

  n = ceil (l) * 2;
  if ((n % 2) == 0)
    n += 1;

  curve = g_new (gint, n);

  *length = n / 2;
  curve += *length;
  curve[0] = 255;

  for (i = 1; i <= *length; i++)
    {
      temp = (gint) (exp (- (i * i) / sigma2) * 255);
      curve[-i] = temp;
      curve[i] = temp;
    }

  return curve;
}

static void
run_length_encode (guchar *src,
		   gint   *dest,
		   gint    bytes,
		   gint    width)
{
  gint start;
  gint i;
  gint j;
  guchar last;

  last = *src;
  src += bytes;
  start = 0;

  for (i = 1; i < width; i++)
    {
      if (*src != last)
	{
	  for (j = start; j < i; j++)
	    {
	      *dest++ = (i - j);
	      *dest++ = last;
	    }
	  start = i;
	  last = *src;
	}
      src += bytes;
    }

  for (j = start; j < i; j++)
    {
      *dest++ = (i - j);
      *dest++ = last;
    }
}
