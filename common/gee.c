/*
 * (c) Adam D. Moss : 1998-2000 : adam@gimp.org : adam@foxbox.org
 *
 * Enjoy.
 */

/*
 * Version 1.01 : 2000-12-12
 *
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


/* Is the plug-in hidden?  Hey, if you can read this, you may
   as well comment-out the next line...! */
#define HIDDEN


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
static void init_preview_misc      (void);


GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};


/* These aren't really redefinable, easily. */
#define IWIDTH 256
#define IHEIGHT 256


/* Global widgets'n'stuff */
static guchar     *disp;      /* RGBX preview data      */
static guchar     *env;	      /* src warping image data */
static guchar     *bump1base;
static guchar     *bump1;
static guchar     *bump2base;
static guchar     *bump2;
static guchar     *srcbump;
static guchar     *destbump;

static gint       idle_tag;
static GtkWidget *eventbox;
static GtkWidget  *drawing_area;

static gint32      image_id;
static GimpDrawable      *drawable;
static GimpImageBaseType  imagetype;
static guchar     *palette;
static gint        ncolours;


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

  gimp_install_procedure("plug_in_the_slimy_egg",
			 "A big hello from the GIMP team!",
			 "Beyond help.",
			 "Adam D. Moss <adam@gimp.org>",
			 "Adam D. Moss <adam@gimp.org>",
			 "2000",
#ifdef HIDDEN
			 NULL,
#else
			 "<Image>/Filters/Toys/Gee-Slime",
#endif
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

#if 0
      fprintf(stderr, "Got these: %d, %d, %d(%p)\n",
	      (int)param[0].data.d_int32,
	      (int)param[1].data.d_image,
	      (int)param[2].data.d_drawable,
	      drawable
	      );
#endif

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

  gimp_ui_init ("gee", TRUE);

  dlg = gtk_dialog_new ();
  gtk_window_set_title (GTK_WINDOW (dlg),
			_("GEE-SLIME"));

  gtk_window_set_position (GTK_WINDOW (dlg), GTK_WIN_POS_MOUSE);
  gtk_signal_connect (GTK_OBJECT (dlg), "delete_event",
		      (GtkSignalFunc) window_delete_callback,
		      NULL);

  gimp_help_connect_help_accel (dlg, gimp_standard_help_func, "filters/geeslime.html");

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
		       _("A less-obsolete creation of Adam D. Moss / adam@gimp.org / adam@foxbox.org / 1998-2000"),
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

  drawing_area = gtk_drawing_area_new ();
  gtk_widget_set_usize (drawing_area, IWIDTH, IHEIGHT);
  gtk_container_add (GTK_CONTAINER (eventbox), drawing_area);
  gtk_widget_show (drawing_area);

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


/* #define LIGHT 0x19
#define LIGHT 0x1a
#define LIGHT 0x21 */
#define LIGHT 0x0d
static guchar llut[256];
static void
gen_llut(void)
{
  int i,k;

  for (i=0; i<256; i++)
    {
      /* k = i + RINT (((double)LIGHT) * pow(((double)i / 255.0), 0.5)); 
	 k = i + ((LIGHT*i)/255); */
      k = i + ((LIGHT*( /* (255*255)- */ i*i))/(255*255));
#if 0
      k = i + ((LIGHT*( /* (255*255*255)- */ i*i*i))/(255*255*255));
#endif
      k = k + 8;
      k = (k>255 ? 255 : k);
      llut[i] = k;
    }
}


static void 
do_fun (void)
{
  imagetype = gimp_image_base_type(image_id);

  if (imagetype == GIMP_INDEXED)
    palette = gimp_image_get_cmap(image_id, &ncolours);
  else
    if (imagetype == GIMP_GRAY)
      {
	int i;
	palette = g_malloc(256 * 3);
	for (i=0; i<256; i++)
	  {
	    palette[i*3+0] =
	      palette[i*3+1] =
	      palette[i*3+2] = i;
	  }
      }

  /* cache hint */
  gimp_tile_cache_ntiles (1);

  init_preview_misc();
  build_dialog(gimp_image_base_type(image_id),
               gimp_image_get_filename(image_id));

  gen_llut();

  render_frame();

  gtk_main ();
  gdk_flush ();
}


static void
show(void)
{
  gdk_draw_rgb_32_image (drawing_area->window,
			 drawing_area->style->white_gc,
			 0, 0, IWIDTH, IHEIGHT,
			 GDK_RGB_DITHER_NORMAL,
			 (guchar*)disp, IWIDTH * 4);
}


/* Rendering Functions */


static void
bumpbob(int x, int y, int size)
{
  int o;

  /*  for (o=0; o<size; o++)
    {
      bump[x+(size/2)+(y+o)*IWIDTH] = 255;
    }
  memset(&bump[x+(y+(size/2))*IWIDTH], 255, size);
  */
  
  for (o=0; o<size; o++)
    {
      int p;
      for (p=0; p<size; p++)
	{
	  int k;
#define BOB_INC 45
	  k = destbump[p+x+(y+o)*IWIDTH] + BOB_INC;
	  if (k&256)
	    destbump[p+x+(y+o)*IWIDTH] = 255;
	  else
	    destbump[p+x+(y+o)*IWIDTH] = k;
	}
      /* memset(&destbump[x+(y+o)*IWIDTH], 131, size); */
    }
}


/* Adam's sillier algorithm. */
static void 
iterate (void)
{
  static guint frame = 0;
  gint i,j;
  gint thisbump;
  guchar sx,sy;
  guint32 *dest;
  guint32 *environment;
  guchar *basebump;
  unsigned int basesx;
  unsigned int basesy;
  /*  signed int bycxmcybx;
  signed int bx2,by2;
  signed int cx2,cy2;
  const gint bx = -(123-128);
  const gint by = (128+123);
  const gint cx = by;
  const gint cy = -bx;*/
#define bx (-(123-128))
#define by (128+123)
#define cx (by)
#define cy (-bx)
#define bycxmcybx (by*cx-cy*bx)
#define bx2 (((bx)<<19)/bycxmcybx)
#define by2 (((by)<<19)/bycxmcybx)
#define cx2 (((cx)<<19)/bycxmcybx)
#define cy2 (((cy)<<19)/bycxmcybx)

  frame++;

  environment = (guint32*) env;
  dest = (guint32*) disp;
  srcbump = (frame&1) ? bump1 : bump2;
  destbump = (frame&1) ? bump2 : bump1;

  /* WARP DISTORTION MAP (plughole-effect) */

  /* this setup obsolete, tranformation is constant */
  /*if ((cx+bx) == 0)
    cx++;
    
    if ((cy+by) == 0)
    by++;

  bycxmcybx = (by*cx-cy*bx);

  if (bycxmcybx == 0)
    bycxmcybx = 1;

  bx2 = ((bx)<<19)/bycxmcybx;
  cx2 = ((cx)<<19)/bycxmcybx;
  by2 = ((by)<<19)/bycxmcybx;
  cy2 = ((cy)<<19)/bycxmcybx;
  */

  /* A little sub-pixel jitter to liven things up. */
  basesx = (((RAND_FUNC()%(29<<19)))/bycxmcybx) + ((-128-((128*256)/(cx+bx)))<<11);
  basesy = (((RAND_FUNC()%(29<<19)))/bycxmcybx) + ((-128-((128*256)/(cy+by)))<<11);
  
  basebump = srcbump;

  
#if 0
  /* identity only */
  j = IHEIGHT;
  while (j--)
    {
      i = IWIDTH;
      while (i--)
	{
	  *dest++ = *environment++;
	}
    }
  return;
#endif


  /* MELT DISTORTION MAP, APPLY IT */
  j = IHEIGHT;
  while (j--)
    {
      unsigned int tx;
      unsigned int ty;
      
      ty = (basesy+=cx2);
      tx = (basesx-=bx2);

      i = IWIDTH;
      while (i--)
	{
	  unsigned char *bptr =
	    (srcbump + (
			(
			 ((255&(
				(tx>>11)
				)))
			 |
			 ((((255&(
				  (ty>>11)
				  ))<<8)))
			 )
			));

	  thisbump = (11 * *(basebump) + (
					  *(bptr-IWIDTH) +
					  *(bptr-1) +
					  *(bptr) +
					  *(bptr+1) +
					  *(bptr+IWIDTH)
					  )
		      );
	  basebump++;
	  
	  tx += by2;
	  ty -= cy2;

	  /* TODO: Can accelerate search for non-zero bumps with
	     casting an aligned long-word search. */
	  if (thisbump == 0)
	    {
	      *(dest++) = *( environment + (i | (j<<8) ) );
	      /* *(dest++) = 111; */
	      *(destbump++) = 0;
	    }
	  else
	    {
	      if (thisbump <= (131<<4) )
		{
		  thisbump >>= 4;
		  *destbump = thisbump;
		}
	      else
		*destbump = thisbump = 131;

	      /* sy = j + ( ((thisbump) - *(destbump+IWIDTH))<<1);
		 sx = i + ( ((thisbump) - *(++destbump))<<1);  + blah; */
	      sy = j + ( ((thisbump) - *(destbump+IWIDTH)));
	      sx = i + ( ((thisbump) - *(++destbump)));
	      *dest++ = *( environment + (sx | (sy<<8) ) );
	      /* sx = ( ((thisbump) - *(destbump+IWIDTH)));
		 sy = ( ((thisbump) - *(++destbump)));
		 *dest++ = (sx) | (sy<<8) | (sx<<16); */
	    }
	}
    }


  srcbump = (frame&1) ? bump1 : bump2;
  destbump = (frame&1) ? bump2 : bump1;
  dest = (guint32 *) disp;
  memset(destbump, 0, IWIDTH);

#if 1
  /*  CAUSTICS!  */
  /* The idea here is that we refract IWIDTH*IHEIGHT parallel rays
     through the surface of the slime and illuminate the points
     where they hit the backing-image.  There are some unrealistic
     shortcuts taken, but the result is quite pleasing.
  */
  j = IHEIGHT;
  while (j--)
    {
      i = IWIDTH;
      while (i--)
	{
	  /* Apply caustics */
	  sx = *(destbump++);
	  if (sx!=0)
	    {
	      guchar* cptr;

	      sy = j + ( ((sx) - *(destbump+IWIDTH-1)));
	      sx = i + ( ((sx) - *(destbump)));

	      /* cptr = (guchar*)((guint32*)(( dest+ (0xffff^(sx | (sy<<8) )) ))); */
	      cptr = (guchar*)( dest + (0xffff^(sx | (sy<<8) )) );

	      *cptr = llut[*cptr]; cptr++;
	      *cptr = llut[*cptr]; cptr++;
	      *cptr = llut[*cptr];
	      /* this second point of light's offset (1 across, 1 down)
		 isn't really 'right' but it gives a more pleasing,
		 diffuse look. */
	      cptr+= 2 + IWIDTH*4;

	      *cptr = llut[*cptr]; cptr++;
	      *cptr = llut[*cptr]; cptr++;
	      *cptr = llut[*cptr];
	    }
	}
    }
#endif


    /* Interactive bumpmap */
#define BOBSIZE 6
#define BOBSPREAD 40
#define BOBS_PER_FRAME 70
  destbump = (frame&1) ? bump2 : bump1;
  {
    gint rxp, ryp, posx, posy;
    GdkModifierType mask; 
    gint size, i;

    gdk_window_get_pointer (eventbox->window, &rxp, &ryp, &mask);

    for (i = 0; i < BOBS_PER_FRAME; i++)
      {
	size = 1 + RAND_FUNC()%BOBSIZE;

	posx = rxp + BOBSPREAD/2 -
	  RINT(sqrt((RAND_FUNC()%BOBSPREAD)*(RAND_FUNC()%BOBSPREAD)));
	posy = ryp + BOBSPREAD/2 -
	  RINT(sqrt((RAND_FUNC()%BOBSPREAD)*(RAND_FUNC()%BOBSPREAD)));

	if (! ((posx>IWIDTH-size) ||
	       (posy>IHEIGHT-size) ||
	       (posx<1) ||
	       (posy<1) ))
	  bumpbob(posx, posy, size);
      }
  }
}


static void
render_frame (void)
{
  static int frame = 0;

#if 0
  if (frame==0)
    {
      gint i, bytes;
      
      bytes = IWIDTH*IHEIGHT*4;
      
      for (i=0;i<bytes;i++)
	{
	  disp[i] = env[i];
	}
    }
#endif

  iterate();

  show();
  
  frame++;
}


static void
init_preview_misc (void)
{
  GimpPixelRgn pixel_rgn;
  gint i;
  gboolean has_alpha;

  has_alpha = gimp_drawable_has_alpha(drawable->id);

  env = g_malloc (4 * IWIDTH * IHEIGHT * 2);
  disp = g_malloc ((IWIDTH + 2 + IWIDTH * IHEIGHT) * 4);
  bump1base = g_malloc (IWIDTH * IHEIGHT + IWIDTH+IWIDTH);
  bump2base = g_malloc (IWIDTH * IHEIGHT + IWIDTH+IWIDTH);

  bump1 = &bump1base[IWIDTH];
  bump2 = &bump2base[IWIDTH];

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
				       &env[(256*i +
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
			       env,
			       drawable->width>256?(drawable->width/2-128):0,
			       drawable->height>256?(drawable->height/2-128):0,
			       MIN(256,drawable->width),
			       MIN(256,drawable->height));
    }

  gimp_drawable_detach(drawable);


  /* convert the image data of varying types into flat grey or rgb. */
  switch (imagetype)
    {
    case GIMP_GRAY:
    case GIMP_INDEXED:
      if (has_alpha)
	{
	  for (i=IWIDTH*IHEIGHT;i>0;i--)
	    {
	      env[4*(i-1)+2] =
		((palette[3*(env[(i-1)*2])+2]*env[(i-1)*2+1])/255)
		+ ((255-env[(i-1)*2+1])*((i&255) ^ (i>>8)))/255;
	      env[4*(i-1)+1] =
		((palette[3*(env[(i-1)*2])+1]*env[(i-1)*2+1])/255)
		+ ((255-env[(i-1)*2+1])*((i&255) ^ (i>>8)))/255;
	      env[4*(i-1)+0] =
		((palette[3*(env[(i-1)*2])+0]*env[(i-1)*2+1])/255)
		+ ((255-env[(i-1)*2+1])*((i&255) ^ (i>>8)))/255;
	    }
	}
      else
	{
	  for (i=IWIDTH*IHEIGHT;i>0;i--)
	    {
	      env[4*(i-1)+2] = palette[3*(env[i-1])+2];
	      env[4*(i-1)+1] = palette[3*(env[i-1])+1];
	      env[4*(i-1)+0] = palette[3*(env[i-1])+0];
	    }
	}
      break;

    case GIMP_RGB:
      if (has_alpha)
	{
	  for (i=0;i<IWIDTH*IHEIGHT;i++)
	    {
	      env[i*4+2] =
		(env[i*4+2]*env[i*4+3])/255
		+ ((255-env[i*4+3])*((i&255) ^ (i>>8)))/255;
	      env[i*4+1] =
		(env[i*4+1]*env[i*4+3])/255
		+ ((255-env[i*4+3])*((i&255) ^ (i>>8)))/255;
	      env[i*4+0] =
		(env[i*4+0]*env[i*4+3])/255
		+ ((255-env[i*4+3])*((i&255) ^ (i>>8)))/255;
	    }
	}
      else
	{
	  for (i=IWIDTH*IHEIGHT;i>0;i--)
	    {
	      env[4*(i-1)+2] = env[(i-1)*3+2];
	      env[4*(i-1)+1] = env[(i-1)*3+1];
	      env[4*(i-1)+0] = env[(i-1)*3+0];
	    }
	}
      break;

    default:
      break;
    }

  /* Finally, 180-degree flip the environmental image! */
  for (i = 0; i < IWIDTH*IHEIGHT/2; i++)
    {
      guchar t;
      t = env[4*(i)+0];
      env[4*(i)+0] = env[4*(IWIDTH*IHEIGHT-(i+1))+0];
      env[4*(IWIDTH*IHEIGHT-(i+1))+0] = t;
      t = env[4*(i)+1];
      env[4*(i)+1] = env[4*(IWIDTH*IHEIGHT-(i+1))+1];
      env[4*(IWIDTH*IHEIGHT-(i+1))+1] = t;
      t = env[4*(i)+2];
      env[4*(i)+2] = env[4*(IWIDTH*IHEIGHT-(i+1))+2];
      env[4*(IWIDTH*IHEIGHT-(i+1))+2] = t;
    }
}



/* Util. */

static int
do_iteration (void)
{
  render_frame ();
  show ();

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

}


static gint
iteration_callback (gpointer   data)
{
  do_iteration();

  return TRUE;
}
