/*
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This is a plug-in for the GIMP.
 *
 * Tileit - This plugin will take an image an make repeated
 * copies of it the stepping is 1/(2**n); 1<=n<=6
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
 * A fair proprotion of this code was taken from the Whirl plug-in
 * which was copyrighted by Federico Mena Quintero (as below).
 * 
 * Whirl plug-in --- distort an image into a whirlpool
 * Copyright (C) 1997 Federico Mena Quintero           
 *
 */

/* Change log:-
 * 0.2  Added new functions to allow "editing" of the tile patten.
 *
 * 0.1 First version released.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#ifdef __GNUC__
#warning GTK_DISABLE_DEPRECATED
#endif
#undef GTK_DISABLE_DEPRECATED

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


/***** Magic numbers *****/

#define PREVIEW_SIZE 128 
#define SCALE_WIDTH   80
#define ENTRY_WIDTH   50

#define MAX_SEGS       6

#define PREVIEW_MASK   GDK_EXPOSURE_MASK | \
                       GDK_BUTTON_PRESS_MASK | \
		       GDK_BUTTON_MOTION_MASK

/* Variables set in dialog box */
typedef struct data
{
  gint numtiles;
} TileItVals;

typedef struct
{
  GtkWidget *preview;
  guchar     preview_row[PREVIEW_SIZE * 4];
  gint       img_bpp;
  guchar    *pv_cache;

  gint       run;
} TileItInterface;

static TileItInterface tint =
{
  NULL,  /* Preview */
  {
    '4',
    'u'
  },     /* Preview_row */
  4,     /* bpp of drawable */
  NULL,
  FALSE, /* run */
};

static GimpDrawable *tileitdrawable;
static gint          tile_width, tile_height;
static GimpTile     *the_tile = NULL;
static gint          img_width, img_height,img_bpp;

static void      query  (void);
static void      run    (gchar       *name,
			 gint         nparams,
			 GimpParam   *param,
			 gint        *nreturn_vals,
			 GimpParam  **return_vals);
/* static void      check  (GimpDrawable * drawable); */

static gint      tileit_dialog          (void);

static void      tileit_ok_callback     (GtkWidget     *widget,
					 gpointer       data);

static void      tileit_scale_update    (GtkAdjustment *adjustment,
					 gpointer       data);

static void      tileit_exp_update      (GtkWidget *widget, gpointer value);
static void      tileit_exp_update_f    (GtkWidget *widget, gpointer value);

static void      tileit_reset           (GtkWidget *widget,
					 gpointer   value);
static void      tileit_radio_update    (GtkWidget *widget,
					 gpointer   data);
static void      tileit_hvtoggle_update (GtkWidget *widget,
					 gpointer   data);

static void      do_tiles  (void);
static gint      tiles_xy  (gint width, gint height,gint x,gint y,gint *nx,gint *ny);
static void      all_update     (void);
static void      alt_update     (void);
static void      explict_update (gint);

static void      dialog_update_preview (void);
static void	 cache_preview         (void);
static gint      tileit_preview_expose (GtkWidget *widget,
					GdkEvent  *event);
static gint      tileit_preview_events (GtkWidget *widget,
					GdkEvent  *event);


GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};

/* Values when first invoked */
static TileItVals itvals =
{
  2
};

/* Structures for call backs... */
/* The "explict tile" & family */
typedef enum
{
  ALL,
  ALT,
  EXPLICT
} AppliedTo;

typedef struct
{
  AppliedTo  type;

  gint       x;        /* X - pos of tile   */
  gint       y;        /* Y - pos of tile   */
  GtkObject *r_adj;    /* row adjustment    */
  GtkObject *c_adj;    /* column adjustment */
  GtkWidget *applybut; /* The apply button  */
} Exp_Call;

Exp_Call exp_call =
{
  ALL,
  -1,
  -1,
  NULL,
  NULL,
  NULL,
};

/* The reset button needs to know some toggle widgets.. */

typedef struct
{
  GtkWidget *htoggle;
  GtkWidget *vtoggle;
} Reset_Call;

Reset_Call res_call =
{
  NULL,
  NULL,
};
  
/* 2D - Array that holds the actions for each tile */
/* Action type on cell */
#define HORIZONTAL 0x1
#define VERTICAL   0x2

gint tileactions[MAX_SEGS][MAX_SEGS];

/* What actions buttons toggled */
static gint   do_horz = FALSE;
static gint   do_vert = FALSE;
static gint   opacity = 100;

/* Stuff for the preview bit */
static gint   sel_x1, sel_y1, sel_x2, sel_y2;
static gint   sel_width, sel_height;
static gint   preview_width, preview_height;
static gint   has_alpha;

MAIN ()

