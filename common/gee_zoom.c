/*
 * (c) Adam D. Moss : 1998-2000 : adam@gimp.org : adam@foxbox.org
 *
 * This is part of the GIMP package and is released under the GNU
 * Public License.
 */

/*
 * Version 1.06 : 2000-12-12
 *
 * 1.06:
 * "Out of hyding" remix.  Dr Jekyll is still suspiciously
 * absent from the fine bogey tale until Chapter Three.
 *
 * 1.05:
 * Sub-pixel jitter is now less severe and less coarse.
 *
 * 1.04:
 * Wigglyness and button-click fun.
 *
 * 1.03:
 * Fix for pseudocolor displays w/gdkrgb.
 *
 * 1.02:
 * Massive speedup if you have a very recent version of GTK 1.1.
 * Removed possible div-by-0 errors, took the plugin out
 * of hiding (guess we need a new easter-egg for GIMP 1.2!)
 */
#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


/* Test for GTK1.2-style gdkrgb code, else use old 'preview' code. */
#ifdef __GDK_RGB_H__
#define RAPH_IS_HOME yep
#endif



/* Declare local functions. */
static void query (void);
static void run   (gchar      *name,
		   gint        nparams,
		   GimpParam  *param,
		   gint       *nreturn_vals,
		   GimpParam **return_vals);

static void do_fun            (void);

static gint window_delete_callback (GtkWidget *widget,
				    GdkEvent  *event,
				    gpointer   data);
static void window_close_callback  (GtkWidget *widget,
				    gpointer   data);
static gint iteration_callback          (gpointer   data);
static void toggle_feedbacktype    (GtkWidget *widget,
				    gpointer   data);

static void render_frame           (void);
static void show_frame             (void);
static void init_preview_misc      (void);


GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};


static const guint  width = 256;
static const guint height = 256;


#define LUTSIZE 512
#define LUTSIZEMASK ((LUTSIZE)-1)
static gint wigglelut[LUTSIZE];

#define LOWAMP 2
#define HIGHAMP 11
static gint wiggleamp = LOWAMP;


/* Global widgets'n'stuff */
static guchar     *seed_data;
static guchar     *preview_data1;
static guchar     *preview_data2;
#ifdef RAPH_IS_HOME
static GtkWidget  *drawing_area;
#else
static GtkPreview *preview = NULL;
#endif
static gint32      image_id;
static GimpDrawable      *drawable;
static GimpImageBaseType  imagetype;
static guchar     *palette;
static gint        ncolours;

static gint       idle_tag;
static GtkWidget *eventbox;
static gboolean   feedbacktype = FALSE;
static gboolean   wiggly = TRUE;
static gboolean   rgb_mode;


MAIN ()

static void
query (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Must be interactive (1)" },
    { GIMP_PDB_IMAGE, "image", "Input Image" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input Drawable" },
  };
  static gint nargs = sizeof (args) / sizeof (args[0]);

  gimp_install_procedure("plug_in_the_old_egg",
			 "A big hello from the GIMP team!",
			 "Hay-ulp",
			 "Adam D. Moss <adam@gimp.org>",
			 "Adam D. Moss <adam@gimp.org>",
			 "1998",
			 "<Image>/Filters/Toys/Gee-Zoom",
			 "RGB*, INDEXED*, GRAY*",
			 GIMP_PLUGIN,
			 nargs, 0,
			 args, NULL);
}

static void
run (gchar      *name,
     gint        n_params,
     GimpParam  *param, 
     gint       *nreturn_vals,
     GimpParam **return_vals)
{
  static GimpParam  values[1];
  GimpRunModeType   run_mode;
  GimpPDBStatusType status = GIMP_PDB_SUCCESS;

  *nreturn_vals = 1;
  *return_vals = values;

  run_mode = param[0].data.d_int32;

  INIT_I18N_UI();

  if (run_mode == GIMP_RUN_NONINTERACTIVE ||
      n_params != 3)
    {
      status = GIMP_PDB_CALLING_ERROR;
    }
  
  if (status == GIMP_PDB_SUCCESS)
    {
      image_id = param[1].data.d_image;
      drawable = gimp_drawable_get (param[2].data.d_drawable);

      if (drawable)
	do_fun();
      else
	status = GIMP_PDB_CALLING_ERROR;
    }

  values[0].type = GIMP_PDB_STATUS;
  values[0].data.d_status = status;
}


