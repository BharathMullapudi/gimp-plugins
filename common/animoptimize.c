/*
 * Animation Optimizer plug-in version 1.1.0 [ALPHA]
 *
 * (c) Adam D. Moss, 1997-2001
 *     adam@gimp.org
 *     adam@foxbox.org
 *
 * This is part of the GIMP package and falls under the GPL.
 */

/*
 * REVISION HISTORY:
 *
 * 2001-04-28 : version 1.1.0 [ALPHA]
 *              Support automated background (or foreground) removal.
 *              It's half-broken.
 *              Eliminated special optimized frame alignment cases --
 *              we're not trying to be real-time like animationplay
 *              and it complicates the code.
 *
 * 2000-08-30 : version 1.0.4
 *              Change default frame duration from 125ms to 100ms for
 *              consistancy.
 *
 * 2000-06-05 : version 1.0.3
 *              Fix old bug which could cause errors in evaluating the
 *              final pixel of each composed layer.
 *
 * 2000-01-13 : version 1.0.2
 *              Collapse timing of completely optimized-away frames
 *              onto previous surviving frame.  Also be looser with
 *              (XXXXX) tag parsing.
 *
 * 2000-01-07 : version 1.0.1
 *              PDB interface submitted by Andreas Jaekel
 *              <jaekel@cablecats.de>
 *
 * 98.05.17 : version 1.0.0
 *            Finally preserves frame timings / layer names.  Has
 *            a progress bar now.  No longer beta, I suppose.
 *
 * 98.04.19 : version 0.70.0
 *            Plug-in doubles up as Animation UnOptimize too!  (This
 *            is somewhat more useful than it sounds.)
 *
 * 98.03.16 : version 0.61.0
 *            Support more rare opaque/transparent combinations.
 *
 * 97.12.09 : version 0.60.0
 *            Added support for INDEXED* and GRAY* images.
 *
 * 97.12.09 : version 0.52.0
 *            Fixed some bugs.
 *
 * 97.12.08 : version 0.51.0
 *            Relaxed bounding box on optimized layers marked
 *            'replace'.
 *
 * 97.12.07 : version 0.50.0
 *            Initial release.
 */

/*
 * BUGS:
 *  ?
 */

/*
 * TODO:
 *   User interface
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <libgimp/gimp.h>

#include "libgimp/stdplugins-intl.h"


typedef enum
{
  DISPOSE_UNDEFINED = 0x00,
  DISPOSE_COMBINE   = 0x01,
  DISPOSE_REPLACE   = 0x02
} DisposeType;


typedef enum
{
  OPOPTIMIZE   = 0L,
  OPUNOPTIMIZE = 1L,
  OPFOREGROUND = 2L,
  OPBACKGROUND = 3L
} operatingMode;


/* Declare local functions. */
static void query (void);
static void run   (gchar   *name,
		   gint     nparams,
		   GimpParam  *param,
		   gint    *nreturn_vals,
		   GimpParam **return_vals);

static      gint32 do_optimizations   (GimpRunModeType run_mode);


/* tag util functions*/
static         int parse_ms_tag        (const char *str);
static DisposeType parse_disposal_tag  (const char *str);
static DisposeType get_frame_disposal  (const guint whichframe);
static     guint32 get_frame_duration  (const guint whichframe);
static        void remove_disposal_tag (char* dest, char *src);
static        void remove_ms_tag       (char* dest, char *src);
static int is_disposal_tag (const char *str,
			    DisposeType *disposal,
			    int *taglength);
static int is_ms_tag (const char *str,
		      int *duration,
		      int *taglength);


GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};


/* Global widgets'n'stuff */
static guint          width,height;
static gint32         image_id;
static gint32         new_image_id;
static gint32         total_frames;
static gint32        *layers;
static GimpDrawable     *drawable;
static GimpImageBaseType     imagetype;
static GimpImageType  drawabletype_alpha;
static guchar         pixelstep;
static guchar        *palette;
static gint           ncolours;
static operatingMode  opmode;


MAIN ()