static void
query (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE, "image", "Input image (unused)" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable" },
    { GIMP_PDB_INT32, "number_of_tiles", "Number of tiles to make" } 
  };

  gimp_install_procedure ("plug_in_small_tiles",
			  "Tiles image into smaller versions of the orginal",
			  "More here later",
			  "Andy Thomas",
			  "Andy Thomas",
			  "1997",
			  N_("<Image>/Filters/Map/Small Tiles..."),
			  "RGB*, GRAY*",
			  GIMP_PLUGIN,
			  G_N_ELEMENTS (args), 0,
			  args, NULL);
}

static void
run (gchar       *name,
     gint         nparams,
     GimpParam   *param,
     gint        *nreturn_vals,
     GimpParam  **return_vals)
{
  static GimpParam   values[1];
  GimpDrawable      *drawable;
  GimpRunMode        run_mode;
  GimpPDBStatusType  status = GIMP_PDB_SUCCESS;

  gint pwidth, pheight;

  run_mode = param[0].data.d_int32;

  *nreturn_vals = 1;
  *return_vals  = values;

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  tileitdrawable = 
    drawable = 
    gimp_drawable_get (param[2].data.d_drawable);

  tile_width  = gimp_tile_width ();
  tile_height = gimp_tile_height ();

  gimp_drawable_mask_bounds (drawable->drawable_id,
			     &sel_x1, &sel_y1, &sel_x2, &sel_y2);

  sel_width  = sel_x2 - sel_x1;
  sel_height = sel_y2 - sel_y1;
  
  /* Calculate preview size */
  
  if (sel_width > sel_height)
    {
      pwidth  = MIN (sel_width, PREVIEW_SIZE);
      pheight = sel_height * pwidth / sel_width;
    }
  else
    {
      pheight = MIN (sel_height, PREVIEW_SIZE);
      pwidth  = sel_width * pheight / sel_height;
    }
  
  preview_width  = MAX (pwidth, 2);  /* Min size is 2 */
  preview_height = MAX (pheight, 2); 

  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE:
      INIT_I18N_UI();
      gimp_get_data ("plug_in_tileit", &itvals);
      if (! tileit_dialog ())
	{
	  gimp_drawable_detach (drawable);
	  return;
	}
      break;

    case GIMP_RUN_NONINTERACTIVE:
      if (nparams != 4)
	{
	  status = GIMP_PDB_CALLING_ERROR;
	}
      else
	{
	  itvals.numtiles = param[3].data.d_int32;
	}
      INIT_I18N();
      break;

    case GIMP_RUN_WITH_LAST_VALS:
      INIT_I18N();
      gimp_get_data ("plug_in_tileit", &itvals);
      break;

    default:
      break;
    }

  if (gimp_drawable_is_rgb (drawable->drawable_id) ||
      gimp_drawable_is_gray (drawable->drawable_id))
    {
      /* Set the tile cache size */

      gimp_tile_cache_ntiles ((drawable->width + gimp_tile_width () - 1) /
			      gimp_tile_width ());

      gimp_progress_init (_("Tiling..."));

      do_tiles ();
   
      if (run_mode != GIMP_RUN_NONINTERACTIVE)
	gimp_displays_flush ();

      if (run_mode == GIMP_RUN_INTERACTIVE)
	gimp_set_data ("plug_in_tileit", &itvals, sizeof (TileItVals));
    }
  else
    {
      status = GIMP_PDB_EXECUTION_ERROR;
    }

  values[0].data.d_status = status;

  gimp_drawable_detach (drawable);
}

