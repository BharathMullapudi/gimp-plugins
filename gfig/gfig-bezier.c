/*
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This is a plug-in for the GIMP.
 *
 * Generates images containing vector type drawings.
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
 */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "gfig.h"

#include "libgimp/stdplugins-intl.h"

static gint bezier_closed     = 0; /* Closed curve 0 = false 1 = true */
static gint bezier_line_frame = 0; /* Show frame = false 1 = true */
Dobject *tmp_bezier;   		   /* Needed when drawing bezier curves */

static void       d_paint_bezier          (Dobject *obj);
static Dobject  * d_copy_bezier           (Dobject * obj);
static Dobject  * d_new_bezier            (gint x, gint y);

static void
d_save_bezier (Dobject *obj,
	       FILE    *to)
{
  fprintf (to, "<BEZIER>\n");
  do_save_obj (obj, to);
  fprintf (to, "<EXTRA>\n");
  fprintf (to, "%d\n</EXTRA>\n", obj->type_data);
  fprintf (to, "</BEZIER>\n");
}

Dobject *
d_load_bezier (FILE *from)
{
  Dobject *new_obj = NULL;
  gint xpnt;
  gint ypnt;
  gchar buf[MAX_LOAD_LINE];

  while (get_line (buf, MAX_LOAD_LINE, from, 0))
    {
      if (sscanf (buf, "%d %d", &xpnt, &ypnt) != 2)
	{
	  /* Must be the end */
	  if (!strcmp ("<EXTRA>", buf))
	    {
	      gint nsides = 3;
	      /* Number of sides - data item */
	      if ( !new_obj)
		{
		  g_message ("[%d] Internal load error while loading bezier "
			     "(extra area)", line_no);
		  return NULL;
		}
	      get_line (buf, MAX_LOAD_LINE, from, 0);
	      if (sscanf (buf, "%d", &nsides) != 1)
		{
		  g_message ("[%d] Internal load error while loading bezier "
			     "(extra area scanf)", line_no);
		  return NULL;
		}
	      new_obj->type_data = nsides;
	      get_line (buf, MAX_LOAD_LINE, from, 0);
	      if (strcmp ("</EXTRA>", buf))
		{
		  g_message ("[%d] Internal load error while loading bezier",
			     line_no);
		  return NULL;
		} 
	      /* Go around and read the last line */
	      continue;
	    }
	  else if (strcmp ("</BEZIER>", buf))
	    {
	      g_message ("[%d] Internal load error while loading bezier",
			 line_no);
	      return NULL;
	    }
	  return new_obj;
	}
      
      if (!new_obj)
	new_obj = d_new_bezier (xpnt, ypnt);
      else
	d_pnt_add_line (new_obj, xpnt, ypnt, -1);
    }

  return new_obj;
}


#define FP_PNT_MAX  10

static int fp_pnt_cnt = 0;
static int fp_pnt_chunk = 0;
static gdouble *fp_pnt_pnts = NULL;

static void
fp_pnt_start (void)
{
  fp_pnt_cnt = 0;
}

/* Add a line segment to collection array */
static void
fp_pnt_add (gdouble p1,
	    gdouble p2,
	    gdouble p3,
	    gdouble p4)
{
  if (!fp_pnt_pnts)
    {
      fp_pnt_pnts = g_new0 (gdouble, FP_PNT_MAX);
      fp_pnt_chunk = 1;
    }

  if (((fp_pnt_cnt + 4) / FP_PNT_MAX) >= fp_pnt_chunk)
    {
      /* more space pls */
      fp_pnt_chunk++;
      fp_pnt_pnts =
	(gdouble *) g_realloc (fp_pnt_pnts,
			       sizeof (gdouble) * fp_pnt_chunk * FP_PNT_MAX);
    }

  fp_pnt_pnts[fp_pnt_cnt++] = p1;
  fp_pnt_pnts[fp_pnt_cnt++] = p2;
  fp_pnt_pnts[fp_pnt_cnt++] = p3;
  fp_pnt_pnts[fp_pnt_cnt++] = p4;
}