static void
query (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE, "image", "Input image" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable (unused)" }
  };
  static GimpParamDef return_args[] =
  {
    { GIMP_PDB_IMAGE, "result", "Resulting image" }
  };
  static gint nargs = sizeof (args) / sizeof (args[0]);
  static gint nreturn_args = sizeof (return_args) / sizeof (return_args[0]);

  gimp_install_procedure ("plug_in_animationoptimize",
			  "This procedure applies various optimizations to"
			  " a GIMP layer-based animation in an attempt to"
			  " reduce the final file size.",
			  "",
			  "Adam D. Moss <adam@gimp.org>",
			  "Adam D. Moss <adam@gimp.org>",
			  "1997-2001",
			  N_("<Image>/Filters/Animation/Animation Optimize"),
			  "RGB*, INDEXED*, GRAY*",
			  GIMP_PLUGIN,
			  nargs, nreturn_args,
			  args, return_args);

  gimp_install_procedure ("plug_in_animationunoptimize",
			  "This procedure 'simplifies' a GIMP layer-based"
			  " animation that has been AnimationOptimized.  This"
			  " makes the animation much easier to work with if,"
			  " for example, the optimized version is all you"
			  " have.",
			  "",
			  "Adam D. Moss <adam@gimp.org>",
			  "Adam D. Moss <adam@gimp.org>",
			  "1997-2001",
			  N_("<Image>/Filters/Animation/Animation UnOptimize"),
			  "RGB*, INDEXED*, GRAY*",
			  GIMP_PLUGIN,
			  nargs, nreturn_args,
			  args, return_args);

  gimp_install_procedure ("plug_in_animation_remove_backdrop",
			  "This procedure attempts to remove the backdrop"
			  " from a GIMP layer-based animation, leaving"
			  " the foreground animation over transparency.",
			  "",
			  "Adam D. Moss <adam@gimp.org>",
			  "Adam D. Moss <adam@gimp.org>",
			  "2001",
			  N_("<Image>/Filters/Animation/Animation: Remove Backdrop"),
			  "RGB*, INDEXED*, GRAY*",
			  GIMP_PLUGIN,
			  nargs, nreturn_args,
			  args, return_args);

  gimp_install_procedure ("plug_in_animation_find_backdrop",
			  "This procedure attempts to remove the foreground"
			  " from a GIMP layer-based animation, leaving"
			  " a one-layered image containing only the"
			  " constant backdrop image.",
			  "",
			  "Adam D. Moss <adam@gimp.org>",
			  "Adam D. Moss <adam@gimp.org>",
			  "2001",
			  N_("<Image>/Filters/Animation/Animation: Find Backdrop"),
			  "RGB*, INDEXED*, GRAY*",
			  GIMP_PLUGIN,
			  nargs, nreturn_args,
			  args, return_args);
}

static void
run (gchar   *name,
     gint     n_params,
     GimpParam  *param,
     gint    *nreturn_vals,
     GimpParam **return_vals)
{
  static GimpParam values[2];
  GimpRunModeType run_mode;
  GimpPDBStatusType status = GIMP_PDB_SUCCESS;

  *nreturn_vals = 2;
  *return_vals = values;

  run_mode = param[0].data.d_int32;
  
  if (run_mode == GIMP_RUN_NONINTERACTIVE)
    {
      if (n_params != 3)
	{
	  status = GIMP_PDB_CALLING_ERROR;
	}
    }
  INIT_I18N();
  
  /* Check the procedure name we were called with, to decide
     what needs to be done. */
  if (strcmp(name,"plug_in_animationoptimize")==0)
    opmode = OPOPTIMIZE;
  else if (strcmp(name,"plug_in_animationunoptimize")==0)
    opmode = OPUNOPTIMIZE;
  else if (strcmp(name,"plug_in_animation_find_backdrop")==0)
    opmode = OPBACKGROUND;
  else if (strcmp(name,"plug_in_animation_remove_backdrop")==0)
    opmode = OPFOREGROUND;
  else
    g_error("GAH!!!");
  
  if (status == GIMP_PDB_SUCCESS)
    {
      image_id = param[1].data.d_image;

      new_image_id = do_optimizations(run_mode);
      
      if (run_mode != GIMP_RUN_NONINTERACTIVE)
	gimp_displays_flush();
    }
  
  values[0].type = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  values[1].type = GIMP_PDB_IMAGE;
  values[1].data.d_image = new_image_id;
}



/* Rendering Functions */

static void
total_alpha(guchar* imdata, guint32 numpix, guchar bytespp)
{
  /* Set image to total-transparency w/black
   */

  memset(imdata, 0, numpix*bytespp);
}