static void
build_dialog (GimpImageBaseType  basetype,
	      gchar             *imagename)
{
  GtkWidget *dlg;
  GtkWidget *button;
  GtkWidget *frame;
  GtkWidget *frame2;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *hbox2;
  GtkTooltips *tooltips;

  gimp_ui_init ("gee_zoom", TRUE);

  dlg = gtk_dialog_new ();
  gtk_window_set_title (GTK_WINDOW (dlg),
			_("GEE-ZOOM: The Plug-In Formerly Known As \"The GIMP E'er Egg\""));

  gtk_window_set_position (GTK_WINDOW (dlg), GTK_WIN_POS_MOUSE);
  gtk_signal_connect (GTK_OBJECT (dlg), "delete_event",
		      (GtkSignalFunc) window_delete_callback,
		      NULL);

  gimp_help_connect_help_accel (dlg, gimp_standard_help_func, "filters/gee.html");

  /* Action area - 'close' button only. */

  button = gtk_button_new_with_label (_("** Thank you for choosing GIMP **"));
  GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
			     (GtkSignalFunc) window_close_callback,
			     GTK_OBJECT (dlg));
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->action_area),
		      button, TRUE, TRUE, 0);
  gtk_widget_grab_default (button);
  gtk_widget_show (button);

  tooltips = gtk_tooltips_new();
  gtk_tooltips_set_tip(GTK_TOOLTIPS(tooltips), button,
		       _("An obsolete creation of Adam D. Moss / adam@gimp.org / adam@foxbox.org / 1998-2000"),
		       NULL);
  gtk_tooltips_enable (tooltips);

  /* The 'fun' half of the dialog */
    
  frame = gtk_frame_new (NULL);

  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
  gtk_container_set_border_width (GTK_CONTAINER (frame), 3);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), frame, TRUE, TRUE, 0);

  hbox = gtk_hbox_new (FALSE, 5);
  gtk_container_set_border_width (GTK_CONTAINER (hbox), 3);
  gtk_container_add (GTK_CONTAINER (frame), hbox);

  vbox = gtk_vbox_new (FALSE, 5);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 3);
  gtk_container_add (GTK_CONTAINER (hbox), vbox);

  hbox2 = gtk_hbox_new (TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (hbox2), 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox2, FALSE, FALSE, 0);

  frame2 = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame2), GTK_SHADOW_ETCHED_IN);
  gtk_box_pack_start (GTK_BOX (hbox2), frame2, FALSE, FALSE, 0);

  eventbox = gtk_event_box_new();
  gtk_container_add (GTK_CONTAINER (frame2), GTK_WIDGET (eventbox));

#ifdef RAPH_IS_HOME
  drawing_area = gtk_drawing_area_new ();
  gtk_widget_set_usize (drawing_area, width, height);
  gtk_container_add (GTK_CONTAINER (eventbox), drawing_area);
  gtk_widget_show (drawing_area);
#else
  preview = GTK_PREVIEW (gtk_preview_new (rgb_mode ?
					  GTK_PREVIEW_COLOR :
					  GTK_PREVIEW_GRAYSCALE));
  gtk_preview_size (preview, width, height);
  gtk_container_add (GTK_CONTAINER (eventbox), GTK_WIDGET (preview));
  gtk_widget_show (GTK_WIDGET (preview));
#endif /* RAPH_IS_HOME */

  gtk_widget_show (eventbox);
  gtk_widget_set_events (eventbox,
			 gtk_widget_get_events (eventbox)
			 | GDK_BUTTON_RELEASE_MASK);

  gtk_widget_show (frame2);

  gtk_widget_show (hbox2);

  gtk_widget_show (vbox);

  gtk_widget_show (hbox);

  gtk_widget_show (frame);

  gtk_widget_show (dlg);
	    
  idle_tag = gtk_idle_add_priority (GTK_PRIORITY_LOW,
				    (GtkFunction) iteration_callback,
				    NULL);
  
  gtk_signal_connect (GTK_OBJECT (eventbox), "button_release_event",
		      GTK_SIGNAL_FUNC (toggle_feedbacktype),
		      NULL);
}