static gdouble *
d_bz_get_array (gint *sz)
{
  *sz = fp_pnt_cnt;
  return fp_pnt_pnts;
}

static void
d_bz_line (void)
{
  gint i, x0, y0, x1, y1; 

  g_assert ((fp_pnt_cnt % 4) == 0);

  for (i = 0 ; i < fp_pnt_cnt; i += 4)
    {
      x0 = fp_pnt_pnts[i];
      y0 = fp_pnt_pnts[i + 1];
      x1 = fp_pnt_pnts[i + 2];
      y1 = fp_pnt_pnts[i + 3];

      gfig_draw_line (x0, y0, x1, y1);
    }
}

/*  Return points to plot */
/* Terminate by point with DBL_MAX, DBL_MAX */
typedef gdouble (*fp_pnt)[2];

static void
DrawBezier (gdouble (*points)[2],
	    gint      np,
	    gdouble   mid,
	    gint      depth)
{
  gint i, j, x0 = 0, y0 = 0, x1, y1; 
  fp_pnt left;
  fp_pnt right;
  
    if (depth == 0) /* draw polyline */
      {
	for (i = 0; i < np; i++)
	  {
	    x1 = (int) points[i][0];
	    y1 = (int) points[i][1];
	    if (i > 0 && (x1 != x0 || y1 != y0))
	      {
		/* Add pnts up */
		fp_pnt_add ((gdouble) x0, (gdouble) y0,
			    (gdouble) x1, (gdouble) y1);
	      }
	    x0 = x1;
	    y0 = y1;
	  }
      }
    else /* subdivide control points at mid */
      {
	left = (fp_pnt) g_new (gdouble, np * 2);
	right = (fp_pnt) g_new (gdouble, np * 2);
	for (i = 0; i < np; i++)
	  {
	    right[i][0] = points[i][0];
	    right[i][1] = points[i][1];
	  } 
	left[0][0] = right[0][0];
	left[0][1] = right[0][1];
	for (j = np - 1; j >= 1; j--)
	  {
	    for (i = 0; i < j; i++)
	      {
		right[i][0] = (1 - mid) * right[i][0] + mid * right[i + 1][0];
		right[i][1] = (1 - mid) * right[i][1] + mid * right[i + 1][1];
	      }
	    left[np - j][0] = right[0][0];
	    left[np - j][1] = right[0][1];
	  }
	if (depth > 0)
	  {
	    DrawBezier (left, np, mid, depth - 1);
	    DrawBezier (right, np, mid, depth - 1);
	    g_free (left);
	    g_free (right);
	  }
      }
}

void
d_draw_bezier (Dobject *obj)
{
  DobjPoints * spnt;
  gint seg_count = 0;
  gint i = 0;
  gdouble (*line_pnts)[2];

  spnt = obj->points;

  /* First count the number of points */
  for (spnt = obj->points; spnt; spnt = spnt->next)
    seg_count++;

  if (!seg_count)
    return; /* no-line */

  line_pnts = (fp_pnt) g_new0 (gdouble, 2 * seg_count + 1);

  /* Go around all the points drawing a line from one to the next */
  for (spnt = obj->points; spnt; spnt = spnt->next)
    {
      draw_sqr (&spnt->pnt);
      line_pnts[i][0] = spnt->pnt.x;
      line_pnts[i++][1] = spnt->pnt.y;
    }

  /* Generate an array of doubles which are the control points */

  if (!drawing_pic && bezier_line_frame && tmp_bezier)
    {
      fp_pnt_start ();
      DrawBezier (line_pnts, seg_count, 0.5, 0);
      d_bz_line ();
    }

  fp_pnt_start ();
  DrawBezier (line_pnts, seg_count, 0.5, 3);
  d_bz_line ();
  /*bezier4 (line_pnts, seg_count, 20);*/

  g_free (line_pnts);
}

