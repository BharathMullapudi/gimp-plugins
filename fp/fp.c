/*
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This is a plug-in for the GIMP.
 *
 * Copyright (C) Pavel Grinfeld (pavel@ml.com)
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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>

#include <libgimp/gimp.h>

#include "fp.h"

#include "libgimp/stdplugins-intl.h"


FP_Params Current =
{
  1,
  .25,                /* Initial Roughness */
  NULL,
  .6,                 /* Initial Degree of Aliasing */
  NULL,
  80,
  NULL,
  MIDTONES,           /* Initial Range */
  BY_VAL,             /* Initial God knows what */
  TRUE,               /* Selection Only */
  TRUE,               /* Real Time */
  0,                  /* Offset */
  0,
  {32,224,255},
  {0,0,0}
};

GimpDrawable *drawable, *mask;

void      query  (void);
void      run    (const gchar      *name,
		  gint              nparams,
		  const GimpParam  *param,
		  gint             *nreturn_vals,
		  GimpParam       **return_vals);

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};

MAIN()

void
query (void)
{
  GimpParamDef args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE, "image", "Input image (used for indexed images)" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable" },
  };
  
  gimp_install_procedure ("plug_in_filter_pack",
			  "Allows the user to change H, S, or C with many previews",
			  "No help available",
			  "Pavel Grinfeld (pavel@ml.com)",
			  "Pavel Grinfeld (pavel@ml.com)",
			  "27th March 1997",
			  N_("<Image>/Filters/Colors/Filter Pack..."),
			  "RGB*",
			  GIMP_PLUGIN,
			  G_N_ELEMENTS (args), 0,
			  args, NULL);
}

/********************************STANDARD RUN*************************/

void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  GimpParam         values[1];
  GimpPDBStatusType status = GIMP_PDB_SUCCESS;
  
  *nreturn_vals = 1;
  *return_vals = values;

  INIT_I18N (); 

  values[0].type = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  initializeFilterPacks();

  drawable = gimp_drawable_get (param[2].data.d_drawable);
  mask = gimp_drawable_get (gimp_image_get_selection (param[1].data.d_image));

  if (gimp_drawable_is_indexed (drawable->drawable_id) ||
      gimp_drawable_is_gray (drawable->drawable_id) )
    {
      gimp_message (_("Convert the image to RGB first!"));
      status = GIMP_PDB_EXECUTION_ERROR;
    }
  else if (gimp_drawable_is_rgb (drawable->drawable_id) && fp_dialog())
    {
      gimp_progress_init (_("Applying the Filter Pack...")); 
      gimp_tile_cache_ntiles (2 * (drawable->width / gimp_tile_width () + 1));
      fp (drawable);
      gimp_displays_flush ();
    }
  else status = GIMP_PDB_EXECUTION_ERROR;
  
  
  values[0].data.d_status = status;
  if (status==GIMP_PDB_SUCCESS)
    gimp_drawable_detach (drawable);
}


void
fp_row (const guchar *src_row,
	guchar       *dest_row,
	gint         row,
	gint         row_width,
	gint         bytes)
{
  gint    col, bytenum, k;
  gint    JudgeBy, Intensity=0, P[3], backupP[3];
  GimpRGB rgb;
  GimpHSV hsv;
  gint    M, m, middle;

  for (col = 0; col < row_width ; col++)
    {
      
      backupP[0] = P[0] = src_row[col * bytes + 0];
      backupP[0] = P[1] = src_row[col * bytes + 1];
      backupP[0] = P[2] = src_row[col * bytes + 2];

      gimp_rgb_set_uchar (&rgb, (guchar) P[0], (guchar) P[1], (guchar) P[2]);
      gimp_rgb_to_hsv (&rgb, &hsv);

      for (JudgeBy = BY_HUE; JudgeBy < JUDGE_BY; JudgeBy++)
        {
          if (! Current.Touched[JudgeBy])
            continue;

          switch (JudgeBy)
            {
            case BY_HUE:
              Intensity = 255 * hsv.h;
              break;

            case BY_SAT:
              Intensity = 255 * hsv.s;
              break;

            case BY_VAL:
              Intensity = 255 * hsv.v;
              break;
            }


          /* It's important to take care of Saturation first!!! */
      
          m = MIN (MIN (P[0], P[1]), P[2]);
          M = MAX (MAX (P[0], P[1]), P[2]);
          middle = (M + m) / 2;

          for (k = 0; k < 3; k++)
            if (P[k] != m && P[k] != M)
              middle = P[k];
      
          for (k = 0; k < 3; k++) 
            if (M != m)
              {
                if (P[k] == M)
                  P[k] = MAX (P[k] + Current.satAdj[JudgeBy][Intensity], middle);
                else if (P[k] == m)
                  P[k] = MIN (P[k] - Current.satAdj[JudgeBy][Intensity], middle); 
              }

          P[0] += Current.redAdj[JudgeBy][Intensity];
          P[1] += Current.greenAdj[JudgeBy][Intensity];
          P[2] += Current.blueAdj[JudgeBy][Intensity];

          P[0]  =  MAX (0, MIN (255, P[0]));
          P[1]  =  MAX (0, MIN (255, P[1]));
          P[2]  =  MAX (0, MIN (255, P[2]));
        }

      dest_row[col * bytes + 0] = P[0];
      dest_row[col * bytes + 1] = P[1];
      dest_row[col * bytes + 2] = P[2];

      if (bytes > 3)
	for (bytenum = 3; bytenum < bytes; bytenum++)
	  dest_row[col * bytes + bytenum] = src_row[col * bytes + bytenum];
    }
}


void fp (GimpDrawable *drawable)
{
  GimpPixelRgn srcPR, destPR;
  gint width, height;
  gint bytes;
  guchar *src_row, *dest_row;
  gint row;
  gint x1, y1, x2, y2;
  
  gimp_drawable_mask_bounds (drawable->drawable_id, &x1, &y1, &x2, &y2);

  width = drawable->width;
  height = drawable->height;
  bytes = drawable->bpp;

  src_row = (guchar *) malloc ((x2 - x1) * bytes);
  dest_row = (guchar *) malloc ((x2 - x1) * bytes);

  gimp_pixel_rgn_init (&srcPR, drawable, 0, 0, width, height, FALSE, FALSE);
  gimp_pixel_rgn_init (&destPR, drawable, 0, 0, width, height, TRUE, TRUE);

  for (row = y1; row < y2; row++)
    {
      gimp_pixel_rgn_get_row (&srcPR, src_row, x1, row, (x2 - x1));

      fp_row (src_row,
		  dest_row,
		  row,
		  (x2 - x1),
		  bytes
		  );

      gimp_pixel_rgn_set_row (&destPR, dest_row, x1, row, (x2 - x1));
      
      if ((row % 10) == 0)
	gimp_progress_update ((double) row / (double) (y2 - y1));
    }


  /*  update the processed region  */

  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
  gimp_drawable_update (drawable->drawable_id, x1, y1, (x2 - x1), (y2 - y1));
  
  free (src_row);
  free (dest_row);
 
}