static void 
init_lut (void)
{
  gint i;

  for (i=0; i<LUTSIZE; i++)
    {
      wigglelut[i] = RINT((double)(wiggleamp<<11))*(sin((double)(i) /
					    ((double)LUTSIZEMASK /
					     10 * G_PI)));
    }
}


static void 
do_fun (void)
{
  imagetype = gimp_image_base_type(image_id);

  if (imagetype == GIMP_INDEXED)
    palette = gimp_image_get_cmap(image_id, &ncolours);

  /* cache hint */
  gimp_tile_cache_ntiles (1);

  init_preview_misc();
  build_dialog(gimp_image_base_type(image_id),
               gimp_image_get_filename(image_id));

  init_lut();
  
  render_frame();
  show_frame();

  gtk_main ();
  gdk_flush ();
}


/* Rendering Functions */

/* Adam's silly algorithm. */
static void 
domap1 (unsigned char *src, unsigned char *dest,
	int bx, int by, int cx, int cy)
{
  unsigned int dy;
  signed int bycxmcybx;
  signed int bx2,by2;
  signed int cx2,cy2;
  unsigned int basesx;
  unsigned int basesy;

  static unsigned int grrr=0;

  grrr++;

  if ((cx+bx) == 0)
    cx++;

  if ((cy+by) == 0)
    by++;

  bycxmcybx = (by*cx-cy*bx);

  if (bycxmcybx == 0)
    bycxmcybx = 1;

  /* A little sub-pixel jitter to liven things up. */
  basesx = (((RAND_FUNC ()%(29<<19))/bycxmcybx)) + ((-128-((128*256)/(cx+bx)))<<11);
  basesy = (((RAND_FUNC ()%(29<<19))/bycxmcybx)) + ((-128-((128*256)/(cy+by)))<<11);

  bx2 = ((bx)<<19)/bycxmcybx;
  cx2 = ((cx)<<19)/bycxmcybx;
  by2 = ((by)<<19)/bycxmcybx;
  cy2 = ((cy)<<19)/bycxmcybx;

  for (dy=0;dy<256;dy++)
    {
      unsigned int sx;
      unsigned int sy;
      unsigned int dx;

      sy = (basesy+=cx2);
      sx = (basesx-=bx2);

      if (wiggly)
	{
	  sx += wigglelut[(((basesy)>>11)+grrr) & LUTSIZEMASK];
	  sy += wigglelut[(((basesx)>>11)+(grrr/3)) & LUTSIZEMASK];
	}

      dx = 256;
      do
	{
	  *dest++ = (*(src +
		   (
		    (
		     ((255&(
			    (sx>>11)
			    )))
		     |
		     ((((255&(
			      (sy>>11)
			      ))<<8)))
		     )
		    )));
	  ;
	  sx += by2;
	  sy -= cy2;
	}
      while (--dx);
    }
}

/* 3bypp variant */
static void 
domap3(unsigned char *src, unsigned char *dest,
       int bx, int by, int cx, int cy)
{
  unsigned int dy;
  signed int bycxmcybx;
  signed int bx2,by2;
  signed int cx2,cy2;
  unsigned int basesx;
  unsigned int basesy;

  static unsigned int grrr=0;

  grrr++;

  if ((cx+bx) == 0)
    cx++;

  if ((cy+by) == 0)
    by++;

  bycxmcybx = (by*cx-cy*bx);

  if (bycxmcybx == 0)
    bycxmcybx = 1;

  /* A little sub-pixel jitter to liven things up. */
  basesx = (((RAND_FUNC ()%(29<<19))/bycxmcybx)) + ((-128-((128*256)/(cx+bx)))<<11);
  basesy = (((RAND_FUNC ()%(29<<19))/bycxmcybx)) + ((-128-((128*256)/(cy+by)))<<11);

  bx2 = ((bx)<<19)/bycxmcybx;
  cx2 = ((cx)<<19)/bycxmcybx;
  by2 = ((by)<<19)/bycxmcybx;
  cy2 = ((cy)<<19)/bycxmcybx;

  for (dy=0;dy<256;dy++)
    {
      unsigned int sx;
      unsigned int sy;
      unsigned int dx; 
      
      sy = (basesy+=cx2);
      sx = (basesx-=bx2);

      if (wiggly)
	{
	  sx += wigglelut[(((basesy)>>11)+grrr) & LUTSIZEMASK];
	  sy += wigglelut[(((basesx)>>11)+(grrr/3)) & LUTSIZEMASK];
	}
      
      dx = 256;
      
      do
	{
	  unsigned char* addr;
	  
	  addr = src + 3*
	    (
	     (
	      ((255&(
		     (sx>>11)
		     )))
	      |
	      ((((255&(
		       (sy>>11)
		       ))<<8)))
	      )
	     );
	  
	  *dest++ = *(addr);
	  *dest++ = *(addr+1);
	  *dest++ = *(addr+2);
	  
	  sx += by2;
	  sy -= cy2;
	}
      while (--dx);
    }
}