static void
compose_row(int frame_num,
	    DisposeType dispose,
	    int row_num,
	    unsigned char *dest,
	    int dest_width,
	    GimpDrawable *drawable,
	    const gboolean cleanup)
{
  static unsigned char *line_buf = NULL;
  guchar *srcptr;
  GimpPixelRgn pixel_rgn;
  gint rawx, rawy, rawbpp, rawwidth, rawheight;
  int i;
  gboolean has_alpha;

  if (cleanup)
    {
      if (line_buf)
	{
	  g_free(line_buf);
	  line_buf = NULL;
	}

      return;
    }

  if (dispose == DISPOSE_REPLACE)
    {
      total_alpha (dest, dest_width, pixelstep);
    }

  gimp_drawable_offsets (drawable->id,
			 &rawx,
			 &rawy);

  rawheight = gimp_drawable_height (drawable->id);

  /* this frame has nothing to give us for this row; return */
  if (row_num >= rawheight + rawy ||
      row_num < rawy)
    return;

  rawbpp = gimp_drawable_bpp (drawable->id);
  rawwidth = gimp_drawable_width (drawable->id);
  has_alpha = gimp_drawable_has_alpha (drawable->id);

  if (line_buf)
    {
      g_free(line_buf);
      line_buf = NULL;
    }
  line_buf = g_malloc(rawwidth * rawbpp);

  /* Initialise and fetch the raw new frame row */

  gimp_pixel_rgn_init (&pixel_rgn,
		       drawable,
		       0, row_num - rawy,
		       rawwidth, 1,
		       FALSE,
		       FALSE);
  gimp_pixel_rgn_get_rect (&pixel_rgn,
			   line_buf,
			   0, row_num - rawy,
			   rawwidth, 1);

  /* render... */

  srcptr = line_buf;
  
  for (i=rawx; i<rawwidth+rawx; i++)
    {
      if (i>=0 && i<dest_width)
	{
	  if ((!has_alpha) || ((*(srcptr+rawbpp-1))&128))
	    {
	      int pi;
	      for (pi = 0; pi < pixelstep-1; pi++)
		{
		  dest[i*pixelstep +pi] = *(srcptr + pi);
		}
	      dest[i*pixelstep + pixelstep - 1] = 255;
	    }
	}
      
      srcptr += rawbpp;
    }
  
}


