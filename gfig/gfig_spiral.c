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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "config.h"
#include "libgimp/stdplugins-intl.h"

#include "gfig.h"

static void       d_draw_spiral           (Dobject *obj);
static void       d_paint_spiral          (Dobject *obj);
static Dobject  * d_copy_spiral           (Dobject * obj);
static Dobject  * d_new_spiral            (gint x, gint y);

static gint spiral_num_turns  = 4; /* Default to 4 turns */
static gint spiral_toggle     = 0; /* 0 = clockwise -1 = anti-clockwise */

gint
spiral_button_press (GtkWidget      *widget,
		     GdkEventButton *event,
		     gpointer        data)
{
  if ((event->type == GDK_2BUTTON_PRESS) &&
      (event->button == 1))
    num_sides_dialog (_("Spiral Number of Points"),
		      &spiral_num_turns, &spiral_toggle, 1, 20);
  return FALSE;
}

static void
d_save_spiral (Dobject *obj,
	       FILE    *to)
{
  DobjPoints * spnt;
  
  spnt = obj->points;

  if (!spnt)
    return; /* End-of-line */

  fprintf (to, "<SPIRAL>\n");

  while (spnt)
    {
      fprintf (to, "%d %d\n",
	       spnt->pnt.x,
	       spnt->pnt.y);
      spnt = spnt->next;
    }
  
  fprintf (to, "<EXTRA>\n");
  fprintf (to, "%d\n</EXTRA>\n", obj->type_data);
  fprintf (to, "</SPIRAL>\n");
}

/* Load a spiral from the specified stream */

Dobject *
d_load_spiral (FILE *from)
{
  Dobject *new_obj = NULL;
  gint xpnt;
  gint ypnt;
  gchar buf[MAX_LOAD_LINE];

#ifdef DEBUG
  printf ("Load spiral called\n");
#endif /* DEBUG */

  while (get_line (buf, MAX_LOAD_LINE, from, 0))
    {
      if (sscanf (buf, "%d %d", &xpnt, &ypnt) != 2)
	{
	  /* Must be the end */
	  if (!strcmp ("<EXTRA>", buf))
	    {
	      gint nsides = 3;
	      /* Number of sides - data item */
	      if (!new_obj)
		{
		  g_warning ("[%d] Internal load error while loading spiral (extra area)",
			    line_no);
		  return (NULL);
		}
	      get_line (buf, MAX_LOAD_LINE, from, 0);
	      if (sscanf (buf, "%d", &nsides) != 1)
		{
		  g_warning ("[%d] Internal load error while loading spiral (extra area scanf)",
			    line_no);
		  return (NULL);
		}
	      new_obj->type_data = nsides;
	      get_line (buf, MAX_LOAD_LINE, from, 0);
	      if (strcmp ("</EXTRA>", buf))
		{
		  g_warning ("[%d] Internal load error while loading spiral",
			    line_no);
		  return (NULL);
		} 
	      /* Go around and read the last line */
	      continue;
	    }
	  else if (strcmp ("</SPIRAL>", buf))
	    {
	      g_warning ("[%d] Internal load error while loading spiral",
			line_no);
	      return (NULL);
	    }
	  return (new_obj);
	}
      
      if (!new_obj)
	new_obj = d_new_spiral (xpnt, ypnt);
      else
	d_pnt_add_line (new_obj, xpnt, ypnt,-1);
    }
  return (new_obj);
}