static void
render_frame (void)
{
  int i;
  static int frame = 0;
  unsigned char* tmp;
  static gint xp=128, yp=128;
  gint rxp, ryp;
  GdkModifierType mask;
  gint pixels;

  pixels = width*height*(rgb_mode?3:1);

#ifdef RAPH_IS_HOME
#else
  gdk_flush();
#endif

  tmp = preview_data2;
  preview_data2 = preview_data1;
  preview_data1 = tmp;

  if (frame==0)
    {
      for (i=0;i<pixels;i++)
	{
	  preview_data2[i] =
	    preview_data1[i] =
	    seed_data[i];
	}
    }

  gdk_window_get_pointer (eventbox->window, &rxp, &ryp, &mask);

  if ((abs(rxp)>60)||(abs(ryp)>60))
    {
      xp = rxp;
      yp = ryp;
    }

  if (rgb_mode)
    {
      domap3(preview_data2, preview_data1,
	     -(yp-xp)/2, xp+yp
	     ,
	     xp+yp, (yp-xp)/2
	     );

#ifdef RAPH_IS_HOME
      gdk_draw_rgb_image (drawing_area->window,
			  drawing_area->style->white_gc,
			  0, 0, width, height,
			  GDK_RGB_DITHER_NORMAL,
			  preview_data1, width * 3);
#else
      for (i=0;i<height;i++)
	{
	  gtk_preview_draw_row (preview,
				&preview_data1[i*width*3],
				0, i, width);
	}
#endif

      /*      memcpy(preview_data1, seed_data, 256*256*3); */

      if (frame != 0)
	{
	  if (feedbacktype)
	    {
	      for (i=0;i<pixels;i++)
		{
		  gint t;
		  t = preview_data1[i] + seed_data[i] - 128;
		  preview_data1[i] = /*CLAMP(t,0,255);*/
		    (t&256)? (~(t>>10)) : t; /* Quick specialized clamp */
		}
	    }
	  else/* if (0) */
	    {
	      gint pixwords = pixels/sizeof(gint32);
	      gint32* seedwords = (gint32*) seed_data;
	      gint32* prevwords = (gint32*) preview_data1;

	      for (i=0;i<pixwords;i++)
		{
		  /*preview_data1[i] = (preview_data1[i]*2 +
		    seed_data[i]) /3;*/

		  /* mod'd version of the below for a 'deeper' mix */
		  prevwords[i] =
		    ((prevwords[i] >> 1) & 0x7f7f7f7f) +
		    ((prevwords[i] >> 2) & 0x3f3f3f3f) +
		    ((seedwords[i] >> 2) & 0x3f3f3f3f);
		  /* This is from Raph L... it should be a fast 50%/50%
		     blend, though I don't know if 50%/50% is as nice as
		     the old ratio. */
		  /*
		    prevwords[i] =
		    ((prevwords[i] >> 1) & 0x7f7f7f7f) +
		    ((seedwords[i] >> 1) & 0x7f7f7f7f) +
		    (prevwords[i] & seedwords[i] & 0x01010101); */
		}
	    }	
	}
    }
  else /* GRAYSCALE */
    {
      domap1(preview_data2, preview_data1,
	     -(yp-xp)/2, xp+yp
	     ,
	     xp+yp, (yp-xp)/2
	     );

#ifdef RAPH_IS_HOME
      gdk_draw_gray_image (drawing_area->window,
			   drawing_area->style->white_gc,
			   0, 0, width, height,
			   GDK_RGB_DITHER_NORMAL,
			   preview_data1, width);
#else
      for (i=0;i<height;i++)
	{
	  gtk_preview_draw_row (preview,
				&preview_data1[i*width],
				0, i, width);
	}
#endif

      if (frame != 0)
	{
	  if (feedbacktype)
	    {
	      for (i=0;i<pixels;i++)
		{
		  int t;
		  t = preview_data1[i] + seed_data[i] - 128;
		  preview_data1[i] = /*CLAMP(t,0,255);*/
		    (t&256)? (~(t>>10)) : t; /* Quick specialized clamp */
		}
	    }
	  else
	    {
	      gint pixwords = pixels/sizeof(gint32);
	      gint32* seedwords = (gint32*) seed_data;
	      gint32* prevwords = (gint32*) preview_data1;

	      for (i=0;i<pixwords;i++)
		{

		  /* mod'd version of the below for a 'deeper' mix */
		  prevwords[i] =
		    ((prevwords[i] >> 1) & 0x7f7f7f7f) +
		    ((prevwords[i] >> 2) & 0x3f3f3f3f) +
		    ((seedwords[i] >> 2) & 0x3f3f3f3f);
		  /* This is from Raph L... it should be a fast 50%/50%
		     blend, though I don't know if 50%/50% is as nice as
		     the old ratio. */
		  /*
		    prevwords[i] =
		    ((prevwords[i] >> 1) & 0x7f7f7f7f) +
		    ((seedwords[i] >> 1) & 0x7f7f7f7f) +
		    (prevwords[i] & seedwords[i] & 0x01010101); */
		}
	    }	
	}
    }

  frame++;
}