static gint32
do_optimizations(GimpRunModeType run_mode)
{
  GimpPixelRgn pixel_rgn;
  static guchar* rawframe = NULL;
  guchar* srcptr;
  guchar* destptr;
  gint row,this_frame_num;
  guint32 frame_sizebytes;
  gint32 new_layer_id;
  DisposeType dispose;
  guchar* this_frame = NULL;
  guchar* last_frame = NULL;
  guchar* opti_frame = NULL;
  guchar* back_frame = NULL;

  int this_delay;
  int cumulated_delay = 0;
  int last_true_frame = -1;
  int buflen;

  gchar* oldlayer_name;
  gchar* newlayer_name;
  
  gboolean can_combine;

  gint32 bbox_top, bbox_bottom, bbox_left, bbox_right;
  gint32 rbox_top, rbox_bottom, rbox_left, rbox_right;

  switch (opmode)
    {
    case OPUNOPTIMIZE:
      gimp_progress_init (_("UnOptimizing Animation..."));
      break;
    case OPFOREGROUND:
      gimp_progress_init (_("Removing Animation Background..."));
      break;
    case OPBACKGROUND:
      gimp_progress_init (_("Finding Animation Background..."));
      break;
    case OPOPTIMIZE:
    default:
      gimp_progress_init (_("Optimizing Animation..."));
      break;
    }

  width     = gimp_image_width(image_id);
  height    = gimp_image_height(image_id);
  layers    = gimp_image_get_layers (image_id, &total_frames);
  imagetype = gimp_image_base_type(image_id);
  pixelstep = (imagetype == GIMP_RGB) ? 4 : 2;

  /*  gimp_tile_cache_ntiles(total_frames * (width / gimp_tile_width() + 1) );*/
  

  drawabletype_alpha = (imagetype == GIMP_RGB) ? GIMP_RGBA_IMAGE :
    ((imagetype == GIMP_INDEXED) ? GIMP_INDEXEDA_IMAGE : GIMP_GRAYA_IMAGE);

  frame_sizebytes = width * height * pixelstep;

  this_frame = g_malloc (frame_sizebytes);
  last_frame = g_malloc (frame_sizebytes);
  opti_frame = g_malloc (frame_sizebytes);
  if (opmode == OPBACKGROUND ||
      opmode == OPFOREGROUND)
    back_frame = g_malloc (frame_sizebytes);

  total_alpha (this_frame, width*height, pixelstep);
  total_alpha (last_frame, width*height, pixelstep);

  new_image_id = gimp_image_new(width, height, imagetype);

  if (imagetype == GIMP_INDEXED)
    {
      palette = gimp_image_get_cmap (image_id, &ncolours);
      gimp_image_set_cmap (new_image_id, palette, ncolours);
    }

  if ((this_frame == NULL) || (last_frame == NULL) || (opti_frame == NULL))
    g_error(_("Not enough memory to allocate buffers for optimization.\n"));

#if 1
  if (opmode == OPBACKGROUND ||
      opmode == OPFOREGROUND)
    {
      /* iterate through all rows of all frames, find statistical
	 mode for each pixel position. */
      int i,j;
      guchar **these_rows;
      guchar **red;
      guchar **green;
      guchar **blue;
      guint **count;

      guint *num_colours;
      
      these_rows = g_malloc(total_frames * sizeof(guchar *));
      red =        g_malloc(total_frames * sizeof(guchar *));
      green =      g_malloc(total_frames * sizeof(guchar *));
      blue =       g_malloc(total_frames * sizeof(guchar *));
      count =      g_malloc(total_frames * sizeof(guint *));

      num_colours = g_malloc(width * sizeof(guint));

g_warning("stat fun");

      for (this_frame_num=0; this_frame_num<total_frames; this_frame_num++)
	{
	  these_rows[this_frame_num] = g_malloc(width * pixelstep);

	  red[this_frame_num] = g_malloc(width * sizeof(guchar));
	  green[this_frame_num] = g_malloc(width * sizeof(guchar));
	  blue[this_frame_num] = g_malloc(width * sizeof(guchar));

	  count[this_frame_num] = g_malloc0(width * sizeof(guint));
	}
      
      for (row = 0; row < height; row++)
	{
	  memset(num_colours, 0, width * sizeof(guint));

	  for (this_frame_num=0; this_frame_num<total_frames; this_frame_num++)
	    {
	      /*g_warning("stat fun : %d / %d", row, this_frame_num);*/

	      drawable =
		gimp_drawable_get (layers[total_frames-(this_frame_num+1)]);
	      
	      dispose = get_frame_disposal (this_frame_num);
	      
	      compose_row(this_frame_num,
			  dispose,
			  row,
			  these_rows[this_frame_num],
			  width,
			  drawable,
			  FALSE
			  );

	      gimp_drawable_detach(drawable);
	    }
	  
	  /*	  g_warning("eh2."); */

	  for (this_frame_num=0; this_frame_num<total_frames; this_frame_num++)
	    {
	      for (i=0; i<width; i++)
		{
		  /*		  g_warning("eh4(%d).", i); */
		  if (these_rows[this_frame_num][i * pixelstep + pixelstep -1]
		      >= 128)
		    {
		      /*		      fprintf(stderr, "%d ", */
		      /*			      these_rows[this_frame_num][i * pixelstep + pixelstep -1]); */
		      for (j=0; j<num_colours[i]; j++)
			{
			  /*		      g_warning("eh3(%d,%d).", i,j); */

			  switch (pixelstep)
			    {
			    case 4:
			      if (these_rows[this_frame_num][i * 4 +0] ==
				  red[j][i] &&
				  these_rows[this_frame_num][i * 4 +1] ==
				  green[j][i] &&
				  these_rows[this_frame_num][i * 4 +2] ==
				  blue[j][i])
				{
				  (count[j][i])++;
				  goto same;
				}
			      break;
			    case 2:
			      if (these_rows[this_frame_num][i * 2 +0] ==
				  red[j][i])
				{
				  (count[j][i])++;
				  goto same;
				}
			      break;
			    default:
			      g_error ("Eeep!");
			      break;
			    }
			}

		      count[num_colours[i]][i] = 1;
		      red[num_colours[i]][i] =
			these_rows[this_frame_num][i * pixelstep];
		      if (pixelstep == 4)
			{
			  green[num_colours[i]][i] =
			    these_rows[this_frame_num][i * 4 +1];
			  blue[num_colours[i]][i] =
			    these_rows[this_frame_num][i * 4 +2];
			}
		      num_colours[i]++;
		    }
		  /*		  else
				  g_warning("OOH!");*/
		same:
		}
	    }

	  /*	  g_warning("eh."); */
	  
	  for (i=0; i<width; i++)
	    {
	      guint best_count = 0;
	      guchar best_r=255, best_g=0, best_b=255;

	      for (j=0; j<num_colours[i]; j++)
		{
		  if (count[j][i] > best_count)
		    {
		      best_count = count[j][i];
		      best_r = red[j][i];
		      best_g = green[j][i];
		      best_b = blue[j][i];
		    }
		}

	      back_frame[width * pixelstep * row +i*pixelstep + 0] = best_r;
	      if (pixelstep == 4)
		{
		  back_frame[width * pixelstep * row +i*pixelstep + 1] =
		    best_g;
		  back_frame[width * pixelstep * row +i*pixelstep + 2] =
		    best_b;
		}
	      back_frame[width * pixelstep * row +i*pixelstep +pixelstep-1] =
		(best_count == 0) ? 0 : 255;

	      if (best_count == 0)
		g_warning("yayyyy!");
	    }
	  /*	  memcpy(&back_frame[width * pixelstep * row],
		  these_rows[0],
		  width * pixelstep);*/
	}
      
      g_warning("stat fun over");
      
      for (this_frame_num=0; this_frame_num<total_frames; this_frame_num++)
	{
	  g_free(these_rows[this_frame_num]);
	  g_free(red[this_frame_num]);
	  g_free(green[this_frame_num]);
	  g_free(blue[this_frame_num]);
	  g_free(count[this_frame_num]);
	}
      g_free(these_rows);
      g_free(red);
      g_free(green);
      g_free(blue);
      g_free(count);
      g_free(num_colours);
    }
#endif

  if (opmode == OPBACKGROUND)
    {      
      new_layer_id = gimp_layer_new(new_image_id,
				    "Backgroundx",
				    width, height,
				    drawabletype_alpha,
				    100.0,
				    GIMP_NORMAL_MODE);
      
      gimp_image_add_layer (new_image_id, new_layer_id, 0);
      
      drawable = gimp_drawable_get (new_layer_id);
      
      gimp_pixel_rgn_init (&pixel_rgn, drawable,
			   0, 0,
			   width, height,
			   TRUE, FALSE);
      gimp_pixel_rgn_set_rect (&pixel_rgn, back_frame,
			       0, 0,
			       width, height);
      gimp_drawable_flush (drawable);
      gimp_drawable_detach (drawable);
    }
  else
    {
      for (this_frame_num=0; this_frame_num<total_frames; this_frame_num++)
	{
	  /*
	   * BUILD THIS FRAME into our 'this_frame' buffer.
	   */

	  drawable =
	    gimp_drawable_get (layers[total_frames-(this_frame_num+1)]);

	  /* Image has been closed/etc since we got the layer list? */
	  /* FIXME - How do we tell if a gimp_drawable_get() fails? */
	  if (gimp_drawable_width (drawable->id) == 0)
	    {
	      gimp_quit ();
	    }

	  this_delay = get_frame_duration (this_frame_num);
	  dispose    = get_frame_disposal (this_frame_num);
      
	  for (row = 0; row < height; row++)
	    {
	      compose_row(this_frame_num,
			  dispose,
			  row,
			  &this_frame[pixelstep*width * row],
			  width,
			  drawable,
			  FALSE
			  );
	    }

	  /* clean up */
	  gimp_drawable_detach(drawable);


	  if (opmode == OPFOREGROUND)
	    {
	      int xit, yit, byteit;

	      g_warning("matcher");

	      for (yit=0; yit<height; yit++)
		{
		  for (xit=0; xit<width; xit++)
		    {
		      for (byteit=0; byteit<pixelstep-1; byteit++)
			{
			  if (back_frame[yit*width*pixelstep + xit*pixelstep
					+ byteit]
			      !=
			      this_frame[yit*width*pixelstep + xit*pixelstep
					+ byteit])
			    {
			      goto enough;
			    }		    
			}
		      this_frame[yit*width*pixelstep + xit*pixelstep
				+ pixelstep - 1] = 0;
		    enough:
		    }
		}
	    }

	  can_combine = FALSE;
	  bbox_left = 0;
	  bbox_top = 0;
	  bbox_right = width;
	  bbox_bottom = height;
	  rbox_left = 0;
	  rbox_top = 0;
	  rbox_right = width;
	  rbox_bottom = height;
	  /* copy 'this' frame into a buffer which we can safely molest */
	  memcpy(opti_frame, this_frame, frame_sizebytes);
	  /*
	   *
	   * OPTIMIZE HERE!
	   *
	   */
	  if (
	      (this_frame_num != 0) /* Can't delta bottom frame! */
	      && (opmode == OPOPTIMIZE)
	      )
	    {
	      int xit, yit, byteit;

	      can_combine = TRUE;

	      /*
	       * SEARCH FOR BOUNDING BOX
	       */
	      bbox_left = width;
	      bbox_top = height;
	      bbox_right = 0;
	      bbox_bottom = 0;
	      rbox_left = width;
	      rbox_top = height;
	      rbox_right = 0;
	      rbox_bottom = 0;
	  
	      for (yit=0; yit<height; yit++)
		{
		  for (xit=0; xit<width; xit++)
		    {
		      gboolean keep_pix;
		      gboolean opaq_pix;
		      /* Check if 'this' and 'last' are transparent */
		      if (!(this_frame[yit*width*pixelstep + xit*pixelstep
				      + pixelstep-1]&128)
			  &&
			  !(last_frame[yit*width*pixelstep + xit*pixelstep
				      + pixelstep-1]&128))
			{
			  keep_pix = FALSE;
			  opaq_pix = FALSE;
			  goto decided;
			}
		      /* Check if just 'this' is transparent */
		      if ((last_frame[yit*width*pixelstep + xit*pixelstep
				     + pixelstep-1]&128)
			  &&
			  !(this_frame[yit*width*pixelstep + xit*pixelstep
				      + pixelstep-1]&128))
			{
			  keep_pix = TRUE;
			  opaq_pix = FALSE;
			  can_combine = FALSE;
			  goto decided;
			}
		      /* Check if just 'last' is transparent */
		      if (!(last_frame[yit*width*pixelstep + xit*pixelstep
				      + pixelstep-1]&128)
			  &&
			  (this_frame[yit*width*pixelstep + xit*pixelstep
				     + pixelstep-1]&128))
			{
			  keep_pix = TRUE;
			  opaq_pix = TRUE;
			  goto decided;
			}
		      /* If 'last' and 'this' are opaque, we have
		       *  to check if they're the same colour - we
		       *  only have to keep the pixel if 'last' or
		       *  'this' are opaque and different.
		       */
		      keep_pix = FALSE;
		      opaq_pix = TRUE;
		      for (byteit=0; byteit<pixelstep-1; byteit++)
			{
			  if ((last_frame[yit*width*pixelstep + xit*pixelstep
					 + byteit]
			       !=
			       this_frame[yit*width*pixelstep + xit*pixelstep
					 + byteit])
			      )
			    {
			      keep_pix = TRUE;
			      goto decided;
			    }			    
			}
		    decided:
		      if (opaq_pix)
			{
			  if (xit<rbox_left) rbox_left=xit;
			  if (xit>rbox_right) rbox_right=xit;
			  if (yit<rbox_top) rbox_top=yit;
			  if (yit>rbox_bottom) rbox_bottom=yit;
			}
		      if (keep_pix)
			{
			  if (xit<bbox_left) bbox_left=xit;
			  if (xit>bbox_right) bbox_right=xit;
			  if (yit<bbox_top) bbox_top=yit;
			  if (yit>bbox_bottom) bbox_bottom=yit;
			}
		      else
			{
			  /* pixel didn't change this frame - make
			   *  it transparent in our optimized buffer!
			   */
			  opti_frame[yit*width*pixelstep + xit*pixelstep
				    + pixelstep-1] = 0;
			}
		    } /* xit */
		} /* yit */

	      if (!can_combine)
		{
		  bbox_left = rbox_left;
		  bbox_top = rbox_top;
		  bbox_right = rbox_right;
		  bbox_bottom = rbox_bottom;
		}

	      bbox_right++;
	      bbox_bottom++;

	      /*
	       * Collapse opti_frame data down such that the data
	       *  which occupies the bounding box sits at the start
	       *  of the data (for convenience with ..set_rect()).
	       */
	      destptr = opti_frame;
	      /*
	       * If can_combine, then it's safe to use our optimized
	       *  alpha information.  Otherwise, an opaque pixel became
	       *  transparent this frame, and we'll have to use the
	       *  actual true frame's alpha.
	       */
	      if (can_combine)
		srcptr = opti_frame;
	      else
		srcptr = this_frame;
	      for (yit=bbox_top; yit<bbox_bottom; yit++)
		{
		  for (xit=bbox_left; xit<bbox_right; xit++)
		    {
		      for (byteit=0; byteit<pixelstep; byteit++)
			{
			  *(destptr++) = srcptr[yit*pixelstep*width +
					       pixelstep*xit + byteit];
			}
		    }
		}
	    } /* !bot frame? */
	  else
	    {
	      memcpy(opti_frame, this_frame, frame_sizebytes);
	    }

	  /*
	   *
	   * REMEMBER THE ANIMATION STATUS TO DELTA AGAINST NEXT TIME
	   *
	   */
	  memcpy(last_frame, this_frame, frame_sizebytes);

      
	  /*
	   *
	   * PUT THIS FRAME INTO A NEW LAYER IN THE NEW IMAGE
	   *
	   */

	  oldlayer_name =
	    gimp_layer_get_name(layers[total_frames-(this_frame_num+1)]);

	  buflen = strlen(oldlayer_name) + 40;

	  newlayer_name = g_malloc(buflen);

	  remove_disposal_tag(newlayer_name, oldlayer_name);
	  g_free(oldlayer_name);

	  oldlayer_name = g_malloc(buflen);

	  remove_ms_tag(oldlayer_name, newlayer_name);

	  g_snprintf(newlayer_name, buflen, "%s(%dms)%s",
		     oldlayer_name, this_delay,
		     (this_frame_num ==  0) ? "" :
		     can_combine ? "(combine)" : "(replace)");

	  g_free(oldlayer_name);

	  /* Empty frame! */
	  if (bbox_right <= bbox_left ||
	      bbox_bottom <= bbox_top)
	    {
	      cumulated_delay += this_delay;

	      g_free (newlayer_name);

	      oldlayer_name =
		gimp_layer_get_name(last_true_frame);
	  
	      buflen = strlen(oldlayer_name) + 40;
	  
	      newlayer_name = g_malloc(buflen);
	  
	      remove_disposal_tag(newlayer_name, oldlayer_name);
	      g_free(oldlayer_name);
	  
	      oldlayer_name = g_malloc(buflen);
	  
	      remove_ms_tag(oldlayer_name, newlayer_name);
	  
	      g_snprintf(newlayer_name, buflen, "%s(%dms)%s",
			 oldlayer_name, cumulated_delay,
			 (this_frame_num ==  0) ? "" :
			 can_combine ? "(combine)" : "(replace)");

	      gimp_layer_set_name(last_true_frame, newlayer_name);

	      g_free (newlayer_name);
	    }
	  else
	    {
	      cumulated_delay = this_delay;

	      last_true_frame =
		new_layer_id = gimp_layer_new(new_image_id,
					      newlayer_name,
					      bbox_right-bbox_left,
					      bbox_bottom-bbox_top,
					      drawabletype_alpha,
					      100.0,
					      GIMP_NORMAL_MODE);
	      g_free(newlayer_name);
	  
	      gimp_image_add_layer (new_image_id, new_layer_id, 0);
	  
	      drawable = gimp_drawable_get (new_layer_id);
	  
	      gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0,
				   bbox_right-bbox_left,
				   bbox_bottom-bbox_top,
				   TRUE, FALSE);
	      gimp_pixel_rgn_set_rect (&pixel_rgn, opti_frame, 0, 0,
				       bbox_right-bbox_left,
				       bbox_bottom-bbox_top);
	      gimp_drawable_flush (drawable);
	      gimp_drawable_detach (drawable);     
	      gimp_layer_translate (new_layer_id, (gint)bbox_left, (gint)bbox_top);
	    }

	  gimp_progress_update (((double)this_frame_num+1.0) /
				((double)total_frames));
	}
    }
  
  if (run_mode != GIMP_RUN_NONINTERACTIVE)
    gimp_display_new (new_image_id);

  g_free(rawframe);
  rawframe = NULL;
  g_free(last_frame);
  last_frame = NULL;
  g_free(this_frame);
  this_frame = NULL;
  g_free(opti_frame);
  opti_frame = NULL;
  if (back_frame)
    {
      g_free(opti_frame);
      opti_frame = NULL;
    }

  return new_image_id;
}