static void
d_paint_bezier (Dobject *obj)
{
  gdouble *line_pnts;
  gdouble (*bz_line_pnts)[2];
  DobjPoints *spnt;
  gint seg_count = 0;
  gint i = 0;

  /* First count the number of points */
  for (spnt = obj->points; spnt; spnt = spnt->next)
    seg_count++;

  if (!seg_count)
    return; /* no-line */

  bz_line_pnts = (fp_pnt) g_new0 (gdouble, 2 * seg_count + 1);

  /* Go around all the points drawing a line from one to the next */
  for (spnt = obj->points; spnt; spnt = spnt->next)
    {
      bz_line_pnts[i][0] = spnt->pnt.x;
      bz_line_pnts[i++][1] = spnt->pnt.y;
    }

  fp_pnt_start ();
  DrawBezier (bz_line_pnts, seg_count, 0.5, 5);
  line_pnts = d_bz_get_array (&i);

  /* Reverse line if approp */
  if (selvals.reverselines)
    reverse_pairs_list (&line_pnts[0], i / 2);

  /* Scale before drawing */
  if (selvals.scaletoimage)
    scale_to_original_xy (&line_pnts[0], i / 2);
  else
    scale_to_xy (&line_pnts[0], i / 2);

  /* One go */
  if (selvals.painttype == PAINT_BRUSH_TYPE)
    {
      gfig_paint (selvals.brshtype,
		  gfig_drawable,
		  i, line_pnts);
    }
  else
    {
      gimp_free_select (gfig_image,
			i, line_pnts,
			selopt.type,
			selopt.antia,
			selopt.feather,
			selopt.feather_radius);
    }

  g_free (bz_line_pnts);
  /* Don't free line_pnts - may need again */
}

static Dobject *
d_copy_bezier (Dobject *obj)
{
  Dobject *np;

  g_assert (obj->type == BEZIER);

  np = d_new_bezier (obj->points->pnt.x, obj->points->pnt.y);
  np->points->next = d_copy_dobjpoints (obj->points->next);
  np->type_data = obj->type_data;

  return np;
}

static Dobject *
d_new_bezier (gint x, gint y)
{
  Dobject *nobj;
 
  nobj = g_new0 (Dobject, 1);

  nobj->type = BEZIER;
  nobj->type_data = 4; /* Default to four turns */
  nobj->points = new_dobjpoint (x, y);
  nobj->drawfunc  = d_draw_bezier;
  nobj->loadfunc  = d_load_bezier;
  nobj->savefunc  = d_save_bezier;
  nobj->paintfunc = d_paint_bezier;
  nobj->copyfunc  = d_copy_bezier;

  return nobj;
}

void
d_update_bezier (GdkPoint *pnt)
{
  DobjPoints *s_pnt, *l_pnt;
  gint saved_cnt_pnt = selvals.opts.showcontrol;

  g_assert (tmp_bezier != NULL);

  /* Undraw last one then draw new one */
  s_pnt = tmp_bezier->points;
  
  if (!s_pnt)
    return; /* No points */

  /* Hack - turn off cnt points in draw routine 
   */

  if ((l_pnt = s_pnt->next))
    {
      /* Undraw */
      while (l_pnt->next)
	{
	  l_pnt = l_pnt->next;
	}

      draw_circle (&l_pnt->pnt);
      selvals.opts.showcontrol = 0;
      d_draw_bezier (tmp_bezier);
      l_pnt->pnt = *pnt;
    }
  else
    {
      /* Radius is a few pixels away */
      /* First edge point */
      d_pnt_add_line (tmp_bezier, pnt->x, pnt->y,-1);
      l_pnt = s_pnt->next;
    }

  /* draw it */
  selvals.opts.showcontrol = 0;
  d_draw_bezier (tmp_bezier);
  selvals.opts.showcontrol = saved_cnt_pnt;

  /* Realy draw the control points */
  draw_circle (&l_pnt->pnt);
}