static void
d_draw_spiral (Dobject *obj)
{
  DobjPoints * center_pnt;
  DobjPoints * radius_pnt;
  gint16 shift_x;
  gint16 shift_y;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble radius;
  gdouble offset_angle;
  gdouble sp_cons;
  gint loop;
  GdkPoint start_pnt;
  GdkPoint first_pnt;
  gint do_line = 0;
  gint clock_wise = 1;

  center_pnt = obj->points;

  if (!center_pnt)
    return; /* End-of-line */

  /* First point is the center */
  /* Just draw a control point around it */

  draw_sqr (&center_pnt->pnt);

  /* Next point defines the radius */
  radius_pnt = center_pnt->next; /* this defines the vetices */

  if (!radius_pnt)
    {
#ifdef DEBUG
      g_warning ("Internal error in spiral - no vertice point \n");
#endif /* DEBUG */
      return;
    }

  /* Other control point */
  draw_sqr (&radius_pnt->pnt);

  /* Have center and radius - draw spiral */

  shift_x = radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = radius_pnt->pnt.y - center_pnt->pnt.y;

  radius = sqrt ((shift_x*shift_x) + (shift_y*shift_y));

  offset_angle = atan2 (shift_y, shift_x);

  clock_wise = obj->type_data / abs (obj->type_data);

  if (offset_angle < 0)
    offset_angle += 2*G_PI;

  sp_cons = radius/(obj->type_data * 2 * G_PI + offset_angle);
  /* Lines */
  ang_grid = 2.0*G_PI/(gdouble)180;


  for (loop = 0 ; loop <= abs (obj->type_data * 180) + clock_wise*(gint)RINT (offset_angle/ang_grid) ; loop++)
    {
      gdouble lx, ly;
      GdkPoint calc_pnt;

      ang_loop = (gdouble)loop * ang_grid;
	
      lx = sp_cons * ang_loop * cos (ang_loop)*clock_wise;
      ly = sp_cons * ang_loop * sin (ang_loop);

      calc_pnt.x = RINT (lx + center_pnt->pnt.x);
      calc_pnt.y = RINT (ly + center_pnt->pnt.y);

      if (do_line)
	{

	  /* Miss out points that come to the same location */
	  if (calc_pnt.x == start_pnt.x && calc_pnt.y == start_pnt.y)
	    continue;

	  if (drawing_pic)
	    {
	      gdk_draw_line (pic_preview->window,
			     pic_preview->style->black_gc,			    
			     adjust_pic_coords (calc_pnt.x,
						preview_width),
			     adjust_pic_coords (calc_pnt.y,
						preview_height),
			     adjust_pic_coords (start_pnt.x,
						preview_width),
			     adjust_pic_coords (start_pnt.y,
						preview_height));
	    }
	  else
	    {
	      gdk_draw_line (gfig_preview->window,
			     gfig_gc,
			     gfig_scale_x (calc_pnt.x),
			     gfig_scale_y (calc_pnt.y),
			     gfig_scale_x (start_pnt.x),
			     gfig_scale_y (start_pnt.y));
	    }
	}
      else
	{
	  do_line = 1;
	  first_pnt.x = calc_pnt.x;
	  first_pnt.y = calc_pnt.y;
	}
      start_pnt.x = calc_pnt.x;
      start_pnt.y = calc_pnt.y;
    }
}

static void
d_paint_spiral (Dobject *obj)
{
  /* first point center */
  /* Next point is radius */
  gdouble *line_pnts;
  gint seg_count = 0;
  gint i = 0;
  DobjPoints * center_pnt;
  DobjPoints * radius_pnt;
  gint16 shift_x;
  gint16 shift_y;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble radius;
  gdouble offset_angle;
  gdouble sp_cons;
  gint loop;
  GdkPoint last_pnt;
  gint clock_wise = 1;

  g_assert (obj != NULL);

  center_pnt = obj->points;

  if (!center_pnt || !center_pnt->next)
    return; /* no-line */

  /* Go around all the points drawing a line from one to the next */

  radius_pnt = center_pnt->next; /* this defines the vetices */

  /* Have center and radius - get lines */
  shift_x = radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = radius_pnt->pnt.y - center_pnt->pnt.y;

  radius = sqrt ((shift_x*shift_x) + (shift_y*shift_y));

  clock_wise = obj->type_data / abs (obj->type_data);

  offset_angle = atan2 (shift_y, shift_x);

  if (offset_angle < 0)
    offset_angle += 2*G_PI;

  sp_cons = radius/(obj->type_data * 2 * G_PI + offset_angle);
  /* Lines */
  ang_grid = 2.0*G_PI/(gdouble)180;


  /* count - */
  seg_count = abs (obj->type_data * 180) + clock_wise*(gint)RINT (offset_angle/ang_grid);

  line_pnts = g_new0 (gdouble, 2 * seg_count + 3);

  for (loop = 0 ; loop <= seg_count; loop++)
    {
      gdouble lx, ly;
      GdkPoint calc_pnt;

      ang_loop = (gdouble)loop * ang_grid;
	
      lx = sp_cons * ang_loop * cos (ang_loop)*clock_wise;
      ly = sp_cons * ang_loop * sin (ang_loop);

      calc_pnt.x = RINT (lx + center_pnt->pnt.x);
      calc_pnt.y = RINT (ly + center_pnt->pnt.y);

      /* Miss out duped pnts */
      if (!loop)
	{
	  if (calc_pnt.x == last_pnt.x && calc_pnt.y == last_pnt.y)
	    {
	      continue;
	    }
	}

      last_pnt.x = line_pnts[i++] = calc_pnt.x;
      last_pnt.y = line_pnts[i++] = calc_pnt.y;
    }

  /* Reverse line if approp */
  if (selvals.reverselines)
    reverse_pairs_list (&line_pnts[0], i/2);

  /* Scale before drawing */
  if (selvals.scaletoimage)
    scale_to_original_xy (&line_pnts[0], i/2);
  else
    scale_to_xy (&line_pnts[0], i/2);

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

  g_free (line_pnts);
}