static void
show_frame (void)
{
#ifdef RAPH_IS_HOME
#else
  /* Tell GTK to physically draw the preview */
  gtk_widget_draw (GTK_WIDGET (preview), NULL);
#endif /* RAPH_IS_HOME */
}


static void
init_preview_misc (void)
{
  GimpPixelRgn pixel_rgn;
  gint i;
  gboolean has_alpha;

  if ((imagetype == GIMP_RGB) || (imagetype == GIMP_INDEXED))
    rgb_mode = TRUE;
  else
    rgb_mode = FALSE;

  has_alpha = gimp_drawable_has_alpha(drawable->id);

  seed_data = g_malloc(width*height*4);
  preview_data1 = g_malloc(width*height*(rgb_mode?3:1));
  preview_data2 = g_malloc(width*height*(rgb_mode?3:1));

  if ((drawable->width<256) || (drawable->height<256))
    {
      for (i=0;i<256;i++)
	{
	  if (i < drawable->height)
	    {
	      gimp_pixel_rgn_init (&pixel_rgn,
				   drawable,
				   drawable->width>256?
				   (drawable->width/2-128):0,
				   (drawable->height>256?
				   (drawable->height/2-128):0)+i,
				   MIN(256,drawable->width),
				   1,
				   FALSE,
				   FALSE);
	      gimp_pixel_rgn_get_rect (&pixel_rgn,
				       &seed_data[(256*i +
						 (
						  (
						   drawable->width<256 ?
						   (256-drawable->width)/2 :
						   0
						   )
						  +
						  (
						   drawable->height<256 ?
						   (256-drawable->height)/2 :
						   0
						   ) * 256
						  )) *
						 gimp_drawable_bpp
						 (drawable->id)
				       ],
				       drawable->width>256?
				       (drawable->width/2-128):0,
				       (drawable->height>256?
				       (drawable->height/2-128):0)+i,
				       MIN(256,drawable->width),
				       1);
	    }
	}
    }
  else
    {
      gimp_pixel_rgn_init (&pixel_rgn,
			   drawable,
			   drawable->width>256?(drawable->width/2-128):0,
			   drawable->height>256?(drawable->height/2-128):0,
			   MIN(256,drawable->width),
			   MIN(256,drawable->height),
			   FALSE,
			   FALSE);
      gimp_pixel_rgn_get_rect (&pixel_rgn,
			       seed_data,
			       drawable->width>256?(drawable->width/2-128):0,
			       drawable->height>256?(drawable->height/2-128):0,
			       MIN(256,drawable->width),
			       MIN(256,drawable->height));
    }

  gimp_drawable_detach(drawable);


  /* convert the image data of varying types into flat grey or rgb. */
  switch (imagetype)
    {
    case GIMP_INDEXED:
      if (has_alpha)
	{
	  for (i=width*height;i>0;i--)
	    {
	      seed_data[3*(i-1)+2] =
		((palette[3*(seed_data[(i-1)*2])+2]*seed_data[(i-1)*2+1])/255)
		+ ((255-seed_data[(i-1)*2+1])*(RAND_FUNC ()%256))/255;
	      seed_data[3*(i-1)+1] =
		((palette[3*(seed_data[(i-1)*2])+1]*seed_data[(i-1)*2+1])/255)
		+ ((255-seed_data[(i-1)*2+1])*(RAND_FUNC ()%256))/255;
	      seed_data[3*(i-1)+0] =
		((palette[3*(seed_data[(i-1)*2])+0]*seed_data[(i-1)*2+1])/255)
		+ ((255-seed_data[(i-1)*2+1])*(RAND_FUNC ()%256))/255;
	    }
	}
      else
	{
	  for (i=width*height;i>0;i--)
	    {
	      seed_data[3*(i-1)+2] = palette[3*(seed_data[i-1])+2];
	      seed_data[3*(i-1)+1] = palette[3*(seed_data[i-1])+1];
	      seed_data[3*(i-1)+0] = palette[3*(seed_data[i-1])+0];
	    }
	}
      break;

    case GIMP_GRAY:
      if (has_alpha)
	{
	  for (i=0;i<width*height;i++)
	    {
	      seed_data[i] =
		(seed_data[i*2]*seed_data[i*2+1])/255
		+ ((255-seed_data[i*2+1])*(RAND_FUNC ()%256))/255;
	    }
	}
      break;

    case GIMP_RGB:
      if (has_alpha)
	{
	  for (i=0;i<width*height;i++)
	    {
	      seed_data[i*3+2] =
		(seed_data[i*4+2]*seed_data[i*4+3])/255
		+ ((255-seed_data[i*4+3])*(RAND_FUNC ()%256))/255;
	      seed_data[i*3+1] =
		(seed_data[i*4+1]*seed_data[i*4+3])/255
		+ ((255-seed_data[i*4+3])*(RAND_FUNC ()%256))/255;
	      seed_data[i*3+0] =
		(seed_data[i*4+0]*seed_data[i*4+3])/255
		+ ((255-seed_data[i*4+3])*(RAND_FUNC ()%256))/255;
	    }
	}
      break;

    default:
      break;
    }
}