void
d_bezier_start (GdkPoint *pnt, gint shift_down)
{
  if (!tmp_bezier)
    {
      /* New curve */
      tmp_bezier = obj_creating = d_new_bezier (pnt->x, pnt->y);
    }
}

void
d_bezier_end (GdkPoint *pnt, gint shift_down)
{
  DobjPoints *l_pnt;

  if (!tmp_bezier)
    {
      tmp_bezier = obj_creating;
    }
  
  l_pnt = tmp_bezier->points->next;

  if (!l_pnt) 
    return;

  if (shift_down)
    {
      /* Undraw circle on last pnt */
      while (l_pnt->next)
	{
	  l_pnt = l_pnt->next;
	}

      if (l_pnt)
	{
	  draw_circle (&l_pnt->pnt);
	  draw_sqr (&l_pnt->pnt);

	  if (bezier_closed)
	    {
	      gint tmp_frame = bezier_line_frame;
	      /* if closed then add first point */
	      d_draw_bezier (tmp_bezier);
	      d_pnt_add_line (tmp_bezier,
			     tmp_bezier->points->pnt.x,
			     tmp_bezier->points->pnt.y,-1);
	      /* Final has no frame */
	      bezier_line_frame = 0; /* False */
	      d_draw_bezier (tmp_bezier);
	      bezier_line_frame = tmp_frame; /* What is was */
	    }
	  else if (bezier_line_frame)
	    {
	      d_draw_bezier (tmp_bezier);
	      bezier_line_frame = 0; /* False */
	      d_draw_bezier (tmp_bezier);
	      bezier_line_frame = 1; /* What is was */
	    }

	  add_to_all_obj (current_obj, obj_creating);
	}

      /* small mem leak if !l_pnt ? */
      tmp_bezier = NULL;
      obj_creating = NULL;
    }
  else
    {
      if (!tmp_bezier->points->next)
	{
	  draw_circle (&tmp_bezier->points->pnt);
	  draw_sqr (&tmp_bezier->points->pnt);
	}

      d_draw_bezier (tmp_bezier);
      d_pnt_add_line (tmp_bezier, pnt->x, pnt->y,-1);
      d_draw_bezier (tmp_bezier);
    }
}

void
bezier_dialog (void)
{
  static GtkWidget *window = NULL;
  GtkWidget *vbox;
  GtkWidget *toggle;

  if (window)
    {
      gtk_window_present (GTK_WINDOW (window));
      return;
    }

  window = gimp_dialog_new (_("Bezier Settings"), "gfig",
			    gimp_standard_help_func, "filters/gfig.html",
			    GTK_WIN_POS_MOUSE,
			    FALSE, FALSE, FALSE,

			    GTK_STOCK_CLOSE, gtk_widget_destroy,
			    NULL, 1, NULL, TRUE, TRUE,

			    NULL);

  g_signal_connect (window, "destroy",
                    G_CALLBACK (gtk_widget_destroyed),
                    &window);

  vbox = gtk_vbox_new (FALSE, 2);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 4);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (window)->vbox), vbox,
		      FALSE, FALSE, 0);
  gtk_widget_show (vbox);

  toggle = gtk_check_button_new_with_label (_("Closed"));
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &bezier_closed);
  gimp_help_set_help_data (toggle,
			_("Close curve on completion"), NULL);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), bezier_closed);
  gtk_box_pack_start (GTK_BOX (vbox), toggle, FALSE, FALSE, 0);
  gtk_widget_show (toggle);

  toggle = gtk_check_button_new_with_label (_("Show Line Frame"));
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &bezier_line_frame);
  gimp_help_set_help_data (toggle,
			_("Draws lines between the control points. "
			  "Only during curve creation"), NULL);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), bezier_line_frame);
  gtk_box_pack_start (GTK_BOX (vbox), toggle, FALSE, FALSE, 0);
  gtk_widget_show (toggle);

  gtk_widget_show (window);
}
