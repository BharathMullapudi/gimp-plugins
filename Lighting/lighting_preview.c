/*************************************************/
/* Compute a preview image and preview wireframe */
/*************************************************/

#include "config.h"

#include <libgimp/gimp.h>
#include <gdk/gdkimage.h>

#include <gck/gck.h>
#include <libgimpmath/gimpvector.h>

#include "lighting_main.h"
#include "lighting_ui.h"
#include "lighting_image.h"
#include "lighting_apply.h"
#include "lighting_shade.h"

#include "lighting_preview.h"

#define LIGHT_SYMBOL_SIZE 8

gint lightx, lighty;
BackBuffer backbuf = { 0, 0, 0, 0, NULL };

/* g_free()'ed on exit */
gdouble *xpostab = NULL;
gdouble *ypostab = NULL;

static gint xpostab_size = -1;	/* if preview size change, do realloc */
static gint ypostab_size = -1;

guint light_hit           = FALSE;
guint left_button_pressed = FALSE;
GtkWidget * spin_pos_x = NULL;
GtkWidget * spin_pos_y = NULL;
GtkWidget * spin_pos_z = NULL;
static guint preview_update_timer = 0;


/* Protos */
/* ====== */
static void
interactive_preview_callback (GtkWidget *widget);

static void
interactive_preview_timer_callback ( void );

static void
compute_preview (gint startx, gint starty, gint w, gint h)
{
	gint xcnt, ycnt, f1, f2;
	gdouble imagex, imagey;
	gint32 index = 0;
	GimpRGB color;
	GimpRGB lightcheck, darkcheck;
	GimpVector3 pos;
	get_ray_func ray_func;

	if (xpostab_size != w)
	{
		if (xpostab)
		{
			g_free (xpostab);
			xpostab = NULL;
		}
	}

	if (!xpostab)
	{
		xpostab = g_new (gdouble, w);
		xpostab_size = w;
	}

	if (ypostab_size != h)
	{
		if (ypostab)
		{
			g_free (ypostab);
			ypostab = NULL;
		}
	}
	if (!ypostab)
	{
		ypostab = g_new (gdouble, h);
		ypostab_size = h;
	}

	for (xcnt = 0; xcnt < w; xcnt++)
		xpostab[xcnt] =
			(gdouble) width *((gdouble) xcnt / (gdouble) w);
	for (ycnt = 0; ycnt < h; ycnt++)
		ypostab[ycnt] =
			(gdouble) height *((gdouble) ycnt / (gdouble) h);

	init_compute ();
	precompute_init (width, height);

	gimp_rgba_set (&lightcheck,
		       GIMP_CHECK_LIGHT, GIMP_CHECK_LIGHT, GIMP_CHECK_LIGHT,
		       1.0);
	gimp_rgba_set (&darkcheck, GIMP_CHECK_DARK, GIMP_CHECK_DARK,
		       GIMP_CHECK_DARK, 1.0);

	if (mapvals.bump_mapped == TRUE && mapvals.bumpmap_id != -1)
	{
		gimp_pixel_rgn_init (&bump_region,
				     gimp_drawable_get (mapvals.bumpmap_id),
				     0, 0, width, height, FALSE, FALSE);
	}

	imagey = 0;

	if (mapvals.previewquality)
		ray_func = get_ray_color;
	else
		ray_func = get_ray_color_no_bilinear;

	if (mapvals.env_mapped == TRUE && mapvals.envmap_id != -1)
	{
		env_width = gimp_drawable_width (mapvals.envmap_id);
		env_height = gimp_drawable_height (mapvals.envmap_id);

		gimp_pixel_rgn_init (&env_region,
				     gimp_drawable_get (mapvals.envmap_id), 0,
				     0, env_width, env_height, FALSE, FALSE);

		if (mapvals.previewquality)
			ray_func = get_ray_color_ref;
		else
			ray_func = get_ray_color_no_bilinear_ref;
	}

	for (ycnt = 0; ycnt < PREVIEW_HEIGHT; ycnt++)
	{
		for (xcnt = 0; xcnt < PREVIEW_WIDTH; xcnt++)
		{
			if ((ycnt >= starty && ycnt < (starty + h)) &&
			    (xcnt >= startx && xcnt < (startx + w)))
			{
				imagex = xpostab[xcnt - startx];
				imagey = ypostab[ycnt - starty];
				pos = int_to_posf (imagex, imagey);

				if (mapvals.bump_mapped == TRUE &&
				    mapvals.bumpmap_id != -1 &&
				    xcnt == startx)
				{
					pos_to_float (pos.x, pos.y, &imagex,
						      &imagey);
					precompute_normals (0, width,
							    RINT (imagey));
				}

				color = (*ray_func) (&pos);

				if (color.a < 1.0)
				{
					f1 = ((xcnt % 32) < 16);
					f2 = ((ycnt % 32) < 16);
					f1 = f1 ^ f2;

					if (f1)
					{
						if (color.a == 0.0)
							color = lightcheck;
						else
							gimp_rgb_composite
								(&color,
								 &lightcheck,
								 GIMP_RGB_COMPOSITE_BEHIND);
					}
					else
					{
						if (color.a == 0.0)
							color = darkcheck;
						else
							gimp_rgb_composite
								(&color,
								 &darkcheck,
								 GIMP_RGB_COMPOSITE_BEHIND);
					}
				}

				gimp_rgb_get_uchar (&color,
						    preview_rgb_data + index,
						    preview_rgb_data + index +
						    1,
						    preview_rgb_data + index +
						    2);
				index += 3;
				imagex++;
			}
			else
			{
				preview_rgb_data[index++] = 200;
				preview_rgb_data[index++] = 200;
				preview_rgb_data[index++] = 200;
			}
		}
	}

	gck_rgb_to_gdkimage (visinfo,
			     preview_rgb_data,
			     image, PREVIEW_WIDTH, PREVIEW_HEIGHT);
}