/* Util. */

static DisposeType
get_frame_disposal (const guint whichframe)
{
  gchar* layer_name;
  DisposeType disposal;
  
  layer_name = gimp_layer_get_name(layers[total_frames-(whichframe+1)]);
  disposal = parse_disposal_tag(layer_name);
  g_free(layer_name);

  return(disposal);
}



static guint32
get_frame_duration (const guint whichframe)
{
  gchar* layer_name;
  gint   duration = 0;

  layer_name = gimp_layer_get_name(layers[total_frames-(whichframe+1)]);
  if (layer_name != NULL)
    {
      duration = parse_ms_tag(layer_name);
      g_free(layer_name);
    }
  
  if (duration < 0) duration = 100;  /* FIXME for default-if-not-said  */
  if (duration == 0) duration = 100; /* FIXME - 0-wait is nasty */

  return ((guint32) duration);
}


static int
is_ms_tag (const char *str, int *duration, int *taglength)
{
  gint sum = 0;
  gint offset;
  gint length;

  length = strlen(str);

  if (str[0] != '(')
    return 0;

  offset = 1;

  /* eat any spaces between open-parenthesis and number */
  while ((offset<length) && (str[offset] == ' '))
    offset++;
  
  if ((offset>=length) || (!isdigit(str[offset])))
    return 0;

  do
    {
      sum *= 10;
      sum += str[offset] - '0';
      offset++;
    }
  while ((offset<length) && (isdigit(str[offset])));  

  if (length-offset <= 2)
    return 0;

  /* eat any spaces between number and 'ms' */
  while ((offset<length) && (str[offset] == ' '))
    offset++;

  if ((length-offset <= 2) ||
      (toupper(str[offset]) != 'M') || (toupper(str[offset+1]) != 'S'))
    return 0;

  offset += 2;

  /* eat any spaces between 'ms' and close-parenthesis */
  while ((offset<length) && (str[offset] == ' '))
    offset++;

  if ((length-offset < 1) || (str[offset] != ')'))
    return 0;

  offset++;
  
  *duration = sum;
  *taglength = offset;

  return 1;
}