static Dobject *
d_copy_spiral (Dobject * obj)
{
  Dobject *np;

#if DEBUG
  printf ("Copy spiral\n");
#endif /* DEBUG */

  if (!obj)
    return (NULL);

  g_assert (obj->type == SPIRAL);

  np = d_new_spiral (obj->points->pnt.x, obj->points->pnt.y);

  np->points->next = d_copy_dobjpoints (obj->points->next);

  np->type_data = obj->type_data;

#if DEBUG
  printf ("Done spiral copy\n");
#endif /* DEBUG */

  return (np);
}

static Dobject *
d_new_spiral (gint x,
	      gint y)
{
  Dobject *nobj;
  DobjPoints *npnt;
 
  /* Get new object and starting point */

  /* Start point */
  npnt = g_new0 (DobjPoints, 1);

#if DEBUG
  printf ("New SPIRAL start at (%x,%x)\n", x, y);
#endif /* DEBUG */

  npnt->pnt.x = x;
  npnt->pnt.y = y;

  nobj = g_new0 (Dobject, 1);

  nobj->type = SPIRAL;
  nobj->type_data = 4; /* Default to four turns */
  nobj->points = npnt;
  nobj->drawfunc  = d_draw_spiral;
  nobj->loadfunc  = d_load_spiral;
  nobj->savefunc  = d_save_spiral;
  nobj->paintfunc = d_paint_spiral;
  nobj->copyfunc  = d_copy_spiral;

  return (nobj);
}

void
d_update_spiral (GdkPoint *pnt)
{
  DobjPoints *center_pnt, *edge_pnt;
  gint saved_cnt_pnt = selvals.opts.showcontrol;

  /* Undraw last one then draw new one */
  center_pnt = obj_creating->points;
  
  if (!center_pnt)
    return; /* No points */

  /* Leave the first pnt alone -
   * Edge point defines "radius"
   * Only undraw if already have edge point.
   */

  /* Hack - turn off cnt points in draw routine 
   * Looking back over the other update routines I could
   * use this trick again and cut down on code size!
   */

  if ((edge_pnt = center_pnt->next))
    {
      /* Undraw */
      draw_circle (&edge_pnt->pnt);
      selvals.opts.showcontrol = 0;
      d_draw_spiral (obj_creating);

      edge_pnt->pnt.x = pnt->x;
      edge_pnt->pnt.y = pnt->y;
    }
  else
    {
      /* Radius is a few pixels away */
      /* First edge point */
      d_pnt_add_line (obj_creating, pnt->x, pnt->y, -1);
      edge_pnt = center_pnt->next;
    }

  /* draw it */
  selvals.opts.showcontrol = 0;
  d_draw_spiral (obj_creating);
  selvals.opts.showcontrol = saved_cnt_pnt;

  /* Realy draw the control points */
  draw_circle (&edge_pnt->pnt);
}

/* first point is center 
 * next defines the radius
 */

void
d_spiral_start (GdkPoint *pnt,
		gint      shift_down)
{
  gint16 x, y;
  /* First is center point */
  obj_creating = d_new_spiral (x = pnt->x, y = pnt->y);
  obj_creating->type_data = spiral_num_turns * ((spiral_toggle == 0) ? 1 : -1);
}

void
d_spiral_end (GdkPoint *pnt,
	      gint     shift_down)
{
  draw_circle (pnt);
  add_to_all_obj (current_obj, obj_creating);
  obj_creating = NULL;
}