/* Util. */

static int
do_iteration (void)
{
  render_frame ();

  return 1;
}



/*  Callbacks  */

static gint
window_delete_callback (GtkWidget *widget,
		        GdkEvent  *event,
		        gpointer   data)
{
  gtk_idle_remove (idle_tag);

  gdk_flush ();
  gtk_main_quit ();

  return FALSE;
}

static void
window_close_callback (GtkWidget *widget,
                       gpointer   data)
{
  if (data)
    gtk_widget_destroy(GTK_WIDGET(data));

  window_delete_callback (NULL, NULL, NULL);
}

static void
toggle_feedbacktype (GtkWidget *widget,
		     gpointer   event)
{
  GdkEventButton *bevent = (GdkEventButton *) event;

  if (bevent->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK))
    {
      wiggleamp = bevent->x/5;
      
      wiggly = TRUE;
      init_lut();

      return;
    }

  if (bevent->state & GDK_BUTTON1_MASK)
    feedbacktype = !feedbacktype;

  if (bevent->state & GDK_BUTTON2_MASK)
    wiggly = !wiggly;

  if (bevent->state & GDK_BUTTON3_MASK)
    {
      if (wiggleamp == LOWAMP)
	wiggleamp = HIGHAMP;
      else
	wiggleamp = LOWAMP;

      wiggly = TRUE;
      init_lut();
    }
}


static gint
iteration_callback (gpointer   data)
{
  do_iteration();
  show_frame();

  return TRUE;
}