static void
compute_preview_rectangle (gint * xp, gint * yp, gint * wid, gint * heig)
{
	gdouble x, y, w, h;

	if (width >= height)
	{
		w = (PREVIEW_WIDTH - 50.0);
		h = (gdouble) height *(w / (gdouble) width);

		x = (PREVIEW_WIDTH - w) / 2.0;
		y = (PREVIEW_HEIGHT - h) / 2.0;
	}
	else
	{
		h = (PREVIEW_HEIGHT - 50.0);
		w = (gdouble) width *(h / (gdouble) height);

		x = (PREVIEW_WIDTH - w) / 2.0;
		y = (PREVIEW_HEIGHT - h) / 2.0;
	}

	*xp = RINT (x);
	*yp = RINT (y);
	*wid = RINT (w);
	*heig = RINT (h);
}

/*************************************************/
/* Check if the given position is within the     */
/* light marker. Return TRUE if so, FALSE if not */
/*************************************************/

gboolean
check_marker_hit (gint xpos, gint ypos)
{
	gdouble dxpos, dypos;
	gint lightx, lighty;
	gdouble dx, dy, r;
	gint startx, starty, pw, ph;
	GimpVector3 viewpoint;

	/* swap z to reverse light position */
	viewpoint = mapvals.viewpoint;
	viewpoint.z = -viewpoint.z;

	compute_preview_rectangle (&startx, &starty, &pw, &ph);

	gimp_vector_3d_to_2d (startx, starty, pw, ph, &dxpos, &dypos,
			      &viewpoint, &mapvals.lightsource.position);

	lightx = (gint) (dxpos + 0.5);
	lighty = (gint) (dypos + 0.5);

	dx = lightx - xpos;
	dy = lighty - ypos;


	if (mapvals.lightsource.type == POINT_LIGHT)
	{
		r = sqrt (dx * dx + dy * dy) + 0.5;

		if ((gint) r > 7)
		{
			return (FALSE);
		}
		else
		{
			return (TRUE);
		}

	}

	return FALSE;
}

/****************************************/
/* Draw a light symbol                  */
/****************************************/