static int
parse_ms_tag (const char *str)
{
  int i;
  int rtn;
  int dummy;
  int length;

  length = strlen(str);

  for (i=0; i<length; i++)
    {
      if (is_ms_tag(&str[i], &rtn, &dummy))
	return rtn;
    }
  
  return -1;
}


static int
is_disposal_tag (const char *str, DisposeType *disposal, int *taglength)
{
  if (strlen(str) != 9)
    return 0;
  
  if (strncmp(str, "(combine)", 9) == 0)
    {
      *taglength = 9;
      *disposal = DISPOSE_COMBINE;
      return 1;
    }
  else if (strncmp(str, "(replace)", 9) == 0)
    {
      *taglength = 9;
      *disposal = DISPOSE_REPLACE;
      return 1;
    }

  return 0;
}


static DisposeType
parse_disposal_tag (const char *str)
{
  DisposeType rtn;
  int i, dummy;
  gint length;

  length = strlen(str);

  for (i=0; i<length; i++)
    {
      if (is_disposal_tag(&str[i], &rtn, &dummy))
	{
	  return rtn;
	}
    }

  return (DISPOSE_UNDEFINED); /* FIXME */
}



static void
remove_disposal_tag (char *dest, char *src)
{
  gint offset = 0;
  gint destoffset = 0;
  gint length;
  int taglength;
  DisposeType dummy;

  length = strlen(src);

  strcpy(dest, src);

  while (offset<=length)
    {
      if (is_disposal_tag(&src[offset], &dummy, &taglength))
	{
	  offset += taglength;
	}
      dest[destoffset] = src[offset];
      destoffset++;
      offset++;
    }

  dest[offset] = '\0';
}



static void
remove_ms_tag (char *dest, char *src)
{
  gint offset = 0;
  gint destoffset = 0;
  gint length;
  int taglength;
  int dummy;

  length = strlen(src);

  strcpy(dest, src);

  while (offset<=length)
    {
      if (is_ms_tag(&src[offset], &dummy, &taglength))
	{
	  offset += taglength;
	}
      dest[destoffset] = src[offset];
      destoffset++;
      offset++;
    }

  dest[offset] = '\0';
}
