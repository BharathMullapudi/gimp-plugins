/* Lighting Effects - A plug-in for GIMP
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

#ifndef __LIGHTING_PREVIEW_H__
#define __LIGHTING_PREVIEW_H__

#define PREVIEW_WIDTH  200
#define PREVIEW_HEIGHT 200

typedef struct
{
  gint      x, y, w, h;
  GdkImage *image;
} BackBuffer;

/* Externally visible variables */

extern gint        lightx, lighty;
extern BackBuffer  backbuf;
extern gdouble    *xpostab, *ypostab;
extern guint light_hit;
extern guint left_button_pressed;
extern GtkWidget * spin_pos_x;
extern GtkWidget * spin_pos_y;
extern GtkWidget * spin_pos_z;

/* Externally visible functions */

void draw_preview_image (gint recompute);
void update_preview_image(void);
gint preview_events (GtkWidget *area,
				     GdkEvent  *event);


gint check_marker_hit    (gint xpos,
			 gint ypos);
void update_light       (gint xpos,
			 gint ypos);

void preview_callback          (GtkWidget *widget);

#endif  /* __LIGHTING_PREVIEW_H__ */