static void
draw_lights ()
{
	gdouble dxpos, dypos;
	gint xpos, ypos;
	gint startx, starty, pw, ph;
	GimpVector3 viewpoint;
	GimpVector3 light_position;
	
	gfloat length;
	gfloat	delta_x = 0.0,
			delta_y = 0.0;

	/* swap z to reverse light position */
	viewpoint = mapvals.viewpoint;
	viewpoint.z = -viewpoint.z;

	compute_preview_rectangle (&startx, &starty, &pw, &ph);

	if (mapvals.lightsource.type == DIRECTIONAL_LIGHT)
	{
		light_position.x = light_position.y = 0.5;
		light_position.z = 0.0;
	} else
	{
		light_position = mapvals.lightsource.position;
	}
	
	gimp_vector_3d_to_2d (startx, starty, pw, ph, &dxpos, &dypos,
			      &viewpoint, &light_position);

	xpos = (gint) (dxpos + 0.5);
	ypos = (gint) (dypos + 0.5);

	compute_preview_rectangle (&startx, &starty, &pw, &ph);

	gdk_gc_set_function (gc, GDK_COPY);

	if (mapvals.lightsource.type != NO_LIGHT)
	{
		/* Restore background if it has been saved */
		/* ======================================= */

		if (backbuf.image != NULL)
		{
			gdk_gc_set_function (gc, GDK_COPY);
			gdk_draw_image (previewarea->window, gc,
					backbuf.image, 0, 0, backbuf.x,
					backbuf.y, backbuf.w, backbuf.h);
			gdk_image_unref (backbuf.image);
			backbuf.image = NULL;
		}

		/* calculate symbol size */
		switch (mapvals.lightsource.type)
		{
			case  POINT_LIGHT:
				backbuf.x = xpos - LIGHT_SYMBOL_SIZE / 2;
				backbuf.y = ypos - LIGHT_SYMBOL_SIZE / 2;
				backbuf.w = LIGHT_SYMBOL_SIZE;
				backbuf.h = LIGHT_SYMBOL_SIZE;
				break;
			case  DIRECTIONAL_LIGHT:
				length = sqrt(	(mapvals.lightsource.direction.x * mapvals.lightsource.direction.x) + 
								(mapvals.lightsource.direction.y * mapvals.lightsource.direction.y));
				length = (1.0 / length) * (PREVIEW_HEIGHT/4.0);
				delta_x = mapvals.lightsource.direction.x * length;
				delta_y = mapvals.lightsource.direction.y * length;
				backbuf.x = xpos - fabs(delta_x);
				backbuf.y = ypos - fabs(delta_y);
				backbuf.w = fabs(delta_x * 2.0);
				backbuf.h = fabs(delta_y * 2.0);
				break;
			case  SPOT_LIGHT:
			case  NO_LIGHT:
				backbuf.x = xpos - LIGHT_SYMBOL_SIZE / 2;
				backbuf.y = ypos - LIGHT_SYMBOL_SIZE / 2;
				backbuf.w = LIGHT_SYMBOL_SIZE;
				backbuf.h = LIGHT_SYMBOL_SIZE;
				break;
		}

		/* Save background */
		/* =============== */
		if ((backbuf.x >= 0) &&
		    (backbuf.x <= PREVIEW_WIDTH) &&
		    (backbuf.y >= 0) && (backbuf.y <= PREVIEW_HEIGHT))
		{

			/* clip coordinates to preview widget sizes */

			if ((backbuf.x + backbuf.w) > PREVIEW_WIDTH)
				backbuf.w = (PREVIEW_WIDTH - backbuf.x);

			if ((backbuf.y + backbuf.h) > PREVIEW_HEIGHT)
				backbuf.h = (PREVIEW_HEIGHT - backbuf.y);

			backbuf.image =
				gdk_image_get (previewarea->window, backbuf.x,
					       backbuf.y, backbuf.w,
					       backbuf.h);
		}

		gck_gc_set_background (visinfo, gc, 0, 0, 0);
		gck_gc_set_foreground (visinfo, gc, 0, 50, 255);


		/* draw circle at light position */
		switch (mapvals.lightsource.type)
		{
			case  	POINT_LIGHT:
			case 	SPOT_LIGHT:
				gdk_draw_arc (	previewarea->window, gc, TRUE,
								xpos - LIGHT_SYMBOL_SIZE / 2,
							    ypos - LIGHT_SYMBOL_SIZE / 2,
								LIGHT_SYMBOL_SIZE,
			                    LIGHT_SYMBOL_SIZE, 0, 360 * 64);
				break;
			case DIRECTIONAL_LIGHT:
				gdk_draw_arc (	previewarea->window, gc, TRUE,
								xpos - LIGHT_SYMBOL_SIZE / 2,
							    ypos - LIGHT_SYMBOL_SIZE / 2,
								LIGHT_SYMBOL_SIZE,
								LIGHT_SYMBOL_SIZE, 0, 360 * 64);
				gdk_draw_line( previewarea->window, gc, 
								xpos, ypos, xpos+delta_x, ypos+delta_y);
				break;
			case NO_LIGHT:
				break;
		
		}
	}
}