/* Build the dialog up. This was the hard part! */
static gint
tileit_dialog (void)
{
  GtkWidget *dlg;
  GtkWidget *main_vbox;
  GtkWidget *hbox;
  GtkWidget *vbox;
  GtkWidget *vbox2;
  GtkWidget *frame;
  GtkWidget *xframe;
  GtkWidget *table;
  GtkWidget *sep;
  GtkWidget *table2;
  GtkWidget *button;
  GtkWidget *label;
  GtkWidget *spinbutton;
  GtkObject *adj;
  GtkObject *size_data;
  GtkObject *op_data;
  GtkWidget *toggle;
  GSList  *orientation_group = NULL;

  gimp_ui_init ("tileit", TRUE);

  cache_preview (); /* Get the preview image and store it also set has_alpha */

  /* Start buildng the dialog up */
  dlg = gimp_dialog_new ( _("TileIt"), "tileit",
			 gimp_standard_help_func, "filters/tileit.html",
			 GTK_WIN_POS_MOUSE,
			 FALSE, TRUE, FALSE,

			 GTK_STOCK_CANCEL, gtk_widget_destroy,
			 NULL, 1, NULL, FALSE, TRUE,
			 GTK_STOCK_OK, tileit_ok_callback,
			 NULL, NULL, NULL, TRUE, FALSE,

			 NULL);

  g_signal_connect (G_OBJECT (dlg), "destroy",
                    G_CALLBACK (gtk_main_quit),
                    NULL);

  main_vbox = gtk_vbox_new (FALSE, 4);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), main_vbox,
		      TRUE, TRUE, 0);
  gtk_widget_show (main_vbox);

  hbox = gtk_hbox_new (FALSE, 6);
  gtk_box_pack_start (GTK_BOX (main_vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  frame = gtk_frame_new ( _("Preview"));
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
  gtk_box_pack_start (GTK_BOX (hbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  vbox2 = gtk_vbox_new (TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox2), 4);
  gtk_container_add (GTK_CONTAINER (frame), vbox2);
  gtk_widget_show (vbox2);

  xframe = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (xframe), GTK_SHADOW_IN);
  gtk_box_pack_start (GTK_BOX (vbox2), xframe, TRUE, FALSE, 0);
  gtk_widget_show (xframe);

  tint.preview = gtk_preview_new (GTK_PREVIEW_COLOR);
  gtk_preview_size (GTK_PREVIEW (tint.preview), preview_width, preview_height);
  gtk_widget_set_events (GTK_WIDGET (tint.preview), PREVIEW_MASK);
  gtk_container_add (GTK_CONTAINER (xframe), tint.preview);

  g_signal_connect_after (G_OBJECT (tint.preview), "expose_event",
                          G_CALLBACK (tileit_preview_expose),
                          NULL);
  g_signal_connect (G_OBJECT (tint.preview), "event",
                    G_CALLBACK (tileit_preview_events),
                    NULL);

  /* Area for buttons etc */

  frame = gtk_frame_new (_("Flipping"));
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
  gtk_box_pack_start (GTK_BOX (hbox), frame, TRUE, TRUE, 0);
  gtk_widget_show (frame);

  vbox = gtk_vbox_new (FALSE, 4);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 4);
  gtk_container_add (GTK_CONTAINER (frame), vbox);
  gtk_widget_show (vbox);

  hbox = gtk_hbox_new (TRUE, 4);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  toggle = gtk_check_button_new_with_label (_("Horizontal"));
  gtk_box_pack_start (GTK_BOX (hbox), toggle, TRUE, TRUE, 0);
  gtk_widget_show (toggle);

  g_signal_connect (G_OBJECT (toggle), "toggled",
                    G_CALLBACK (tileit_hvtoggle_update),
                    &do_horz);

  res_call.htoggle = toggle;

  toggle = gtk_check_button_new_with_label (_("Vertical"));
  gtk_box_pack_start (GTK_BOX (hbox), toggle, TRUE, TRUE, 0);
  gtk_widget_show (toggle);

  g_signal_connect (G_OBJECT (toggle), "toggled",
                    G_CALLBACK (tileit_hvtoggle_update),
                    &do_vert);

  res_call.vtoggle = toggle;

  button = gtk_button_new_from_stock (GIMP_STOCK_RESET);
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);
  gtk_widget_show (button);

  g_signal_connect (G_OBJECT (button), "clicked",
                    G_CALLBACK (tileit_reset),
                    &res_call);

  xframe = gtk_frame_new (_("Applied to Tile"));
  gtk_frame_set_shadow_type (GTK_FRAME (xframe), GTK_SHADOW_ETCHED_IN);
  gtk_box_pack_start (GTK_BOX (vbox), xframe, FALSE, FALSE, 0);
  gtk_widget_show (xframe);

  /* Table for the inner widgets..*/
  table = gtk_table_new (6, 4, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_container_set_border_width (GTK_CONTAINER (table), 4);
  gtk_container_add (GTK_CONTAINER (xframe), table);
  gtk_widget_show (table);

  toggle = gtk_radio_button_new_with_label (orientation_group, _("All Tiles"));
  orientation_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (toggle));
  gtk_table_attach (GTK_TABLE (table), toggle, 0, 4, 0, 1,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
  gtk_widget_show (toggle);

  g_object_set_data (G_OBJECT (toggle), "gimp-item-data",
                     GINT_TO_POINTER (ALL));

  g_signal_connect (G_OBJECT (toggle), "toggled",
                    G_CALLBACK (tileit_radio_update),
                    &exp_call.type);

  toggle = gtk_radio_button_new_with_label (orientation_group,
					    _("Alternate Tiles"));
  orientation_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (toggle));
  gtk_table_attach (GTK_TABLE (table), toggle, 0, 4, 1, 2,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
  gtk_widget_show (toggle);

  g_object_set_data (G_OBJECT (toggle), "gimp-item-data",
                     GINT_TO_POINTER (ALT));

  g_signal_connect (G_OBJECT (toggle), "toggled",
                    G_CALLBACK (tileit_radio_update),
                    &exp_call.type);

  toggle = gtk_radio_button_new_with_label (orientation_group,
                                            _("Explicit Tile"));
  orientation_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (toggle));  
  gtk_table_attach (GTK_TABLE (table), toggle, 0, 1, 2, 4,
		    GTK_FILL | GTK_SHRINK, GTK_FILL, 0, 0);
  gtk_widget_show (toggle);

  label = gtk_label_new (_("Row:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 1, 2, 2, 3,
		    GTK_FILL | GTK_SHRINK , GTK_FILL, 0, 0);
  gtk_widget_show (label); 

  gtk_widget_set_sensitive (label, FALSE);
  g_object_set_data (G_OBJECT (toggle), "set_sensitive", label);

  spinbutton = gimp_spin_button_new (&adj, 2, 1, 6, 1, 1, 0, 1, 0);
  gtk_widget_set_size_request (spinbutton, ENTRY_WIDTH, -1);
  gtk_table_attach (GTK_TABLE (table), spinbutton, 2, 3, 2, 3,
		    GTK_FILL | GTK_SHRINK, GTK_FILL, 0, 0);
  gtk_widget_show (spinbutton);

  g_signal_connect (G_OBJECT (adj), "value_changed",
                    G_CALLBACK (tileit_exp_update_f),
                    &exp_call);

  exp_call.r_adj = adj;

  gtk_widget_set_sensitive (spinbutton, FALSE);
  g_object_set_data (G_OBJECT (label), "set_sensitive", spinbutton);

  label = gtk_label_new ( _("Column:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_widget_show (label); 
  gtk_table_attach (GTK_TABLE (table), label, 1, 2, 3, 4,
		    GTK_FILL , GTK_FILL, 0, 0);

  gtk_widget_set_sensitive (label, FALSE);
  g_object_set_data (G_OBJECT (spinbutton), "set_sensitive", label);

  spinbutton = gimp_spin_button_new (&adj, 2, 1, 6, 1, 1, 0, 1, 0);
  gtk_widget_set_size_request (spinbutton, ENTRY_WIDTH, -1);
  gtk_table_attach (GTK_TABLE (table), spinbutton, 2, 3, 3, 4,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
  gtk_widget_show (spinbutton);

  g_signal_connect (G_OBJECT (adj), "value_changed",
                    G_CALLBACK (tileit_exp_update_f),
                    &exp_call);

  exp_call.c_adj = adj;

  gtk_widget_set_sensitive (spinbutton, FALSE);
  g_object_set_data (G_OBJECT (label), "set_sensitive", spinbutton);

  g_object_set_data (G_OBJECT (toggle), "gimp-item-data",
                     GINT_TO_POINTER (EXPLICT));

  g_signal_connect (G_OBJECT (toggle), "toggled",
                    G_CALLBACK (tileit_radio_update),
                    &exp_call.type);

  button = gtk_button_new_with_label (_("Apply"));
  gtk_table_attach (GTK_TABLE (table), button, 3, 4, 2, 4, 0, 0, 0, 0);
  gtk_widget_show (button);

  g_signal_connect (G_OBJECT (button), "clicked",
                    G_CALLBACK (tileit_exp_update),
                    &exp_call);

  exp_call.applybut = button;

  gtk_widget_set_sensitive (button, FALSE);
  g_object_set_data (G_OBJECT (spinbutton), "set_sensitive", button);

  /* Widget for selecting the Opacity */
  sep = gtk_hseparator_new ();
  gtk_table_attach (GTK_TABLE (table), sep, 0, 4, 4, 5,
		    GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 1);
  gtk_widget_show (sep);

  table2 = gtk_table_new (1, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table2), 4);
  gtk_table_attach_defaults (GTK_TABLE (table), table2, 0, 4, 5, 6);
  gtk_widget_show (table2);

  op_data = gimp_scale_entry_new (GTK_TABLE (table2), 0, 0,
				  _("Opacity:"), SCALE_WIDTH, ENTRY_WIDTH,
				  opacity, 0, 100, 1, 10, 0,
				  TRUE, 0, 0,
				  NULL, NULL);
  g_signal_connect (G_OBJECT (op_data), "value_changed",
                    G_CALLBACK (tileit_scale_update),
                    &opacity);

  gtk_widget_show (frame); 

  /* Lower frame saying howmany segments */
  frame = gtk_frame_new (_("Segment Setting"));
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
  gtk_box_pack_start (GTK_BOX (main_vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  table = gtk_table_new (1, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_container_set_border_width (GTK_CONTAINER (table), 4);
  gtk_container_add (GTK_CONTAINER (frame), table);
  gtk_widget_show (table);

  gtk_widget_set_sensitive (table2, has_alpha);

  size_data = gimp_scale_entry_new (GTK_TABLE (table), 0, 0,
				    "1 / (2 ** n)", SCALE_WIDTH, ENTRY_WIDTH,
				    itvals.numtiles, 2, MAX_SEGS, 1, 1, 0,
				    TRUE, 0, 0,
				    NULL, NULL);
  g_signal_connect (G_OBJECT (size_data), "value_changed",
                    G_CALLBACK (tileit_scale_update),
                    &itvals.numtiles);

  gtk_widget_show (tint.preview);

  gtk_widget_show (dlg);
  dialog_update_preview ();

  gtk_main ();
  gdk_flush ();

  return tint.run;
}

static void
tileit_ok_callback (GtkWidget *widget,
		      gpointer   data)
{
  tint.run = TRUE;

  gtk_widget_destroy (GTK_WIDGET (data));
}

static void
tileit_hvtoggle_update (GtkWidget *widget,
			gpointer   data)
{
  gimp_toggle_button_update (widget, data);

  switch (exp_call.type)
    {
    case ALL:
      /* Clear current settings */
      memset (tileactions, 0, sizeof (tileactions));
      all_update ();
      break;

    case ALT:
      /* Clear current settings */
      memset (tileactions, 0, sizeof (tileactions));
      alt_update ();
      break;

    case EXPLICT:
      break;
    }

  dialog_update_preview ();
}

static void 
draw_explict_sel (void)
{
  if (exp_call.type == EXPLICT)
    {
      gdouble x,y;
      gdouble width  = (gdouble) preview_width / (gdouble) itvals.numtiles;
      gdouble height = (gdouble) preview_height / (gdouble) itvals.numtiles;

      x = width * (exp_call.x - 1);
      y = height * (exp_call.y - 1);

      gdk_gc_set_function (tint.preview->style->black_gc, GDK_INVERT);

      gdk_draw_rectangle (tint.preview->window,
			  tint.preview->style->black_gc,
			  0,
			  (gint) x,
			  (gint) y,
			  (gint) width,
			  (gint) height);
      gdk_draw_rectangle (tint.preview->window,
			  tint.preview->style->black_gc,
			  0,
			  (gint) x + 1,
			  (gint) y + 1,
			  (gint) width - 2,
			  (gint) height - 2);
      gdk_draw_rectangle (tint.preview->window,
			  tint.preview->style->black_gc,
			  0,
			  (gint) x + 2,
			  (gint) y + 2,
			  (gint) width - 4,
			  (gint) height - 4);

      gdk_gc_set_function (tint.preview->style->black_gc, GDK_COPY);
    }
}

static gint
tileit_preview_expose (GtkWidget *widget,
		       GdkEvent  *event)
{
  draw_explict_sel ();

  return FALSE;
}

static void
exp_need_update (gint nx,
		 gint ny)
{
  if (nx <= 0 || nx > itvals.numtiles || ny <= 0 || ny > itvals.numtiles)
    return;

  if( nx != exp_call.x ||
      ny != exp_call.y )
    {
      draw_explict_sel (); /* Clear old 'un */
      exp_call.x = nx;
      exp_call.y = ny;
      draw_explict_sel ();

      g_signal_handlers_block_by_func (G_OBJECT (exp_call.c_adj),
                                       tileit_exp_update_f,
                                       &exp_call);
      g_signal_handlers_block_by_func (G_OBJECT (exp_call.r_adj),
                                       tileit_exp_update_f,
                                       &exp_call);

      gtk_adjustment_set_value (GTK_ADJUSTMENT (exp_call.c_adj), nx);
      gtk_adjustment_set_value (GTK_ADJUSTMENT (exp_call.r_adj), ny);

      g_signal_handlers_unblock_by_func (G_OBJECT (exp_call.c_adj),
                                         tileit_exp_update_f,
                                         &exp_call);
      g_signal_handlers_unblock_by_func (G_OBJECT (exp_call.r_adj),
                                         tileit_exp_update_f,
                                         &exp_call);
    }
}

static gint
tileit_preview_events (GtkWidget *widget,
		       GdkEvent  *event)
{
  GdkEventButton *bevent;
  GdkEventMotion *mevent;
  gint nx, ny;
  gint twidth  = preview_width / itvals.numtiles;
  gint theight = preview_height / itvals.numtiles;

  switch (event->type)
    {
    case GDK_EXPOSE:
      break;

    case GDK_BUTTON_PRESS:
      bevent = (GdkEventButton *) event;
      nx = bevent->x/twidth + 1;
      ny = bevent->y/theight + 1;
      exp_need_update (nx, ny);
      break;

    case GDK_MOTION_NOTIFY:
      mevent = (GdkEventMotion *) event;
      if ( !mevent->state ) 
	break;
      if(mevent->x < 0 || mevent->y < 0)
	break;
      nx = mevent->x/twidth + 1;
      ny = mevent->y/theight + 1;
      exp_need_update (nx, ny);
      break;

    default:
      break;
    }

  return FALSE;
}

static void 
explict_update (gint settile)
{
  gint x,y;

  /* Make sure bounds are OK */
  y = ROUND (GTK_ADJUSTMENT (exp_call.r_adj)->value);
  if (y > itvals.numtiles || y <= 0)
    {
      y = itvals.numtiles;
    }
  x = ROUND (GTK_ADJUSTMENT (exp_call.c_adj)->value);
  if (x > itvals.numtiles || x <= 0)
    {
      x = itvals.numtiles;
    }

  /* Set it */
  if (settile == TRUE)
    tileactions[x-1][y-1] = (((do_horz) ? HORIZONTAL : 0) |
			     ((do_vert) ? VERTICAL : 0));

  exp_call.x = x;
  exp_call.y = y;
}

static void 
all_update (void)
{
  gint x,y;

  for (x = 0 ; x < MAX_SEGS; x++)
    for (y = 0 ; y < MAX_SEGS; y++)
      tileactions[x][y] |= (((do_horz) ? HORIZONTAL : 0) |
			    ((do_vert) ? VERTICAL : 0));
}

static void
alt_update (void)
{
  gint x,y;

  for (x = 0 ; x < MAX_SEGS; x++)
    for (y = 0 ; y < MAX_SEGS; y++)
      if (!((x + y) % 2))
	tileactions[x][y] |= (((do_horz) ? HORIZONTAL : 0) |
			      ((do_vert) ? VERTICAL : 0));
}

static void
tileit_radio_update (GtkWidget *widget,
		     gpointer   data)
{
  gimp_radio_button_update (widget, data);

  if (GTK_TOGGLE_BUTTON (widget)->active)
    {
      switch (exp_call.type)
	{
	case ALL:
	  /* Clear current settings */
	  memset (tileactions, 0, sizeof (tileactions));
	  all_update ();
	  break;

	case ALT:
	  /* Clear current settings */
	  memset (tileactions, 0, sizeof (tileactions));
	  alt_update ();
	  break;

	case EXPLICT:
	  explict_update (FALSE);
	  break;
	}

      dialog_update_preview ();
    }
}             


static void
tileit_scale_update (GtkAdjustment *adjustment,
		     gpointer       data)
{
  gimp_int_adjustment_update (adjustment, data);

  dialog_update_preview ();
} 

static void
tileit_reset (GtkWidget *widget,
	      gpointer   data)
{
  Reset_Call *r = (Reset_Call *) data;

  memset (tileactions, 0, sizeof (tileactions));

  g_signal_handlers_block_by_func (G_OBJECT (r->htoggle),
                                   tileit_hvtoggle_update,
                                   &do_horz);
  g_signal_handlers_block_by_func (G_OBJECT (r->vtoggle),
                                   tileit_hvtoggle_update,
                                   &do_vert);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (r->htoggle), FALSE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (r->vtoggle), FALSE);

  g_signal_handlers_unblock_by_func (G_OBJECT (r->htoggle),
                                     tileit_hvtoggle_update,
                                     &do_horz);
  g_signal_handlers_unblock_by_func (G_OBJECT (r->vtoggle),
                                     tileit_hvtoggle_update,
                                     &do_vert);

  do_horz = do_vert = FALSE; 

  dialog_update_preview ();
} 


/* Could avoid almost dup. functions by using a field in the data 
 * passed.  Must still pass the data since used in sig blocking func.
 */

static void
tileit_exp_update (GtkWidget *widget,
		   gpointer   applied)
{
  explict_update (TRUE);
  dialog_update_preview ();
}

static void
tileit_exp_update_f (GtkWidget *widget,
		     gpointer   applied)
{
  explict_update (FALSE);
  dialog_update_preview ();
}

/* Cache the preview image - updates are a lot faster. */
/* The preview_cache will contain the small image */

static void
cache_preview (void)
{
  GimpPixelRgn src_rgn;
  gint y,x;
  guchar *src_rows;
  guchar *p;
  gint isgrey = FALSE;

  gimp_pixel_rgn_init (&src_rgn, tileitdrawable,
		       sel_x1, sel_y1, sel_width, sel_height, FALSE, FALSE);

  src_rows = g_new (guchar, sel_width * 4); 
  p = tint.pv_cache = g_new (guchar, preview_width * preview_height * 4);

  img_width  = gimp_drawable_width (tileitdrawable->drawable_id);
  img_height = gimp_drawable_height (tileitdrawable->drawable_id);

  tint.img_bpp = gimp_drawable_bpp (tileitdrawable->drawable_id);   

  has_alpha = gimp_drawable_has_alpha (tileitdrawable->drawable_id);

  if (tint.img_bpp < 3)
    {
      tint.img_bpp = 3 + has_alpha;
    }

  switch (gimp_drawable_type (tileitdrawable->drawable_id))
    {
    case GIMP_GRAYA_IMAGE:
    case GIMP_GRAY_IMAGE:
      isgrey = TRUE;
      break;
    default:
      isgrey = FALSE;
      break;
    }

  for (y = 0; y < preview_height; y++)
    {
      gimp_pixel_rgn_get_row (&src_rgn,
			      src_rows,
			      sel_x1,
			      sel_y1 + (y * sel_height) / preview_height,
			      sel_width);

      for (x = 0; x < (preview_width); x ++)
	{
	  /* Get the pixels of each col */
	  gint i;
	  for (i = 0 ; i < 3; i++)
	    p[x * tint.img_bpp + i] =
	      src_rows[((x * sel_width) / preview_width) * src_rgn.bpp +
		      ((isgrey) ? 0 : i)]; 
	  if (has_alpha)
	    p[x * tint.img_bpp + 3] =
	      src_rows[((x * sel_width) / preview_width) * src_rgn.bpp +
		      ((isgrey) ? 1 : 3)];
	}
      p += (preview_width * tint.img_bpp);
    }
  g_free (src_rows);
}

static void
tileit_get_pixel (gint    x,
		  gint    y,
		  guchar *pixel)
{
  static gint row  = -1;
  static gint col  = -1;
  
  gint    newcol, newrow;
  gint    newcoloff, newrowoff;
  guchar *p;
  int     i;
  
  if ((x < 0) || (x >= img_width) || (y < 0) || (y >= img_height)) {
    pixel[0] = 0;
    pixel[1] = 0;
    pixel[2] = 0;
    pixel[3] = 0;
    
    return;
  }
  
  newcol    = x / tile_width;
  newcoloff = x % tile_width;
  newrow    = y / tile_height;
  newrowoff = y % tile_height;
  
  if ((col != newcol) || (row != newrow) || (the_tile == NULL)) {
    if (the_tile != NULL)
      gimp_tile_unref(the_tile, FALSE);
    
    the_tile = gimp_drawable_get_tile(tileitdrawable, FALSE, newrow, newcol);
    gimp_tile_ref(the_tile);
    
    col = newcol;
    row = newrow;
  } 
  
  p = the_tile->data + the_tile->bpp * (the_tile->ewidth * newrowoff + newcoloff);
  
  for (i = img_bpp; i; i--)
    *pixel++ = *p++;
}


static void
do_tiles(void)
{
  GimpPixelRgn dest_rgn;
  gpointer  pr;
  gint      progress, max_progress;
  guchar   *dest_row;
  guchar   *dest;
  gint      row, col;
  guchar    pixel[4];
  int 	    nc,nr;
  int       i;
  
  /* Initialize pixel region */
  
  gimp_pixel_rgn_init(&dest_rgn, tileitdrawable, sel_x1, sel_y1, sel_width, sel_height, TRUE, TRUE);
  
  progress     = 0;
  max_progress = sel_width * sel_height;
  
  img_bpp = gimp_drawable_bpp(tileitdrawable->drawable_id);
  
  for (pr = gimp_pixel_rgns_register(1, &dest_rgn);
       pr != NULL; pr = gimp_pixel_rgns_process(pr)) {
    dest_row = dest_rgn.data;
    
    for (row = dest_rgn.y; row < (dest_rgn.y + dest_rgn.h); row++) {
      dest = dest_row;
      
      for (col = dest_rgn.x; col < (dest_rgn.x + dest_rgn.w); col++)
	{
	  int an_action;
	  
	  an_action = 
	    tiles_xy(sel_width,
		     sel_height,
		     col-sel_x1,row-sel_y1,
		     &nc,&nr);
	  tileit_get_pixel(nc+sel_x1,nr+sel_y1,pixel);
	  for (i = 0; i < img_bpp; i++)
	    *dest++ = pixel[i];
	  
	  if(an_action && has_alpha)
	    {
	      dest--;
	      *dest = ((*dest)*opacity)/100;
	      dest++;
	    }
	}
      dest_row += dest_rgn.rowstride;
    } 
    
    progress += dest_rgn.w * dest_rgn.h;
    gimp_progress_update((double) progress / max_progress);
  }
  
  if (the_tile != NULL) {
    gimp_tile_unref(the_tile, FALSE);
    the_tile = NULL;
  }
  
  gimp_drawable_flush(tileitdrawable);
  gimp_drawable_merge_shadow(tileitdrawable->drawable_id, TRUE);
  gimp_drawable_update(tileitdrawable->drawable_id,
		       sel_x1, sel_y1, sel_width, sel_height);
} 


/* Get the xy pos and any action */
static gint
tiles_xy(gint width,
	 gint height,
	 gint x,
	 gint y,
	 gint *nx,
	 gint *ny)
{
  gint px,py;
  gint rnum,cnum; 
  gint actiontype;
  gdouble rnd = 1 - (1.0/(gdouble)itvals.numtiles) +0.01;

  rnum = y*itvals.numtiles/height;

  py = (y*itvals.numtiles)%height;
  px = (x*itvals.numtiles)%width; 
  cnum = x*itvals.numtiles/width;
      
  if((actiontype = tileactions[cnum][rnum]))
    {
      if(actiontype & HORIZONTAL)
	{
	  gdouble pyr;
	  pyr =  height - y - 1 + rnd;
	  py = ((int)(pyr*(gdouble)itvals.numtiles))%height;
	}
      
      if(actiontype & VERTICAL)
	{
	  gdouble pxr;
	  pxr = width - x - 1 + rnd;
	  px = ((int)(pxr*(gdouble)itvals.numtiles))%width; 
	}
    }
  
  *nx = px;
  *ny = py;

  return(actiontype);
}


/* Given a row then srink it down a bit */
static void
do_tiles_preview(guchar *dest_row, 
	    guchar *src_rows,
	    gint width,
	    gint dh,
	    gint height,
	    gint bpp)
{
  gint x;
  gint i;
  gint px,py;
  gint rnum,cnum; 
  gint actiontype;
  gdouble rnd = 1 - (1.0/(gdouble)itvals.numtiles) +0.01;

  rnum = dh*itvals.numtiles/height;

  for (x = 0; x < width; x ++) 
    {
      
      py = (dh*itvals.numtiles)%height;
      
      px = (x*itvals.numtiles)%width; 
      cnum = x*itvals.numtiles/width;
      
      if((actiontype = tileactions[cnum][rnum]))
	{
	  if(actiontype & HORIZONTAL)
	    {
	      gdouble pyr;
	      pyr =  height - dh - 1 + rnd;
	      py = ((int)(pyr*(gdouble)itvals.numtiles))%height;
	    }
	  
	  if(actiontype & VERTICAL)
	    {
	      gdouble pxr;
	      pxr = width - x - 1 + rnd;
	      px = ((int)(pxr*(gdouble)itvals.numtiles))%width; 
	    }
	}

      for (i = 0 ; i < bpp; i++ )
	dest_row[x*tint.img_bpp+i] = 
	  src_rows[(px + (py*width))*bpp+i]; 

      if(has_alpha && actiontype)
	dest_row[x*tint.img_bpp + (bpp - 1)] = 
	  (dest_row[x*tint.img_bpp + (bpp - 1)]*opacity)/100;

    }
}

static void
dialog_update_preview (void)
{
  gint y;
  gint check, check_0, check_1;  

  for (y = 0; y < preview_height; y++)
    {
      if ((y / GIMP_CHECK_SIZE) & 1)
	{
	  check_0 = GIMP_CHECK_DARK * 255;
	  check_1 = GIMP_CHECK_LIGHT * 255;
	}
      else
	{
	  check_0 = GIMP_CHECK_LIGHT * 255;
	  check_1 = GIMP_CHECK_DARK * 255;
	}

      do_tiles_preview (tint.preview_row,
			tint.pv_cache,
			preview_width,
			y,
			preview_height,
			tint.img_bpp);

      if (tint.img_bpp > 3)
	{
	  gint i, j;

	  for (i = 0, j = 0 ; i < sizeof (tint.preview_row); i += 4, j += 3 )
	    {
	      gint alphaval;

	      if (((i/4) / GIMP_CHECK_SIZE) & 1)
		check = check_0;
	      else
		check = check_1;

	      alphaval = tint.preview_row[i + 3];

	      tint.preview_row[j] = 
		check + (((tint.preview_row[i] - check)*alphaval)/255);
	      tint.preview_row[j + 1] = 
		check + (((tint.preview_row[i + 1] - check)*alphaval)/255);
	      tint.preview_row[j + 2] = 
		check + (((tint.preview_row[i + 2] - check)*alphaval)/255);
	    }
	}

      gtk_preview_draw_row (GTK_PREVIEW (tint.preview),
			    tint.preview_row, 0, y, preview_width);
    }

  draw_explict_sel ();

  gtk_widget_queue_draw (tint.preview);
}