/*************************************************/
/* Update light position given new screen coords */
/*************************************************/

void
update_light (gint xpos, gint ypos)
{
	gint startx, starty, pw, ph;
	GimpVector3 vp;

	compute_preview_rectangle (&startx, &starty, &pw, &ph);

	vp = mapvals.viewpoint;
	vp.z = -vp.z;

	gimp_vector_2d_to_3d (startx,
			      starty,
			      pw,
			      ph,
			      xpos, ypos, &vp, &mapvals.lightsource.position);
}


/******************************************************************/
/* Draw preview image. if DoCompute is TRUE then recompute image. */
/******************************************************************/

void
draw_preview_image (gint recompute)
{
	gint startx, starty, pw, ph;

	gck_gc_set_foreground (visinfo, gc, 255, 255, 255);
	gck_gc_set_background (visinfo, gc, 0, 0, 0);

	gdk_gc_set_function (gc, GDK_COPY);

	compute_preview_rectangle (&startx, &starty, &pw, &ph);

	if (recompute == TRUE)
	{
		GdkCursor *newcursor;

		newcursor = gdk_cursor_new (GDK_WATCH);
		gdk_window_set_cursor (previewarea->window, newcursor);
		gdk_cursor_unref (newcursor);
		gdk_flush ();

		compute_preview (startx, starty, pw, ph);

		newcursor = gdk_cursor_new (GDK_HAND2);
		gdk_window_set_cursor (previewarea->window, newcursor);
		gdk_cursor_unref (newcursor);
		gdk_flush ();
		/* if we recompute, clear backbuf, so we don't 
		 * restore the wrong bitmap */
		if (backbuf.image != NULL)
		{
			gdk_image_unref (backbuf.image);
			backbuf.image = NULL;
		}


	}

	gdk_draw_image (previewarea->window, gc, image,
			0, 0, 0, 0, PREVIEW_WIDTH, PREVIEW_HEIGHT);

	/* draw symbols if enabled in UI */
	if (mapvals.interactive_preview)
	{
		draw_lights ();
	}

}

/******************************/
/* Preview area event handler */
/******************************/

gint
preview_events (GtkWidget *area,
		GdkEvent  *event)
{

  switch (event->type)
    {
      case GDK_EXPOSE:

        /* Is this the first exposure? */
        /* =========================== */

        if (!gc)
          {
            gc = gdk_gc_new (area->window);
            draw_preview_image (TRUE);
          }
        else
          draw_preview_image (FALSE);
        break; 
      case GDK_ENTER_NOTIFY:
        break;
      case GDK_LEAVE_NOTIFY:
        break;
      case GDK_BUTTON_PRESS:
        light_hit = check_marker_hit (event->button.x, event->button.y);
        left_button_pressed = TRUE;
        break;
      case GDK_BUTTON_RELEASE:
        left_button_pressed = FALSE;
        break;
      case GDK_MOTION_NOTIFY:
        if (left_button_pressed == TRUE && light_hit == TRUE) {
          interactive_preview_callback(NULL);
	  update_light (event->motion.x, event->motion.y);
	  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_pos_x), mapvals.lightsource.position.x);
	  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_pos_y), mapvals.lightsource.position.y);
	  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_pos_z), mapvals.lightsource.position.z);
	}

        break;
      default:
        break;
    }

  return FALSE;
}

static void
interactive_preview_callback (GtkWidget *widget)
{
 
    if ( preview_update_timer != 0)
      {
	gtk_timeout_remove ( preview_update_timer );
      }
    /* start new timer */
    preview_update_timer = gtk_timeout_add(100, (GtkFunction) interactive_preview_timer_callback, NULL);
}

static void
interactive_preview_timer_callback ( void )
{

  gtk_timeout_remove ( preview_update_timer );

  if (mapvals.interactive_preview)
  {
    draw_preview_image (TRUE);
  } else {
     draw_preview_image(FALSE);
  }
}
void update_preview_image(void)
{
	interactive_preview_callback(NULL);
}



