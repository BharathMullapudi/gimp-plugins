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
 * Some of this code was taken from the Whirl plug-in
 * which was copyrighted by Federico Mena Quintero (as below).
 * 
 * Whirl plug-in --- distort an image into a whirlpool
 * Copyright (C) 1997 Federico Mena Quintero           
 *
 */

/* Change log:
 * 0.9 First public release. 
 * 0.95 Second release.
 * 
 * 0.96 Added patch from  Rob Saunders that introduces a isometric type grid
 *      Removed use of gtk_idle* stuff on position update. Not required.
 *
 * 1.0  Fixed to work with the new gtk+-0.99.4 (tooltips stuff has changed).  
 * 
 * 1.1  Fixed crashes when objects not fully defined
 * 
 * 1.2  More bug fixes and prevent gtk warning when creating new figs
 * 
 * 1.3  Portability fixes and fixed bug reports 257 and 258 from and 81 & 101 & 133
 *      http://www.wilberworks.com/bugs.cgi
 */

#include "config.h"

#include <glib.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <string.h>
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#include <ctype.h>
#include <gtk/gtk.h>

#ifdef G_OS_WIN32
#  include <io.h>
#  ifndef W_OK
#    define W_OK 2
#  endif
#  ifndef S_ISDIR
#    define S_ISDIR(m) ((m) & _S_IFDIR)
#  endif
#  ifndef S_ISREG
#    define S_ISREG(m) ((m) & _S_IFREG)
#  endif
#endif

#if defined (GTK_CHECK_VERSION) && GTK_CHECK_VERSION (1,3,0)
#define gdk_root_parent (*gdk_parent_root)
#endif

#ifdef G_OS_WIN32
extern __declspec(dllimport) void *gdk_root_parent;
#else
extern void * gdk_root_parent;
#endif

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"

#include "pix_data.h"

/***** Magic numbers *****/

#define PREVIEW_SIZE 400
#define SCALE_WIDTH  120

#define MIN_GRID 10
#define MAX_GRID 50
#define MAX_UNDO 10
#define MIN_UNDO 1
#define MAX_LOAD_LINE 256
#define SMALL_PREVIEW_SZ 48
#define BRUSH_PREVIEW_SZ 32
#define GFIG_HEADER "GFIG Version 0.1\n"

#define PREVIEW_MASK   GDK_EXPOSURE_MASK | \
                       GDK_MOTION_NOTIFY | \
		       GDK_POINTER_MOTION_MASK | \
                       GDK_BUTTON_PRESS_MASK | \
		       GDK_BUTTON_RELEASE_MASK | \
		       GDK_BUTTON_MOTION_MASK | \
		       GDK_KEY_PRESS_MASK | \
		       GDK_KEY_RELEASE_MASK 

static GimpDrawable *gfig_select_drawable;
static GtkWidget *gfig_preview;
static GtkWidget *pic_preview;
static GtkWidget *gfig_gtk_list;
static gint       gfig_preview_exp_id;
static gint32     gfig_image;
static gint32     gfig_drawable;
static GtkWidget *brush_page_pw;
static GtkWidget *brush_sel_button;

static gint   tile_width, tile_height;
static gint   img_width, img_height, img_bpp, real_img_bpp;

static void      query  (void);
static void      run    (gchar    *name,
			 gint      nparams,
			 GimpParam   *param,
			 gint     *nreturn_vals,
			 GimpParam  **return_vals);

static gint      gfig_dialog               (void);
static void      gfig_ok_callback          (GtkWidget *widget,
					    gpointer   data);
static void      gfig_paint_callback       (GtkWidget *widget,
					    gpointer   data);
static void      gfig_clear_callback       (GtkWidget *widget,
					    gpointer   data);
static void      gfig_undo_callback        (GtkWidget *widget,
					    gpointer   data);
static gint      gfig_preview_expose       (GtkWidget *widget,
					    GdkEvent  *event);
static gint      pic_preview_expose        (GtkWidget *widget,
					    GdkEvent  *event);
static gint      gfig_preview_events       (GtkWidget *widget,
					    GdkEvent  *event);
static gint      gfig_brush_preview_events (GtkWidget *widget,
					    GdkEvent  *event);

static void      gfig_scale_update_scale   (GtkAdjustment *adjustment,
					    gdouble       *value);

static void      gfig_scale2img_update     (GtkWidget *widget,
					    gpointer   data);

static gint      gfig_scale_x              (gint       x);
static gint      gfig_scale_y              (gint       y);
static gint      gfig_invscale_x           (gint       x);
static gint      gfig_invscale_y           (gint       y);
static GdkGC *   gfig_get_grid_gc          (GtkWidget *widget,
					    gint       gctype);
static void      gfig_pos_enable           (GtkWidget *widget,
					    gpointer   data);

static gint      list_button_press         (GtkWidget      *widget,
					    GdkEventButton *event,
					    gpointer        data);

static void      rescan_button_callback    (GtkWidget *widget,
					    gpointer   data);
static void      load_button_callback      (GtkWidget *widget,
					    gpointer   data);
static void      save_button_callback      (GtkWidget *widget,
					    gpointer   data);
static void      new_button_callback       (GtkWidget *widget,
					    gpointer   data);
static void   gfig_do_delete_gfig_callback (GtkWidget *widget,
					    gboolean   delete,
					    gpointer   data);
static void      gfig_delete_gfig_callback (GtkWidget *widget,
					    gpointer   data);
static void      edit_button_callback      (GtkWidget *widget,
					    gpointer   data);
static void      merge_button_callback     (GtkWidget *widget,
					    gpointer   data);
static void      about_button_callback     (GtkWidget *widget,
					    gpointer   data);
static void      reload_button_callback    (GtkWidget *widget,
					    gpointer   data);

static void      do_gfig                   (void);
static void      dialog_update_preview     (void);

static void      draw_grid_clear           (GtkWidget *widget,
					    gpointer   data);
static void      toggle_show_image         (GtkWidget *widget,
					    gpointer   data);
static void      toggle_tooltips           (GtkWidget *widget,
					    gpointer   data);
static void      toggle_obj_type           (GtkWidget *widget,
					    gpointer   data);
static void      draw_grid                 (GtkWidget *widget,
					    gpointer   data);

static void      gfig_new_gc               (void);
static void      find_grid_pos             (GdkPoint  *p,
					    GdkPoint  *gp,
					    guint      state);

static void      brush_list_button_callback (GtkWidget *widget,
					     gpointer   data);
static gint      calculate_point_to_line_distance (GdkPoint *p,
						   GdkPoint *A,
						   GdkPoint *B,
						   GdkPoint *I);

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};

/* The types of an object */
/* Also includes actions that can be performed on objects */

typedef enum
{
  LINE,
  CIRCLE,
  ELLIPSE,
  ARC,
  POLY,
  STAR,
  SPIRAL,
  BEZIER,
  MOVE_OBJ,
  MOVE_POINT,
  COPY_OBJ,
  MOVE_COPY_OBJ,
  DEL_OBJ,
  NULL_OPER
} DobjType;

typedef enum
{
  RECT_GRID = 0,
  POLAR_GRID,
  ISO_GRID
} GridType;

typedef enum
{
  ORIGINAL_LAYER = 0,
  SINGLE_LAYER,
  MULTI_LAYER
} DrawonLayers;

typedef enum
{
  LAYER_TRANS_BG = 0,
  LAYER_BG_BG,
  LAYER_FG_BG,
  LAYER_WHITE_BG,
  LAYER_COPY_BG
} LayersBGType;

typedef enum
{
  PAINT_BRUSH_TYPE = 0,
  PAINT_SELECTION_TYPE,
  PAINT_SELECTION_FILL_TYPE
} PaintType;

typedef enum
{
  BRUSH_BRUSH_TYPE = 0,
  BRUSH_PENCIL_TYPE,
  BRUSH_AIRBRUSH_TYPE,
  BRUSH_PATTERN_TYPE
} BrushType;


#define GRID_TYPE_MENU 1
#define GRID_RENDER_MENU 2
#define GRID_IGNORE 0 
#define GRID_HIGHTLIGHT 1
#define GRID_RESTORE 2

#define GFIG_BLACK_GC -2
#define GFIG_WHITE_GC -3
#define GFIG_GREY_GC -4

#define PAINT_LAYERS_MENU 1
#define PAINT_BGS_MENU 2
#define PAINT_TYPE_MENU 3

#define SELECT_TYPE_MENU 1
#define SELECT_ARCTYPE_MENU 2
#define SELECT_TYPE_MENU_FILL 3
#define SELECT_TYPE_MENU_WHEN 4

#define OBJ_SELECT_GT 1
#define OBJ_SELECT_LT 2
#define OBJ_SELECT_EQ 4


typedef struct
{
  gint     gridspacing;
  GridType gridtype;
  gint     drawgrid;
  gint     snap2grid;
  gint     lockongrid;
  gint     showcontrol;
} GfigOpts;

/* Must keep in step with the above */
typedef struct
{
  void      *gridspacing;
  GtkWidget *gridtypemenu;
  GtkWidget *drawgrid;
  GtkWidget *snap2grid;
  GtkWidget *lockongrid;
  GtkWidget *showcontrol;
} GfigOptWidgets;

static GfigOptWidgets gfig_opt_widget;

typedef struct
{
  GfigOpts      opts;
  gint          showimage;
  gint          maxundo;
  gint          showpos;
  gdouble       brushfade;
  gdouble       brushgradient;
  gdouble       airbrushpressure;
  gint          showtooltips;
  DrawonLayers  onlayers;
  LayersBGType  onlayerbg;
  PaintType     painttype;
  gint          reverselines;
  gint          scaletoimage;
  gdouble       scaletoimagefp;
  gint          approxcircles;
  BrushType    brshtype;
  DobjType      otype;
} SelectItVals;

/* Values when first invoked */
static SelectItVals selvals =
{
  {
    MIN_GRID + (MAX_GRID - MIN_GRID)/2, /* Gridspacing */
    RECT_GRID, /* Default to rectangle type */
    0,  /* drawgrid */
    0,  /* snap2grid */
    0,  /* lockongrid */
    1,  /* show control points */
  },
  0,  /* show image */
  MIN_UNDO + (MAX_UNDO - MIN_UNDO)/2,  /* Max level of undos */
  FALSE, /* Show pos updates */
  0.0, /* Brush fade */
  0.0, /* Brush gradient */
  20.0, /* Air bursh pressure */
  TRUE,  /* show Tool tips */
  ORIGINAL_LAYER, /* Draw all objects on one layer */
  LAYER_TRANS_BG, /* New layers background */
  PAINT_BRUSH_TYPE, /* Default to use brushes */
  FALSE, /* reverse lines */
  TRUE, /* Scale to image when painting */
  1.0, /* Scale to image fp */
  FALSE, /* Approx circles by drawing lines */
  BRUSH_BRUSH_TYPE, /* Default to use a brush */
  LINE /* Initial object type */
};

typedef enum
{
  ADD=0,
  SUBTRACT=1,
  REPLACE=2,
  INTERSECT=3
} SelectionType;
    

typedef enum
{
  ARC_SEGMENT,
  ARC_SECTOR
} ArcType;

typedef enum
{
  FILL_FOREGROUND = 0,
  FILL_BACKGROUND = 1,
  FILL_PATTERN = 2
} FillType;

typedef enum
{
  FILL_EACH = 0,
  FILL_AFTER
} FillWhen;

struct selection_option
{
  SelectionType type; /* ADD etc .. */
  gint antia; /* Boolean for Antia */
  gint feather; /* Feather it ? */
  gdouble feather_radius; /* Radius to feather */
  ArcType as_pie; /* Arc type selection segment/sector */
  FillType fill_type; /* Fill type for selection */
  FillWhen fill_when; /* Fill on each selection or after all? */
  gdouble fill_opacity; /* You can guess this one */
} selopt =
{
  ADD, /* type */
  FALSE, /* Antia */
  FALSE, /* Feather */
  10.0, /* feather radius */
  ARC_SEGMENT,  /* Arc as a segment */
  FILL_PATTERN, /* Fill as pattern */
  FILL_EACH, /* Fill after each selection */
  100.0, /* Max opacity */
};


static GList *gfig_path_list = NULL;
static GList *gfig_list      = NULL;
static gint   line_no;

static gint poly_num_sides    = 3; /* Default to three sided object */
static gint star_num_sides    = 3; /* Default to three sided object */
static gint spiral_num_turns  = 4; /* Default to 4 turns */
static gint spiral_toggle     = 0; /* 0 = clockwise -1 = anti-clockwise */
static gint bezier_closed     = 0; /* Closed curve 0 = false 1 = true */
static gint bezier_line_frame = 0; /* Show frame = false 1 = true */

static gint obj_show_single = -1; /* -1 all >= 0 object number */

/* Structures etc for the objects */
/* Points used to draw the object  */

typedef struct DobjPoints
{
  struct DobjPoints * next;
  GdkPoint pnt;
  gint found_me;
} DobjPoints;


struct Dobject; /* fwd declaration for DobjFunc */

typedef void            (*DobjFunc) (struct Dobject *);
typedef struct Dobject *(*DobjGenFunc) (struct Dobject *);
typedef struct Dobject *(*DobjLoadFunc) (FILE *);
typedef void            (*DobjSaveFunc) (struct Dobject *, FILE *);

/* The object itself */
typedef struct Dobject
{
  DobjType      type; /* What is the type? */
  gpointer      type_data; /* Extra data needed by the object */
  DobjPoints   *points; /* List of points */
  DobjFunc      drawfunc; /* How do I draw myself */
  DobjFunc      paintfunc; /* Draw me on canvas */
  DobjGenFunc   copyfunc;  /* copy */
  DobjLoadFunc  loadfunc;  /* Load this type of object */
  DobjSaveFunc  savefunc;  /* Save me out */
} Dobject;


static Dobject *obj_creating; /* Object we are creating */
static Dobject *tmp_line; /* Needed when drawing lines */
static Dobject *tmp_bezier; /* Neeed when drawing bezier curves */

typedef struct DAllObjs
{
  struct DAllObjs *next; 
  Dobject         *obj; /* Object on list */
} DAllObjs;

/* States of the object */
#define GFIG_OK       0x0
#define GFIG_MODIFIED 0x1
#define GFIG_READONLY 0x2

typedef struct DFigObj
{
  gchar     *name;     /* Trailing name of file  */
  gchar     *filename; /* Filename itself */
  gchar     *draw_name;/* Name of the drawing */
  gfloat     version;     /* Version number of data file */
  GfigOpts   opts;    /* Options enforced when fig saved */
  DAllObjs  *obj_list; /* Objects that make up this list */
  gint       obj_status;    /* See above for possible values */
  GtkWidget *list_item;
  GtkWidget *label_widget;
  GtkWidget *pixmap_widget;
} GFigObj;  


typedef struct BrushDesc
{
  gchar  *bname; /* name of the brush */
  gint32  width;  /* Width of brush */
  gint32  height;  /* Height of brush */
  guchar *pv_buf; /* Buffer where brush placed */
  gint16  x_off;
  gint16  y_off;
  gint    bpp; /* Depth - should ALWAYS be the same for all BrushDesc */
} BrushDesc;

static GFigObj  *current_obj;
static Dobject  *operation_obj;
static GdkPoint *move_all_pnt; /* Point moving all from */
static GFigObj  *pic_obj;
static DAllObjs *undo_table[MAX_UNDO];
static gint      need_to_scale;
static gint32    brush_image_ID = -1;

static GtkWidget *undo_widget;
static GtkWidget *gfig_op_menu; /* Popup menu in the list box */
static GtkWidget *delete_frame_to_freeze; /* Top preview frame window */
static GtkWidget *fade_out_hbox;   /* Fade out widget in brush page */
static GtkWidget *gradient_hbox;   /* Gradient widget in brush page */
static GtkWidget *pressure_hbox;   /* Pressure widget in brush page */
static GtkWidget *pencil_hbox;     /* Dummy widget in brush page */
static GtkWidget *pos_label;       /* XY pos marker */
static GtkWidget *brush_page_widget; /* Widget for the brush part of notebook */
static GtkWidget *select_page_widget; /* Widget for the selection part
				       * of notebook */

static gint undo_water_mark = -1; /* Last slot filled in -1 = no undo */
static gint drawing_pic = FALSE;  /* If true drawing to the small preview */
static GtkWidget *status_label_dname;
static GtkWidget *status_label_fname;
static GFigObj   *gfig_obj_for_menu; /* More static data -
				      * need to know which object was selected*/
static GtkWidget *save_menu_item;  
static GtkWidget *save_button;


/* Don't up just like BIGGG source files? */

static void       object_start            (GdkPoint *pnt, gint);
static void       object_operation        (GdkPoint *pnt, gint);
static void       object_operation_start  (GdkPoint *pnt, gint shift_down);
static void       object_operation_end    (GdkPoint *pnt, gint);
static void       object_end              (GdkPoint *pnt, gint shift_down);
static void       object_update           (GdkPoint * pnt);
static void       add_to_all_obj          (GFigObj * fobj, Dobject *obj);
static void       d_delete_dobjpoints     (DobjPoints *);
static Dobject  * d_new_line              (gint x, gint y);
static Dobject  * d_new_circle            (gint x, gint y);
static DAllObjs * copy_all_objs           (DAllObjs *objs);
static void       setup_undo              (void);
static void       d_pnt_add_line          (Dobject *obj,
					   gint x, gint y, gint pos);
static GFigObj  * gfig_load               (const gchar *filename,
                                           const gchar *name);
static void       free_all_objs           (DAllObjs * objs);
static void       draw_objects            (DAllObjs *objs, gint show_single);
static Dobject  * d_load_line             (FILE *from);
static Dobject  * d_load_circle           (FILE *from);
static gchar    * get_line                (gchar *buf, gint s,
					   FILE * from, gint init);
static GFigObj  * gfig_new                (void);
static void       clear_undo              (void);
static void       list_button_update      (GFigObj *obj);
static void       prepend_to_all_obj      (GFigObj *fobj, DAllObjs *nobj);
static void       gfig_update_stat_labels (void);
static void       gfig_obj_modified       (GFigObj *obj, gint stat_type);
static void       gfig_op_menu_create     (GtkWidget *window);
static void       gridtype_menu_callback  (GtkWidget *widget, gpointer data);
static void       draw_one_obj            (Dobject * obj);
static void       d_save_poly             (Dobject * obj, FILE *to);
static Dobject  * d_load_poly             (FILE *from);
static void       d_draw_poly             (Dobject *obj);
static void       d_paint_poly            (Dobject *obj);
static Dobject  * d_copy_poly             (Dobject * obj);
static Dobject  * d_new_poly              (gint x, gint y);
static void       d_update_poly           (GdkPoint *pnt);
static void       d_poly_start            (GdkPoint *pnt, gint shift_down);
static void       d_poly_end              (GdkPoint *pnt, gint shift_down);
static void       d_save_star             (Dobject * obj, FILE *to);
static Dobject  * d_load_star             (FILE *from);
static void       d_draw_star             (Dobject *obj);
static void       d_paint_star            (Dobject *obj);
static Dobject  * d_copy_star             (Dobject * obj);
static Dobject  * d_new_star              (gint x, gint y);
static void       d_update_star           (GdkPoint *pnt);
static void       d_star_start            (GdkPoint *pnt, gint shift_down);
static void       d_star_end              (GdkPoint *pnt, gint shift_down);
static Dobject  * d_load_spiral           (FILE *from);
static void       d_draw_spiral           (Dobject *obj);
static void       d_paint_spiral          (Dobject *obj);
static Dobject  * d_copy_spiral           (Dobject * obj);
static Dobject  * d_new_spiral            (gint x, gint y);
static void       d_update_spiral         (GdkPoint *pnt);
static void       d_spiral_start          (GdkPoint *pnt, gint shift_down);
static void       d_spiral_end            (GdkPoint *pnt, gint shift_down);

static Dobject  * d_load_bezier           (FILE *from);
static void       d_draw_bezier           (Dobject *obj);
static void       d_paint_bezier          (Dobject *obj);
static Dobject  * d_copy_bezier           (Dobject * obj);
static Dobject  * d_new_bezier            (gint x, gint y);
static void       d_update_bezier         (GdkPoint *pnt);
static void       d_bezier_start          (GdkPoint *pnt, gint shift_down);
static void       d_bezier_end            (GdkPoint *pnt, gint shift_down);

static void       new_obj_2edit           (GFigObj *obj);
static Dobject  * d_new_ellipse           (gint x, gint y);
static Dobject  * d_load_ellipse          (FILE *from);
static Dobject  * d_new_arc               (gint x, gint y);
static Dobject  * d_load_arc              (FILE *from);
static gint       load_options            (GFigObj *gfig, FILE *fp);
static gint       gfig_obj_counts         (DAllObjs * objs);

static void    gfig_brush_fill_preview_xy (GtkWidget *pw, gint x , gint y);


/* globals */

static gint    gfig_run;
static GdkGC  *gfig_gc;
static GdkGC  *grid_hightlight_drawgc;
static gint    grid_gc_type = GTK_STATE_NORMAL;
static guchar *pv_cache = NULL;
static guchar  preview_row[PREVIEW_SIZE*4];

/* Stuff for the preview bit */
static gint    sel_x1, sel_y1, sel_x2, sel_y2;
static gint    sel_width, sel_height;
static gint    preview_width, preview_height;
static gint    has_alpha;
static gdouble scale_x_factor, scale_y_factor;
static gdouble org_scale_x_factor, org_scale_y_factor;

MAIN ()

static void
query (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE, "image", "Input image (unused)" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable" },
    { GIMP_PDB_INT32, "dummy", "dummy" } 
  };
  static gint nargs = sizeof (args) / sizeof (args[0]);

  gimp_install_procedure ("plug_in_gfig",
			  "Create Geometrical shapes with the Gimp",
			  "More here later",
			  "Andy Thomas",
			  "Andy Thomas",
			  "1997",
			  N_("<Image>/Filters/Render/Gfig..."),
			  "RGB*, GRAY*",
			  GIMP_PLUGIN,
			  nargs, 0,
			  args, NULL);
}

static void
run (gchar    *name,
     gint      nparams,
     GimpParam   *param,
     gint     *nreturn_vals,
     GimpParam  **return_vals)
{
  GimpParam * values = g_new (GimpParam, 1);
  GimpDrawable *drawable;
  GimpRunModeType run_mode;
  GimpPDBStatusType status = GIMP_PDB_SUCCESS;

  gint pwidth, pheight;

  /*kill (getpid (), 19);*/

  run_mode = param[0].data.d_int32;
  gfig_image = param[1].data.d_image;
  gfig_drawable = param[2].data.d_drawable;

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  gfig_select_drawable = drawable =
    gimp_drawable_get (param[2].data.d_drawable);

  tile_width  = gimp_tile_width ();
  tile_height = gimp_tile_height ();

  /* TMP Hack - clear any selections */
  gimp_selection_clear (gfig_image);

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

  org_scale_x_factor = scale_x_factor =
    (gdouble) sel_width / (gdouble) preview_width;
  org_scale_y_factor = scale_y_factor =
    (gdouble) sel_height / (gdouble) preview_height;
  
  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE:
      /*gimp_get_data ("plug_in_gfig", &selvals);*/
      INIT_I18N_UI ();
      if (!gfig_dialog ())
	{
	  gimp_drawable_detach (drawable);
	  return;
	}
      break;

    case GIMP_RUN_NONINTERACTIVE:
      status = GIMP_PDB_CALLING_ERROR;
      break;

    case GIMP_RUN_WITH_LAST_VALS:
      /*gimp_get_data ("plug_in_gfig", &selvals);*/
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

      do_gfig ();

      if (run_mode != GIMP_RUN_NONINTERACTIVE)
	gimp_displays_flush ();

#if 0
      if (run_mode == GIMP_RUN_INTERACTIVE)
	gimp_set_data ("plug_in_gfig", &selvals, sizeof (SelectItVals));
#endif /* 0 */
    }
  else
    {
      status = GIMP_PDB_EXECUTION_ERROR;
    }

  values[0].data.d_status = status;

  gimp_drawable_detach (drawable);
}

/*
 *  Query gimprc for gfig-path, and parse it.
 */

static void
plug_in_parse_gfig_path (void)
{
  GList *fail_list = NULL;
  GList *list;
  gchar *gfig_path;

  if (gfig_path_list)
    gimp_path_free (gfig_path_list);
  
  gfig_path_list = NULL;
  
  gfig_path = gimp_gimprc_query ("gfig-path");

  if (!gfig_path)
    {
      gchar *gimprc = gimp_personal_rc_file ("gimprc");
      gchar *path = gimp_strescape
	("${gimp_dir}" G_DIR_SEPARATOR_S "gfig"
	 G_SEARCHPATH_SEPARATOR_S
	 "${gimp_data_dir}" G_DIR_SEPARATOR_S "gfig",
	 NULL);
      g_message (_("No gfig-path in gimprc:\n"
		   "You need to add an entry like\n"
		   "(gfig-path \"%s\")\n"
		   "to your %s file."),
		   path, gimprc);
      g_free (gimprc);
      g_free (path);
      return;
    }
  
  gfig_path_list = gimp_path_parse (gfig_path, 16, TRUE, &fail_list);

  g_free (gfig_path);

  if (fail_list)
    {
      GString *err =
	g_string_new (_("gfig-path misconfigured - "
			"the following directories were not found"));

      for (list = fail_list; list; list = g_list_next (list))
	{
	  g_string_append_c (err, '\n');
	  g_string_append (err, (gchar *) list->data);
	}

      g_message (err->str);

      g_string_free (err, TRUE);
      gimp_path_free (fail_list);
    }
}


/*
  Translate SPACE to "\\040", etc.
  Taken from gflare plugin
 */
static void
gfig_name_encode (gchar *dest,
		  gchar *src)
{
  gint cnt = MAX_LOAD_LINE - 1;

  while (*src && cnt--)
    {
      if (iscntrl (*src) || isspace (*src) || *src == '\\')
	{
	  sprintf (dest, "\\%03o", *src++);
	  dest += 4;
	}
      else
	*dest++ = *src++;
    }
  *dest = '\0';
}

/*
  Translate "\\040" to SPACE, etc.
 */
static void
gfig_name_decode (gchar *dest,
		  gchar *src)
{
  gint cnt = MAX_LOAD_LINE - 1;
  gint tmp;

  while (*src && cnt--)
    {
      if (*src == '\\' && *(src+1) && *(src+2) && *(src+3))
	{
	  sscanf (src+1, "%3o", &tmp);
	  *dest++ = tmp;
	  src += 4;
	}
      else
	*dest++ = *src++;
    }
  *dest = '\0';
}


/*
 * Load all gfig, which are founded in gfig-path-list, into gfig_list.
 * gfig-path-list must be initialized first. (plug_in_parse_gfig_path ())
 * based on code from Gflare.
 */

static gint
gfig_list_pos (GFigObj *gfig)
{
  GFigObj *g;
  gint n;
  GList *tmp;

  n = 0;

  for (tmp = gfig_list; tmp; tmp = g_list_next (tmp)) 
    {
      g = tmp->data;
      
      if (strcmp (gfig->draw_name, g->draw_name) <= 0)
	break;

      n++;
    }

  return n;
}

static gint
gfig_list_insert (GFigObj *gfig)
{
  gint n;

  /*
   *	Insert gfigs in alphabetical order
   */

  n = gfig_list_pos (gfig);

  gfig_list = g_list_insert (gfig_list, gfig, n);

#ifdef DEBUG
  printf ("gfig_list_insert %s => %d\n", gfig->draw_name, n);
#endif /* DEBUG */

  return n;
}

static void
gfig_free (GFigObj *gfig)
{
  g_assert (gfig != NULL);

  if (gfig->obj_list)
    free_all_objs (gfig->obj_list);

  g_free (gfig->name);
  g_free (gfig->filename);
  g_free (gfig->draw_name);

  g_free (gfig);
}

static void
gfig_free_everything (GFigObj *gfig)
{
  g_assert (gfig != NULL);

  if (gfig->filename)
    {
#ifdef DEBUG
      printf ("Removing filename '%s'\n", gfig->filename);
#endif /* DEBUG */
      remove (gfig->filename);
    }

  gfig_free (gfig);
}

static void
gfig_list_free_all (void)
{
  GList   *list;
  GFigObj *gfig;

  for (list = gfig_list; list; list = g_list_next (list))
    {
      gfig = (GFigObj *) list->data;
      gfig_free (gfig);
    }

  g_list_free (gfig_list);
  gfig_list = NULL;
}


static void
gfig_list_load_all (GList *plist)
{
  GFigObj *gfig;
  GList   *list;
  gchar	  *path;
  gchar	  *filename;
  DIR	  *dir;
  struct dirent *dir_ent;
  struct stat    filestat;
  gint err;

  /*  Make sure to clear any existing gfigs  */
  current_obj = pic_obj = NULL;
  gfig_list_free_all ();

  list = plist;
  while (list)
    {
      path = list->data;
      list = list->next;

      /* Open directory */
      dir = opendir (path);

      if (!dir)
	g_warning ("Error reading GFig directory \"%s\"", path);
      else
	{
	  while ((dir_ent = readdir (dir)))
	    {
	      filename = g_malloc (strlen (path) + strlen (dir_ent->d_name) + 1);

	      sprintf (filename, "%s%s", path, dir_ent->d_name);

	      /* Check the file and see that it is not a sub-directory */
	      err = stat (filename, &filestat);

	      if (!err && S_ISREG (filestat.st_mode))
		{
		  gfig = gfig_load (filename, dir_ent->d_name);
		  
		  if (gfig)
		    {
		      /* Read only ?*/
		      if (access (filename, W_OK))
			gfig->obj_status |= GFIG_READONLY;

		      gfig_list_insert (gfig);
		    }
		}

	      g_free (filename);
	    } /* while */
	  closedir (dir);
	} /* else */
    }

  if (!gfig_list)
    {
      /* lets have at least one! */
      gfig = gfig_new ();
      gfig->draw_name = g_strdup (_("First Gfig"));
      gfig_list_insert (gfig);
    }
  pic_obj = current_obj = gfig_list->data;  /* set to first entry */

}

static GFigObj *
gfig_new (void)
{
  GFigObj * new;

  new = g_new0 (GFigObj, 1);

  return new;
}

static void
gfig_load_objs (GFigObj *gfig,
		gint     load_count,
		FILE    *fp)
{
  Dobject *obj;
  gchar load_buf[MAX_LOAD_LINE];

  /* Loading object */
  /*kill (getpid (), 19);*/
  /* Read first line */
  while (load_count-- > 0)
    {
      obj = NULL;
      get_line (load_buf, MAX_LOAD_LINE, fp, 0);

      if (!strcmp (load_buf, "<LINE>"))
	{
	  obj = d_load_line (fp);
	}
      else if (!strcmp (load_buf, "<CIRCLE>"))
	{
	  obj = d_load_circle (fp);
	}
      else if (!strcmp (load_buf, "<ELLIPSE>"))
	{
	  obj = d_load_ellipse (fp);
	}
      else if (!strcmp (load_buf, "<POLY>"))
	{
	  obj = d_load_poly (fp);
	}
      else if (!strcmp (load_buf, "<STAR>"))
	{
	  obj = d_load_star (fp);
	}
      else if (!strcmp (load_buf, "<SPIRAL>"))
	{
	  obj = d_load_spiral (fp);
	}
      else if (!strcmp (load_buf, "<BEZIER>"))
	{
	  obj = d_load_bezier (fp);
	}
      else if (!strcmp (load_buf, "<ARC>"))
	{
	  obj = d_load_arc (fp);
	}
      else
	{
	  g_warning ("Unknown obj type file %s line %d\n", gfig->filename, line_no);
	}
      
      if (obj)
	{
	  add_to_all_obj (gfig, obj);
	}
    }
}

static GFigObj *
gfig_load (const gchar *filename,
           const gchar *name)
{
  GFigObj * gfig;
  FILE * fp;
  gchar load_buf[MAX_LOAD_LINE];
  gchar str_buf[MAX_LOAD_LINE];
  gint chk_count;
  gint load_count = 0;
  
  g_assert (filename != NULL);

#ifdef DEBUG
  printf ("Loading %s (%s)\n", filename, name);
#endif /* DEBUG */

  fp = fopen (filename, "r");
  if (!fp)
    {
      g_warning ("Error opening: %s", filename);
      return NULL;
    }

  gfig = gfig_new ();

  gfig->name = g_strdup (name);
  gfig->filename = g_strdup (filename);


  /* HEADER
   * draw_name
   * version
   * obj_list
   */

  get_line (load_buf, MAX_LOAD_LINE, fp, 1);

  if (strncmp (GFIG_HEADER, load_buf, strlen (load_buf)))
    {
      g_message ("File '%s' is not a gfig file", gfig->filename);
      return NULL;
    }
  
  get_line (load_buf, MAX_LOAD_LINE, fp, 0);
  sscanf (load_buf, "Name: %100s", str_buf);
  gfig_name_decode (load_buf, str_buf);
  gfig->draw_name = g_strdup (load_buf);

  get_line (load_buf, MAX_LOAD_LINE, fp, 0);
  sscanf (load_buf, "Version: %f", &gfig->version);

  get_line (load_buf, MAX_LOAD_LINE, fp, 0);
  sscanf (load_buf, "ObjCount: %d", &load_count);

  if (load_options (gfig, fp))
    {
      g_message ("File '%s' corrupt file - Line %d Option section incorrect",
		 filename, line_no);
      return NULL;
    }

  /*return (NULL);*/

  gfig_load_objs (gfig, load_count, fp);

  /* Check count ? */
  
  chk_count = gfig_obj_counts (gfig->obj_list);

  if (chk_count != load_count)
    {
      g_message ("File '%s' corrupt file - Line %d Object count to small",
		 filename, line_no);
      return NULL;
    }

  fclose (fp);

  if (!pic_obj)
    pic_obj = gfig;

  gfig->obj_status = GFIG_OK;

  return gfig;
}

static void
save_options (FILE *fp)
{
  /* Save options */
  fprintf (fp, "<OPTIONS>\n");
  fprintf (fp, "GridSpacing: %d\n", selvals.opts.gridspacing);
  if (selvals.opts.gridtype == RECT_GRID)
    fprintf (fp, "GridType: RECT_GRID\n");
  else if (selvals.opts.gridtype == POLAR_GRID)
    fprintf (fp, "GridType: POLAR_GRID\n");
  else if (selvals.opts.gridtype == ISO_GRID)
    fprintf (fp, "GridType: ISO_GRID\n");
  else fprintf (fp, "GridType: RECT_GRID\n"); /* If in doubt, default to RECT_GRID */
  fprintf (fp, "DrawGrid: %s\n", (selvals.opts.drawgrid)?"TRUE":"FALSE");
  fprintf (fp, "Snap2Grid: %s\n", (selvals.opts.snap2grid)?"TRUE":"FALSE");
  fprintf (fp, "LockOnGrid: %s\n", (selvals.opts.lockongrid)?"TRUE":"FALSE");
  /*  fprintf (fp, "ShowImage: %s\n", (selvals.opts.showimage)?"TRUE":"FALSE");*/
  fprintf (fp, "ShowControl: %s\n", (selvals.opts.showcontrol)?"TRUE":"FALSE");
  fprintf (fp, "</OPTIONS>\n");
}

static gint
load_bool (gchar *opt_buf,
	   gint  *toset)
{
  if (!strcmp (opt_buf, "TRUE"))
    *toset = 1;
  else if (!strcmp (opt_buf, "FALSE"))
    *toset = 0;
  else
    return (-1);

  return (0);
}

static void
update_options (GFigObj *old_obj)
{
  /* Save old vals */
  if (selvals.opts.gridspacing != old_obj->opts.gridspacing)
    {
      old_obj->opts.gridspacing = selvals.opts.gridspacing;
    }
  if (selvals.opts.gridtype != old_obj->opts.gridtype)
    {
      old_obj->opts.gridtype = selvals.opts.gridtype;
    }
  if (selvals.opts.drawgrid != old_obj->opts.drawgrid)
    {
      old_obj->opts.drawgrid = selvals.opts.drawgrid;
    }
  if (selvals.opts.snap2grid != old_obj->opts.snap2grid)
    {
      old_obj->opts.snap2grid = selvals.opts.snap2grid;
    }
  if (selvals.opts.lockongrid != old_obj->opts.lockongrid)
    {
      old_obj->opts.lockongrid = selvals.opts.lockongrid;
    }
  if (selvals.opts.showcontrol != old_obj->opts.showcontrol)
    {
      old_obj->opts.showcontrol = selvals.opts.showcontrol;
    }

  /* New vals */
  if (selvals.opts.gridspacing != current_obj->opts.gridspacing)
    {
      gtk_adjustment_set_value
	(GTK_ADJUSTMENT (gfig_opt_widget.gridspacing),
	 current_obj->opts.gridspacing);
    }
  if (selvals.opts.drawgrid != current_obj->opts.drawgrid)
    {
      gtk_toggle_button_set_active
	(GTK_TOGGLE_BUTTON (gfig_opt_widget.drawgrid),
	 current_obj->opts.drawgrid);
    }
  if (selvals.opts.snap2grid != current_obj->opts.snap2grid)
    {
      gtk_toggle_button_set_active
	(GTK_TOGGLE_BUTTON (gfig_opt_widget.snap2grid),
	 current_obj->opts.snap2grid);
    }
  if (selvals.opts.lockongrid != current_obj->opts.lockongrid)
    {
      gtk_toggle_button_set_active
	(GTK_TOGGLE_BUTTON (gfig_opt_widget.lockongrid),
	 current_obj->opts.lockongrid);
    }
  if (selvals.opts.showcontrol != current_obj->opts.showcontrol)
    {
      gtk_toggle_button_set_active
	(GTK_TOGGLE_BUTTON (gfig_opt_widget.showcontrol),
	 current_obj->opts.showcontrol);
    }
  if (selvals.opts.gridtype != current_obj->opts.gridtype)
    {
      gtk_option_menu_set_history
	(GTK_OPTION_MENU (gfig_opt_widget.gridtypemenu),
	 current_obj->opts.gridtype); 

      gridtype_menu_callback
	(gtk_menu_get_active
	 (GTK_MENU (gtk_option_menu_get_menu
		    (GTK_OPTION_MENU (gfig_opt_widget.gridtypemenu)))),
	 (gpointer) GRID_TYPE_MENU);
#ifdef DEBUG
      printf ("Gridtype set in options to ");
      if (current_obj->opts.gridtype == RECT_GRID)
	printf ("RECT_GRID\n");
      else if (current_obj->opts.gridtype == POLAR_GRID)
	printf ("POLAR_GRID\n");
      else if (current_obj->opts.gridtype == ISO_GRID)
	printf ("ISO_GRID\n");
      else printf ("NONE\n");
#endif /* DEBUG */
    }
}

static gint
load_options (GFigObj *gfig,
	      FILE    *fp)
{
  gchar load_buf[MAX_LOAD_LINE];
  gchar str_buf[MAX_LOAD_LINE];
  gchar opt_buf[MAX_LOAD_LINE];

  get_line (load_buf, MAX_LOAD_LINE, fp, 0);

#ifdef DEBUG
  printf ("load '%s'\n", load_buf);
#endif /* DEBUG */

  if (strcmp (load_buf, "<OPTIONS>"))
    return (-1);
  
  get_line (load_buf, MAX_LOAD_LINE, fp, 0);

#ifdef DEBUG
  printf ("opt line '%s'\n", load_buf);
#endif /* DEBUG */

  while (strcmp (load_buf, "</OPTIONS>"))
    {
      /* Get option name */
#ifdef DEBUG
      printf ("num = %d\n", sscanf (load_buf, "%s %s", str_buf, opt_buf));

      printf ("option %s val %s\n", str_buf, opt_buf);
#else
      sscanf (load_buf, "%s %s", str_buf, opt_buf);
#endif /* DEBUG */

      if (!strcmp (str_buf, "GridSpacing:"))
	{
	  /* Value is decimal */
	  int sp = 0;
	  sp = atoi (opt_buf);
	  if (sp <= 0)
	    return (-1);
	  gfig->opts.gridspacing = sp;
	}
      else if (!strcmp (str_buf, "DrawGrid:"))
	{
	  /* Value is bool */
	  if (load_bool (opt_buf, &gfig->opts.drawgrid))
	    return (-1);
	}
      else if (!strcmp (str_buf, "Snap2Grid:"))
	{
	  /* Value is bool */
	  if (load_bool (opt_buf, &gfig->opts.snap2grid))
	    return (-1);
	}
      else if (!strcmp (str_buf, "LockOnGrid:"))
	{
	  /* Value is bool */
	  if (load_bool (opt_buf, &gfig->opts.lockongrid))
	    return (-1);
	}
      else if (!strcmp (str_buf, "ShowControl:"))
	{
	  /* Value is bool */
	  if (load_bool (opt_buf, &gfig->opts.showcontrol))
	    return (-1);
	}
      else if (!strcmp (str_buf, "GridType:"))
	{
	  /* Value is string */
	  if (!strcmp (opt_buf, "RECT_GRID"))
	    gfig->opts.gridtype = RECT_GRID;
	  else if (!strcmp (opt_buf, "POLAR_GRID"))
	    gfig->opts.gridtype = POLAR_GRID;
	  else if (!strcmp (opt_buf, "ISO_GRID"))
	    gfig->opts.gridtype = ISO_GRID;
	  else
	    return (-1);
	}

      get_line (load_buf, MAX_LOAD_LINE, fp, 0);
#ifdef DEBUG
      printf ("opt line '%s'\n", load_buf);
#endif /* DEBUG */
    }  
  return (0);
}

static gint
gfig_obj_counts (DAllObjs *objs)
{
  gint count = 0;

  while (objs)
    {
      count++;
      objs = objs->next;
    }

  return (count);
}

static void
gfig_save_callbk (void)
{
  FILE *fp;
  DAllObjs * objs;
  gint count = 0;
  gchar *savename;
  gchar *message;
  gchar conv_buf[MAX_LOAD_LINE*3 +1];

  savename = current_obj->filename;

  fp = fopen (savename, "w+");
  
  if (!fp)
    {
      message = g_strconcat (_("Error opening: %s"), 
			     "\n",
			     _("Could not save."), 
			     savename);
      g_message (message);
      g_free (message);
      return;
    }

  /* Write header out */
  fputs (GFIG_HEADER, fp);
  
  /* 
   * draw_name 
   * version
   * obj_list
   *
   */

  gfig_name_encode (conv_buf, current_obj->draw_name);
  fprintf (fp, "Name: %s\n", conv_buf);
  fprintf (fp, "Version: %f\n", current_obj->version);
  objs = current_obj->obj_list;

  count = gfig_obj_counts (objs);

  fprintf (fp, "ObjCount: %d\n", count);

  save_options (fp);

  objs = current_obj->obj_list;
  while (objs)
    {
      objs->obj->savefunc (objs->obj, fp);
      objs = objs->next;
    }

  if (ferror (fp))
    g_message ("Failed to write file\n");
  else
    {
      gfig_obj_modified (current_obj, GFIG_OK);
      current_obj->obj_status &= ~(GFIG_MODIFIED | GFIG_READONLY);
    }

  fclose (fp);

  gfig_update_stat_labels ();
}

static void
file_selection_ok (GtkWidget        *w,
		   GtkFileSelection *fs,
		   gpointer data)
{
  const gchar *filenamebuf;
  struct stat filestat;
  gint	err;
  GFigObj *obj = (GFigObj *)gtk_object_get_user_data (GTK_OBJECT (fs));
  GFigObj *real_current;

  filenamebuf = gtk_file_selection_get_filename (GTK_FILE_SELECTION (fs));
#ifdef DEBUG
  g_print ("name selected '%s'\n", filenamebuf);
#endif /* DEBUG */

  /* Get the name */
  if (strlen (filenamebuf) == 0)
    {
      g_message ("Save: No filename given");
      return;
    }

  /* Check if directory exists */
  err = stat (filenamebuf, &filestat);
  
  if (!err && S_ISDIR (filestat.st_mode))
    {
      g_message ("Save: Can't save to a directory");
      return;
    }
  
  obj->filename = g_strdup (filenamebuf);

  real_current = current_obj;
  current_obj = obj;
  gfig_save_callbk ();
  current_obj = current_obj;

  gtk_widget_destroy (GTK_WIDGET (fs));

}

static void
create_file_selection (GFigObj *obj,
		       gchar   *tpath)
{
  static GtkWidget *window = NULL;

  if (!window)
    {
      window = gtk_file_selection_new (_("Save Gfig drawing"));
      gtk_window_set_position (GTK_WINDOW (window), GTK_WIN_POS_MOUSE);

      gtk_signal_connect (GTK_OBJECT (window), "destroy",
			  GTK_SIGNAL_FUNC (gtk_widget_destroyed),
			  &window);

      gtk_object_set_user_data (GTK_OBJECT (window), obj);
      gtk_signal_connect (GTK_OBJECT (GTK_FILE_SELECTION (window)->ok_button),
			  "clicked",
			  GTK_SIGNAL_FUNC (file_selection_ok),
			  (gpointer) window);
      gtk_signal_connect_object (GTK_OBJECT (GTK_FILE_SELECTION (window)->cancel_button),
				 "clicked",
				 GTK_SIGNAL_FUNC (gtk_widget_destroy),
				 GTK_OBJECT (window));
    }

  if (tpath)
    {
      gtk_file_selection_set_filename (GTK_FILE_SELECTION (window), tpath);
    }
  else if (gfig_path_list)
    {
      gchar *dir;

      dir = gimp_path_get_user_writable_dir (gfig_path_list);

      if (!dir)
	dir = g_strdup (gimp_directory ());

      gtk_file_selection_set_filename (GTK_FILE_SELECTION (window), dir);

      g_free (dir);
    }
  else
    {
      const gchar *tmp = g_get_tmp_dir ();

      gtk_file_selection_set_filename (GTK_FILE_SELECTION (window), tmp);
    }

  if (!GTK_WIDGET_VISIBLE (window))
    gtk_widget_show (window);
}

static void
gfig_save (void)
{
  /* Save the current object */
  if (!current_obj->filename)
   {
     create_file_selection (current_obj, NULL);
     return;
   }
  gfig_save_callbk ();
}

/* HACK WARNING */
void * xxx;
void * yyy;

/* Cache the preview image - updates are a lot faster. */
/* The preview_cache will contain the small image */

static void
cache_preview (void)
{
  GimpPixelRgn src_rgn;
  int y, x;
  guchar *src_rows;
  guchar *p;
  int isgrey = 0;

  gimp_pixel_rgn_init (&src_rgn, gfig_select_drawable,
		       sel_x1, sel_y1, sel_width, sel_height, FALSE, FALSE);

  src_rows = g_new (guchar , sel_width * 4); 
  p = pv_cache = g_new (guchar , preview_width * preview_height * 4);

  real_img_bpp = gimp_drawable_bpp (gfig_select_drawable->drawable_id);

  has_alpha = gimp_drawable_has_alpha (gfig_select_drawable->drawable_id);

  if (real_img_bpp < 3)
    {
      img_bpp = 3 + has_alpha;
    }
  else
    {
      img_bpp = real_img_bpp;
    }

  switch (gimp_drawable_type (gfig_select_drawable->drawable_id))
    {
    case GIMP_GRAYA_IMAGE:
    case GIMP_GRAY_IMAGE:
      isgrey = 1;
    default:
      break;
    }

  /*memset (p,-1, preview_width*preview_height*4); return;*/

  for (y = 0; y < preview_height; y++)
    {
      gimp_pixel_rgn_get_row (&src_rgn,
			      src_rows,
			      sel_x1,
			      sel_y1 + (y*sel_height)/preview_height,
			      sel_width);

    for (x = 0; x < (preview_width); x ++)
      {
	/* Get the pixels of each col */
	int i;
	for (i = 0 ; i < 3; i++)
	  p[x*img_bpp+i] =
	    src_rows[((x*sel_width)/preview_width)*src_rgn.bpp +((isgrey)?0:i)]; 
	if (has_alpha)
	  p[x*img_bpp+3] =
	    src_rows[((x*sel_width)/preview_width)*src_rgn.bpp + ((isgrey)?1:3)];
      }
    p += (preview_width*img_bpp);
    }
  g_free (src_rows);
}

static void
refill_cache (void)
{
  GdkCursorType ctype1 = GDK_WATCH;
  GdkCursorType ctype2 = GDK_TOP_LEFT_ARROW;
  static GdkCursor *preview_cursor1;  
  static GdkCursor *preview_cursor2;  

  if (!preview_cursor1)
    preview_cursor1 = gdk_cursor_new (ctype1);

  if (!preview_cursor2)
    preview_cursor2 = gdk_cursor_new (ctype2);

  gdk_window_set_cursor
    (gtk_widget_get_toplevel (GTK_WIDGET (gfig_preview))->window,
     preview_cursor1);

  gdk_window_set_cursor (gfig_preview->window, preview_cursor1);

  gdk_flush ();

  cache_preview ();

  gdk_window_set_cursor
    (gtk_widget_get_toplevel (GTK_WIDGET (gfig_preview))->window,
     preview_cursor2);

  toggle_obj_type (NULL, (gpointer) selvals.otype);
}

static GtkWidget *
gfig_list_item_new_with_label_and_pixmap (GFigObj   *obj,
					  gchar     *label,
					  GtkWidget *pix_widget)
{
  GtkWidget *list_item;
  GtkWidget *label_widget;
  GtkWidget *hbox;

  list_item = gtk_list_item_new ();

  hbox = gtk_hbox_new (FALSE, 1);
  gtk_container_add (GTK_CONTAINER (list_item), hbox);
  gtk_widget_show (hbox);

  gtk_box_pack_start (GTK_BOX (hbox), pix_widget, FALSE, FALSE, 0);

  label_widget = gtk_label_new (label);
  gtk_misc_set_alignment (GTK_MISC (label_widget), 0.0, 0.5);
  gtk_container_add (GTK_CONTAINER (hbox), label_widget);

  gtk_widget_show (obj->label_widget = label_widget);
  gtk_widget_show (obj->pixmap_widget = pix_widget);
  gtk_widget_show (obj->list_item = list_item);

  return list_item;
}

static void
gfig_obj_modified (GFigObj *obj,
		   gint     stat_type)
{
  g_assert (obj != NULL);

  if (obj->obj_status == stat_type)
    return;

  /* Set the new one up */
  if (stat_type == GFIG_MODIFIED)
    gimp_pixmap_set (GIMP_PIXMAP (obj->pixmap_widget), Floppy6_xpm);
  else 
    gimp_pixmap_set (GIMP_PIXMAP (obj->pixmap_widget), blank_xpm);
}

static gint
select_button_press (GtkWidget      *widget,
		     GdkEventButton *event,
		     gpointer        data)
{
  gint type = (gint) data;
  gint count = 0;
  DAllObjs * objs;

  if (current_obj)
    {
      objs = current_obj->obj_list;

      while (objs)
	{
	  objs = objs->next;
	  count++;
	}
    }

  switch (type)
    {
    case OBJ_SELECT_LT:
      obj_show_single--;
      if (obj_show_single < 0)
	obj_show_single = count - 1;
      break;
    case OBJ_SELECT_GT:
      obj_show_single++;
      if (obj_show_single >= count)
	obj_show_single = 0;
      break;
    case OBJ_SELECT_EQ:
      obj_show_single = -1; /* Reset to show all */
      break;
    default:
      break;
    }

  draw_grid_clear (widget, data);

  return FALSE;
}

static GtkWidget *
obj_select_buttons (void)
{
  GtkWidget *button;
  GtkWidget *hbox, *vbox;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);
  gtk_widget_show (hbox);

  button = gtk_button_new_with_label ("<");
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);
  gtk_signal_connect (GTK_OBJECT (button), "button_press_event",
		      GTK_SIGNAL_FUNC (select_button_press),
		      (gpointer) OBJ_SELECT_LT);
  gtk_widget_show (button);

  button = gtk_button_new_with_label (">");
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);
  gtk_signal_connect (GTK_OBJECT (button), "button_press_event",
		      GTK_SIGNAL_FUNC (select_button_press),
		      (gpointer) OBJ_SELECT_GT);
  gtk_widget_show (button);

  button = gtk_button_new_with_label ("==");
  gtk_box_pack_start (GTK_BOX (vbox), button, TRUE, TRUE, 0);
  gtk_signal_connect (GTK_OBJECT (button), "button_press_event",
		      GTK_SIGNAL_FUNC (select_button_press),
		      (gpointer) OBJ_SELECT_EQ);
  gtk_widget_show (button);

  return vbox;
}

static GtkWidget *
but_with_pix (gchar  **pixdata,
	      GSList **group,
	      gint     baction)
{
  GtkWidget *button;
  GtkWidget *alignment;
  GtkWidget *pixmap_widget;

  button = gtk_radio_button_new (*group);
  gtk_toggle_button_set_mode (GTK_TOGGLE_BUTTON (button), FALSE); 
  gtk_signal_connect (GTK_OBJECT (button), "toggled",
		      GTK_SIGNAL_FUNC (toggle_obj_type),
		      (gpointer) baction);
  gtk_widget_show (button);

  *group = gtk_radio_button_group (GTK_RADIO_BUTTON (button));

  alignment = gtk_alignment_new (0.5, 0.5, 0.0, 0.0);
  gtk_container_add (GTK_CONTAINER (button), alignment);
  gtk_widget_show (alignment);

  pixmap_widget = gimp_pixmap_new (pixdata);
  gtk_container_add (GTK_CONTAINER (alignment), pixmap_widget);
  gtk_widget_show (pixmap_widget);

  return button;
}

static GtkWidget *
small_preview (GtkWidget *list)
{
  GtkWidget *label;
  GtkWidget *frame;
  GtkWidget *button;
  GtkWidget *vbox;
  gint y;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  label = gtk_label_new (_("Prev"));
  gtk_container_add (GTK_CONTAINER (vbox), label);
  gtk_widget_show (label);

  frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER (vbox), frame);
  gtk_widget_show (frame);

  pic_preview = gtk_preview_new (GTK_PREVIEW_COLOR);
  gtk_preview_size (GTK_PREVIEW (pic_preview),
		    SMALL_PREVIEW_SZ, SMALL_PREVIEW_SZ);
  gtk_container_add (GTK_CONTAINER (frame), pic_preview);
  gtk_widget_show (pic_preview);

  /* Fill with white */
  for (y = 0; y < SMALL_PREVIEW_SZ; y++)
    {
      guchar prow[SMALL_PREVIEW_SZ*3];
      memset (prow, -1, SMALL_PREVIEW_SZ * 3);
      gtk_preview_draw_row (GTK_PREVIEW (pic_preview), prow,
			    0, y, SMALL_PREVIEW_SZ);
    }

  gtk_signal_connect_after (GTK_OBJECT (pic_preview), "expose_event",
			    GTK_SIGNAL_FUNC (pic_preview_expose),
			    NULL);

  /* More Buttons */
  button = gtk_button_new_with_label (_("Edit"));
  gtk_container_add (GTK_CONTAINER (vbox), button);
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
		      GTK_SIGNAL_FUNC (edit_button_callback),
		      (gpointer) list);
  gimp_help_set_help_data (button, _("Edit Gfig object collection"), NULL); 
  gtk_widget_show (button);

  button = gtk_button_new_with_label (_("Merge"));
  gtk_container_add (GTK_CONTAINER (vbox), button);
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
		      GTK_SIGNAL_FUNC (merge_button_callback),
		      (gpointer) list);
  gimp_help_set_help_data (button, _("Merge Gfig Object collection into the "
				     "current edit session"), NULL); 
  gtk_widget_show (button);

  return vbox;
}

/* Special case for now - options on poly/star/spiral button */

static void
num_sides_dialog (gchar *d_title,
		  gint  *num_sides,
		  gint  *which_way,
		  gint   adj_min,
		  gint   adj_max)
{
  GtkWidget *window;
  GtkWidget *table;
  GtkObject *size_data;

  window = gimp_dialog_new (d_title, "gfig",
			    gimp_standard_help_func, "filters/gfig.html",
			    GTK_WIN_POS_MOUSE,
			    FALSE, TRUE, FALSE,

			    GTK_STOCK_CLOSE, gtk_widget_destroy,
			    NULL, 1, NULL, TRUE, TRUE,

			    NULL);

  table = gtk_table_new (which_way ? 2 : 1, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_container_set_border_width (GTK_CONTAINER (table), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (window)->vbox), table,
		      FALSE, FALSE, 0);
  gtk_widget_show (table);

  size_data = gimp_scale_entry_new (GTK_TABLE (table), 0, 0,
				    _("Number of Sides/Points/Turns:"), 0, 0,
				    *num_sides, adj_min, adj_max, 1, 10, 0,
				    TRUE, 0, 0,
				    NULL, NULL);
  gtk_signal_connect (GTK_OBJECT (size_data), "value_changed",
		      GTK_SIGNAL_FUNC (gimp_int_adjustment_update),
		      num_sides);

  if (which_way)
    {
      GtkWidget *option_menu;

      option_menu =
	gimp_option_menu_new2 (FALSE, G_CALLBACK (gimp_menu_item_update),
			       which_way, (gpointer) *which_way,

			       _("Clockwise"),      (gpointer) 0, NULL,
			       _("Anti-Clockwise"), (gpointer) 1, NULL,

			       NULL);
      gimp_table_attach_aligned (GTK_TABLE (table), 0, 1,
				 _("Orientation:"), 1.0, 0.5,
				 option_menu, 1, TRUE);
    }

  gtk_widget_show (window);
}

static void
bezier_dialog (void)
{
  static GtkWidget *window = NULL;
  GtkWidget *vbox;
  GtkWidget *toggle;

  if (window)
    {
      gdk_window_raise (window->window);
      return;
    }

  window = gimp_dialog_new (_("Bezier Settings"), "gfig",
			    gimp_standard_help_func, "filters/gfig.html",
			    GTK_WIN_POS_MOUSE,
			    FALSE, FALSE, FALSE,

			    GTK_STOCK_CLOSE, gtk_widget_destroy,
			    NULL, 1, NULL, TRUE, TRUE,

			    NULL);

  gtk_signal_connect (GTK_OBJECT (window), "destroy",
		      GTK_SIGNAL_FUNC (gtk_widget_destroyed),
		      &window);

  vbox = gtk_vbox_new (FALSE, 2);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 4);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (window)->vbox), vbox,
		      FALSE, FALSE, 0);
  gtk_widget_show (vbox);

  toggle = gtk_check_button_new_with_label (_("Closed"));
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &bezier_closed);
  gimp_help_set_help_data (toggle,
			_("Close curve on completion"), NULL);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), bezier_closed);
  gtk_box_pack_start (GTK_BOX (vbox), toggle, FALSE, FALSE, 0);
  gtk_widget_show (toggle);

  toggle = gtk_check_button_new_with_label (_("Show Line Frame"));
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &bezier_line_frame);
  gimp_help_set_help_data (toggle,
			_("Draws lines between the control points. "
			  "Only during curve creation"), NULL);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), bezier_line_frame);
  gtk_box_pack_start (GTK_BOX (vbox), toggle, FALSE, FALSE, 0);
  gtk_widget_show (toggle);

  gtk_widget_show (window);
}

static gint
poly_button_press (GtkWidget      *widget,
		   GdkEventButton *event,
		   gpointer        data)
{
  if ((event->type == GDK_2BUTTON_PRESS) &&
      (event->button == 1))
    num_sides_dialog (_("Regular Polygon Number of Sides"),
		      &poly_num_sides, NULL, 3, 200);
  return FALSE;
}

static gint
star_button_press (GtkWidget      *widget,
		   GdkEventButton *event,
		   gpointer        data)
{
  if ((event->type == GDK_2BUTTON_PRESS) &&
      (event->button == 1))
    num_sides_dialog (_("Star Number of Points"),
		      &star_num_sides, NULL, 3, 200);
  return FALSE;
}

static gint
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

static gint
bezier_button_press (GtkWidget      *widget,
		     GdkEventButton *event,
		     gpointer        data)
{
  if ((event->type == GDK_2BUTTON_PRESS) &&
      (event->button == 1))
    bezier_dialog ();
  return FALSE;
}              

static GtkWidget *
draw_buttons (GtkWidget *ww)
{
  GtkWidget *frame;
  GtkWidget *button;
  GtkWidget *vbox;
  GSList    *group;

  frame = gtk_frame_new (_("Ops"));
  gtk_container_set_border_width (GTK_CONTAINER (frame), 1);
  
  /* Create group */
  group = NULL;
  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (frame), vbox); 
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 2);

  /* Put buttons in */
  button = but_with_pix (line_xpm, &group, LINE);
  gtk_box_pack_start (GTK_BOX (vbox), button, TRUE, TRUE, 0);
  gtk_widget_show (button);
  gimp_help_set_help_data (button, _("Create line"), NULL); 

  button = but_with_pix (circle_xpm, &group, CIRCLE);
  gtk_container_add (GTK_CONTAINER (vbox), button);
  gtk_widget_show (button);
  gimp_help_set_help_data (button, _("Create circle"), NULL); 

  button = but_with_pix (ellipse_xpm, &group, ELLIPSE);
  gtk_container_add (GTK_CONTAINER (vbox), button);
  gtk_widget_show (button);
  gimp_help_set_help_data (button, _("Create ellipse"), NULL); 

  button = but_with_pix (curve_xpm, &group, ARC);
  gtk_container_add (GTK_CONTAINER (vbox), button);
  gtk_widget_show (button);
  gimp_help_set_help_data (button, _("Create arch"), NULL); 

  button = but_with_pix (poly_xpm, &group, POLY);
  gtk_container_add (GTK_CONTAINER (vbox), button);
  gtk_widget_show (button);

  gtk_signal_connect (GTK_OBJECT (button), "button_press_event",
		      GTK_SIGNAL_FUNC (poly_button_press),
		      NULL);
  gimp_help_set_help_data (button, _("Create reg polygon"), NULL); 

  button = but_with_pix (star_xpm, &group, STAR);
  gtk_container_add (GTK_CONTAINER (vbox), button);
  gtk_widget_show (button);
  gtk_signal_connect (GTK_OBJECT (button), "button_press_event",
		      GTK_SIGNAL_FUNC (star_button_press),
		      NULL);
  gimp_help_set_help_data (button, _("Create star"), NULL); 

  button = but_with_pix (spiral_xpm, &group, SPIRAL);
  gtk_box_pack_start (GTK_BOX (vbox), button, TRUE, TRUE, 0);
  gtk_widget_show (button);

  gtk_signal_connect (GTK_OBJECT (button), "button_press_event",
		      GTK_SIGNAL_FUNC (spiral_button_press),
		      NULL);
  gimp_help_set_help_data (button, _("Create spiral"), NULL); 

  button = but_with_pix (bezier_xpm, &group, BEZIER);
  gtk_box_pack_start (GTK_BOX (vbox), button, TRUE, TRUE, 0);
  gtk_widget_show (button);
  gtk_signal_connect (GTK_OBJECT (button), "button_press_event",
		      GTK_SIGNAL_FUNC (bezier_button_press),
		      NULL);

  gimp_help_set_help_data (button,
			_("Create bezier curve. "
			  "Shift + Button ends object creation."), NULL); 

  button = but_with_pix (move_obj_xpm, &group, MOVE_OBJ);
  gtk_container_add (GTK_CONTAINER (vbox), button);
  gtk_widget_show (button);
  gimp_help_set_help_data (button, _("Move an object"), NULL); 

  button = but_with_pix (move_point_xpm, &group, MOVE_POINT);
  gtk_container_add (GTK_CONTAINER (vbox), button);
  gtk_widget_show (button);
  gimp_help_set_help_data (button, _("Move a single point"), NULL); 

  button = but_with_pix (copy_obj_xpm, &group, COPY_OBJ);
  gtk_container_add (GTK_CONTAINER (vbox), button);
  gtk_widget_show (button);
  gimp_help_set_help_data (button, _("Copy an object"), NULL); 

  button = but_with_pix (delete_xpm, &group, DEL_OBJ);
  gtk_container_add (GTK_CONTAINER (vbox), button);
  gtk_widget_show (button);
  gimp_help_set_help_data (button, _("Delete an object"), NULL); 

  button = obj_select_buttons ();
  gtk_container_add (GTK_CONTAINER (vbox), button);
  gtk_widget_show (button);

#if 0
  button = but_with_pix (blank_xpm, &group, NULL_OPER);
  gtk_container_add (GTK_CONTAINER (vbox), button);
  gtk_widget_set_sensitive (button, FALSE);
  gtk_widget_show (button);
#endif /* 0 */

  gtk_widget_show (vbox);
  gtk_widget_show (frame);

  return frame;
}

/* Brush preview stuff */
static gint
gfig_brush_preview_events (GtkWidget *widget,
			   GdkEvent  *event)
{
  GdkEventButton *bevent;
  GdkEventMotion *mevent;
  static GdkPoint point;
  static int have_start = 0;

  switch (event->type)
    {
    case GDK_EXPOSE:
      break;

    case GDK_BUTTON_PRESS:
      bevent = (GdkEventButton *) event;
      point.x = bevent->x;
      point.y = bevent->y;
      have_start = 1;

      break;
    case GDK_BUTTON_RELEASE:
      bevent = (GdkEventButton *) event;
      have_start = 0;

      break;
    case GDK_MOTION_NOTIFY:
      mevent = (GdkEventMotion *) event;

      if (!have_start || !(mevent->state & GDK_BUTTON1_MASK))
	break;

      gfig_brush_fill_preview_xy (widget,
				  point.x - mevent->x,
				  point.y - mevent->y);
      gtk_widget_draw (widget, NULL);
      point.x = mevent->x;
      point.y = mevent->y;
      break;
    default:
      break;
    }
  return FALSE;
}

static void
gfig_brush_update_preview (GtkWidget *widget,
			   gpointer   data)
{
  GtkWidget *pw = (GtkWidget *) data;
  BrushDesc *bdesc;

  /* Must update the dialog area */
  /* Use the same brush as already set in the dialog */
  bdesc = gtk_object_get_user_data (GTK_OBJECT (pw));
  brush_list_button_callback (NULL, bdesc);
}

static void
gfig_brush_menu_callback (GtkWidget *widget,
			  gpointer   data)
{
  gimp_menu_item_update (widget, &selvals.brshtype);

  switch (selvals.brshtype)
    {
    case BRUSH_BRUSH_TYPE:
      gtk_widget_hide (pressure_hbox);
      gtk_widget_hide (pencil_hbox);
      gtk_widget_show (fade_out_hbox);
      gtk_widget_show (gradient_hbox);
      break;
    case BRUSH_PENCIL_TYPE:
      gtk_widget_hide (fade_out_hbox);
      gtk_widget_hide (gradient_hbox);
      gtk_widget_hide (pressure_hbox);
      gtk_widget_show (pencil_hbox);
      break;
    case BRUSH_AIRBRUSH_TYPE:
      gtk_widget_hide (fade_out_hbox);
      gtk_widget_hide (gradient_hbox);
      gtk_widget_hide (pencil_hbox);
      gtk_widget_show (pressure_hbox);
      break;
    case BRUSH_PATTERN_TYPE:
      gtk_widget_hide (fade_out_hbox);
      gtk_widget_hide (gradient_hbox);
      gtk_widget_hide (pressure_hbox);
      gtk_widget_show (pencil_hbox);
      break;
    default:
      g_warning ("Internal error - invalid brush type");
      break;
    }

  gfig_brush_update_preview (widget, data);
}


static GtkWidget *
gfig_brush_preview (GtkWidget **pv)
{
  GtkWidget *option_menu;
  GtkWidget *frame;
  GtkWidget *hbox;
  GtkWidget *vbox;
  gint y;

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (hbox), 4);
  gtk_widget_show (hbox);

  frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
  gtk_container_set_border_width (GTK_CONTAINER (frame), 0);
  gtk_widget_show (frame);

  *pv = gtk_preview_new (GTK_PREVIEW_COLOR);
  gtk_widget_show (*pv);
  gtk_widget_set_events (GTK_WIDGET (*pv), PREVIEW_MASK);
  gtk_signal_connect (GTK_OBJECT (*pv), "event",
		      GTK_SIGNAL_FUNC (gfig_brush_preview_events),
		      NULL);
  gtk_preview_size (GTK_PREVIEW (*pv), BRUSH_PREVIEW_SZ, BRUSH_PREVIEW_SZ);
  gtk_container_add (GTK_CONTAINER (frame), *pv);

  /* Fill with white */
  for (y = 0; y < BRUSH_PREVIEW_SZ; y++)
    {
      guchar prow[BRUSH_PREVIEW_SZ*3];
      memset (prow, -1, BRUSH_PREVIEW_SZ * 3);
      gtk_preview_draw_row (GTK_PREVIEW (*pv), prow, 0, y, BRUSH_PREVIEW_SZ);
    }

  /* Now the buttons */
  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 4);
  gtk_widget_show (vbox);

  option_menu =
    gimp_option_menu_new2 (FALSE, G_CALLBACK (gfig_brush_menu_callback),
			   *pv, (gpointer) selvals.brshtype,

			   _("Brush"),    (gpointer) BRUSH_BRUSH_TYPE, NULL,
			   _("Airbrush"), (gpointer) BRUSH_AIRBRUSH_TYPE, NULL,
			   _("Pencil"),   (gpointer) BRUSH_PENCIL_TYPE, NULL,
			   _("Pattern"),  (gpointer) BRUSH_PATTERN_TYPE, NULL,

			   NULL);
  gtk_widget_show (option_menu);

  gtk_container_add (GTK_CONTAINER (vbox), option_menu);
  gimp_help_set_help_data (option_menu,
			_("Use the brush/pencil or the airbrush when drawing "
			  "on the image. Pattern paints with currently "
			  "selected brush with a pattern. Only applies to "
			  "circles/ellipses if Approx. Circles/Ellipses "
			  "toggle is set."), NULL);

  gtk_container_add (GTK_CONTAINER (hbox), vbox);
  gtk_container_add (GTK_CONTAINER (hbox), frame);

  return hbox;
}

static void
gfig_brush_fill_preview_xy (GtkWidget *pw,
			    gint       x1,
			    gint       y1)
{
  gint row_count;
  BrushDesc *bdesc = (BrushDesc*) gtk_object_get_user_data (GTK_OBJECT (pw));

  /* Adjust start position */
  bdesc->x_off += x1;
  bdesc->y_off += y1;

  if (bdesc->y_off < 0)
    bdesc->y_off = 0;
  if (bdesc->y_off > (bdesc->height - BRUSH_PREVIEW_SZ))
    bdesc->y_off = bdesc->height - BRUSH_PREVIEW_SZ;

  if (bdesc->x_off < 0)
    bdesc->x_off = 0;
  if (bdesc->x_off > (bdesc->width - BRUSH_PREVIEW_SZ))
    bdesc->x_off = bdesc->width - BRUSH_PREVIEW_SZ;

  /* Given an x and y fill preview in correctly offsetted */
  for (row_count = 0; row_count < BRUSH_PREVIEW_SZ; row_count++)
    gtk_preview_draw_row (GTK_PREVIEW (pw),
			  &bdesc->pv_buf[bdesc->x_off*bdesc->bpp 
					+ (bdesc->width
					   *bdesc->bpp
					   *(row_count + bdesc->y_off))],
			  0,
			  row_count,
			  BRUSH_PREVIEW_SZ);
}

static void
gfig_brush_fill_preview (GtkWidget *pw,
			 gint32     layer_ID,
			 BrushDesc *bdesc)
{
  GimpPixelRgn src_rgn;
  GimpDrawable *brushdrawable;
  gint bcount = 3;

  if (bdesc->pv_buf)
    {
      g_free (bdesc->pv_buf); /* Free old area */
    }

  brushdrawable = gimp_drawable_get (layer_ID);

  bdesc->bpp = bcount;

  /* Fill the preview with the current brush name */
  gimp_pixel_rgn_init (&src_rgn, brushdrawable,
		       0, 0, bdesc->width, bdesc->height, FALSE, FALSE);

  bdesc->pv_buf = g_new (guchar, bdesc->width * bdesc->height * bcount);
  bdesc->x_off = bdesc->y_off = 0; /* Start from top left */

  gimp_pixel_rgn_get_rect (&src_rgn, bdesc->pv_buf,
			   0, 0, bdesc->width, bdesc->height);

  /* Dump the pv_buf into the preview area */
  gfig_brush_fill_preview_xy (pw, 0, 0);
}

static void
mygimp_brush_set (gchar *bname)
{
  GimpParam *return_vals;
  int nreturn_vals;

  return_vals = gimp_run_procedure ("gimp_brushes_set_brush",
				    &nreturn_vals,
				    GIMP_PDB_STRING, bname,
				    GIMP_PDB_END);

  if (return_vals[0].data.d_status != GIMP_PDB_SUCCESS)
    {
      g_message ("Can't set brush...(1)");
    }

  gimp_destroy_params (return_vals, nreturn_vals);
}

static gchar *
mygimp_brush_get (void)
{
  GimpParam *return_vals;
  int nreturn_vals;
  static gchar saved_bname[1024]; /* required to be static - returned from proc */

  return_vals = gimp_run_procedure ("gimp_brushes_get_brush",
                                    &nreturn_vals,
				    GIMP_PDB_END);

  if (return_vals[0].data.d_status == GIMP_PDB_SUCCESS)
    {
      strncpy (saved_bname, return_vals[1].data.d_string, sizeof (saved_bname));
    }
  else
    {
      saved_bname[0] = '\0';
    }

  gimp_destroy_params (return_vals, nreturn_vals);

  return (saved_bname);
}

static void
mygimp_brush_info (gint32 *width,
		   gint32 *height)
{
  GimpParam *return_vals;
  int nreturn_vals;
 
  return_vals = gimp_run_procedure ("gimp_brushes_get_brush",
                                    &nreturn_vals,
				    GIMP_PDB_END);

  if (return_vals[0].data.d_status == GIMP_PDB_SUCCESS)
    {
      *width  = MAX (return_vals[2].data.d_int32, 32);
      *height = MAX (return_vals[3].data.d_int32, 32);
    }
  else
    {
      g_message ("Failed to get brush info");
      *width = *height = 48;
    }

  gimp_destroy_params (return_vals, nreturn_vals);
}          

static void
gfig_paint (BrushType brush_type,
	    gint32    drawable_ID,
	    gint      seg_count,
	    gdouble   line_pnts[])
{
  switch (brush_type)
    {
    case BRUSH_BRUSH_TYPE:
      gimp_paintbrush (drawable_ID,
		       selvals.brushfade,
		       seg_count, line_pnts,
		       GIMP_HARD,
		       selvals.brushgradient);
      break;

    case BRUSH_PENCIL_TYPE:
      gimp_pencil (drawable_ID,
		   seg_count, line_pnts);
      break;

    case BRUSH_AIRBRUSH_TYPE:
      gimp_airbrush (drawable_ID,
		     selvals.airbrushpressure,
		     seg_count, line_pnts); 
      break;

    case BRUSH_PATTERN_TYPE:
      gimp_clone (drawable_ID,
		  drawable_ID,
		  GIMP_PATTERN_CLONE,
		  0.0, 0.0,
		  seg_count, line_pnts);
      break;
    }
}

static gint32
gfig_gen_brush_preview (BrushDesc *bdesc)
{
  /* Given the name of a brush then paint it and return the ID of the image 
   * the preview can be got from
   */
  static  gint32 layer_ID = -1;
  gchar  *saved_bname;
  gint32  width, height;
  gdouble line_pnts[2];
  GimpRGB foreground;
  GimpRGB background;
  GimpRGB color;

  if (brush_image_ID == -1)
    {
      /* Create a new image */
      brush_image_ID = gimp_image_new (48, 48, 0);
      if (brush_image_ID < 0)
	{
	  g_message ("Failed to generate brush preview");
	  return -1;
	}
      if ((layer_ID = gimp_layer_new (brush_image_ID,
				      "Brush preview",
				      48,
				      48,
				      0, /* RGB type */
				      100.0, /* opacity */
				      0 /* mode */)) < 0)
	{
	  g_message ("Error in creating layer for brush preview");
	  return -1;
	}
      gimp_image_add_layer (brush_image_ID, layer_ID, -1);
    }

  /* Need this later to delete it */

  /* Store foreground & backgroud colours set to black/white
   * paint with brush
   * restore colours
   */

  gimp_palette_get_foreground (&foreground);
  gimp_palette_get_background (&background);

  saved_bname = mygimp_brush_get ();

  gimp_rgba_set (&color, 1.0, 1.0, 1.0, 1.0);
  gimp_palette_set_background (&color);
  gimp_rgba_set (&color, 0.0, 0.0, 0.0, 1.0);
  gimp_palette_set_foreground (&color);

  mygimp_brush_set (bdesc->bname);

  mygimp_brush_info (&width, &height);
  bdesc->width = width;
  bdesc->height = height;
  line_pnts[0] = (gdouble) width / 2;
  line_pnts[1] = (gdouble) height / 2;

  gimp_layer_resize (layer_ID, width, height, 0, 0);
  gimp_image_resize (brush_image_ID, width, height, 0, 0);

  gimp_drawable_fill (layer_ID, 1); /* Clear... Fill with white ... */

  /* Blob of paint */
  gfig_paint (selvals.brshtype,
	      layer_ID,
	      2, line_pnts);

  gimp_palette_set_background (&background);  
  gimp_palette_set_foreground (&foreground);

  mygimp_brush_set (saved_bname);

  return layer_ID;
}

static void
brush_list_button_callback (GtkWidget *widget,
			    gpointer   data)
{
  gint32 layer_ID;

  BrushDesc *bdesc = (BrushDesc *) data;
  if ((layer_ID = gfig_gen_brush_preview (bdesc)) != -1)
    {
      gtk_object_set_user_data (GTK_OBJECT (brush_page_pw), (gpointer) bdesc);
      gfig_brush_fill_preview (brush_page_pw, layer_ID, bdesc);
      gtk_widget_draw (brush_page_pw, NULL);
    }
}

/* Build the dialog up. This was the hard part! */

static GtkWidget *page_menu_bg;
static GtkWidget *page_menu_layers;

static void
gfig_brush_pane_activate (GtkWidget *widget,
			  gpointer   data)
{
  gint flag = (gint) data;

  if (flag)
    {
      gtk_widget_show (brush_page_widget);
    }
  else
    {
      gtk_widget_hide (brush_page_widget);
    }

  gtk_widget_set_sensitive (brush_page_widget, flag);
}

static void
gfig_select_pane_activate (GtkWidget *widget,
			   gpointer   data)
{
  gint flag = (gint) data;

  if (flag)
    {
      gtk_widget_show (select_page_widget);
    }
  else
    {
      gtk_widget_hide (select_page_widget);
    }

  gtk_widget_set_sensitive (select_page_widget, flag);
}


static void
paint_menu_callback (GtkWidget *widget,
		     gpointer   data)
{
  gint mtype = (gint)data;

  if (mtype == PAINT_LAYERS_MENU)
    {
#ifdef DEBUG
      printf ("layer type set to %s\n",
	     ((DrawonLayers)gtk_object_get_user_data (GTK_OBJECT (widget)) == SINGLE_LAYER)?"SINGLE_LAYER":"MULTI_LAYER");
#endif /* DEBUG */
      selvals.onlayers = (DrawonLayers)gtk_object_get_user_data (GTK_OBJECT (widget));
      /* Type only meaningful if creating new layers */
      if (selvals.onlayers == ORIGINAL_LAYER)
	gtk_widget_set_sensitive (page_menu_bg, FALSE);
      else
	gtk_widget_set_sensitive (page_menu_bg, TRUE);
    }
  else if (mtype == PAINT_BGS_MENU)
    {
#ifdef DEBUG
      printf ("BG type = %d\n",
	     ((LayersBGType)gtk_object_get_user_data (GTK_OBJECT (widget))));
#endif /* DEBUG */
      selvals.onlayerbg = (LayersBGType)gtk_object_get_user_data (GTK_OBJECT (widget));
    }
  else if (mtype == PAINT_TYPE_MENU)
    {
#ifdef DEBUG
      printf ("Got type menu = %d\n", (PaintType)gtk_object_get_user_data (GTK_OBJECT (widget)));
#endif /* DEBUG */
      selvals.painttype = (PaintType)gtk_object_get_user_data (GTK_OBJECT (widget));
      switch (selvals.painttype)
	{
	case PAINT_BRUSH_TYPE:
	  gtk_widget_set_sensitive (select_page_widget, FALSE);
	  gtk_widget_set_sensitive (brush_page_widget, TRUE);
	  gtk_widget_set_sensitive (page_menu_layers, TRUE);
	  if (selvals.onlayers == ORIGINAL_LAYER)
	    gtk_widget_set_sensitive (page_menu_bg, FALSE);
	  else
	    gtk_widget_set_sensitive (page_menu_bg, TRUE);
	  break;
	case PAINT_SELECTION_TYPE:
	  gtk_widget_set_sensitive (select_page_widget, TRUE);
	  gtk_widget_set_sensitive (brush_page_widget, FALSE);
	  gtk_widget_set_sensitive (page_menu_layers, FALSE);
	  gtk_widget_set_sensitive (page_menu_bg, FALSE);
	  break;
	case PAINT_SELECTION_FILL_TYPE:
	  gtk_widget_set_sensitive (select_page_widget, TRUE);
	  gtk_widget_set_sensitive (brush_page_widget, FALSE);
	  gtk_widget_set_sensitive (page_menu_layers, TRUE);
	  if (selvals.onlayers == ORIGINAL_LAYER)
	    gtk_widget_set_sensitive (page_menu_bg, FALSE);
	  else
	    gtk_widget_set_sensitive (page_menu_bg, TRUE);
	  break;
	default:
	  break;
	}
    }
}

static GtkWidget *
paint_page (void)
{
  GtkWidget *table;
  GtkWidget *vbox;
  GtkWidget *vbox2;
  GtkWidget *hbox;
  GtkWidget *toggle;
  GtkWidget *page_menu_type;
  GtkWidget *scale_scale;
  GtkObject *scale_scale_data;
  GtkWidget *item1, *item2, *item3;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 4);

  table = gtk_table_new (5, 2, FALSE); 
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  page_menu_layers =
    gimp_option_menu_new2 (FALSE, G_CALLBACK (paint_menu_callback),
			   (gpointer) PAINT_LAYERS_MENU, 0,

			   _("Original"), (gpointer) ORIGINAL_LAYER, NULL,
			   _("New"),      (gpointer) SINGLE_LAYER, NULL,
			   _("Multiple"), (gpointer) MULTI_LAYER, NULL,

			   NULL);

  gimp_help_set_help_data (page_menu_layers,
			_("Draw all objects on one layer (original or new) "
			  "or one object per layer"), NULL);
  if (gimp_drawable_is_channel (gfig_drawable))
      gtk_widget_set_sensitive (page_menu_layers, FALSE);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 0,
			     _("Draw on:"), 1.0, 0.5,
			     page_menu_layers, 1, TRUE);

  page_menu_type =
    gimp_option_menu_new2 (FALSE, G_CALLBACK (paint_menu_callback),
			   (gpointer) PAINT_TYPE_MENU, 0,

			   _("Brush"),
			   (gpointer) PAINT_BRUSH_TYPE, &item1,
			   _("Selection"),
			   (gpointer) PAINT_SELECTION_TYPE, &item2,
			   _("Selection+Fill"),
			   (gpointer) PAINT_SELECTION_FILL_TYPE, &item3,

			   NULL);

  gtk_signal_connect (GTK_OBJECT (item1), "activate",
		      GTK_SIGNAL_FUNC (gfig_brush_pane_activate),
		      (gpointer) 1);
  gtk_signal_connect (GTK_OBJECT (item1), "activate",
		      GTK_SIGNAL_FUNC (gfig_select_pane_activate),
		      (gpointer) 0);

  gtk_signal_connect (GTK_OBJECT (item2), "activate",
		      GTK_SIGNAL_FUNC (gfig_brush_pane_activate),
		      (gpointer) 0);
  gtk_signal_connect (GTK_OBJECT (item2), "activate",
		      GTK_SIGNAL_FUNC (gfig_select_pane_activate),
		      (gpointer) 1);

  gtk_signal_connect (GTK_OBJECT (item3), "activate",
		      GTK_SIGNAL_FUNC (gfig_brush_pane_activate),
		      (gpointer) 0);
  gtk_signal_connect (GTK_OBJECT (item3), "activate",
		      GTK_SIGNAL_FUNC (gfig_select_pane_activate),
		      (gpointer) 1);

  gimp_help_set_help_data (page_menu_type,
			_("Draw type. Either a brush or a selection. "
			  "See brush page or selection page for more options"),
			NULL);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 1,
			     _("Using:"), 1.0, 0.5,
			     page_menu_type, 1, TRUE);

  page_menu_bg =
    gimp_option_menu_new2 (FALSE, G_CALLBACK (paint_menu_callback),
			   (gpointer) PAINT_BGS_MENU, 0,

			   _("Transparent"), (gpointer) LAYER_TRANS_BG, NULL,
			   _("Background"),  (gpointer) LAYER_BG_BG, NULL,
			   _("Foreground"),  (gpointer) LAYER_FG_BG, NULL,
			   _("White"),       (gpointer) LAYER_WHITE_BG, NULL,
			   _("Copy"),        (gpointer) LAYER_COPY_BG, NULL,

			   NULL);
  gimp_help_set_help_data (page_menu_bg,
			_("Layer background type. Copy causes previous "
			  "layer to be copied before the draw is performed"),
			NULL);
  gtk_widget_set_sensitive (page_menu_bg, FALSE);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 2,
			     _("With BG of:"), 1.0, 0.5,
			     page_menu_bg, 1, TRUE);

  toggle = gtk_check_button_new_with_label (_("Reverse Line"));
  gtk_table_attach (GTK_TABLE (table), toggle, 0, 1, 4, 5,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      (gpointer)&selvals.reverselines);
  gimp_help_set_help_data (toggle,
			_("Draw lines in reverse order"), NULL);
  gtk_widget_show (toggle);

  vbox2 = gtk_vbox_new (FALSE, 0);
  gtk_table_attach (GTK_TABLE (table), vbox2, 0, 1, 3, 4,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (vbox2);

  toggle = gtk_check_button_new_with_label (_("Scale to Image"));
  gtk_box_pack_end (GTK_BOX (vbox2), toggle, FALSE, FALSE, 0);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle),
				selvals.scaletoimage);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gfig_scale2img_update),
		      &selvals.scaletoimage);
  gimp_help_set_help_data (toggle,
			_("Scale drawings to images size"), NULL);
  gtk_widget_show (toggle);

  hbox = gtk_hbox_new (FALSE, 1);
  scale_scale_data = gtk_adjustment_new (1.0, 0.1, 5.0, 0.01, 0.01, 0.0);
  scale_scale = gtk_hscale_new (GTK_ADJUSTMENT (scale_scale_data));
  gtk_box_pack_start (GTK_BOX (hbox), scale_scale, TRUE, TRUE, 0);
  gtk_scale_set_value_pos (GTK_SCALE (scale_scale), GTK_POS_TOP);
  gtk_scale_set_digits (GTK_SCALE (scale_scale), 2);
  gtk_range_set_update_policy (GTK_RANGE (scale_scale), GTK_UPDATE_CONTINUOUS);
  gtk_signal_connect (GTK_OBJECT (scale_scale_data), "value_changed",
                      GTK_SIGNAL_FUNC (gfig_scale_update_scale),
                      &selvals.scaletoimagefp);
  gtk_widget_show (scale_scale);
  gtk_widget_show (hbox);   
  gtk_table_attach (GTK_TABLE (table), hbox, 1, 2, 3, 4,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);

  gtk_widget_set_sensitive (GTK_WIDGET (scale_scale), FALSE);
  gtk_object_set_data (GTK_OBJECT (toggle), "inverse_sensitive", scale_scale);
  gtk_object_set_user_data (GTK_OBJECT (toggle), scale_scale_data);

  toggle = gtk_check_button_new_with_label (_("Approx. Circles/Ellipses"));
  gtk_table_attach (GTK_TABLE (table), toggle, 1, 2, 4, 5,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &selvals.approxcircles);
  gimp_help_set_help_data (toggle,
			_("Approx. circles & ellipses using lines. Allows "
			  "the use of brush fading with these types of "
			  "objects."), NULL); 
  gtk_widget_show (toggle);

  return vbox;
}

static void
gfig_brush_invoker (gchar    *name,
		    gdouble  opacity,
		    gint     spacing,
		    gint     paint_mode,
		    gint     width,
		    gint     height,
		    gchar   *mask_data,
		    gint     closing,
		    gpointer udata)
{
  BrushDesc *bdesc = g_new0 (BrushDesc, 1); /* Mem leak */

  bdesc->bpp = 3;
  bdesc->width = width;
  bdesc->height = height;
  bdesc->bname = g_strdup (name);

  brush_list_button_callback (NULL, bdesc);
}

static void
select_brush_callback (GtkWidget *widget,
		       gpointer   data)
{
  BrushDesc *bdesc = g_new0 (BrushDesc, 1);
 
  gimp_interactive_selection_brush (_("Gfig brush selection"),
				    mygimp_brush_get (),
				    1.0, /* Opacity */
				    -1,  /* spacing (default)*/
				    1,   /* Paint mode */
				    gfig_brush_invoker,
				    NULL);

   bdesc->bpp = 3; 
   bdesc->bname = mygimp_brush_get ();
   
   brush_list_button_callback (NULL, bdesc); 
}

static GtkWidget *
brush_page (void)
{
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *pw;
  GtkWidget *scale;
  GtkObject *fade_out_scale_data;
  GtkObject *gradient_scale_data;
  GtkObject *pressure_scale_data;
  GtkWidget *vbox;
  GtkWidget *button;
  BrushDesc *bdesc = g_new0 (BrushDesc, 1); /* Initial brush settings */

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 4);

  table = gtk_table_new (2, 3, FALSE); 
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  /* Fade option */
  /*  the fade-out scale  From GIMP itself*/
  fade_out_hbox = gtk_hbox_new (FALSE, 4);

  label = gtk_label_new (_("Fade out:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 1.0);
  gtk_box_pack_start (GTK_BOX (fade_out_hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  fade_out_scale_data = gtk_adjustment_new (0.0, 0.0, 3000.0, 1.0, 1.0, 0.0);
  scale = gtk_hscale_new (GTK_ADJUSTMENT (fade_out_scale_data));
  gtk_box_pack_start (GTK_BOX (fade_out_hbox), scale, TRUE, TRUE, 0);
  gtk_scale_set_value_pos (GTK_SCALE (scale), GTK_POS_TOP);
  gtk_range_set_update_policy (GTK_RANGE (scale), GTK_UPDATE_DELAYED);
  gtk_signal_connect (GTK_OBJECT (fade_out_scale_data), "value_changed",
                      GTK_SIGNAL_FUNC (gimp_double_adjustment_update),
                      &selvals.brushfade);
  gtk_widget_show (scale);

  gtk_table_attach (GTK_TABLE (table), fade_out_hbox, 0, 2, 1, 2,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
  gtk_widget_show (fade_out_hbox);   


  /* Gradient drawing */
  gradient_hbox = gtk_hbox_new (FALSE, 4);

  label = gtk_label_new (_("Gradient:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 1.0);
  gtk_box_pack_start (GTK_BOX (gradient_hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  gradient_scale_data = gtk_adjustment_new (0.0, 0.0, 3000.0, 1.0, 1.0, 0.0);
  scale = gtk_hscale_new (GTK_ADJUSTMENT (gradient_scale_data));
  gtk_box_pack_start (GTK_BOX (gradient_hbox), scale, TRUE, TRUE, 0);
  gtk_scale_set_value_pos (GTK_SCALE (scale), GTK_POS_TOP);
  gtk_range_set_update_policy (GTK_RANGE (scale), GTK_UPDATE_DELAYED);
  gtk_signal_connect (GTK_OBJECT (gradient_scale_data), "value_changed",
                      GTK_SIGNAL_FUNC (gimp_double_adjustment_update),
                      &selvals.brushgradient);
  gtk_widget_show (scale);
  gtk_table_attach (GTK_TABLE (table), gradient_hbox, 0, 2, 2, 3,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
  gtk_widget_show (gradient_hbox);   


  pressure_hbox = gtk_hbox_new (FALSE, 4);
  label = gtk_label_new (_("Pressure:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 1.0);
  gtk_box_pack_start (GTK_BOX (pressure_hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  pressure_scale_data = gtk_adjustment_new (20.0, 0.0, 100.0, 1.0, 1.0, 0.0);
  scale = gtk_hscale_new (GTK_ADJUSTMENT (pressure_scale_data));
  gtk_box_pack_start (GTK_BOX (pressure_hbox), scale, TRUE, TRUE, 0);
  gtk_widget_show (scale);
  gtk_scale_set_value_pos (GTK_SCALE (scale), GTK_POS_TOP);
  gtk_range_set_update_policy (GTK_RANGE (scale), GTK_UPDATE_DELAYED);
  gtk_signal_connect (GTK_OBJECT (pressure_scale_data), "value_changed",
                      GTK_SIGNAL_FUNC (gimp_double_adjustment_update),
                      &selvals.airbrushpressure);
  gtk_table_attach (GTK_TABLE (table), pressure_hbox, 0, 2, 1, 2,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);

  pencil_hbox = gtk_hbox_new (FALSE, 4);
  label = gtk_label_new (_("No Options..."));
  gtk_box_pack_start (GTK_BOX (pencil_hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);
  gtk_table_attach (GTK_TABLE (table), pencil_hbox, 0, 2, 1, 2,
		    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);

  /* Preview widget */
  pw = gfig_brush_preview (&brush_page_pw);
  gtk_table_attach (GTK_TABLE (table), pw, 0, 1, 0, 1, 0, 0, 0, 0);

  gtk_signal_connect (GTK_OBJECT (pressure_scale_data), "value_changed",
                      GTK_SIGNAL_FUNC (gfig_brush_update_preview),
                      (gpointer)brush_page_pw);

  /* Start of new brush selection code */
  brush_sel_button = button = gtk_button_new_with_label (_("Set Brush..."));
  gtk_misc_set_padding (GTK_MISC (GTK_BIN (brush_sel_button)->child), 2, 0);
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
		      GTK_SIGNAL_FUNC (select_brush_callback),
		      NULL);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 0,
			     NULL, 0, 0,
			     button, 1,  TRUE);

  /* Setup initial brush settings */
  bdesc->bpp   = 3;
  bdesc->bname = mygimp_brush_get ();
  brush_list_button_callback (NULL, bdesc);

  return vbox;
}

static void
select_menu_callback (GtkWidget *widget,
		      gpointer   data)
{
  gint mtype = (gint) data;

  if (mtype == SELECT_TYPE_MENU)
    {
      SelectionType type = 
	(SelectionType) gtk_object_get_user_data (GTK_OBJECT (widget));

      selopt.type = type;
    }
  else if (mtype == SELECT_ARCTYPE_MENU)
    {
      ArcType type = 
	(ArcType) gtk_object_get_user_data (GTK_OBJECT (widget));

      selopt.as_pie = type;
    }
  else if (mtype == SELECT_TYPE_MENU_FILL)
    {
      FillType type =
	(FillType) gtk_object_get_user_data (GTK_OBJECT (widget));

      selopt.fill_type = type;
    }
  else if (mtype == SELECT_TYPE_MENU_WHEN)
    {
      FillWhen type = 
	(FillWhen) gtk_object_get_user_data (GTK_OBJECT (widget));
      selopt.fill_when = type;
    }
}

static GtkWidget *
select_page (void)
{
  GtkWidget *menu;
  GtkWidget *toggle;
  GtkWidget *scale;
  GtkObject *scale_data;
  GtkWidget *table;
  GtkWidget *vbox;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 4);

  table = gtk_table_new (4, 4, FALSE); 
  gtk_table_set_row_spacings (GTK_TABLE (table), 4);
  gtk_table_set_col_spacings (GTK_TABLE (table), 2);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  /* The secltion settings - 
   * 1) Type (option menu)
   * 2) Anti A (toggle)
   * 3) Feather (toggle)
   * 4) F radius (slider)
   * 5) Fill type (option menu) 
   * 6) Opacity (slider)
   * 7) When to fill (toggle)
   * 8) Arc as segment/sector 
   */

  /* 1 */
  menu = gimp_option_menu_new2 (FALSE, G_CALLBACK (select_menu_callback),
				(gpointer) SELECT_TYPE_MENU, 0,

				_("Add"),       (gpointer) ADD, NULL,
				_("Subtract"),  (gpointer) SUBTRACT, NULL,
				_("Replace"),   (gpointer) REPLACE, NULL,
				_("Intersect"), (gpointer) INTERSECT, NULL,

				NULL);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 0,
			     _("Selection Type:"), 1.0, 0.5,
			     menu, 1, FALSE);

  /* 2 */
  toggle = gtk_check_button_new_with_label (_("Antialiasing"));
  gtk_table_attach (GTK_TABLE (table), toggle, 2, 4, 0, 1,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &selopt.antia);
  gtk_widget_show (toggle); 

  /* 3 */
  toggle = gtk_check_button_new_with_label (_("Feather"));
  gtk_table_attach (GTK_TABLE (table), toggle, 2, 4, 2, 3,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &selopt.feather);
  gtk_widget_show (toggle); 

  /* 4 */
  scale_data =
    gtk_adjustment_new (selopt.feather_radius, 0.0, 100.0, 1.0, 1.0, 0.0);
  scale = gtk_hscale_new (GTK_ADJUSTMENT (scale_data));
  gtk_scale_set_value_pos (GTK_SCALE (scale), GTK_POS_TOP);
  gtk_range_set_update_policy (GTK_RANGE (scale), GTK_UPDATE_DELAYED);
  gtk_signal_connect (GTK_OBJECT (scale_data), "value_changed",
                      GTK_SIGNAL_FUNC (gimp_double_adjustment_update),
                      &selopt.feather_radius);
  gimp_table_attach_aligned (GTK_TABLE (table), 2, 3,
			     _("Radius:"), 1.0, 1.0,
			     scale, 1, FALSE);

  /* 5 */
  menu =
    gimp_option_menu_new2 (FALSE, G_CALLBACK (select_menu_callback),
			   (gpointer) SELECT_TYPE_MENU_FILL, 0,

			   _("Pattern"),    (gpointer) FILL_PATTERN, NULL,
			   _("Foreground"), (gpointer) FILL_FOREGROUND, NULL,
			   _("Background"), (gpointer) FILL_BACKGROUND, NULL,

			   NULL);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 1,
			     _("Fill Type:"), 1.0, 0.5,
			     menu, 1, FALSE);

  /* 6 */
  scale_data =
    gtk_adjustment_new (selopt.fill_opacity, 0.0, 100.0, 1.0, 1.0, 0.0);
  scale = gtk_hscale_new (GTK_ADJUSTMENT (scale_data));
  gtk_scale_set_value_pos (GTK_SCALE (scale), GTK_POS_TOP);
  gtk_range_set_update_policy (GTK_RANGE (scale), GTK_UPDATE_DELAYED);
  gtk_signal_connect (GTK_OBJECT (scale_data), "value_changed",
                      GTK_SIGNAL_FUNC (gimp_double_adjustment_update),
                      &selopt.fill_opacity);
  gimp_table_attach_aligned (GTK_TABLE (table), 2, 1,
			     _("Fill Opacity:"), 1.0, 1.0,
			     scale, 1, FALSE);

  /* 7 */
  menu =
    gimp_option_menu_new2 (FALSE, G_CALLBACK (select_menu_callback),
			   (gpointer) SELECT_TYPE_MENU_WHEN, 0,

			   _("Each Selection"), (gpointer) FILL_EACH, NULL,
			   _("All Selections"), (gpointer) FILL_AFTER, NULL,

			   NULL);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 2,
			     _("Fill after:"), 1.0, 0.5,
			     menu, 1, FALSE);

  /* 8 */
  menu = gimp_option_menu_new2 (FALSE, G_CALLBACK (select_menu_callback),
				(gpointer) SELECT_ARCTYPE_MENU, 0,

				_("Segment"), (gpointer) ARC_SEGMENT, NULL,
				_("Sector"),  (gpointer) ARC_SECTOR, NULL,

				NULL);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 3,
			     _("Arc as:"), 1.0, 0.5,
			     menu, 1, FALSE);

  return vbox;
}

static void
gridtype_menu_callback (GtkWidget *widget,
			gpointer   data)
{
  gint mtype = (gint)data;

  if (mtype == GRID_TYPE_MENU)
    {
#ifdef DEBUG
      printf ("Gridtype set to ");
      if (current_obj->opts.gridtype == RECT_GRID)
	printf ("RECT_GRID\n");
      else if (current_obj->opts.gridtype == POLAR_GRID)
	printf ("POLAR_GRID\n");
      else if (current_obj->opts.gridtype == ISO_GRID)
	printf ("ISO_GRID\n");
      else printf ("NONE\n");
#endif /* DEBUG */
      selvals.opts.gridtype = (GridType)gtk_object_get_user_data (GTK_OBJECT (widget));
    }
  else
    {
      grid_gc_type = (gint) gtk_object_get_user_data (GTK_OBJECT (widget));
    }

  draw_grid_clear (widget, 0);
}

static GtkWidget *
options_page (void)
{
  GtkWidget *table;
  GtkWidget *menu;
  GtkWidget *toggle;
  GtkWidget *button;
  GtkWidget *vbox;
  GtkObject *size_data;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 4);

  table = gtk_table_new (6, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  /* Put buttons in */
  toggle = gtk_check_button_new_with_label (_("Show Image"));
  gtk_table_attach (GTK_TABLE (table), toggle, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &selvals.showimage);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      (GtkSignalFunc) toggle_show_image,
		      (gpointer) 1);
  gtk_widget_show (toggle);

  button = gtk_button_new_with_label (_("Reload Image"));
  gtk_misc_set_padding (GTK_MISC (GTK_BIN (button)->child), 2, 0);
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
		      GTK_SIGNAL_FUNC (reload_button_callback),
		      NULL);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 0,
			     NULL, 0, 0,
			     button, 1, TRUE);

  menu = gimp_option_menu_new2 (FALSE, G_CALLBACK (gridtype_menu_callback),
				(gpointer) GRID_TYPE_MENU, 0,

				_("Rectangle"), (gpointer) RECT_GRID, NULL,
				_("Polar"),     (gpointer) POLAR_GRID, NULL,
				_("Isometric"), (gpointer) ISO_GRID, NULL,

				NULL);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 1,
			     _("Grid Type:"), 1.0, 0.5,
			     menu, 1, TRUE);

  gfig_opt_widget.gridtypemenu = menu;

  menu =
    gimp_option_menu_new2 (FALSE, G_CALLBACK (gridtype_menu_callback),
			   (gpointer) GRID_RENDER_MENU, 0,

			   _("Normal"),     (gpointer) GTK_STATE_NORMAL, NULL,
			   _("Black"),      (gpointer) GFIG_BLACK_GC, NULL,
			   _("White"),      (gpointer) GFIG_WHITE_GC, NULL,
			   _("Grey"),       (gpointer) GFIG_GREY_GC, NULL,
			   _("Darker"),     (gpointer) GTK_STATE_ACTIVE, NULL,
			   _("Lighter"),    (gpointer) GTK_STATE_PRELIGHT, NULL,
			   _("Very Dark"),  (gpointer) GTK_STATE_SELECTED, NULL,

			   NULL);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 2,
			     _("Grid Color:"), 1.0, 0.5,
			     menu, 1, TRUE);

  size_data = gimp_scale_entry_new (GTK_TABLE (table), 0, 3,
				    _("Max Undo:"), 0, 50,
				    selvals.maxundo, MIN_UNDO, MAX_UNDO, 1, 2, 0,
				    TRUE, 0, 0,
				    NULL, NULL);
  gtk_signal_connect (GTK_OBJECT (size_data), "value_changed",
		      GTK_SIGNAL_FUNC (gimp_int_adjustment_update),
		      &selvals.maxundo);

  toggle = gtk_check_button_new_with_label (_("Show Position"));
  gtk_table_attach (GTK_TABLE (table), toggle, 0, 1, 4, 5,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &selvals.showpos);
  gtk_signal_connect_after (GTK_OBJECT (toggle), "toggled",
		      (GtkSignalFunc) gfig_pos_enable,
		      (gpointer) 1);
  gtk_widget_show (toggle); 

  toggle = gtk_check_button_new_with_label (_("Hide Control Points"));
  gtk_table_attach (GTK_TABLE (table), toggle, 1, 3, 4, 5,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &selvals.opts.showcontrol);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (toggle_show_image),
		      (gpointer) 1);
  gtk_widget_show (toggle); 
  gfig_opt_widget.showcontrol = toggle;

  toggle = gtk_check_button_new_with_label (_("Show Tooltips"));
  gtk_table_attach (GTK_TABLE (table), toggle, 0, 1, 5, 6,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (toggle_tooltips),
		      &selvals.showtooltips);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle),
				selvals.showtooltips);
  gtk_widget_show (toggle); 

  button = gtk_button_new_with_label (_("About"));
  gtk_misc_set_padding (GTK_MISC (GTK_BIN (button)->child), 8, 0);
  gtk_table_attach (GTK_TABLE (table), button, 1, 3, 5, 6,
		    0, 0, 0, 0);
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
		      GTK_SIGNAL_FUNC (about_button_callback),
		      NULL);
  gtk_widget_show (button);

  return vbox;
}

static GtkWidget *
grid_frame (void)
{
  GtkWidget *frame;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *table;
  GtkWidget *toggle;
  GtkObject *size_data;

  frame = gtk_frame_new (_("Grid"));

  vbox = gtk_vbox_new (FALSE, 2);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 2);
  gtk_container_add (GTK_CONTAINER (frame), vbox);
  gtk_widget_show (vbox);

  hbox = gtk_hbox_new (FALSE, 4);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0); 
  gtk_widget_show (hbox);

  toggle = gtk_check_button_new_with_label (_("Snap to Grid"));
  gtk_box_pack_start (GTK_BOX (hbox), toggle, FALSE, FALSE, 0); 
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &selvals.opts.snap2grid);
  gtk_widget_show (toggle);
  gfig_opt_widget.snap2grid = toggle;

  toggle = gtk_check_button_new_with_label (_("Display Grid"));
  gtk_box_pack_start (GTK_BOX (hbox), toggle, FALSE, FALSE, 0); 
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &selvals.opts.drawgrid);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (draw_grid_clear),
		      (gpointer) 1);
  gtk_widget_show (toggle);
  gfig_opt_widget.drawgrid = toggle;

  toggle = gtk_check_button_new_with_label (_("Lock on Grid"));
  gtk_box_pack_start (GTK_BOX (hbox), toggle, FALSE, FALSE, 0); 
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &selvals.opts.lockongrid);
  gtk_widget_show (toggle);
  gfig_opt_widget.lockongrid = toggle;

  table = gtk_table_new (1, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0); 
  gtk_widget_show (table);

  size_data = gimp_scale_entry_new (GTK_TABLE (table), 0, 0,
				    _("Grid Spacing:"), 0, 50,
				    selvals.opts.gridspacing,
				    MIN_GRID, MAX_GRID, 1, 10, 0,
				    TRUE, 0, 0,
				    NULL, NULL);
  gtk_signal_connect (GTK_OBJECT (size_data), "value_changed",
		      GTK_SIGNAL_FUNC (gimp_int_adjustment_update),
		      &selvals.opts.gridspacing);
  gtk_signal_connect (GTK_OBJECT (size_data), "value_changed",
		      GTK_SIGNAL_FUNC (draw_grid_clear),
		      (gpointer) 0);
  gfig_opt_widget.gridspacing = size_data;

  gtk_widget_show (frame);

  return frame;
}

static void 
clear_list_items (GtkList *list)
{
  gtk_list_clear_items (list, 0, -1);
}

static void
build_list_items (GtkWidget *list)
{
  GList     *tmp;
  GtkWidget *list_item;
  GtkWidget *list_pix;
  GFigObj   *g;

  for (tmp = gfig_list; tmp; tmp = g_list_next (tmp))
    {
      g = tmp->data;

      if (g->obj_status & GFIG_READONLY)
	list_pix = gimp_pixmap_new (mini_cross_xpm);
      else
	list_pix = gimp_pixmap_new (blank_xpm);

      list_item =
	gfig_list_item_new_with_label_and_pixmap (g, g->draw_name, list_pix);

      gtk_object_set_user_data (GTK_OBJECT (list_item), (gpointer) g);
      gtk_list_append_items (GTK_LIST (list), g_list_append (NULL, list_item));

      gtk_signal_connect (GTK_OBJECT (list_item), "button_press_event",
			  GTK_SIGNAL_FUNC (list_button_press),
			  (gpointer) g);
      gtk_widget_show (list_item);
    }
}

static GtkWidget *
add_objects_list (void)
{
  GtkWidget *vbox;
  GtkWidget *table;
  GtkWidget *frame;
  GtkWidget *list_frame;
  GtkWidget *scrolled_win;
  GtkWidget *list;
  GtkWidget *button;

  frame = gtk_frame_new (_("Object"));
  gtk_widget_show (frame);

  table = gtk_table_new (4, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 2);
  gtk_container_set_border_width (GTK_CONTAINER (table), 2);
  gtk_widget_show (table);

  delete_frame_to_freeze = list_frame = gtk_frame_new (NULL);
  gtk_widget_show (list_frame);

  scrolled_win = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_win),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (list_frame), scrolled_win);
  gtk_widget_show (scrolled_win);

  gfig_gtk_list = list = gtk_list_new ();
  /* gtk_list_set_selection_mode (GTK_LIST (list), GTK_SELECTION_MULTIPLE); */
  gtk_list_set_selection_mode (GTK_LIST (list), GTK_SELECTION_BROWSE);
  gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (scrolled_win),
					 list);
  gtk_widget_show (list);

  /* Load saved objects */
  gfig_list_load_all (gfig_path_list);

  /* Put list in */
  build_list_items (list);

  /* Put buttons in */
  button = gtk_button_new_with_label (_("Rescan"));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
		      GTK_SIGNAL_FUNC (rescan_button_callback),
		      NULL);
  gimp_help_set_help_data (button,
			_("Select directory and rescan Gfig object "
			  "collection"), NULL); 
  gtk_table_attach (GTK_TABLE (table), button, 2, 3, 0, 1,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (button);

  button = gtk_button_new_with_label (_("Load"));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
		      GTK_SIGNAL_FUNC (load_button_callback),
		      list);
  gimp_help_set_help_data (button,
			   _("Load a single Gfig object collection"), NULL); 
  gtk_table_attach (GTK_TABLE (table), button, 2, 3, 1, 2,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (button);

  button = gtk_button_new_with_label (_("New"));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
		      GTK_SIGNAL_FUNC (new_button_callback),
		      "New gfig obj");
  gimp_help_set_help_data (button, _("Create a new Gfig object collection "
				     "for editing"), NULL); 
  gtk_table_attach (GTK_TABLE (table), button, 2, 3, 2, 3,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (button);

  button = gtk_button_new_with_label (_("Delete"));
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
		      GTK_SIGNAL_FUNC (gfig_delete_gfig_callback),
		      (gpointer) list);
  gimp_help_set_help_data (button, _("Delete currently selected Gfig Object "
				     "collection"), NULL); 
  gtk_table_attach (GTK_TABLE (table), button, 2, 3, 3, 4,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (button);

  /* Attach the frame for the list Show the widgets */

  gtk_table_attach (GTK_TABLE (table), list_frame, 1, 2, 0, 4,
		    GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);

  vbox = small_preview (list);
  gtk_table_attach (GTK_TABLE (table), vbox, 0, 1, 0, 4, 0, 0, 0, 0);

  gtk_container_add (GTK_CONTAINER (frame), table);

  return frame;
}

static gint x_pos_val;
static gint y_pos_val;
static gint pos_tag = -1;

static void
gfig_pos_enable (GtkWidget *widget,
		 gpointer   data)
{
  gint enable = selvals.showpos;
  gtk_widget_set_sensitive (GTK_WIDGET (pos_label), enable);
}

static void
gfig_pos_update_labels (gpointer data)
{
  static gchar buf[256];

  /*gtk_idle_remove (pos_tag);*/
  pos_tag = -1;

  g_snprintf (buf, sizeof (buf), "%d, %d", x_pos_val, y_pos_val);
  gtk_label_set_text (GTK_LABEL (pos_label), buf);
}

static void
gfig_pos_update (gint x,
		 gint y)
{
  gint update;

  if (x_pos_val != x || y_pos_val != y)
    update = 1;
  else
    update = 0;

  x_pos_val = x;
  y_pos_val = y;

  if (update && pos_tag == -1 && selvals.showpos)
    {
      /*pos_tag = gtk_idle_add ((GtkFunction)gfig_pos_update_labels, NULL);*/
      gfig_pos_update_labels (NULL);
    }
}

#if 0 /* NOT USED */
static void
gfig_obj_size_update (gint sz)
{
  static gchar buf[256];
  
  sprintf (buf, "%6d", sz);
  gtk_label_set_text (GTK_LABEL (obj_size_label), buf);
}  

static GtkWidget *
gfig_obj_size_label (void)
{
  GtkWidget *label;
  GtkWidget *hbox;
  gchar buf[256];

  hbox = gtk_hbox_new (TRUE, 6);

  /* Position labels */
  label = gtk_label_new ("Size: ");
  gtk_widget_show (label);
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

  obj_size_label = gtk_label_new ("");
  gtk_misc_set_alignment (GTK_MISC (obj_size_label), 0.5, 0.5);    
  gtk_widget_show (obj_size_label);
  gtk_box_pack_start (GTK_BOX (hbox), obj_size_label, FALSE, FALSE, 0);

  gtk_widget_show (hbox);

  sprintf (buf, "%6d", 0);
  gtk_label_set_text (GTK_LABEL (obj_size_label), buf);

  return (hbox);
}

#endif /* NOT USED */

static GtkWidget *
gfig_pos_labels (void)
{
  GtkWidget *label;
  GtkWidget *hbox;
  gchar      buf[256];

  hbox = gtk_hbox_new (FALSE, 6);
  gtk_widget_show (hbox);

  /* Position labels */
  label = gtk_label_new (_("XY Position:"));
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  pos_label = gtk_label_new ("");
  gtk_box_pack_start (GTK_BOX (hbox), pos_label, FALSE, FALSE, 0);
  gtk_widget_show (pos_label);

  g_snprintf (buf, sizeof (buf), "%d, %d", 0, 0);
  gtk_label_set_text (GTK_LABEL (pos_label), buf);

  return hbox;
}

static GtkWidget *
make_pos_info (void)
{
  GtkWidget *xframe;
  GtkWidget *hbox;
  GtkWidget *label;

  xframe = gtk_frame_new (_("Object Details"));

  hbox = gtk_hbox_new (TRUE, 6);
  gtk_container_set_border_width (GTK_CONTAINER (hbox), 2);
  gtk_container_add (GTK_CONTAINER (xframe), hbox);  

  /* Add labels */
  label = gfig_pos_labels ();
  gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);
  gfig_pos_enable (NULL, NULL);

#if 0
  label = gfig_obj_size_label ();
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
#endif /* 0 */

  gtk_widget_show (hbox);
  gtk_widget_show (xframe);

  return xframe;
}

static GtkWidget *
make_status (void)
{
  GtkWidget *xframe;
  GtkWidget *table;
  GtkWidget *label;

  xframe = gtk_frame_new (_("Collection Details"));

  gtk_frame_set_shadow_type (GTK_FRAME (xframe), GTK_SHADOW_ETCHED_IN);
  table = gtk_table_new (6, 6, FALSE);
  gtk_table_set_col_spacing (GTK_TABLE (table), 1, 6);
  gtk_container_set_border_width (GTK_CONTAINER (table), 2);

  label = gtk_label_new (_("Draw Name:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 1, 2, 0, 1,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (label);

  label = gtk_label_new (_("Filename:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 1, 2, 1, 2,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (label);

  status_label_dname = gtk_label_new (_("(none)"));
  gtk_misc_set_alignment (GTK_MISC (status_label_dname), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), status_label_dname, 2, 4, 0, 1,
		    GTK_FILL | GTK_EXPAND, 0, 0, 0);
  gtk_widget_show (status_label_dname);

  status_label_fname = gtk_label_new (_("(none)"));
  gtk_misc_set_alignment (GTK_MISC (status_label_fname), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), status_label_fname, 2, 4, 1, 2,
		    GTK_FILL | GTK_EXPAND, 0, 0, 0);
  gtk_widget_show (status_label_fname);

  gtk_container_add (GTK_CONTAINER (xframe), table);  

  gtk_widget_show (table);
  gtk_widget_show (xframe);

  return xframe;
}

static GtkWidget *
make_preview (void)
{
  GtkWidget *xframe;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *table;
  GtkWidget *ruler;
  
  gfig_preview = gtk_preview_new (GTK_PREVIEW_COLOR);
  gtk_widget_set_events (GTK_WIDGET (gfig_preview), PREVIEW_MASK);
  gfig_preview_exp_id = 
    gtk_signal_connect_after (GTK_OBJECT (gfig_preview), "expose_event",
			      GTK_SIGNAL_FUNC (gfig_preview_expose),
			      NULL);

  gtk_signal_connect (GTK_OBJECT (gfig_preview), "event",
		      GTK_SIGNAL_FUNC (gfig_preview_events),
		      NULL);

  gtk_preview_size (GTK_PREVIEW (gfig_preview), preview_width, preview_height);

  xframe = gtk_frame_new (NULL);

  gtk_frame_set_shadow_type (GTK_FRAME (xframe), GTK_SHADOW_IN);

  table = gtk_table_new (3, 3, FALSE);
  gtk_table_attach (GTK_TABLE (table), gfig_preview, 1, 2, 1, 2,
		    GTK_FILL , GTK_FILL , 0, 0);
  gtk_container_add (GTK_CONTAINER (xframe), table); 

  ruler = gtk_hruler_new ();
  gtk_ruler_set_range (GTK_RULER (ruler), 0, preview_width, 0, PREVIEW_SIZE);
  gtk_signal_connect_object (GTK_OBJECT (gfig_preview), "motion_notify_event",
			     (GtkSignalFunc) GTK_WIDGET_CLASS (G_OBJECT_GET_CLASS (ruler))->motion_notify_event,
			     GTK_OBJECT (ruler));
  gtk_table_attach (GTK_TABLE (table), ruler, 1, 2, 0, 1,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (ruler);

  ruler = gtk_vruler_new ();
  gtk_ruler_set_range (GTK_RULER (ruler), 0, preview_height, 0, PREVIEW_SIZE);
  gtk_signal_connect_object (GTK_OBJECT (gfig_preview), "motion_notify_event",
			     (GtkSignalFunc) GTK_WIDGET_CLASS (G_OBJECT_GET_CLASS (ruler))->motion_notify_event,
			     GTK_OBJECT (ruler));
  gtk_table_attach (GTK_TABLE (table), ruler, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (ruler);

  gtk_widget_show (xframe);
  gtk_widget_show (table);

  vbox = gtk_vbox_new (FALSE, 0);
  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), xframe, FALSE, FALSE, 0);

  gtk_container_set_border_width (GTK_CONTAINER (vbox), 2);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

  xframe = make_pos_info ();
  gtk_box_pack_start (GTK_BOX (vbox), xframe, TRUE, TRUE, 0);

  xframe = make_status ();
  gtk_box_pack_start (GTK_BOX (vbox), xframe, TRUE, TRUE, 0);

  gtk_widget_show (vbox);
  gtk_widget_show (hbox);

  return vbox;
}

#if 0
scatch ()
{

  pause ();

}
#endif /* 0 */

static void
gfig_grid_colours (GtkWidget   *widget,
		   GdkColormap *cmap)
{
  GdkGCValues values;
  GdkColor new_col1;
  GdkColor new_col2;
  guchar stipple[8] =
  {
    0xAA,    /*  ####----  */
    0x55,    /*  ###----#  */
    0xAA,    /*  ##----##  */
    0x55,    /*  #----###  */
    0xAA,    /*  ----####  */
    0x55,    /*  ---####-  */
    0xAA,    /*  --####--  */
    0x55,    /*  -####---  */
  };

  gdk_color_parse ("gray50", &new_col1);
  gdk_colormap_alloc_color (xxx, &new_col1, FALSE, TRUE);
  gdk_color_parse ("gray80", &new_col2);
  gdk_colormap_alloc_color (xxx, &new_col2, FALSE, TRUE);
  values.background.pixel = new_col1.pixel;
  values.foreground.pixel = new_col2.pixel;
  values.fill    = GDK_OPAQUE_STIPPLED;
  values.stipple = gdk_bitmap_create_from_data (widget->window,
						(gchar *) stipple,
						4, 4);
  grid_hightlight_drawgc = gdk_gc_new_with_values (widget->window,
						   &values,
						   GDK_GC_FOREGROUND |
						   GDK_GC_BACKGROUND |
						   GDK_GC_FILL |
						   GDK_GC_STIPPLE);
}


static gint
gfig_dialog (void)
{
  GtkWidget *main_hbox;
  GtkWidget *frame;
  GtkWidget *vbox;
  GtkWidget *xframe;
  GtkWidget *oframe;
  GtkWidget *notebook;
  GtkWidget *page;
  GtkWidget *top_level_dlg;

  gimp_ui_init ("gfig", TRUE);

  yyy = gdk_rgb_get_visual ();
  xxx = gdk_rgb_get_colormap ();

  plug_in_parse_gfig_path ();

  /*cache_preview (); Get the preview image and store it also set has_alpha */

  img_width  = gimp_drawable_width (gfig_select_drawable->drawable_id);
  img_height = gimp_drawable_height (gfig_select_drawable->drawable_id);

  /* Start buildng the dialog up */
  top_level_dlg = gimp_dialog_new (_("GFig"), "gfig",
				   gimp_standard_help_func, "filters/gfig.html",
				   GTK_WIN_POS_MOUSE,
				   FALSE, FALSE, FALSE,

				   _("Done"), gfig_ok_callback,
				   NULL, NULL, NULL, TRUE, FALSE,
				   _("Paint"), gfig_paint_callback,
				   NULL, NULL, NULL, FALSE, FALSE,
				   GTK_STOCK_SAVE, save_button_callback,
				   NULL, NULL, &save_button, FALSE, FALSE,
				   GTK_STOCK_CLEAR, gfig_clear_callback,
				   NULL, NULL, NULL, FALSE, FALSE,
				   GTK_STOCK_UNDO, gfig_undo_callback,
				   NULL, NULL, &undo_widget, FALSE, FALSE,
				   GTK_STOCK_CANCEL, gtk_widget_destroy,
				   NULL, 1, NULL, FALSE, TRUE,

				   NULL);

  gtk_signal_connect (GTK_OBJECT (top_level_dlg), "destroy",
		      GTK_SIGNAL_FUNC (gtk_main_quit),
		      NULL);

  /* Tooltips bis */
  gimp_help_init ();

  main_hbox = gtk_hbox_new (FALSE, 4);
  gtk_container_set_border_width (GTK_CONTAINER (main_hbox), 4);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (top_level_dlg)->vbox), main_hbox,
		      TRUE, TRUE, 0);

  /* Add buttons beside the preview frame */
  xframe = draw_buttons (top_level_dlg);
  gtk_box_pack_start (GTK_BOX (main_hbox), xframe, FALSE, FALSE, 0);
  /*gtk_widget_show (xframe);*/

  /* Start building the frame for the preview area */
  frame = gtk_frame_new (_("Preview"));
  gtk_box_pack_start (GTK_BOX (main_hbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame); 

  /* Preview itself */
  xframe = make_preview ();
  gtk_container_add (GTK_CONTAINER (frame), xframe);
  /* gtk_widget_show (xframe); */

  gtk_widget_show (gfig_preview);

  frame = gtk_frame_new (_("Settings"));
  gtk_box_pack_start (GTK_BOX (main_hbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame); 

  vbox = gtk_vbox_new (FALSE, 2);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 2);
  gtk_container_add (GTK_CONTAINER (frame), vbox);
  gtk_widget_show (vbox);

  /* listbox + entry */
  oframe = add_objects_list ();
  gtk_box_pack_start (GTK_BOX (vbox), oframe, FALSE, FALSE, 0);

  /* Grid entry */
  xframe = grid_frame ();
  gtk_box_pack_start (GTK_BOX (vbox), xframe, FALSE, FALSE, 0);

  /* The notebook */
  notebook = gtk_notebook_new ();
  gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_TOP);
  gtk_box_pack_start (GTK_BOX (vbox), notebook, FALSE, FALSE, 0);
  gtk_widget_show (notebook);
  
  page = paint_page ();
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), page,
			    gtk_label_new (_("Paint")));
  gtk_widget_show (page);

  brush_page_widget = brush_page ();
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), brush_page_widget,
			    gtk_label_new (_("Brush")));
  gtk_widget_show (brush_page_widget);

  /* Sometime maybe allow all objects to be done by selections - this
   * would adjust the selection options.
   */
  select_page_widget = select_page ();
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), select_page_widget,
			    gtk_label_new (_("Select")));
  gtk_widget_set_sensitive (select_page_widget, FALSE);

  page = options_page ();
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), page,
			    gtk_label_new (_("Options")));
  gtk_widget_show (page);

  gtk_widget_show (main_hbox);

  gtk_widget_show (top_level_dlg);

  dialog_update_preview ();
  gfig_new_gc (); /* Need this for drawing */
  gfig_update_stat_labels ();

  gfig_grid_colours (gfig_preview, xxx);
  /* Popup for list area */
  gfig_op_menu_create (top_level_dlg);

  /* signal (11, scatch); For debugging */

  gtk_main ();

  gimp_image_delete (brush_image_ID);
  brush_image_ID = -1;

  gimp_help_free ();

  gdk_flush ();

  return gfig_run;
}

static void
gfig_really_ok_callback (GtkWidget *widget,
			 gboolean   doit,
			 gpointer   data)
{
  if (doit)
    {
      gfig_run = TRUE;
      gtk_widget_destroy (GTK_WIDGET (data));
    }
}

static void
gfig_ok_callback (GtkWidget *widget,
		  gpointer   data)
{
  /* Check if outstanding saves */
  GList   *list;
  GFigObj *gfig;
  gint     count = 0;

  for (list = gfig_list; list; list = g_list_next (list))
    {
      gfig = (GFigObj *) list->data;
      if (gfig->obj_status & GFIG_MODIFIED)
	count++;
    }

  if (count)
    {
      GtkWidget *dialog;
      gchar     *message;

      message =
	g_strdup_printf (_("%d unsaved Gfig objects.\nContinue with exiting?"), count);

      dialog = gimp_query_boolean_box (_("Warning"),
				       gimp_standard_help_func,
				       "filters/gfig.html",
				       FALSE,
				       message,
				       GTK_STOCK_OK, GTK_STOCK_CANCEL,
				       NULL, NULL,
				       gfig_really_ok_callback,
				       data);
      g_free (message);

      gtk_widget_show (dialog);
    }
  else
    {
      gfig_run = TRUE;
      gtk_widget_destroy (GTK_WIDGET (data));
    }
}

/* Update the bits we put on the screen */
static void
update_draw_area (GtkWidget *widget,
		  GdkEvent  *event)
{
  if (!GTK_WIDGET_DRAWABLE (widget))
    return;

  gtk_signal_handler_block (GTK_OBJECT (widget), gfig_preview_exp_id);
  gtk_widget_draw (widget, NULL);
  gtk_signal_handler_unblock (GTK_OBJECT (widget), gfig_preview_exp_id);

  draw_grid (widget, 0);
  draw_objects (current_obj->obj_list, TRUE);
}

static gint
gfig_preview_expose (GtkWidget *widget,
		     GdkEvent  *event)
{
  GdkCursor *preview_cursor;
  static gint changed_cursor = 0;

  if (!changed_cursor && gfig_preview->window)
    {
      changed_cursor = 1;
      preview_cursor = gdk_cursor_new (GDK_CROSSHAIR);
      gdk_window_set_cursor (gfig_preview->window, preview_cursor);
    }
  update_draw_area (widget, event);
  return FALSE;
}

static gint
pic_preview_expose (GtkWidget *widget,
		    GdkEvent  *event)
{
  if (pic_obj)
    {
      drawing_pic = TRUE;
      draw_objects (pic_obj->obj_list, FALSE);
      drawing_pic = FALSE;
    }
  return FALSE;
}

static gint
adjust_pic_coords (gint coord,
		   gint ratio)
{
  /*return ((SMALL_PREVIEW_SZ * coord)/PREVIEW_SIZE);*/
  static gint pratio = -1;

  if (pratio == -1)
    {
      pratio = MAX (preview_width, preview_height);
    }

  return (SMALL_PREVIEW_SZ * coord) / pratio;
}
 
static gint
gfig_preview_events (GtkWidget *widget,
		     GdkEvent  *event)
{
  GdkEventButton *bevent;
  GdkEventMotion *mevent;
  GdkPoint point;
  static gint tmp_show_single = 0;

  switch (event->type)
    {
    case GDK_EXPOSE:
      break;

    case GDK_BUTTON_PRESS:
      bevent = (GdkEventButton *) event;
      point.x = bevent->x;
      point.y = bevent->y;

      g_assert (need_to_scale == 0); /* If not out of step some how */

      /* Start drawing of object */
      if (selvals.otype >= MOVE_OBJ)
	{
	  if (!selvals.scaletoimage)
	    {
	      point.x = gfig_invscale_x (point.x);
	      point.y = gfig_invscale_y (point.y);
	    }
	  object_operation_start (&point, bevent->state & GDK_SHIFT_MASK);

	  /* If constraining save start pnt */
	  if (selvals.opts.snap2grid)
	    {
	      /* Save point to constained point ... if button 3 down */
	      if (bevent->button == 3)
		{
		  find_grid_pos (&point, &point, FALSE);
		}
	    }
	}
      else
	{
	  if (selvals.opts.snap2grid)
	    {
	      if (bevent->button == 3)
		{
		  find_grid_pos (&point, &point, FALSE);
		}
	      else
		{
		  find_grid_pos (&point, &point, FALSE);
		}
	    }
	  object_start (&point, bevent->state & GDK_SHIFT_MASK);
	}

      break;
    case GDK_BUTTON_RELEASE:
      bevent = (GdkEventButton *) event;
      point.x = bevent->x;
      point.y = bevent->y;

      if (selvals.opts.snap2grid)
	find_grid_pos (&point, &point, bevent->button == 3);

      /* Still got shift down ?*/
      if (selvals.otype >= MOVE_OBJ)
	{
	  if (!selvals.scaletoimage)
	    {
	      point.x = gfig_invscale_x (point.x);
	      point.y = gfig_invscale_y (point.y);
	    }
	  object_operation_end (&point, bevent->state & GDK_SHIFT_MASK);
	}
      else
	{
	  if (obj_creating)
	    {
	      object_end (&point, bevent->state & GDK_SHIFT_MASK);
	    }
	  else
	    break;
	}

      /* make small preview reflect changes ?*/
      list_button_update (current_obj);
      break;
    case GDK_MOTION_NOTIFY:

      mevent = (GdkEventMotion *) event;
      point.x = mevent->x;
      point.y = mevent->y;

      if (selvals.opts.snap2grid)
	find_grid_pos (&point, &point, mevent->state & GDK_BUTTON3_MASK);

      if (selvals.otype >= MOVE_OBJ)
	{
	  /* Moving objects around */
	  if (!selvals.scaletoimage)
	    {
	      point.x = gfig_invscale_x (point.x);
	      point.y = gfig_invscale_y (point.y);
	    }
	  object_operation (&point, mevent->state & GDK_SHIFT_MASK);
	  gfig_pos_update (point.x, point.y);
	  return FALSE;
	}

      if (obj_creating)
	{
	  object_update (&point);
	}
      gfig_pos_update (point.x, point.y);
      break;
    case GDK_KEY_PRESS:
      if ((tmp_show_single = obj_show_single) != -1)
	{
	  obj_show_single = -1;
	  draw_grid_clear (NULL, NULL); /*Args not used */
	}
      break;
    case GDK_KEY_RELEASE:
      if (tmp_show_single != -1)
	{
	  obj_show_single = tmp_show_single;
	  draw_grid_clear (NULL, NULL); /*Args not used */
	}
      break;
    default:
      break;
    }
  return FALSE;
}


/*
 *  The edit gfig name attributes dialog
 *  Modified from Gimp source - layer edit.
 */

typedef struct _GfigListOptions
{
  GtkWidget *query_box;
  GtkWidget *name_entry;
  GtkWidget *list_entry;
  GFigObj   *obj;
  gint       created;
} GfigListOptions;

static GtkWidget *
gfig_list_add (GFigObj *obj)
{
  GList     *list;
  gint       pos;
  GtkWidget *list_item;
  GtkWidget *list_pix;

  list_pix  = gimp_pixmap_new (Floppy6_xpm);
  list_item =
    gfig_list_item_new_with_label_and_pixmap (obj, obj->draw_name, list_pix);

  gtk_object_set_user_data (GTK_OBJECT (list_item), (gpointer) obj);

  pos = gfig_list_insert (obj);

  list = g_list_append (NULL, list_item);
  gtk_list_insert_items (GTK_LIST (gfig_gtk_list), list, pos);
  gtk_widget_show (list_item);
  gtk_list_select_item (GTK_LIST (gfig_gtk_list), pos);  

  gtk_signal_connect (GTK_OBJECT (list_item), "button_press_event",
		      GTK_SIGNAL_FUNC (list_button_press),
		      (gpointer) obj);

  return list_item;
}

static void
gfig_list_ok_callback (GtkWidget *widget,
		       gpointer   data)
{
  GfigListOptions *options;
  GtkWidget *list;
  gint pos;

  options = (GfigListOptions *) data;
  list = options->list_entry;

  /*  Set the new layer name  */
#ifdef DEBUG
  printf ("Found obj %s\n", options->obj->draw_name);
#endif /* DEBUG */
  if (options->obj->draw_name)
    {
      g_free (options->obj->draw_name);
    }
  options->obj->draw_name =
    g_strdup (gtk_entry_get_text (GTK_ENTRY (options->name_entry)));
#ifdef DEBUG
  printf ("NEW name %s\n", options->obj->draw_name);
#endif /* DEBUG */

  /* Need to reorder the list */
/* gtk_label_set_text (GTK_LABEL (options->layer_widget->label), layer->name);*/

  pos = gtk_list_child_position (GTK_LIST (gfig_gtk_list), list);
#ifdef DEBUG
  printf ("pos = %d\n", pos);
#endif /* DEBUG */

  gtk_list_clear_items (GTK_LIST (gfig_gtk_list), pos, pos + 1);

  /* remove/Add again */
  gfig_list = g_list_remove (gfig_list, options->obj);
  gfig_list_add (options->obj);

  options->obj->obj_status |= GFIG_MODIFIED;

  gtk_widget_destroy (options->query_box);
  g_free (options);

  gfig_update_stat_labels ();
}

static void
gfig_list_cancel_callback (GtkWidget *widget,
			   gpointer   data)
{
  GfigListOptions *options;

  options = (GfigListOptions *) data;
  if (options->created)
    {
      /* We are creating an entry so if cancelled
       * must del the list item as well
       */
      gfig_do_delete_gfig_callback (widget, TRUE, gfig_gtk_list);
    }

  gtk_widget_destroy (options->query_box);
  g_free (options);
}

static void
gfig_dialog_edit_list (GtkWidget *lwidget,
		       GFigObj   *obj,
		       gint       created)
{
  GfigListOptions *options;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *label;

  /*  the new options structure  */
  options = g_new (GfigListOptions, 1);
  options->list_entry = lwidget;
  options->obj        = obj;
  options->created    = created;

  /*  the dialog  */
  options->query_box =
    gimp_dialog_new (_("Enter Gfig Entry Name"), "gfig",
		     gimp_standard_help_func, "filters/gfig.html",
		     GTK_WIN_POS_MOUSE,
		     FALSE, TRUE, FALSE,

		     GTK_STOCK_OK, gfig_list_ok_callback,
		     options, NULL, NULL, TRUE, FALSE,
		     GTK_STOCK_CANCEL, gfig_list_cancel_callback,
		     options, NULL, NULL, FALSE, TRUE,

		     NULL);

  /*  the main vbox  */
  vbox = gtk_vbox_new (FALSE, 2);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (options->query_box)->vbox), vbox,
		      FALSE, FALSE, 0);
  gtk_widget_show (vbox);

  /*  the name entry hbox, label and entry  */
  hbox = gtk_hbox_new (FALSE, 4);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  label = gtk_label_new (_("Gfig Object Name:"));
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  options->name_entry = gtk_entry_new ();
  gtk_box_pack_start (GTK_BOX (hbox), options->name_entry, TRUE, TRUE, 0);
  gtk_entry_set_text (GTK_ENTRY (options->name_entry), obj->draw_name);
  gtk_widget_show (options->name_entry);

  gtk_widget_show (options->query_box);
}

static void
gfig_rescan_ok_callback (GtkWidget *widget,
			 gpointer   data)
{
  GtkWidget *patheditor;
  gchar     *raw_path;

  gtk_widget_set_sensitive (GTK_WIDGET (data), FALSE);

  gimp_path_free (gfig_path_list);
  gfig_path_list = NULL;

  patheditor = GTK_WIDGET (gtk_object_get_data (GTK_OBJECT (data),
						"patheditor"));

  raw_path = gimp_path_editor_get_path (GIMP_PATH_EDITOR (patheditor));

  gfig_path_list = gimp_path_parse (raw_path, 16, FALSE, NULL);

  g_free (raw_path);

  if (gfig_path_list)
    {
      clear_list_items (GTK_LIST (gfig_gtk_list));
      gfig_list_load_all (gfig_path_list);
      build_list_items (gfig_gtk_list);
      list_button_update (current_obj);
    }

  gtk_widget_destroy (GTK_WIDGET (data));
}

static void
gfig_rescan_list (void)
{
  static GtkWidget *dlg = NULL;

  GtkWidget *patheditor;
  gchar     *path;

  if (dlg)
    {
      gdk_window_raise (dlg->window);
      return;
    }

  /*  the dialog  */
  dlg = gimp_dialog_new (_("Rescan for Gfig Objects"), "gfig",
			 gimp_standard_help_func, "filters/gfig.html",
			 GTK_WIN_POS_MOUSE,
			 FALSE, TRUE, FALSE,

			 GTK_STOCK_OK, gfig_rescan_ok_callback,
			 NULL, NULL, NULL, TRUE, FALSE,
			 GTK_STOCK_CANCEL, gtk_widget_destroy,
			 NULL, 1, NULL, FALSE, TRUE,

			 NULL);

  gtk_signal_connect (GTK_OBJECT (dlg), "destroy",
		      GTK_SIGNAL_FUNC (gtk_widget_destroyed),
		      &dlg);

  path = gimp_path_to_str (gfig_path_list);

  patheditor = gimp_path_editor_new (_("Add Gfig Path"), path);
  gtk_container_set_border_width (GTK_CONTAINER (patheditor), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), patheditor,
		      TRUE, TRUE, 0);
  gtk_widget_show (patheditor);

  g_free (path);

  gtk_object_set_data (GTK_OBJECT (dlg), "patheditor", patheditor);

  gtk_widget_show (dlg);
}

static void
list_button_update (GFigObj *obj)
{
  g_return_if_fail (obj != NULL);

  pic_obj = (GFigObj *) obj;

  gtk_widget_draw (pic_preview, NULL);

  drawing_pic = TRUE;
  draw_objects (pic_obj->obj_list, FALSE);
  drawing_pic = FALSE;
}


static void
gfig_load_file_selection_ok (GtkWidget        *widget,
		             GtkFileSelection *fs,
		             gpointer          data)
{
  const gchar *filename;
  struct stat  filestat;
  gint         err;
  GFigObj     *gfig;
  GFigObj     *current_saved;

  filename = gtk_file_selection_get_filename (GTK_FILE_SELECTION (fs));

#ifdef DEBUG
  printf ("Loading file '%s'\n", filename);
#endif /* DEBUG */

  err = stat (filename, &filestat);

  if (!err && S_ISREG (filestat.st_mode))
    {
      /* Hack - current object MUST be NULL to prevent setup_undo ()
       * from kicking in.
       */
      current_saved = current_obj;
      current_obj = NULL;
      gfig = gfig_load (filename, filename);
      current_obj = current_saved;
      
      if (gfig)
	{
	  /* Read only ?*/
	  if (access (filename, W_OK))
	    gfig->obj_status |= GFIG_READONLY;

	  gfig_list_add (gfig);
	  new_obj_2edit (gfig);
	}
    }

  gtk_widget_destroy (GTK_WIDGET (fs));
}

static void
load_button_callback (GtkWidget *widget,
		      gpointer   data)
{
  static GtkWidget *window = NULL;

  /* Load a single object */
  window = gtk_file_selection_new (_("Load Gfig obj"));
  gtk_window_set_position (GTK_WINDOW (window), GTK_WIN_POS_MOUSE);

  gtk_signal_connect (GTK_OBJECT (window), "destroy",
		      GTK_SIGNAL_FUNC (gtk_widget_destroyed),
		      &window);

  gtk_signal_connect (GTK_OBJECT (GTK_FILE_SELECTION (window)->ok_button),
		      "clicked",
		      GTK_SIGNAL_FUNC (gfig_load_file_selection_ok),
		      (gpointer) window);

  gtk_signal_connect_object (GTK_OBJECT (GTK_FILE_SELECTION (window)->cancel_button),
			     "clicked",
			     GTK_SIGNAL_FUNC (gtk_widget_destroy),
			     GTK_OBJECT (window));
  gtk_widget_show (window);
}

static void
paint_layer_copy (gchar *new_name)
{
  gint32 old_drawable = gfig_drawable;
  if ((gfig_drawable = gimp_layer_copy (gfig_drawable)) < 0)
    {
      g_warning (_("Error in copy layer for onlayers"));
      gfig_drawable = old_drawable;
      return;
    }

  gimp_layer_set_name (gfig_drawable, new_name);
  gimp_image_add_layer (gfig_image, gfig_drawable, -1);
}

static void
paint_layer_new (gchar *new_name)
{
  gint32 layer_id;
  gint32 fill_type;
  int isgrey = 0;

  switch (gimp_drawable_type (gfig_select_drawable->drawable_id))
    {
    case GIMP_GRAYA_IMAGE:
    case GIMP_GRAY_IMAGE:
      isgrey = 2;
    default:
      break;
    }

  if ((layer_id = gimp_layer_new (gfig_image,
				  new_name,
				  img_width,
				  img_height,
				  1 + isgrey, /* RGBA or GRAYA type */
				  100.0, /* opacity */
				  0 /* mode */)) < 0)
    g_warning (_("Error in creating layer"));
  else
    {
      gimp_image_add_layer (gfig_image, layer_id, -1);
      gimp_drawable_fill (layer_id, 1);
    }
  
  gfig_drawable = layer_id;
  
  switch (selvals.onlayerbg)
    {
    case LAYER_TRANS_BG:
      fill_type = 3;
      break;
    case LAYER_BG_BG:
      fill_type = 1;
      break;
    case LAYER_FG_BG:
      fill_type = 0;
      break;
    case LAYER_WHITE_BG:
      fill_type = 2;
      break;
    case LAYER_COPY_BG:
    default:
      fill_type = 1;
      g_warning ("Paint layer new internal error %d\n", selvals.onlayerbg);
      break;
    }
  /* Have to clear layer out since creating transparent layer
   * seems to leave rubbish in it.
   */

  gimp_drawable_fill (layer_id, fill_type);

}

static void
paint_layer_fill ()
{
  gimp_bucket_fill (gfig_drawable,
		    selopt.fill_type,    /* Fill mode */
		    GIMP_NORMAL_MODE,
		    selopt.fill_opacity, /* Fill opacity */
		    0.0,                 /* threshold - ignored */
		    FALSE,               /* Sample merged - ignored */
		    0.0,                 /* x - ignored */
		    0.0);                /* y - ignored */
}
       
static void
gfig_paint_callback (GtkWidget *widget,
		     gpointer   data)
{
  DAllObjs * objs;
  gint layer_count = 0;
  gchar buf[128];
  gint count;
  gint ccount = 0;
  BrushDesc *bdesc;

  objs = current_obj->obj_list;

  count = gfig_obj_counts (objs);
#if 0
  gtk_progress_bar_update (GTK_PROGRESS_BAR (progress_widget), (gfloat) 0.0);
#endif /* 0 */

  /* Set the brush up */
  bdesc = gtk_object_get_user_data (GTK_OBJECT (brush_page_pw));

  if (bdesc)
    mygimp_brush_set (bdesc->bname);

  while (objs)
    {

      if (ccount == obj_show_single || obj_show_single == -1)
	{
	  sprintf (buf, _("Gfig Layer %d"), layer_count++);
	  
	  if (selvals.painttype != PAINT_SELECTION_TYPE)
	    {
	      switch (selvals.onlayers)
		{
		case SINGLE_LAYER:
		  if (layer_count == 1)
		    {
		      if (selvals.onlayerbg == LAYER_COPY_BG)
			paint_layer_copy (buf);
		      else
			paint_layer_new (buf);
		    }
		  break;
		case MULTI_LAYER:
		  if (selvals.onlayerbg == LAYER_COPY_BG)
		    paint_layer_copy (buf);
		  else
		    paint_layer_new (buf);
		  break;
		case ORIGINAL_LAYER:
		  /* Just use the given layer */
		  break;
		default:
		  g_warning ("Error in onlayers val %d", selvals.onlayers);
		  break;
		}
	    }
	  
	  objs->obj->paintfunc (objs->obj);
	  
	  /* Fill layer if required */
	  if (selvals.painttype == PAINT_SELECTION_FILL_TYPE 
	      && selopt.fill_when == FILL_EACH)
	    paint_layer_fill ();
	}

      objs = objs->next;
      
      ccount++;
#if 0 
      gtk_progress_bar_update (GTK_PROGRESS_BAR (progress_widget), (gfloat)ccount/(gfloat)count);
      gtk_widget_draw (GTK_WIDGET (progress_widget), NULL);
#endif /* 0 */
    }

  /* Fill layer if required */
  if (selvals.painttype == PAINT_SELECTION_FILL_TYPE 
      && selopt.fill_when == FILL_AFTER)
    paint_layer_fill ();

  gimp_displays_flush ();
}

static void
reload_button_callback (GtkWidget *widget,
			gpointer   data)
{
  refill_cache ();
  draw_grid_clear (widget, data);
}

static void
about_button_callback (GtkWidget *widget,
		       gpointer   data)
{
  GtkWidget *window;
  GtkWidget *label;
  GtkWidget *hbox;
  GtkWidget *vbox;
  GtkWidget *pm;

  window = gimp_dialog_new (_("About GFig"), "gfig",
			    gimp_standard_help_func, "filters/gfig.html",
			    GTK_WIN_POS_MOUSE,
			    FALSE, FALSE, FALSE,

			    GTK_STOCK_OK, gtk_widget_destroy,
			    NULL, 1, NULL, TRUE, TRUE,

			    NULL);

  /* Bits and bobs */
  pm = gimp_pixmap_new (rulers_comp_xpm);
  gtk_widget_show (pm);

  hbox = gtk_hbox_new (FALSE, 1);
  gtk_widget_show (hbox);

  vbox = gtk_vbox_new (FALSE, 1);
  gtk_widget_show (vbox);

  gtk_box_pack_start (GTK_BOX (hbox), pm, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (window)->vbox), hbox, TRUE, TRUE, 0);

  label = gtk_label_new (_("Gfig - GIMP plug-in"));
  gtk_misc_set_padding (GTK_MISC (label), 2, 2);
  gtk_widget_show (label);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

  label = gtk_label_new (_("Release 1.3"));
  gtk_misc_set_padding (GTK_MISC (label), 2, 2);
  gtk_widget_show (label);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

  label = gtk_label_new ("Andy Thomas");
  gtk_misc_set_padding (GTK_MISC (label), 2, 2);
  gtk_widget_show (label);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

  label = gtk_label_new (_("Email alt@picnic.demon.co.uk"));
  gtk_misc_set_padding (GTK_MISC (label), 2, 2);
  gtk_widget_show (label);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

  label = gtk_label_new ("http://www.picnic.demon.co.uk/");
  gtk_misc_set_padding (GTK_MISC (label), 2, 2);
  gtk_widget_show (label);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

  label = gtk_label_new (_("Isometric grid By Rob Saunders"));
  gtk_misc_set_padding (GTK_MISC (label), 2, 2);
  gtk_widget_show (label);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

  gtk_widget_show (window);
}


static void
save_button_callback (GtkWidget *widget,
		      gpointer   data)
{
  gfig_save ();  /* Save current object */
}

static void
rescan_button_callback (GtkWidget *widget,
			gpointer   data)
{
  gfig_rescan_list ();
}

static GtkWidget *
new_gfig_obj (gchar *name)
{
  GFigObj   *gfig;
  GtkWidget *new_list_item;
  /* Create a new entry */

  gfig = gfig_new ();

  if (!name)
    name = _("New gfig obj");

  gfig->draw_name = g_strdup (name);

  /* Leave options as before */
  pic_obj = current_obj = gfig;

  new_list_item = gfig_list_add (gfig);

  tmp_bezier = obj_creating = tmp_line = NULL;

  /* Redraw areas */
  update_draw_area (gfig_preview, NULL);
  list_button_update (gfig);

  return new_list_item;
}

static void
new_button_callback (GtkWidget *widget,
		     gpointer   data)
{
  GtkWidget *new_list_item;

  new_list_item = new_gfig_obj ((gchar*) data);
  gfig_dialog_edit_list (new_list_item, current_obj, TRUE);
}

static GtkWidget *delete_dialog = NULL;

static void
gfig_do_delete_gfig_callback (GtkWidget *widget,
			      gboolean   delete,
			      gpointer   data)
{
  gint pos;
  GList *sellist;
  GFigObj *sel_obj;
  GtkWidget *list = (GtkWidget *) data;

  if (!delete)
    {
      gtk_widget_set_sensitive (delete_frame_to_freeze, TRUE);
      return;
    }

#ifdef DEBUG
  printf ("Delete button pressed\n");
#endif /* DEBUG */
  /* Must update which object we are editing */
  /* Get the list and which item is selected */
  /* Only allow single selections */

  sellist = GTK_LIST (list)->selection; 

  sel_obj = (GFigObj *) gtk_object_get_user_data (GTK_OBJECT (sellist->data));

  pos = gtk_list_child_position (GTK_LIST (gfig_gtk_list), sellist->data);
#ifdef DEBUG
  printf ("delete pos = %d\n", pos);
#endif /* DEBUG */

  /* Delete the current  item + asssociated file */
  gtk_list_clear_items (GTK_LIST (gfig_gtk_list), pos, pos + 1);
  /* Shadow copy for ordering info */
  gfig_list = g_list_remove (gfig_list, sel_obj);

  if (sel_obj == current_obj)
    {
      clear_undo ();
    }
  
  /* Free current obj */
  gfig_free_everything (sel_obj);

  /* Select previous one */
  pos--;

  if (pos < 0)
    {
      if (g_list_length (gfig_list) == 0)
	{
	  /* Warning - we have a problem here
	   * since we are not really "creating an entry"
	   * why call gfig_new?
	   */
	  new_button_callback (NULL, NULL);
	}

      pos = 0;
    }

  gtk_widget_set_sensitive (delete_frame_to_freeze, TRUE);

  gtk_list_select_item (GTK_LIST (gfig_gtk_list), pos);  

  current_obj = g_list_nth (gfig_list, pos)->data;

  update_draw_area (gfig_preview, NULL);

  list_button_update (current_obj);

  gfig_update_stat_labels ();
}

static void
gfig_delete_gfig_callback (GtkWidget *widget,
			   gpointer   data)
{
  gchar *str;

  GtkWidget *list = (GtkWidget *) data;
  GList * sellist;
  GFigObj * sel_obj;

  sellist = GTK_LIST (list)->selection; 

  sel_obj = (GFigObj *) gtk_object_get_user_data (GTK_OBJECT (sellist->data));

  if (delete_dialog)
    return;

  str = g_strdup_printf (_("Are you sure you want to delete\n"
			   "\"%s\" from the list and from disk?"),
			 sel_obj->draw_name);

  delete_dialog = gimp_query_boolean_box (_("Delete Gfig Drawing"),
					  gimp_standard_help_func,
					  "filters/gfig.html",
					  FALSE,
					  str,
					  GTK_STOCK_DELETE, GTK_STOCK_CANCEL,
					  NULL, NULL,
					  gfig_do_delete_gfig_callback,
					  data);

  g_free (str);

  gtk_signal_connect (GTK_OBJECT (delete_dialog), "destroy",
		      GTK_SIGNAL_FUNC (gtk_widget_destroyed),
		      &delete_dialog);

  gtk_widget_set_sensitive (GTK_WIDGET (delete_frame_to_freeze), FALSE);
  gtk_widget_show (delete_dialog);
} 

static void
gfig_update_stat_labels (void)
{
  gchar str[45];

  if (current_obj->draw_name)
    sprintf (str, "%.34s", current_obj->draw_name);
  else
    sprintf (str,_("<NONE>"));

  gtk_label_set_text (GTK_LABEL (status_label_dname), str);

  if (current_obj->filename)
    {
      gint slen;
      gchar *hm  = (gchar *) g_get_home_dir ();
      gchar *dfn = g_strdup (current_obj->filename);
#ifdef __EMX__
      if (hm)
	hm = _fnslashify (hm);
#endif

#ifndef __EMX__
      if (hm != NULL && !strncmp (dfn, hm, strlen (hm)-1))
#else
      if (hm != NULL && !strnicmp (dfn, hm, strlen (hm)-1))
#endif
	 {
	   strcpy (dfn, "~");
	   strcat (dfn, &dfn[strlen (hm)]);
	 }
      if ((slen = strlen (dfn)) > 40)
	{
	  strncpy (str, dfn, 19);
	  str[19] = '\0';
	  strcat (str, "...");
	  strncat (str, &dfn[slen - 21], 19);
	  str[40] ='\0';
	}
      else
	sprintf (str, "%.40s", dfn);
      g_free (dfn);
#ifdef __EMX__
      g_free (hm);
#endif
    }
  else
    sprintf (str,_("<NONE>"));

  gtk_label_set_text (GTK_LABEL (status_label_fname), str);

}

static void 
new_obj_2edit (GFigObj *obj)
{
  GFigObj *old_current = current_obj;

  /* Clear undo levels */
  /* redraw the preview */
  /* Set up options as define in the selected object */

  clear_undo ();

  /* Point at this one */
  current_obj = obj;

  /* Show all objects to start with */
  obj_show_single = -1;

  /* Change options */
  update_options (old_current);

  /* If have old object and NOT scaleing currently then force 
   * back to saved coord type.
   */
  gfig_update_stat_labels ();

  /* redraw with new */
  update_draw_area (gfig_preview, NULL);
  /* And preview */
  list_button_update (current_obj);

  if (obj->obj_status & GFIG_READONLY)
    {
      g_message (_("Editing read-only object - "
		   "you will not be able to save it"));
      gtk_widget_set_sensitive (save_button, FALSE);
    }
  else
    {
      gtk_widget_set_sensitive (save_button, TRUE);
    }
}

static void
edit_button_callback (GtkWidget *widget,
		      gpointer   data)
{
  GList     *sellist;
  GFigObj   *sel_obj;
  GtkWidget *list = (GtkWidget *) data;

#ifdef DEBUG
  printf ("Edit button pressed\n");
#endif /* DEBUG */
  /* Must update which object we are editing */
  /* Get the list and which item is selected */
  /* Only allow single selections */

  sellist = GTK_LIST (list)->selection; 

  sel_obj = (GFigObj *) gtk_object_get_user_data (GTK_OBJECT (sellist->data));

  if (sel_obj)
    new_obj_2edit (sel_obj);
  else
    g_warning ("Internal error - list item has null object!");
}

static void
merge_button_callback (GtkWidget *widget,
		       gpointer   data)
{
  GList     *sellist;
  GFigObj   *sel_obj;
  DAllObjs  *obj_copies;
  GtkWidget *list = (GtkWidget *) data;

#ifdef DEBUG
  printf ("Merge button pressed\n");
#endif /* DEBUG */
  /* Must update which object we are editing */
  /* Get the list and which item is selected */
  /* Only allow single selections */

  sellist = GTK_LIST (list)->selection; 

  sel_obj = (GFigObj *) gtk_object_get_user_data (GTK_OBJECT (sellist->data));

  if (sel_obj && sel_obj->obj_list && sel_obj != current_obj)
    {
      /* Copy list tag onto current & redraw */
      obj_copies = copy_all_objs (sel_obj->obj_list);
      prepend_to_all_obj (current_obj, obj_copies);

      /* redraw all */
      update_draw_area (gfig_preview, NULL);
      /* And preview */
      list_button_update (current_obj);
    }
}


static void
gfig_save_menu_callback (GtkWidget *widget,
			 gpointer   data)
{
  GFigObj * real_current = current_obj;
  /* Fiddle the current object and save it */
  /* What happens if we get a redraw here ? */

  current_obj = gfig_obj_for_menu;

  gfig_save ();  /* Save current object */

  current_obj = real_current;
}

static void
gfig_edit_menu_callback (GtkWidget *widget,
			 gpointer   data)
{
  new_obj_2edit (gfig_obj_for_menu);
}

static void
gfig_rename_menu_callback (GtkWidget *widget,
			   gpointer   data)
{
  create_file_selection (gfig_obj_for_menu, gfig_obj_for_menu->filename);
}

static void
gfig_copy_menu_callback (GtkWidget *widget,
			 gpointer   data)
{
  /* Create new entry with name + copy at end & copy object into it */
  gchar *new_name = g_strdup_printf (_("%s copy"), gfig_obj_for_menu->draw_name);
  new_gfig_obj (new_name);
  g_free (new_name);

  /* Copy objs across */
  current_obj->obj_list = copy_all_objs (gfig_obj_for_menu->obj_list);
  current_obj->opts = gfig_obj_for_menu->opts; /* Structure copy */

  /* redraw all */
  update_draw_area (gfig_preview, NULL);
  /* And preview */
  list_button_update (current_obj);
}

static void
gfig_op_menu_create (GtkWidget *window)
{
  GtkWidget *menu_item;
#if 0
  GtkAcceleratorTable *accelerator_table;
#endif /* 0 */

  gfig_op_menu = gtk_menu_new ();

#if 0
  accelerator_table = gtk_accelerator_table_new ();
  gtk_menu_set_accelerator_table (GTK_MENU (gfig_op_menu),
				  accelerator_table);
  gtk_window_add_accelerator_table (GTK_WINDOW (window), accelerator_table);
#endif /* 0 */

  save_menu_item = menu_item = gtk_menu_item_new_with_label (_("Save"));
  gtk_menu_append (GTK_MENU (gfig_op_menu), menu_item);
  gtk_widget_show (menu_item);

  gtk_signal_connect (GTK_OBJECT (menu_item), "activate",
		      (GtkSignalFunc)gfig_save_menu_callback,
		      NULL);

#if 0 
  gtk_widget_install_accelerator (menu_item,
				  accelerator_table,
				  "activate", 'S', 0);
#endif /* 0 */

  menu_item = gtk_menu_item_new_with_label (_("Save as..."));
  gtk_menu_append (GTK_MENU (gfig_op_menu), menu_item);
  gtk_widget_show (menu_item);
  gtk_signal_connect (GTK_OBJECT (menu_item), "activate",
		      (GtkSignalFunc)gfig_rename_menu_callback,
		      NULL);

#if 0 
  gtk_widget_install_accelerator (menu_item,
				  accelerator_table,
				  "activate", 'A', 0);
#endif /* 0 */

  menu_item = gtk_menu_item_new_with_label (_("Copy"));
  gtk_menu_append (GTK_MENU (gfig_op_menu), menu_item);
  gtk_widget_show (menu_item);
  gtk_signal_connect (GTK_OBJECT (menu_item), "activate",
		      (GtkSignalFunc)gfig_copy_menu_callback,
		      NULL);

#if 0 
  gtk_widget_install_accelerator (menu_item,
				  accelerator_table,
				  "activate", 'C', 0);
#endif /* 0 */

  menu_item = gtk_menu_item_new_with_label (_("Edit"));
  gtk_menu_append (GTK_MENU (gfig_op_menu), menu_item);
  gtk_widget_show (menu_item);
  gtk_signal_connect (GTK_OBJECT (menu_item), "activate",
		      (GtkSignalFunc)gfig_edit_menu_callback,
		      NULL);

#if 0 
  gtk_widget_install_accelerator (menu_item,
				  accelerator_table,
				  "activate", 'E', 0);
#endif /* 0 */

}

static void
gfig_op_menu_popup (gint     button,
		    guint32  activate_time,
		    GFigObj *obj)
{
  gfig_obj_for_menu = obj; /* Static data again!*/

  if (obj->obj_status & GFIG_READONLY)
    {
      gtk_widget_set_sensitive (save_menu_item, FALSE);
    }
  else
    {
      gtk_widget_set_sensitive (save_menu_item, TRUE);
    }

  gtk_menu_popup (GTK_MENU (gfig_op_menu),
		  NULL, NULL, NULL, NULL,
		  button, activate_time);
}


static gint
list_button_press (GtkWidget      *widget,
		   GdkEventButton *event,
		   gpointer        data)
{
  switch (event->type)
    {
    case GDK_BUTTON_PRESS:
#ifdef DEBUG
      printf ("Single button press\n");
#endif /* DEBUG */
      if (event->button == 3)
	{
#ifdef DEBUG
	  printf ("Popup on '%s'\n", ((GFigObj *)data)->draw_name);
#endif /* DEBUG */
	  gfig_op_menu_popup (event->button, event->time, (GFigObj *) data);
	  return FALSE;
	}
      list_button_update ((GFigObj *) data);
      break;
    case GDK_2BUTTON_PRESS:
#ifdef DEBUG
      printf ("Two button press\n");
#endif /* DEBUG */
      gfig_dialog_edit_list (widget, data, FALSE);
      break;
    default:
      g_warning ("gfig: unknown event.\n");
      break;
    }

  return FALSE;
}

static void
gfig_scale_update_scale (GtkAdjustment *adjustment,
			 gdouble       *value)
{
  gimp_double_adjustment_update (adjustment, value);

  if (!selvals.scaletoimage)
    {
      scale_x_factor = (1 / (*value)) * org_scale_x_factor;
      scale_y_factor = (1 / (*value)) * org_scale_y_factor;
      update_draw_area (gfig_preview, NULL);
    }
} 

/* Use to toggle the toggles */
static void
gfig_scale2img_update (GtkWidget *widget,
		       gpointer   data)
{
  gimp_toggle_button_update (widget, data);

  if (*((gint *) data))
    {
      GtkObject *adj;

      adj = gtk_object_get_user_data (GTK_OBJECT (widget));

      scale_x_factor = org_scale_x_factor;
      scale_y_factor = org_scale_y_factor;
      gtk_adjustment_set_value (GTK_ADJUSTMENT (adj), 1.0);

      update_draw_area (gfig_preview, NULL);
    }
}

/* Given a row then srink it down a bit */
static void
do_gfig_preview (guchar *dest_row, 
		 guchar *src_row,
		 gint    width,
		 gint    dh,
		 gint    height,
		 gint    bpp)
{
  memcpy (dest_row, src_row, width*bpp);
}

static void
dialog_update_preview (void)
{
  gint y;
  gint check, check_0, check_1;  

  if (!selvals.showimage)
    {
      memset (preview_row, -1, preview_width*4);      
      for (y = 0; y < preview_height; y++)
	{
	  gtk_preview_draw_row (GTK_PREVIEW (gfig_preview), preview_row,
				0, y, preview_width);
	}
      return;
    }

  if (!pv_cache)
    {
      refill_cache ();
    }

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

      do_gfig_preview (preview_row,
		       pv_cache+y*preview_width*img_bpp,
		       preview_width,
		       y,
		       preview_height,
		       img_bpp);

      if (img_bpp > 3)
	{
	  int i, j;
	  for (i = 0, j = 0 ; i < sizeof (preview_row); i += 4, j += 3)
	    {
	      gint alphaval;
	      if (((i/4) / GIMP_CHECK_SIZE) & 1)
		check = check_0;
	      else
		check = check_1;
	    
	      alphaval = preview_row[i + 3];

	      preview_row[j] = 
		check + (((preview_row[i] - check)*alphaval)/255);
	      preview_row[j + 1] = 
		check + (((preview_row[i + 1] - check)*alphaval)/255);
	      preview_row[j + 2] = 
		check + (((preview_row[i + 2] - check)*alphaval)/255);
	    }
	}

      gtk_preview_draw_row (GTK_PREVIEW (gfig_preview), preview_row,
			    0, y, preview_width);
    }
}

static void
gfig_new_gc (void)
{
 GdkColor fg, bg;

 /*  create a new graphics context  */
 gfig_gc = gdk_gc_new (gfig_preview->window);
 gdk_gc_set_function (gfig_gc, GDK_INVERT);
 fg.pixel = 0xFFFFFFFF;
 bg.pixel = 0x00000000;
 gdk_gc_set_foreground (gfig_gc, &fg);
 gdk_gc_set_background (gfig_gc, &bg);
 gdk_gc_set_line_attributes (gfig_gc, 1, GDK_LINE_SOLID, GDK_CAP_BUTT, GDK_JOIN_MITER);
}

static gint 
get_num_radials (void)
{
  gint gridsp = MAX_GRID + MIN_GRID;
  /* select number of radials to draw */
  /* Either have 16 32 or 48 */
  /* correspond to GRID_MAX, midway and GRID_MIN */

  return (gridsp - selvals.opts.gridspacing);
}

#define SQ_SIZE 8

static gint 
inside_sqr (GdkPoint *cpnt,
	    GdkPoint *testpnt)
{
  /* Return TRUE if testpnt is near cpnt */
  gint16 x = cpnt->x;
  gint16 y = cpnt->y;
  gint16 tx = testpnt->x;
  gint16 ty = testpnt->y;

#ifdef DEBUG
  printf ("Testing if (%x,%x) is near (%x,%x)\n", tx, ty, x, y);
#endif /* DEBUG */
  return (abs (x - tx) <= SQ_SIZE && abs (y - ty) < SQ_SIZE);
}

/* find_grid_pos - Given an x, y point return the grid position of it */
/* return the new position in the passed point */

static void
find_grid_pos (GdkPoint *p,
	       GdkPoint *gp,
	       guint     is_butt3)
{
  gint16 x = p->x;
  gint16 y = p->y;
  static GdkPoint cons_pnt;
  static gdouble cons_radius;
  static gdouble cons_ang;
  static gboolean cons_center;
  
  if (selvals.opts.gridtype == RECT_GRID)
    {
      if (p->x % selvals.opts.gridspacing > selvals.opts.gridspacing/2)
	x += selvals.opts.gridspacing;
      
      if (p->y % selvals.opts.gridspacing > selvals.opts.gridspacing/2)
	y += selvals.opts.gridspacing;
      
      gp->x = (x/selvals.opts.gridspacing)*selvals.opts.gridspacing;
      gp->y = (y/selvals.opts.gridspacing)*selvals.opts.gridspacing;

      if (is_butt3)
	{
	  if (abs (gp->x - cons_pnt.x) < abs (gp->y - cons_pnt.y))
	    gp->x = cons_pnt.x;
	  else
	    gp->y = cons_pnt.y;
	}
      else
	{
	  /* Store the point since might be used later */
	  cons_pnt = *gp; /* Structure copy */
	}
    }
  else if (selvals.opts.gridtype == POLAR_GRID)
    { 
      gdouble ang_grid;
      gdouble ang_radius;
      gdouble real_radius;
      gdouble real_angle;
      gdouble rounded_angle;
      gint rounded_radius;
      gint16 shift_x = x - preview_width/2;
      gint16 shift_y = -y + preview_height/2;

      real_radius = ang_radius = sqrt ((shift_y*shift_y) + (shift_x*shift_x));

      /* round radius */
      rounded_radius = (gint) (RINT (ang_radius/selvals.opts.gridspacing))*selvals.opts.gridspacing;
      if (rounded_radius <= 0 || real_radius <=0)
	{
	  /* DEAD CENTER */
	  gp->x = preview_width/2;
	  gp->y = preview_height/2;
	  if (!is_butt3) cons_center = TRUE;
#ifdef DEBUG
	  printf ("Dead center\n");
#endif /* DEBUG */
	  return;
	}

      ang_grid = 2*G_PI/get_num_radials ();

      real_angle = atan2 (shift_y, shift_x);
      if (real_angle < 0)
	real_angle += 2*G_PI;

      rounded_angle = (RINT ((real_angle/ang_grid)))*ang_grid;
#ifdef DEBUG
      printf ("real_ang = %f ang_gid = %f rounded_angle = %f rounded radius = %d\n",
	      real_angle, ang_grid, rounded_angle, rounded_radius);

      printf ("preview_width = %d preview_height = %d\n", preview_width, preview_height);
#endif /* DEBUG */
      gp->x = (gint)RINT ((rounded_radius*cos (rounded_angle))) + preview_width/2;
      gp->y = -(gint)RINT ((rounded_radius*sin (rounded_angle))) + preview_height/2;

      if (is_butt3)
	{
	  if (!cons_center)
	    {
	      if (fabs (rounded_angle - cons_ang) > ang_grid/2)
		{
		  gp->x = (gint)RINT ((cons_radius*cos (rounded_angle))) + preview_width/2;
		  gp->y = -(gint)RINT ((cons_radius*sin (rounded_angle))) + preview_height/2;
		}
	      else
		{
		  gp->x = (gint)RINT ((rounded_radius*cos (cons_ang))) + preview_width/2;
		  gp->y = -(gint)RINT ((rounded_radius*sin (cons_ang))) + preview_height/2;
		}
	    }
	}
      else
	{
	  cons_radius = rounded_radius;
	  cons_ang = rounded_angle;
	  cons_center = FALSE;
	}
    }
   else if (selvals.opts.gridtype == ISO_GRID)
     {
	if (is_butt3)
	  {
	     static GdkPoint b_pnt;
	     static GdkPoint i_pnt;
	     static GdkPoint ii_pnt;
	     gint d;
	     gint dd;

	     b_pnt.x = cons_pnt.x;
	     b_pnt.y = cons_pnt.y + preview_width;
	     d = calculate_point_to_line_distance (p, &cons_pnt, &b_pnt, &i_pnt);

	     b_pnt.x = cons_pnt.x;
	     b_pnt.y = cons_pnt.y - preview_width;
	     dd = calculate_point_to_line_distance (p, &cons_pnt, &b_pnt, &ii_pnt);
	     if (dd < d)
	     {
		i_pnt.x = ii_pnt.x;
		i_pnt.y = ii_pnt.y;
		d = dd;
	     }

	     b_pnt.x = cons_pnt.x + preview_width;
	     b_pnt.y = cons_pnt.y + preview_width/2;
	     dd = calculate_point_to_line_distance (p, &cons_pnt, &b_pnt, &ii_pnt);
	     if (dd < d)
	     {
		i_pnt.x = ii_pnt.x;
		i_pnt.y = ii_pnt.y;
		d = dd;
	     }

	     b_pnt.x = cons_pnt.x + preview_width;
	     b_pnt.y = cons_pnt.y - preview_width/2;
	     dd = calculate_point_to_line_distance (p, &cons_pnt, &b_pnt, &ii_pnt);
	     if (dd < d)
	     {
		i_pnt.x = ii_pnt.x;
		i_pnt.y = ii_pnt.y;
		d = dd;
	     }

	     b_pnt.x = cons_pnt.x - preview_width;
	     b_pnt.y = cons_pnt.y + preview_width/2;
	     dd = calculate_point_to_line_distance (p, &cons_pnt, &b_pnt, &ii_pnt);
	     if (dd < d)
	     {
		i_pnt.x = ii_pnt.x;
		i_pnt.y = ii_pnt.y;
		d = dd;
	     }

	     b_pnt.x = cons_pnt.x - preview_width;
	     b_pnt.y = cons_pnt.y - preview_width/2;
	     dd = calculate_point_to_line_distance (p, &cons_pnt, &b_pnt, &ii_pnt);
	     if (dd < d)
	     {
		i_pnt.x = ii_pnt.x;
		i_pnt.y = ii_pnt.y;
		d = dd;
	     }

	     x = i_pnt.x;
	     y = i_pnt.y;
	  }

	if (x % selvals.opts.gridspacing > selvals.opts.gridspacing/2)
	  x += selvals.opts.gridspacing;

	gp->x = (x/selvals.opts.gridspacing)*selvals.opts.gridspacing;

	if (((gp->x/selvals.opts.gridspacing) % 2) != 0)
	  {
	     y -= selvals.opts.gridspacing/2;

	     if (y % selvals.opts.gridspacing > selvals.opts.gridspacing/2)
	       y += selvals.opts.gridspacing;
	
	     gp->y = (selvals.opts.gridspacing/2) + ((y/selvals.opts.gridspacing)*selvals.opts.gridspacing);
	  }
	else
	  {
	     if (y % selvals.opts.gridspacing > selvals.opts.gridspacing/2)
	       y += selvals.opts.gridspacing;
	
	     gp->y = (y/selvals.opts.gridspacing)*selvals.opts.gridspacing;
	  }

	if (!is_butt3)
	  {
	     /* Store the point since it might be used later */
	     cons_pnt = *gp; /* Structure copy */
	  }
     }
}

/* Calculate distance from a point to a line
 * Taken from the newsgroup comp.graphics.algorithms FAQ. */
static gint
calculate_point_to_line_distance (GdkPoint *p,
				  GdkPoint *A,
				  GdkPoint *B,
				  GdkPoint *I)
{
   gint L2;
   gint L;

   L2 = ((B->x - A->x)*(B->x - A->x)) + ((B->y - A->y)*(B->y - A->y));
   L = (gint) sqrt (L2);

   /* gint r; */
   /* gint s; */
   /* r = ((A->y - p->y)*(A->y - B->y) - (A->x - p->x)*(B->x - A->x))/L2; */
   /* s = ((A->y - p->y)*(B->x - A->x) - (A->x - p->x)*(B->y - A->y))/L2; */

   /* Let I be the point of perpendicular projection of C onto AB. */

   I->x = A->x + (((A->y - p->y)*(A->y - B->y) - (A->x - p->x)*(B->x - A->x))*(B->x - A->x))/L2;
   I->y = A->y + (((A->y - p->y)*(A->y - B->y) - (A->x - p->x)*(B->x - A->x))*(B->y - A->y))/L2;

   return abs ((((A->y - p->y)*(B->x - A->x)) - ((A->x - p->x)*(B->y - A->y)))*L);
}

/* Given a point x, y draw a circle */
static void
draw_circle (GdkPoint *p)
{
  if (!selvals.opts.showcontrol || drawing_pic)
    return;

  gdk_draw_arc (gfig_preview->window,
		gfig_gc,
		0,
		p->x - SQ_SIZE/2,
		p->y - SQ_SIZE/2,
		SQ_SIZE,
		SQ_SIZE,
		0,
		360*64);
}


/* Given a point x, y draw a square around it */
static void
draw_sqr (GdkPoint *p)
{
  if (!selvals.opts.showcontrol || drawing_pic)
    return;

  gdk_draw_rectangle (gfig_preview->window,
		      gfig_gc,
		      0,
		      gfig_scale_x ((gint)p->x) - SQ_SIZE/2,
		      gfig_scale_y ((gint)p->y) - SQ_SIZE/2,
		      (gint)SQ_SIZE,
		      (gint)SQ_SIZE);
}

/* Draw the grid on the screen
 */

static void
draw_grid_clear (GtkWidget *widget,
		 gpointer   data)
{
  /* wipe slate and start again */
  dialog_update_preview ();
  draw_grid (widget, data);
  draw_objects (current_obj->obj_list, TRUE);
  gtk_widget_draw (gfig_preview, NULL);
  gdk_flush ();
}

static void
toggle_tooltips (GtkWidget *widget,
		 gpointer   data)
{
  gimp_toggle_button_update (widget, data);

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
    gimp_help_enable_tooltips ();
  else
    gimp_help_disable_tooltips ();
}

static void
toggle_show_image (GtkWidget *widget,
		   gpointer   data)
{
  /* wipe slate and start again */
  draw_grid_clear (widget, data);
}

static void
toggle_obj_type (GtkWidget *widget,
		 gpointer   data)
{
  GdkCursorType ctype = GDK_LAST_CURSOR;
  static GdkCursor* p_cursors[DEL_OBJ + 1];

  if (selvals.otype != (DobjType) data)
    {
      /* Mem leak */
      obj_creating = NULL;
      tmp_line = NULL;
      tmp_bezier = NULL;

      if ((DobjType)data < MOVE_OBJ)
	{
	  obj_show_single = -1; /* Cancel select preview */
	}
      /* Update draw areas */
      update_draw_area (gfig_preview, NULL);
      /* And preview */
      list_button_update (current_obj);
    }

  selvals.otype = (DobjType) data;

  switch (selvals.otype)
    {
    case LINE:
    case CIRCLE:
    case ELLIPSE:
    case ARC:
    case POLY:
    case STAR:
    case SPIRAL:
    case BEZIER:
    default:
      ctype = GDK_CROSSHAIR;
      break;
    case MOVE_OBJ:
    case MOVE_POINT:
    case COPY_OBJ:
    case MOVE_COPY_OBJ:
      ctype = GDK_DIAMOND_CROSS;
      break;
    case DEL_OBJ:
      ctype = GDK_PIRATE;
      break;
    }

  if (!p_cursors[selvals.otype])
    p_cursors[selvals.otype] = gdk_cursor_new (ctype);

  gdk_window_set_cursor (gfig_preview->window, p_cursors[selvals.otype]);
}

static void
draw_grid_polar (GdkGC *drawgc)
{
  gint step;
  gint loop;
  gint radius;
  gint max_rad;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble ang_radius;
  /* Pick center and draw concentric circles */

  gint grid_x_center = preview_width/2;
  gint grid_y_center = preview_height/2;

  step = selvals.opts.gridspacing;
  max_rad = sqrt (preview_width * preview_width +
		  preview_height * preview_height) / 2;

  for (loop = 0; loop < max_rad; loop += step)
    {
      radius = loop;

      gdk_draw_arc (gfig_preview->window,
		    drawgc,
		    0,
		    grid_x_center - radius,
		    grid_y_center - radius,
		    radius*2,
		    radius*2,
		    0,
		    360 * 64);
    }

  /* Lines */
  ang_grid = 2 * G_PI / get_num_radials ();
  ang_radius = sqrt ((preview_width * preview_width) +
		     (preview_height * preview_height)) / 2;

  for (loop = 0; loop <= get_num_radials (); loop++)
    {
      gint lx, ly;

      ang_loop = loop * ang_grid;
	
      lx = (gint) RINT (ang_radius * cos (ang_loop));
      ly = (gint) RINT (ang_radius * sin (ang_loop));

      gdk_draw_line (gfig_preview->window,
		     drawgc,
		     (gint) lx + (preview_width) / 2,
		     -(gint) ly + (preview_height) / 2,
		     (gint) (preview_width) / 2,
		     (gint) (preview_height) / 2);
    }
}

static void
draw_grid_sq (GdkGC *drawgc)
{
  gint step;
  gint loop;

  /* Draw the horizontal lines */
  step = selvals.opts.gridspacing;

  for (loop = 0 ; loop < preview_height ; loop += step)
    {
      gdk_draw_line (gfig_preview->window,
		     drawgc,
		     (gint)0,
		     (gint)loop,
		     (gint)preview_width,
		     (gint)loop);
    }

  /* Draw the vertical lines */

  for (loop = 0 ; loop < preview_width ; loop += step)
    {
      gdk_draw_line (gfig_preview->window,
		     drawgc,
		     (gint)loop,
		     (gint)0,
		     (gint)loop,
		     (gint)preview_height);
    }
}

static void
draw_grid_iso (GdkGC *drawgc)
{
   gint step;
   gint loop;

   gint diagonal_start;
   gint diagonal_end;
   gint diagonal_width;
   gint diagonal_height;
   
   step = selvals.opts.gridspacing;
   
   /* Draw the vertical lines */
   for (loop = 0 ; loop < preview_width ; loop += step)
     {
       gdk_draw_line (gfig_preview->window,
		      drawgc,
		      (gint)loop,
		      (gint)0,
		      (gint)loop,
		      (gint)preview_height);
     }

   diagonal_start = preview_width/2;
   diagonal_start = diagonal_start - (diagonal_start % step);
   diagonal_start = -diagonal_start;
   
   diagonal_end = preview_height + (preview_width/2);
   diagonal_end = diagonal_end - (diagonal_end % step);
   
   diagonal_width = preview_width;
   diagonal_height = diagonal_width/2;
   
   /* Draw diagonal lines */
   for (loop = diagonal_start ; loop < diagonal_end ; loop += step)
     {
       gdk_draw_line (gfig_preview->window,
		      drawgc,
		      (gint)0,
		      (gint)loop,
		      (gint)diagonal_width,
		      (gint)loop + diagonal_height);

       gdk_draw_line (gfig_preview->window,
		      drawgc,
		      (gint)0,
		      (gint)loop,
		      (gint)diagonal_width,
		      (gint)loop - diagonal_height);
     }
}

static GdkGC *
gfig_get_grid_gc (GtkWidget *w, gint gctype)
{
  switch (gctype)
    {
    case GFIG_BLACK_GC:
      return (w->style->black_gc);
    case GFIG_WHITE_GC:
      return (w->style->white_gc);
    case GFIG_GREY_GC:
      return (grid_hightlight_drawgc);
    case GTK_STATE_NORMAL:
      return (w->style->bg_gc[GTK_STATE_NORMAL]);
    case GTK_STATE_ACTIVE:
      return (w->style->bg_gc[GTK_STATE_ACTIVE]);
    case GTK_STATE_PRELIGHT:
      return (w->style->bg_gc[GTK_STATE_PRELIGHT]);
    case GTK_STATE_SELECTED:
      return (w->style->bg_gc[GTK_STATE_SELECTED]);
    case GTK_STATE_INSENSITIVE:
      return (w->style->bg_gc[GTK_STATE_INSENSITIVE]);
    default:
      g_warning ("Unknown type for grid colouring\n");
      return (w->style->bg_gc[GTK_STATE_PRELIGHT]);
    }
}

static void
draw_grid (GtkWidget *widget,
	  gpointer   data)
{
  GdkGC *drawgc;
  /* Get the size of the preview and calc where the lines go */
  /* Draw in prelight to start with... */
  /* Always start in the upper left corner for rect.
   */

  if ((preview_width < selvals.opts.gridspacing &&
       preview_height < selvals.opts.gridspacing) ||
      drawing_pic)
    {
      /* Don't draw if they don't fit */
      return;
    }

  if (selvals.opts.drawgrid)
    drawgc = gfig_get_grid_gc (gfig_preview, grid_gc_type);
  else
    return;

  if (selvals.opts.gridtype == RECT_GRID)
    draw_grid_sq (drawgc);
  else if (selvals.opts.gridtype == POLAR_GRID)
    draw_grid_polar (drawgc);
  else if (selvals.opts.gridtype == ISO_GRID)
    draw_grid_iso (drawgc);
}

static void
do_gfig (void)
{
  /* Not sure if requre post proc - leave stub in */
}

/* This could belong in a separate file ... but makes it easier to lump into
 * one when compiling the plugin.
 */

/* Stuff for the generation/deletion of objects. */

/* Objects are easy one they are created - you just go down the object 
 * list calling the draw function for each object but... when they 
 * are been created we have to be a little more careful. When 
 * the first point is placed on the canvas we create the object, 
 * the mouse position then defines the next point that can move around.
 * careful how we draw this position.
 */

static void
free_one_obj (Dobject *obj)
{
  d_delete_dobjpoints (obj->points);
  g_free (obj);
}

static void
free_all_objs (DAllObjs * objs)
{
  /* Free all objects */
  DAllObjs * next;
  
  while (objs)
    {
      free_one_obj (objs->obj);
      next = objs->next;
      g_free (objs);
      objs = next;
    }
}

static gchar *
get_line (gchar *buf,
	  gint   s,
	  FILE  *from,
	  gint   init)
{
  gint slen;
  char * ret;

  if (init)
    line_no = 1;
  else
    line_no++;

  do
    {
      ret = fgets (buf, s, from);
    } while (!ferror (from) && buf[0] == '#');

  slen = strlen (buf);

  /* The last newline is a pain */
  if (slen > 0)
    buf[slen - 1] = '\0';
  
  if (ferror (from))
    {
      g_warning (_("Error reading file"));
      return (0);
    }

#ifdef DEBUG
  printf ("Processing line '%s'\n", buf);
#endif /* DEBUG */

  return (ret);
}

static void
gfig_clear_callback (GtkWidget *widget,
		     gpointer   data)
{
  /* Make sure we can get back - if we have some objects to get back to */
  if (!current_obj->obj_list)
    return;

  setup_undo ();
  /* Free all objects */
  free_all_objs (current_obj->obj_list);
  current_obj->obj_list = NULL;
  obj_creating = NULL;
  tmp_line = NULL;
  tmp_bezier = NULL;
  update_draw_area (gfig_preview, NULL);
  /* And preview */
  list_button_update (current_obj);
}


static void
gfig_undo_callback (GtkWidget *widget,
		    gpointer   data)
{
  if (undo_water_mark >= 0)
    {
      /* Free current objects an reinstate previous */
      free_all_objs (current_obj->obj_list);
      current_obj->obj_list = NULL;
      tmp_bezier = tmp_line = obj_creating = NULL;
      current_obj->obj_list = undo_table[undo_water_mark];
      undo_water_mark--;
      /* Update the screen */
      update_draw_area (gfig_preview, NULL);
      /* And preview */
      list_button_update (current_obj);
      gfig_obj_modified (current_obj, GFIG_MODIFIED);
      current_obj->obj_status |= GFIG_MODIFIED;
  }

  if (undo_water_mark < 0)
    gtk_widget_set_sensitive (widget, FALSE);
}

static void
clear_undo (void)
{
  int lv;

  for (lv = undo_water_mark; lv >= 0; lv--) 
    {
      if (undo_table[lv])
	free_all_objs (undo_table[lv]);
      undo_table[lv] = NULL;
    }

  undo_water_mark = -1;
  gtk_widget_set_sensitive (undo_widget, FALSE);
}

static void
setup_undo (void)
{
  /* Copy object list to undo buffer */
#if DEBUG
  printf ("setup undo level [%d]\n", undo_water_mark);
#endif /*DEBUG*/  

  if (!current_obj)
    {
      /* If no current_obj must be loading -> no undo */
      return;
    }

  if (undo_water_mark >= selvals.maxundo - 1)
    {
      int loop;
      /* the little one in the bed said "roll over".. */
      if (undo_table[0])
	free_one_obj (undo_table[0]->obj);
      for (loop = 0; loop < undo_water_mark; loop++)
	{
	  undo_table[loop] = undo_table[loop + 1];
	}
    }
  else
    {
      undo_water_mark++;
    }
  undo_table[undo_water_mark] = copy_all_objs (current_obj->obj_list);
  gtk_widget_set_sensitive (undo_widget, TRUE);

  gfig_obj_modified (current_obj, GFIG_MODIFIED);
  current_obj->obj_status |= GFIG_MODIFIED;
}

/* Given a number of float co-ords adjust for scaling back to org size */
/* Size is number of PAIRS of points */
/* FP + int varients */

static void
scale_to_orginal_x (gdouble *list)
{
  *list *= scale_x_factor;
}

static gint
gfig_scale_x (gint x)
{
  if (!selvals.scaletoimage)
    return (gint) (x * (1 / scale_x_factor));
  else
    return x;
}

static gint
gfig_invscale_x (gint x)
{
  if (!selvals.scaletoimage)
    return (gint) (x * (scale_x_factor));
  else
    return x;
}

static void
scale_to_orginal_y (gdouble *list)
{
  *list *= scale_y_factor;
}

static gint
gfig_scale_y (gint y)
{
  if (!selvals.scaletoimage)
    return (gint) (y * (1 / scale_y_factor));
  else
    return y;
}

static gint
gfig_invscale_y (gint y)
{
  if (!selvals.scaletoimage)
    return (gint) (y*(scale_y_factor));
  else
    return y;
}

/* Pairs x followed by y */
static void
scale_to_original_xy (gdouble *list,
		      gint     size)
{
  gint i;

  for (i = 0; i < size * 2; i += 2)
    {
      scale_to_orginal_x (&list[i]);
      scale_to_orginal_y (&list[i + 1]);
    }
}

/* Pairs x followed by y */
static void
scale_to_xy (gdouble *list,
	     gint     size)
{
  gint i;

  for (i = 0; i < size * 2; i += 2)
    {
      list[i] *= (org_scale_x_factor / scale_x_factor);
      list[i + 1] *= (org_scale_y_factor / scale_y_factor);
    }
}

/* Given an list of PAIRS of doubles reverse the list */
/* Size is number of pairs to swap */
static void
reverse_pairs_list (gdouble *list,
		    gint     size)
{
  gint i;

  struct cs
  { 
    gdouble i1; 
    gdouble i2;
  } copyit, *orglist;

  orglist = (struct cs *) list;

  /* Uses struct copies */
  for (i = 0; i < size / 2; i++)
    {
      copyit = orglist[i];
      orglist[i] = orglist[size - 1 - i];
      orglist[size - 1 - i] = copyit;
    }
}

/* Delete a list of points */
static void
d_delete_dobjpoints (DobjPoints * pnts)
{
  DobjPoints * next;
  DobjPoints * pnt2del = pnts;

  while (pnt2del)
    {
      next = pnt2del->next;
      g_free (pnt2del);
      pnt2del = next;
    }
}

static DobjPoints *
d_copy_dobjpoints (DobjPoints * pnts)
{
  DobjPoints *ret = NULL;
  DobjPoints *head = NULL;
  DobjPoints *newpnt;
  DobjPoints *pnt2copy = pnts;

  while (pnt2copy)
    {
      newpnt = g_new0 (DobjPoints, 1);
      newpnt->pnt.x = pnt2copy->pnt.x;
      newpnt->pnt.y = pnt2copy->pnt.y;

      if (!ret)
	head = ret = newpnt;
      else
	{
	  head->next = newpnt;
	  head = newpnt;
	}
      pnt2copy = pnt2copy->next;
    }

  return ret;
}

static gint
scan_obj_points (DobjPoints *opnt,
		 GdkPoint   *pnt)
{
  while (opnt)
    {
      if (inside_sqr (&opnt->pnt, pnt))
	{
	  opnt->found_me = TRUE;
	  return TRUE;
	}
      opnt->found_me = FALSE;
      opnt = opnt->next;
    }
  return FALSE;
}

static Dobject *
get_nearest_objs (GFigObj  *obj,
		  GdkPoint *pnt)
{
  /* Nearest object to given point or NULL */
  DAllObjs *all;
  Dobject  *test_obj;
  gint count = 0;

  if (!obj)
    return NULL;

  all = obj->obj_list;

  while (all)
    {
      test_obj = all->obj;

      if (count == obj_show_single || obj_show_single == -1)
	if (scan_obj_points (test_obj->points, pnt))
	  {
	    return test_obj;
	  }
      all = all->next;
      count++;
    }
  return NULL;
}

static void
scale_obj_points (DobjPoints *opnt,
		  gdouble     scale_x,
		  gdouble     scale_y)
{
  while (opnt)
    {
      opnt->pnt.x = (gint) (opnt->pnt.x * scale_x);
      opnt->pnt.y = (gint) (opnt->pnt.y * scale_y);
      opnt = opnt->next;
    }
}

static void
remove_obj_from_list (GFigObj *obj,
		      Dobject *del_obj)
{
  /* Nearest object to given point or NULL */
  DAllObjs *all;
  DAllObjs *prev_all = NULL;
  
  g_assert (del_obj != NULL);

  all = obj->obj_list;

  while (all)
    {
      if (all->obj == del_obj)
	{
	  /* Found the one to delete */
#ifdef DEBUG
	  printf ("Found the one to delete\n");
#endif /* DEBUG */

	  if (prev_all)
	    prev_all->next = all->next;
	  else
	    obj->obj_list = all->next;

	  /* Draw obj (which will actually undraw it! */
	  del_obj->drawfunc (del_obj);

	  free_one_obj (del_obj);
	  g_free (all);

	  if (obj_show_single != -1)
	    {
	      /* We've just deleted the only visible one */
	      draw_grid_clear (NULL, NULL); /*Args not used */
	      obj_show_single = -1; /* Show all again */
	    }
	  return;
	}
      prev_all = all;
      all = all->next;
    }
  g_warning (_("Hey where has the object gone ?"));
}

static DobjPoints *
get_diffs (Dobject  *obj,
	   gint16   *xdiff,
	   gint16   *ydiff,
	   GdkPoint *to_pnt)
{
  DobjPoints *spnt;

  g_assert (obj != NULL);

  spnt = obj->points;
  
  if (!spnt)
    return (NULL); /* no-line */
  
  /* Slow slow slowwwwww....*/
  while (spnt)
    {
      if (spnt->found_me)
	{
	  *xdiff = spnt->pnt.x - to_pnt->x;
	  *ydiff = spnt->pnt.y - to_pnt->y;
	  return (spnt);
	}
      spnt = spnt->next;
    }
  return (NULL);
}

static void
update_pnts (Dobject *obj,
	     gint16   xdiff,
	     gint16   ydiff)
{
  DobjPoints *spnt;

  g_assert (obj != NULL);

  /* Update all pnts */
  spnt = obj->points;

  if (!spnt)
    return; /* no-line */
  
  /* Go around all the points drawing a line from one to the next */
  while (spnt)
    {
      spnt->pnt.x = spnt->pnt.x - xdiff;
      spnt->pnt.y = spnt->pnt.y - ydiff;
      spnt = spnt->next;
    }
}


static void
do_move_all_obj (GdkPoint *to_pnt)
{
  /* Move all objects in one go */
  /* Undraw/then draw in new pos */
  DAllObjs *all;
  Dobject *obj;
  gint16 xdiff = 0;
  gint16 ydiff = 0;
  
  xdiff = move_all_pnt->x - to_pnt->x;
  ydiff = move_all_pnt->y - to_pnt->y;
  
  if (!xdiff && !ydiff)
    return;
  
  all = current_obj->obj_list;

  while (all)
    {
      obj = all->obj;

      /* undraw ! */
      draw_one_obj (obj);
      
      update_pnts (obj, xdiff, ydiff);
      
      /* Draw in new pos */
      draw_one_obj (obj);

      all = all->next;
    }

  *move_all_pnt = *to_pnt; /* Structure copy */
}


static void
do_move_obj (Dobject  *obj,
	     GdkPoint *to_pnt)
{
  /* Move the whole line - undraw the line to start with */
  /* Then draw in new pos */
  gint16 xdiff = 0;
  gint16 ydiff = 0;
  
  get_diffs (obj, &xdiff, &ydiff, to_pnt);
  
  if (!xdiff && !ydiff)
    return;
  
  /* undraw ! */
  draw_one_obj (obj);
  
  update_pnts (obj, xdiff, ydiff);
  
  /* Draw in new pos */
  draw_one_obj (obj);
  
}

static void
do_move_obj_pnt (Dobject  *obj,
		 GdkPoint *to_pnt)
{
  /* Move the whole line - undraw the line to start with */
  /* Then draw in new pos */
  DobjPoints *spnt;
  gint16 xdiff = 0;
  gint16 ydiff = 0;
  
  spnt = get_diffs (obj, &xdiff, &ydiff, to_pnt);
  
  if ((!xdiff && !ydiff) || !spnt)
    return;
  
  /* undraw ! */
  draw_one_obj (obj);

  spnt->pnt.x = spnt->pnt.x - xdiff;
  spnt->pnt.y = spnt->pnt.y - ydiff;
  
  /* Draw in new pos */
  draw_one_obj (obj);
}

/* Save a line away to the specified stream */

static void
d_save_line (Dobject *obj,
	     FILE    *to)
{
  DobjPoints * spnt;

  spnt = obj->points;

  if (!spnt)
    return; /* End-of-line */

  fprintf (to, "<LINE>\n");

  while (spnt)
    {
      fprintf (to, "%d %d\n",
	      (gint)spnt->pnt.x,
	      (gint)spnt->pnt.y);
      spnt = spnt->next;
    }
  
  fprintf (to, "</LINE>\n");
}

/* Load a line from the specified stream */

static Dobject *
d_load_line (FILE *from)
{
  Dobject *new_obj = NULL;
  gint xpnt;
  gint ypnt;
  gchar buf[MAX_LOAD_LINE];

#ifdef DEBUG
  printf ("Load line called\n");
#endif /* DEBUG */

  while (get_line (buf, MAX_LOAD_LINE, from, 0))
    {
      if (sscanf (buf, "%d %d", &xpnt, &ypnt) != 2)
	{
	  /* Must be the end */
	  if (strcmp ("</LINE>", buf))
	    {
	      g_warning ("[%d] Internal load error while loading line",
			line_no);
	      return (NULL);
	    }
	  return (new_obj);
	}

      if (!new_obj)
	new_obj = d_new_line (xpnt, ypnt);
      else
	d_pnt_add_line (new_obj, xpnt, ypnt, -1);
    }

  return (new_obj);
}

static Dobject *
d_copy_line (Dobject *obj)
{
  Dobject *nl;

  if (!obj)
    return (NULL);

  g_assert (obj->type == LINE);

  nl = d_new_line (obj->points->pnt.x, obj->points->pnt.y);
  
  nl->points->next = d_copy_dobjpoints (obj->points->next);

  return (nl);
}

/* Draw the given line -- */
static void
d_draw_line (Dobject *obj)
{
  DobjPoints *spnt;
  DobjPoints *epnt;

  spnt = obj->points;

  if (!spnt)
    return; /* End-of-line */

  epnt = spnt->next;

  while (spnt && epnt)
    {
#if DEBUG
      printf ("Drawing line 0x%x (%x,%x) -> (%x,%x)\n", spnt,
	      (gint)spnt->pnt.x,
	      (gint)spnt->pnt.y,
	      (gint)epnt->pnt.x,
	      (gint)epnt->pnt.y);
#endif /* DEBUG */

      draw_sqr (&spnt->pnt);
      /* Go around all the points drawing a line from one to the next */
      if (drawing_pic)
	{
	  gdk_draw_line (pic_preview->window,
			 pic_preview->style->black_gc,
			 adjust_pic_coords ((gint)spnt->pnt.x, preview_width),
			 adjust_pic_coords ((gint)spnt->pnt.y, preview_height),
			 adjust_pic_coords ((gint)epnt->pnt.x, preview_width),
			 adjust_pic_coords ((gint)epnt->pnt.y, preview_height));
	}
      else
	{
	  gdk_draw_line (gfig_preview->window,
			 gfig_gc,
			 gfig_scale_x ((gint)spnt->pnt.x),
			 gfig_scale_y ((gint)spnt->pnt.y),
			 gfig_scale_x ((gint)epnt->pnt.x),
			 gfig_scale_y ((gint)epnt->pnt.y));
	}
      spnt = epnt;
      epnt = epnt->next;
    }
  draw_sqr (&spnt->pnt);
}

static void 
d_paint_line (Dobject *obj)
{
  DobjPoints * spnt;
  gdouble *line_pnts;
  gint seg_count = 0;
  gint i = 0;

  spnt = obj->points;

  /* count */

  while (spnt)
    {
      seg_count++;
      spnt = spnt->next;
    }

  spnt = obj->points;

  if (!spnt || !seg_count)
    return; /* no-line */

  line_pnts = g_new0 (gdouble, 2 * seg_count + 1);
  
  /* Go around all the points drawing a line from one to the next */
  while (spnt)
    {
      line_pnts[i++] = spnt->pnt.x;
      line_pnts[i++] = spnt->pnt.y;
      spnt = spnt->next;
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
		  seg_count * 2, line_pnts);
    }
  else 
    {
      gimp_free_select (gfig_image,
			seg_count * 2, line_pnts,
			selopt.type,
			selopt.antia,
			selopt.feather,
			selopt.feather_radius);
    }

  g_free (line_pnts);
}


/* Create a new line object. starting at the x, y point might add styles 
 * later.
 */

static Dobject *
d_new_line (gint x,
	    gint y)
{
  Dobject    *nobj;
  DobjPoints *npnt;
 
  /* Get new object and starting point */

  /* Start point */
  npnt = g_new0 (DobjPoints, 1);

#if DEBUG
  printf ("New line start at (%x,%x)\n", x, y);
#endif /* DEBUG */
  npnt->pnt.x = x;
  npnt->pnt.y = y;

  nobj = g_new0 (Dobject, 1);

  nobj->type = LINE;
  nobj->points = npnt;
  nobj->drawfunc  = d_draw_line;
  nobj->loadfunc  = d_load_line;
  nobj->savefunc  = d_save_line;
  nobj->paintfunc = d_paint_line;
  nobj->copyfunc  = d_copy_line;

  return (nobj);
}

/* You guessed it delete the object !*/
/*
static void
d_delete_line (Dobject *obj)
{
  g_assert (obj != NULL);
  * First free the list of points - then the object itself *
  d_delete_dobjpoints (obj->points);
  g_free (obj);
}
*/

/* Add a point to a line (given x, y)
 * pos = 0 = head
 * pos = -1 = tail
 * 0 < pos = nth position
 */
 
static void
d_pnt_add_line (Dobject *obj,
		gint     x,
		gint     y,
		gint     pos)
{
  DobjPoints *npnts = g_new0 (DobjPoints, 1);

  g_assert (obj != NULL);

  npnts->pnt.x = x;
  npnts->pnt.y = y;

  if (!pos)
    {
      /* Add to head */
      npnts->next = obj->points;
      obj->points = npnts;
    }
  else
    {
      DobjPoints *pnt = obj->points;

      /* Go down chain until the end if pos */
      while (pos < 0 || pos-- > 0)
	{
	  if (!(pnt->next) || !pos)
	    {
	      npnts->next = pnt->next;
	      pnt->next = npnts;
	      break;
	    }
	  else
	    {
	      pnt = pnt->next;
	    }
	}
    }
}

/* Update end point of line */
static void
d_update_line (GdkPoint *pnt)
{
  DobjPoints *spnt, *epnt;
  /* Get last but one segment and undraw it -
   * Then draw new segment in.
   * always dealing with the static object.
   */

  /* Get start of segments */
  spnt = obj_creating->points;
  
  if (!spnt)
    return; /* No points */

  if ((epnt = spnt->next))
    {
      /* undraw  current */
      /* Draw square on point */
      draw_circle (&epnt->pnt);
      
      gdk_draw_line (gfig_preview->window,
		     /*gfig_preview->style->bg_gc[GTK_STATE_NORMAL],*/
		     gfig_gc,
		     (gint) spnt->pnt.x,
		     (gint) spnt->pnt.y,
		     (gint) epnt->pnt.x,
		     (gint) epnt->pnt.y);
      g_free (epnt);
    }

  /* draw new */
  /* Draw circle on point */
  draw_circle (pnt);

  epnt = g_new0 (DobjPoints, 1);

  epnt->pnt.x = pnt->x;
  epnt->pnt.y = pnt->y;

  gdk_draw_line (gfig_preview->window,
		 /*gfig_preview->style->bg_gc[GTK_STATE_NORMAL],*/
		 gfig_gc,
		 (gint) spnt->pnt.x,
		 (gint) spnt->pnt.y,
		 (gint) epnt->pnt.x,
		 (gint) epnt->pnt.y);
  spnt->next = epnt;
}

static void
d_line_start (GdkPoint *pnt,
	      gint      shift_down)
{
  if (!obj_creating || !shift_down)
    {
      /* Draw square on point */
      /* Must delete obj_creating if we have one */
      obj_creating = d_new_line (pnt->x, pnt->y);
    }
  else
    {
      /* Contniuation */
      d_update_line (pnt);
    }
}

static void
d_line_end (GdkPoint *pnt,
	    gint      shift_down)
{
  /* Undraw the last circle */
  draw_circle (pnt);

  if (shift_down)
    {
      if (tmp_line)
	{
	  GdkPoint tmp_pnt = *pnt;

	  if (need_to_scale)
	    {
	      tmp_pnt.x = (gint) (pnt->x * scale_x_factor);
	      tmp_pnt.y = (gint) (pnt->y * scale_y_factor);
	    }

	  d_pnt_add_line (tmp_line, tmp_pnt.x, tmp_pnt.y, -1);
	  free_one_obj (obj_creating);
	  /* Must free obj_creating */
	}
      else
	{
	  tmp_line = obj_creating;
	  add_to_all_obj (current_obj, obj_creating);
	}

      obj_creating = d_new_line (pnt->x, pnt->y);
    }
  else
    {
      if (tmp_line)
	{
	  GdkPoint tmp_pnt = *pnt;

	  if (need_to_scale)
	    {
	      tmp_pnt.x = (gint) (pnt->x * scale_x_factor);
	      tmp_pnt.y = (gint) (pnt->y * scale_y_factor);
	    }

	  d_pnt_add_line (tmp_line, tmp_pnt.x, tmp_pnt.y, -1);
	  free_one_obj (obj_creating);
	  /* Must free obj_creating */
	}
      else
	{
	  add_to_all_obj (current_obj, obj_creating);
	}
      obj_creating = NULL;
      tmp_line = NULL;
    }
  /*update_draw_area (gfig_preview, NULL);*/
}

/* Save a circle away to the specified stream */

static void
d_save_circle (Dobject *obj,
	       FILE    *to)
{
  DobjPoints *spnt;

  spnt = obj->points;

  if (!spnt)
    return;

  fprintf (to, "<CIRCLE>\n");

  while (spnt)
    {
      fprintf (to, "%d %d\n",
	       (gint) spnt->pnt.x,
	       (gint) spnt->pnt.y);
      spnt = spnt->next;
    }

  fprintf (to, "</CIRCLE>\n");
}

/* Load a circle from the specified stream */

static Dobject *
d_load_circle (FILE *from)
{
  Dobject *new_obj = NULL;
  gint     xpnt;
  gint     ypnt;
  gchar    buf[MAX_LOAD_LINE];

#ifdef DEBUG
  printf ("Load circle called\n");
#endif /* DEBUG */

  while (get_line (buf, MAX_LOAD_LINE, from, 0))
    {
      if (sscanf (buf, "%d %d", &xpnt, &ypnt) != 2)
	{
	  /* Must be the end */
	  if (strcmp ("</CIRCLE>", buf))
	    {
	      g_warning ("[%d] Internal load error while loading circle",
			 line_no);
	      return NULL;
	    }
	  return new_obj;
	}

      if (!new_obj)
	new_obj = d_new_circle (xpnt, ypnt);
      else
	{
	  DobjPoints *edge_pnt;
	  /* Circles only have two points */
	  edge_pnt = g_new0 (DobjPoints, 1);

	  edge_pnt->pnt.x = xpnt;
	  edge_pnt->pnt.y = ypnt;
	  
	  new_obj->points->next = edge_pnt;
	}
    }

  g_warning ("[%d] Not enough points for circle", line_no);
  return NULL;
}

static void
d_draw_circle (Dobject * obj)
{
  DobjPoints *center_pnt;
  DobjPoints *edge_pnt;
  gdouble     radius;

  center_pnt = obj->points;

  if (!center_pnt)
    return; /* End-of-line */

  edge_pnt = center_pnt->next;

  if (!edge_pnt)
    {
      g_warning ("Internal error - circle no edge pnt");
    }

  radius = sqrt (((center_pnt->pnt.x - edge_pnt->pnt.x) *
		  (center_pnt->pnt.x - edge_pnt->pnt.x)) +
		 ((center_pnt->pnt.y - edge_pnt->pnt.y) *
		  (center_pnt->pnt.y - edge_pnt->pnt.y)));

  draw_sqr (&center_pnt->pnt);
  draw_sqr (&edge_pnt->pnt);

  if (drawing_pic)
    {
      gdk_draw_arc (pic_preview->window,
		    pic_preview->style->black_gc,
		    0,
		    adjust_pic_coords (center_pnt->pnt.x - radius,
				       preview_width),
		    adjust_pic_coords (center_pnt->pnt.y - radius,
				       preview_height),
		    adjust_pic_coords (radius * 2,
				       preview_width),
		    adjust_pic_coords (radius * 2,
				       preview_height),
		    0,
		    360 * 64);
    }
  else
    {
      gdk_draw_arc (gfig_preview->window,
		    gfig_gc,
		    0,
		    gfig_scale_x (center_pnt->pnt.x - (gint) RINT (radius)),
		    gfig_scale_y (center_pnt->pnt.y - (gint) RINT (radius)),
		    gfig_scale_x ((gint) RINT (radius) * 2),
		    gfig_scale_y ((gint) RINT (radius) * 2),
		    0,
		    360 * 64);
    }
}

static void
d_paint_circle (Dobject *obj)
{
  DobjPoints * center_pnt;
  DobjPoints * edge_pnt;
  gint radius;
  gdouble dpnts[4];

  g_assert (obj != NULL);

  if (selvals.approxcircles)
    {
      obj->type_data = (gpointer) 600;
#ifdef DEBUG
      printf ("Painting circle as polygon\n");
#endif /* DEBUG */
      d_paint_poly (obj);
      return;
    }      

  /* Drawing circles is hard .
   * 1) select circle
   * 2) stroke it
   */
  center_pnt = obj->points;

  if (!center_pnt)
    return; /* End-of-line */

  edge_pnt = center_pnt->next;

  if (!edge_pnt)
    {
      g_error ("Internal error - circle no edge pnt");
    }

  radius = (gint) sqrt (((center_pnt->pnt.x - edge_pnt->pnt.x) *
			 (center_pnt->pnt.x - edge_pnt->pnt.x)) +
			((center_pnt->pnt.y - edge_pnt->pnt.y) *
			 (center_pnt->pnt.y - edge_pnt->pnt.y)));

  dpnts[0] = (gdouble) center_pnt->pnt.x - radius;
  dpnts[1] = (gdouble) center_pnt->pnt.y - radius;
  dpnts[3] = dpnts[2] = (gdouble) radius * 2;

  /* Scale before drawing */
  if (selvals.scaletoimage)
    scale_to_original_xy (&dpnts[0], 2);
  else
    scale_to_xy (&dpnts[0], 2);

  gimp_ellipse_select (gfig_image,
		       dpnts[0], dpnts[1],
		       dpnts[2], dpnts[3],
		       selopt.type,
		       selopt.antia,
		       selopt.feather,
		       selopt.feather_radius);

  /* Is selection all we need ? */
  if (selvals.painttype == PAINT_SELECTION_TYPE)
    return;

  gimp_edit_stroke (gfig_drawable);

  gimp_selection_clear (gfig_image);
}

static Dobject *
d_copy_circle (Dobject * obj)
{
  Dobject *nc;

#if DEBUG
  printf ("Copy circle\n");
#endif /*DEBUG*/
  if (!obj)
    return NULL;

  g_assert (obj->type == CIRCLE);

  nc = d_new_circle (obj->points->pnt.x, obj->points->pnt.y);

  nc->points->next = d_copy_dobjpoints (obj->points->next);

#if DEBUG
  printf ("Circle (%x,%x) to (%x,%x)\n",
	  nc->points->pnt.x, obj->points->pnt.y,
	  nc->points->next->pnt.x, obj->points->next->pnt.y);
  printf ("Done copy\n");
#endif /*DEBUG*/
  return nc;
}

static Dobject *
d_new_circle (gint x,
	      gint y)
{
  Dobject *nobj;
  DobjPoints *npnt;
 
  /* Get new object and starting point */

  /* Start point */
  npnt = g_new0 (DobjPoints, 1);

#if DEBUG
  printf ("New circle start at (%x,%x)\n", x, y);
#endif /* DEBUG */
  npnt->pnt.x = x;
  npnt->pnt.y = y;

  nobj = g_new0 (Dobject, 1);

  nobj->type   = CIRCLE;
  nobj->points = npnt;
  nobj->drawfunc  = d_draw_circle;
  nobj->loadfunc  = d_load_circle;
  nobj->savefunc  = d_save_circle;
  nobj->paintfunc = d_paint_circle;
  nobj->copyfunc  = d_copy_circle;

  return nobj;
}

static void
d_update_circle (GdkPoint *pnt)
{
  DobjPoints *center_pnt, *edge_pnt;
  gdouble radius;

  /* Undraw last one then draw new one */
  center_pnt = obj_creating->points;

  if (!center_pnt)
    return; /* No points */

  if ((edge_pnt = center_pnt->next))
    {
      /* Undraw current */
      draw_circle (&edge_pnt->pnt);
      radius = sqrt (((center_pnt->pnt.x - edge_pnt->pnt.x) *
		      (center_pnt->pnt.x - edge_pnt->pnt.x)) +
		     ((center_pnt->pnt.y - edge_pnt->pnt.y) *
		      (center_pnt->pnt.y - edge_pnt->pnt.y)));
      
      gdk_draw_arc (gfig_preview->window,
		    gfig_gc,
		    0,
		    center_pnt->pnt.x - (gint) RINT (radius),
		    center_pnt->pnt.y - (gint) RINT (radius),
		    (gint) RINT (radius) * 2,
		    (gint) RINT (radius) * 2,
		    0,
		    360 * 64);
    }

  draw_circle (pnt);

  edge_pnt = g_new0 (DobjPoints, 1);

  edge_pnt->pnt.x = pnt->x;
  edge_pnt->pnt.y = pnt->y;

  radius = sqrt (((center_pnt->pnt.x - edge_pnt->pnt.x) *
		  (center_pnt->pnt.x - edge_pnt->pnt.x)) +
		 ((center_pnt->pnt.y - edge_pnt->pnt.y) *
		  (center_pnt->pnt.y - edge_pnt->pnt.y)));

  gdk_draw_arc (gfig_preview->window,
		gfig_gc,
		0,
		center_pnt->pnt.x - (gint) RINT (radius),
		center_pnt->pnt.y - (gint) RINT (radius),
		(gint) RINT (radius) * 2,
		(gint) RINT (radius) * 2,
		0,
		360 * 64);

  center_pnt->next = edge_pnt;
}

static void
d_circle_start (GdkPoint *pnt,
		gint      shift_down)
{
  obj_creating = d_new_circle (pnt->x, pnt->y);
}

static void
d_circle_end (GdkPoint *pnt,
	      gint      shift_down)
{
  /* Under contrl point */
  if (!obj_creating->points->next)
    {
      /* No circle created */
      free_one_obj (obj_creating);
    }
  else
    {
      draw_circle (pnt);
      add_to_all_obj (current_obj, obj_creating);
    }

  obj_creating = NULL;
}

/* Save an ellipse away to the specified stream */

static void
d_save_ellipse (Dobject *obj,
		FILE    *to)
{
  DobjPoints *spnt;

  spnt = obj->points;

  if (!spnt)
    return;

  fprintf (to, "<ELLIPSE>\n");

  while (spnt)
    {
      fprintf (to, "%d %d\n",
	       (gint) spnt->pnt.x,
	       (gint) spnt->pnt.y);
      spnt = spnt->next;
    }

  fprintf (to, "</ELLIPSE>\n");
}

/* Load a circle from the specified stream */

static Dobject *
d_load_ellipse (FILE *from)
{
  Dobject *new_obj = NULL;
  gint     xpnt;
  gint     ypnt;
  gchar    buf[MAX_LOAD_LINE];

#ifdef DEBUG
  printf ("Load ellipse called\n");
#endif /* DEBUG */

  while (get_line (buf, MAX_LOAD_LINE, from, 0))
    {
      if (sscanf (buf, "%d %d", &xpnt, &ypnt) != 2)
	{
	  /* Must be the end */
	  if (strcmp ("</ELLIPSE>", buf))
	    {
	      g_message ("[%d] Internal load error while loading ellipse",
			 line_no);
	      return NULL;
	    }
	  return new_obj;
	}

      if (!new_obj)
	new_obj = d_new_ellipse (xpnt, ypnt);
      else
	{
	  DobjPoints *edge_pnt;
	  /* Circles only have two points */
	  edge_pnt = g_new0 (DobjPoints, 1);

	  edge_pnt->pnt.x = xpnt;
	  edge_pnt->pnt.y = ypnt;

	  new_obj->points->next = edge_pnt;
	}
    }

  g_message ("[%d] Not enough points for ellipse", line_no);
  return NULL;
}

static void
d_draw_ellipse (Dobject * obj)
{
  DobjPoints *center_pnt;
  DobjPoints *edge_pnt;
  gint bound_wx;
  gint bound_wy;
  gint top_x;
  gint top_y;

  center_pnt = obj->points;

  if (!center_pnt)
    return; /* End-of-line */

  edge_pnt = center_pnt->next;

  if (!edge_pnt)
    {
      g_warning ("Internal error - ellipse no edge pnt");
    }

  draw_sqr (&center_pnt->pnt);
  draw_sqr (&edge_pnt->pnt);

  bound_wx = abs (center_pnt->pnt.x - edge_pnt->pnt.x) * 2;
  bound_wy = abs (center_pnt->pnt.y - edge_pnt->pnt.y) * 2;

  if (edge_pnt->pnt.x > center_pnt->pnt.x)
    top_x = 2 * center_pnt->pnt.x - edge_pnt->pnt.x;
  else
    top_x = edge_pnt->pnt.x;
  
  if (edge_pnt->pnt.y > center_pnt->pnt.y)
    top_y = 2 * center_pnt->pnt.y - edge_pnt->pnt.y;
  else
    top_y = edge_pnt->pnt.y;

  if (drawing_pic)
    {
      gdk_draw_arc (pic_preview->window,
		    pic_preview->style->black_gc,
		    0,
		    adjust_pic_coords (top_x,
				       preview_width),
		    adjust_pic_coords (top_y,
				       preview_height),
		    adjust_pic_coords (bound_wx,
				       preview_width),
		    adjust_pic_coords (bound_wy,
				       preview_height),
		    0,
		    360 * 64);
    }
  else
    {
      gdk_draw_arc (gfig_preview->window,
		    gfig_gc,
		    0,
		    gfig_scale_x (top_x),
		    gfig_scale_y (top_y),
		    gfig_scale_x (bound_wx),
		    gfig_scale_y (bound_wy),
		    0,
		    360 * 64);
    }
}

static void
d_paint_approx_ellipse (Dobject *obj)
{
  /* first point center */
  /* Next point is radius */
  gdouble *line_pnts;
  gint seg_count = 0;
  gint i = 0;
  DobjPoints * center_pnt;
  DobjPoints * radius_pnt;
  gdouble a_axis;
  gdouble b_axis;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble radius;
  gint loop;
  GdkPoint first_pnt, last_pnt;
  gint first = 1;

  g_assert (obj != NULL);

  /* count - add one to close polygon */
  seg_count = 600;

  center_pnt = obj->points;

  if (!center_pnt || !seg_count)
    return; /* no-line */

  line_pnts = g_new0 (gdouble, 2 * seg_count + 1);

  /* Go around all the points drawing a line from one to the next */

  radius_pnt = center_pnt->next; /* this defines the vetices */

  /* Have center and radius - get lines */
  a_axis = ((gdouble) (radius_pnt->pnt.x - center_pnt->pnt.x));
  b_axis = ((gdouble) (radius_pnt->pnt.y - center_pnt->pnt.y));

  /* Lines */
  ang_grid = 2 * G_PI / (gdouble) (gint) 600;

  for (loop = 0; loop < (gint) 600; loop++)
    {
      gdouble lx, ly;
      GdkPoint calc_pnt;

      ang_loop = (gdouble)loop * ang_grid;

      radius = (a_axis * b_axis /
		(sqrt (cos (ang_loop) * cos (ang_loop) *
		       (b_axis * b_axis - a_axis * a_axis) + a_axis * a_axis)));

      lx = radius * cos (ang_loop);
      ly = radius * sin (ang_loop);

      calc_pnt.x = (gint) RINT (lx + center_pnt->pnt.x);
      calc_pnt.y = (gint) RINT (ly + center_pnt->pnt.y);

      /* Miss out duped pnts */
      if (!first)
	{
	  if (calc_pnt.x == last_pnt.x && calc_pnt.y == last_pnt.y)
	    {
	      continue;
	    }
	}

      last_pnt.x = line_pnts[i++] = calc_pnt.x;
      last_pnt.y = line_pnts[i++] = calc_pnt.y;

      if (first)
	{
	  first_pnt.x = calc_pnt.x;
	  first_pnt.y = calc_pnt.y;
	  first = 0;
	}
    }

  line_pnts[i++] = first_pnt.x;
  line_pnts[i++] = first_pnt.y;

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

  g_free (line_pnts);
}



static void
d_paint_ellipse (Dobject *obj)
{
  DobjPoints * center_pnt;
  DobjPoints * edge_pnt;
  gint bound_wx;
  gint bound_wy;
  gint top_x;
  gint top_y;
  gdouble dpnts[4];

  /* Drawing ellipse is hard .
   * 1) select circle
   * 2) stroke it
   */

  g_assert (obj != NULL);

  if (selvals.approxcircles)
    {
#ifdef DEBUG
      printf ("Painting ellipse as polygon\n");
#endif /* DEBUG */
      d_paint_approx_ellipse (obj);
      return;
    }      

  center_pnt = obj->points;

  if (!center_pnt)
    return; /* End-of-line */

  edge_pnt = center_pnt->next;

  if (!edge_pnt)
    {
      g_error ("Internal error - ellipse no edge pnt");
    }

  bound_wx = abs (center_pnt->pnt.x - edge_pnt->pnt.x)*2;
  bound_wy = abs (center_pnt->pnt.y - edge_pnt->pnt.y)*2;

  if (edge_pnt->pnt.x > center_pnt->pnt.x)
    top_x = 2*center_pnt->pnt.x - edge_pnt->pnt.x;
  else
    top_x = edge_pnt->pnt.x;
  
  if (edge_pnt->pnt.y > center_pnt->pnt.y)
    top_y = 2*center_pnt->pnt.y - edge_pnt->pnt.y;
  else
    top_y = edge_pnt->pnt.y;

  dpnts[0] = (gdouble)top_x;
  dpnts[1] = (gdouble)top_y;
  dpnts[2] = (gdouble)bound_wx;
  dpnts[3] = (gdouble)bound_wy;

  /* Scale before drawing */
  if (selvals.scaletoimage)
    scale_to_original_xy (&dpnts[0], 2);
  else
    scale_to_xy (&dpnts[0], 2);


  gimp_ellipse_select (gfig_image,
		       dpnts[0], dpnts[1],
		       dpnts[2], dpnts[3],
		       selopt.type,
		       selopt.antia,
		       selopt.feather,
		       selopt.feather_radius);

  /* Is selection all we need ? */
  if (selvals.painttype == PAINT_SELECTION_TYPE)
    return;

  gimp_edit_stroke (gfig_drawable);

  gimp_selection_clear (gfig_image);
}

static Dobject *
d_copy_ellipse (Dobject * obj)
{
  Dobject *nc;

#if DEBUG
  printf ("Copy ellipse\n");
#endif /*DEBUG*/
  if (!obj)
    return (NULL);

  g_assert (obj->type == ELLIPSE);

  nc = d_new_ellipse (obj->points->pnt.x, obj->points->pnt.y);

  nc->points->next = d_copy_dobjpoints (obj->points->next);

#if DEBUG
  printf ("Ellipse (%x,%x) to (%x,%x)\n",
	 nc->points->pnt.x, obj->points->pnt.y,
	 nc->points->next->pnt.x, obj->points->next->pnt.y);
  printf ("Done copy\n");
#endif /*DEBUG*/
  return (nc);
}

static Dobject *
d_new_ellipse (gint x, gint y)
{
  Dobject *nobj;
  DobjPoints *npnt;
 
  /* Get new object and starting point */

  /* Start point */
  npnt = g_new0 (DobjPoints, 1);

#if DEBUG
  printf ("New ellipse start at (%x,%x)\n", x, y);
#endif /* DEBUG */
  npnt->pnt.x = x;
  npnt->pnt.y = y;

  nobj = g_new0 (Dobject, 1);

  nobj->type = ELLIPSE;
  nobj->points = npnt;
  nobj->drawfunc  = d_draw_ellipse;
  nobj->loadfunc  = d_load_ellipse;
  nobj->savefunc  = d_save_ellipse;
  nobj->paintfunc = d_paint_ellipse;
  nobj->copyfunc  = d_copy_ellipse;

  return (nobj);
}

static void
d_update_ellipse (GdkPoint *pnt)
{
  DobjPoints *center_pnt, *edge_pnt;
  gint bound_wx;
  gint bound_wy;
  gint top_x;
  gint top_y;

  /* Undraw last one then draw new one */
  center_pnt = obj_creating->points;
  
  if (!center_pnt)
    return; /* No points */

  
  if ((edge_pnt = center_pnt->next))
    {
      /* Undraw current */
      bound_wx = abs (center_pnt->pnt.x - edge_pnt->pnt.x)*2;
      bound_wy = abs (center_pnt->pnt.y - edge_pnt->pnt.y)*2;
      
      if (edge_pnt->pnt.x > center_pnt->pnt.x)
	top_x = 2*center_pnt->pnt.x - edge_pnt->pnt.x;
      else
	top_x = edge_pnt->pnt.x;
      
      if (edge_pnt->pnt.y > center_pnt->pnt.y)
	top_y = 2*center_pnt->pnt.y - edge_pnt->pnt.y;
      else
	top_y = edge_pnt->pnt.y;

      draw_circle (&edge_pnt->pnt);
      
      gdk_draw_arc (gfig_preview->window,
		    gfig_gc,
		    0,
		    top_x,
		    top_y,
		    bound_wx,
		    bound_wy,
		    0,
		    360*64);
    }

  draw_circle (pnt);

  edge_pnt = g_new0 (DobjPoints, 1);

  edge_pnt->pnt.x = pnt->x;
  edge_pnt->pnt.y = pnt->y;

  bound_wx = abs (center_pnt->pnt.x - edge_pnt->pnt.x)*2;
  bound_wy = abs (center_pnt->pnt.y - edge_pnt->pnt.y)*2;

  if (edge_pnt->pnt.x > center_pnt->pnt.x)
    top_x = 2*center_pnt->pnt.x - edge_pnt->pnt.x;
  else
    top_x = edge_pnt->pnt.x;
  
  if (edge_pnt->pnt.y > center_pnt->pnt.y)
    top_y = 2* center_pnt->pnt.y - edge_pnt->pnt.y;
  else
    top_y = edge_pnt->pnt.y;
  
  gdk_draw_arc (gfig_preview->window,
		gfig_gc,
		0,
		top_x,
		top_y,
		bound_wx,
		bound_wy,
		0,
		360*64);
  
  center_pnt->next = edge_pnt;
}

static void
d_ellipse_start (GdkPoint *pnt, gint shift_down)
{
  obj_creating = d_new_ellipse (pnt->x, pnt->y);
}

static void
d_ellipse_end (GdkPoint *pnt, gint shift_down)
{
  /* Under contrl point */
  if (!obj_creating->points->next)
    {
      /* No circle created */
      free_one_obj (obj_creating);
    }
  else
    {
      draw_circle (pnt);
      add_to_all_obj (current_obj, obj_creating);
    }

  obj_creating = NULL;
}

/* Normal polygon */

static void
d_save_poly (Dobject * obj, FILE *to)
{
  DobjPoints * spnt;
  
  spnt = obj->points;

  if (!spnt)
    return; /* End-of-line */

  fprintf (to, "<POLY>\n");

  while (spnt)
    {
      fprintf (to, "%d %d\n",
	      (gint)spnt->pnt.x,
	      (gint)spnt->pnt.y);
      spnt = spnt->next;
    }
  
  fprintf (to, "<EXTRA>\n");
  fprintf (to, "%d\n</EXTRA>\n", (gint)obj->type_data);
  fprintf (to, "</POLY>\n");

}

/* Load a circle from the specified stream */

static Dobject *
d_load_poly (FILE *from)
{
  Dobject *new_obj = NULL;
  gint xpnt;
  gint ypnt;
  gchar buf[MAX_LOAD_LINE];

#ifdef DEBUG
  printf ("Load poly called\n");
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
		  g_warning ("[%d] Internal load error while loading poly (extra area)",
			    line_no);
		  return (NULL);
		}
	      get_line (buf, MAX_LOAD_LINE, from, 0);
	      if (sscanf (buf, "%d", &nsides) != 1)
		{
		  g_warning ("[%d] Internal load error while loading poly (extra area scanf)",
			    line_no);
		  return (NULL);
		}
	      new_obj->type_data = (gpointer)nsides;
	      get_line (buf, MAX_LOAD_LINE, from, 0);
	      if (strcmp ("</EXTRA>", buf))
		{
		  g_warning ("[%d] Internal load error while loading poly",
			    line_no);
		  return (NULL);
		} 
	      /* Go around and read the last line */
	      continue;
	    }
	  else if (strcmp ("</POLY>", buf))
	    {
	      g_warning ("[%d] Internal load error while loading poly",
			line_no);
	      return (NULL);
	    }
	  return (new_obj);
	}
      
      if (!new_obj)
	new_obj = d_new_poly (xpnt, ypnt);
      else
	d_pnt_add_line (new_obj, xpnt, ypnt, -1);
    }
  return (new_obj);
}

static void
d_draw_poly (Dobject *obj)
{
  DobjPoints * center_pnt;
  DobjPoints * radius_pnt;
  gint16 shift_x;
  gint16 shift_y;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble radius;
  gdouble offset_angle;
  gint loop;
  GdkPoint start_pnt;
  GdkPoint first_pnt;
  gint do_line = 0;

  center_pnt = obj->points;

  if (!center_pnt)
    return; /* End-of-line */

  /* First point is the center */
  /* Just draw a control point around it */

  draw_sqr (&center_pnt->pnt);

  /* Next point defines the radius */
  radius_pnt = center_pnt->next; /* this defines the vertices */

  if (!radius_pnt)
    {
#ifdef DEBUG
      g_warning ("Internal error in polygon - no vertice point \n");
#endif /* DEBUG */
      return;
    }

  /* Other control point */
  draw_sqr (&radius_pnt->pnt);

  /* Have center and radius - draw polygon */

  shift_x = radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = radius_pnt->pnt.y - center_pnt->pnt.y;

  radius = sqrt ((shift_x*shift_x) + (shift_y*shift_y));

  /* Lines */
  ang_grid = 2*G_PI/(gdouble) (gint)obj->type_data;
  offset_angle = atan2 (shift_y, shift_x);

  for (loop = 0 ; loop < (gint)obj->type_data ; loop++)
    {
      gdouble lx, ly;
      GdkPoint calc_pnt;

      ang_loop = (gdouble)loop * ang_grid + offset_angle;
	
      lx = radius * cos (ang_loop);
      ly = radius * sin (ang_loop);

      calc_pnt.x = (gint)RINT (lx + center_pnt->pnt.x);
      calc_pnt.y = (gint)RINT (ly + center_pnt->pnt.y);

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

  /* Join up */
  if (drawing_pic)
    {
      gdk_draw_line (pic_preview->window,
		     pic_preview->style->black_gc,
		     adjust_pic_coords (first_pnt.x, preview_width),
		     adjust_pic_coords (first_pnt.y, preview_width),
		     adjust_pic_coords (start_pnt.x, preview_width),
		     adjust_pic_coords (start_pnt.y, preview_width));
    }
  else
    {
      gdk_draw_line (gfig_preview->window,
		     gfig_gc,
		     gfig_scale_x (first_pnt.x),
		     gfig_scale_y (first_pnt.y),
		     gfig_scale_x (start_pnt.x),
		     gfig_scale_y (start_pnt.y));
    }
}

static void
d_paint_poly (Dobject *obj)
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
  gint loop;
  GdkPoint first_pnt, last_pnt;
  gint first = 1;

  g_assert (obj != NULL);

  /* count - add one to close polygon */
  seg_count = (gint)obj->type_data + 1;

  center_pnt = obj->points;

  if (!center_pnt || !seg_count || !center_pnt->next)
    return; /* no-line */

  line_pnts = g_new0 (gdouble, 2 * seg_count + 1);
  
  /* Go around all the points drawing a line from one to the next */

  radius_pnt = center_pnt->next; /* this defines the vetices */

  /* Have center and radius - get lines */
  shift_x = radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = radius_pnt->pnt.y - center_pnt->pnt.y;

  radius = sqrt ((shift_x*shift_x) + (shift_y*shift_y));

  /* Lines */
  ang_grid = 2*G_PI/(gdouble) (gint)obj->type_data;
  offset_angle = atan2 (shift_y, shift_x);

  for (loop = 0 ; loop < (gint)obj->type_data ; loop++)
    {
      gdouble lx, ly;
      GdkPoint calc_pnt;
      
      ang_loop = (gdouble)loop * ang_grid + offset_angle;
	
      lx = radius * cos (ang_loop);
      ly = radius * sin (ang_loop);

      calc_pnt.x = (gint)RINT (lx + center_pnt->pnt.x);
      calc_pnt.y = (gint)RINT (ly + center_pnt->pnt.y);

      /* Miss out duped pnts */
      if (!first)
	{
	  if (calc_pnt.x == last_pnt.x && calc_pnt.y == last_pnt.y)
	    {
	      continue;
	    }
	}

      last_pnt.x = line_pnts[i++] = calc_pnt.x;
      last_pnt.y = line_pnts[i++] = calc_pnt.y;

      if (first)
	{
	  first_pnt.x = calc_pnt.x;
	  first_pnt.y = calc_pnt.y;
	  first = 0;
	}
    }

  line_pnts[i++] = first_pnt.x;
  line_pnts[i++] = first_pnt.y;

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

static void
d_poly2lines (Dobject *obj)
{
  /* first point center */
  /* Next point is radius */
  gint seg_count = 0;
  DobjPoints * center_pnt;
  DobjPoints * radius_pnt;
  gint16 shift_x;
  gint16 shift_y;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble radius;
  gdouble offset_angle;
  gint loop;
  GdkPoint first_pnt, last_pnt;
  gint first = 1;

  g_assert (obj != NULL);

#ifdef DEBUG
  printf ("d_poly2lines --- \n");
#endif /* DEBUG */

  /* count - add one to close polygon */
  seg_count = (gint)obj->type_data + 1;

  center_pnt = obj->points;

  if (!center_pnt)
    return; /* no-line */

  /* Undraw it to start with - removes control points */ 
  obj->drawfunc (obj);

  /* NULL out these points free later */
  obj->points = NULL;

  /* Go around all the points creating line points */

  radius_pnt = center_pnt->next; /* this defines the vertices */

  /* Have center and radius - get lines */
  shift_x = radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = radius_pnt->pnt.y - center_pnt->pnt.y;

  radius = sqrt ((shift_x*shift_x) + (shift_y*shift_y));

  /* Lines */
  ang_grid = 2*G_PI/(gdouble) (gint)obj->type_data;
  offset_angle = atan2 (shift_y, shift_x);

  for (loop = 0 ; loop < (gint)obj->type_data ; loop++)
    {
      gdouble lx, ly;
      GdkPoint calc_pnt;
      
      ang_loop = (gdouble)loop * ang_grid + offset_angle;
	
      lx = radius * cos (ang_loop);
      ly = radius * sin (ang_loop);

      calc_pnt.x = (gint)RINT (lx + center_pnt->pnt.x);
      calc_pnt.y = (gint)RINT (ly + center_pnt->pnt.y);

      if (!first)
	{
	  if (calc_pnt.x == last_pnt.x && calc_pnt.y == last_pnt.y)
	    {
	      continue;
	    }
	}

      d_pnt_add_line (obj, calc_pnt.x, calc_pnt.y, 0);

      last_pnt.x = calc_pnt.x;
      last_pnt.y = calc_pnt.y;

      if (first)
	{
	  first_pnt.x = calc_pnt.x;
	  first_pnt.y = calc_pnt.y;
	  first = 0;
	}
    }

  d_pnt_add_line (obj, first_pnt.x, first_pnt.y, 0);
  /* Free old pnts */
  d_delete_dobjpoints (center_pnt);

  /* hey we're a line now */
  obj->type = LINE;
  obj->drawfunc  = d_draw_line;
  obj->loadfunc  = d_load_line;
  obj->savefunc  = d_save_line;
  obj->paintfunc = d_paint_line;
  obj->copyfunc  = d_copy_line;

  /* draw it + control pnts */
  obj->drawfunc (obj);
}

static void
d_star2lines (Dobject *obj)
{
  /* first point center */
  /* Next point is radius */
  gint seg_count = 0;
  DobjPoints * center_pnt;
  DobjPoints * outer_radius_pnt;
  DobjPoints * inner_radius_pnt;
  gint16 shift_x;
  gint16 shift_y;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble outer_radius;
  gdouble inner_radius;
  gdouble offset_angle;
  gint loop;
  GdkPoint first_pnt, last_pnt;
  gint first = 1;

  g_assert (obj != NULL);

#ifdef DEBUG
  printf ("d_star2lines --- \n");
#endif /* DEBUG */

  /* count - add one to close polygon */
  seg_count = 2*(gint)obj->type_data + 1;

  center_pnt = obj->points;

  if (!center_pnt)
    return; /* no-line */

  /* Undraw it to start with - removes control points */ 
  obj->drawfunc (obj);

  /* NULL out these points free later */
  obj->points = NULL;

  /* Go around all the points creating line points */
  /* Next point defines the radius */
  outer_radius_pnt = center_pnt->next; /* this defines the vetices */

  if (!outer_radius_pnt)
    {
#ifdef DEBUG
      g_warning ("Internal error in star - no outer vertice point \n");
#endif /* DEBUG */
      return;
    }

  inner_radius_pnt = outer_radius_pnt->next; /* this defines the vetices */

  if (!inner_radius_pnt)
    {
#ifdef DEBUG
      g_warning ("Internal error in star - no inner vertice point \n");
#endif /* DEBUG */
      return;
    }

  shift_x = outer_radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = outer_radius_pnt->pnt.y - center_pnt->pnt.y;

  outer_radius = sqrt ((shift_x*shift_x) + (shift_y*shift_y));

  /* Lines */
  ang_grid = 2*G_PI/(2.0*(gdouble) (gint)obj->type_data);
  offset_angle = atan2 (shift_y, shift_x);

  shift_x = inner_radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = inner_radius_pnt->pnt.y - center_pnt->pnt.y;

  inner_radius = sqrt ((shift_x*shift_x) + (shift_y*shift_y));

  for (loop = 0 ; loop < 2*(gint)obj->type_data ; loop++)
    {
      gdouble lx, ly;
      GdkPoint calc_pnt;
      
      ang_loop = (gdouble)loop * ang_grid + offset_angle;

      if (loop%2)
	{
	  lx = inner_radius * cos (ang_loop);
	  ly = inner_radius * sin (ang_loop);
	}
      else
	{
	  lx = outer_radius * cos (ang_loop);
	  ly = outer_radius * sin (ang_loop);
	}

      calc_pnt.x = (gint)RINT (lx + center_pnt->pnt.x);
      calc_pnt.y = (gint)RINT (ly + center_pnt->pnt.y);

      if (!first)
	{
	  if (calc_pnt.x == last_pnt.x && calc_pnt.y == last_pnt.y)
	    {
	      continue;
	    }
	}

      d_pnt_add_line (obj, calc_pnt.x, calc_pnt.y, 0);

      last_pnt.x = calc_pnt.x;
      last_pnt.y = calc_pnt.y;

      if (first)
	{
	  first_pnt.x = calc_pnt.x;
	  first_pnt.y = calc_pnt.y;
	  first = 0;
	}
    }

  d_pnt_add_line (obj, first_pnt.x, first_pnt.y, 0);
  /* Free old pnts */
  d_delete_dobjpoints (center_pnt);

  /* hey we're a line now */
  obj->type = LINE;
  obj->drawfunc  = d_draw_line;
  obj->loadfunc  = d_load_line;
  obj->savefunc  = d_save_line;
  obj->paintfunc = d_paint_line;
  obj->copyfunc  = d_copy_line;

  /* draw it + control pnts */
  obj->drawfunc (obj);
}

static Dobject *
d_copy_poly (Dobject * obj)
{
  Dobject *np;

#if DEBUG
  printf ("Copy poly\n");
#endif /*DEBUG*/
  if (!obj)
    return (NULL);

  g_assert (obj->type == POLY);

  np = d_new_poly (obj->points->pnt.x, obj->points->pnt.y);

  np->points->next = d_copy_dobjpoints (obj->points->next);

  np->type_data = obj->type_data;

#if DEBUG
  printf ("Done poly copy\n");
#endif /*DEBUG*/
  return (np);
}

static Dobject *
d_new_poly (gint x, gint y)
{
  Dobject *nobj;
  DobjPoints *npnt;
 
  /* Get new object and starting point */

  /* Start point */
  npnt = g_new0 (DobjPoints, 1);

#if DEBUG
  printf ("New POLY start at (%x,%x)\n", x, y);
#endif /* DEBUG */
  npnt->pnt.x = x;
  npnt->pnt.y = y;

  nobj = g_new0 (Dobject, 1);

  nobj->type = POLY;
  nobj->type_data = (gpointer)3; /* Default to three sides */
  nobj->points = npnt;
  nobj->drawfunc  = d_draw_poly;
  nobj->loadfunc  = d_load_poly;
  nobj->savefunc  = d_save_poly;
  nobj->paintfunc = d_paint_poly;
  nobj->copyfunc  = d_copy_poly;

  return (nobj);
}

static void
d_update_poly (GdkPoint *pnt)
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
      d_draw_poly (obj_creating);

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
  d_draw_poly (obj_creating);
  selvals.opts.showcontrol = saved_cnt_pnt;

  /* Realy draw the control points */
  draw_circle (&edge_pnt->pnt);
}

/* first point is center 
 * next defines the radius
 */

static void
d_poly_start (GdkPoint *pnt,
	      gint      shift_down)
{
  gint16 x, y;
  /* First is center point */
  obj_creating = d_new_poly (x = pnt->x, y = pnt->y);
  obj_creating->type_data = (gpointer)poly_num_sides;
}

static void
d_poly_end (GdkPoint *pnt,
	    gint      shift_down)
{
  draw_circle (pnt);
  add_to_all_obj (current_obj, obj_creating);
  obj_creating = NULL;
}

/* ARC stuff */
/* Distance between two lines */
static gdouble
dist (gdouble x1,
      gdouble y1,
      gdouble x2,
      gdouble y2)
{

  double s1 = x1 - x2;
  double s2 = y1 - y2;

  return (sqrt ((s1*s1) + (s2*s2)));
}

/* Mid point of line returned */
static void
mid_point (gdouble x1,
	   gdouble y1,
	   gdouble x2,
	   gdouble y2,
	   gdouble *mx,
	   gdouble *my)
{
  *mx = ((double) (x1 - x2))/2.0 + (double)x2;
  *my = ((double) (y1 - y2))/2.0 + (double)y2;
}

/* Careful about infinite grads */
static gdouble
line_grad (gdouble x1,
	   gdouble y1,
	   gdouble x2,
	   gdouble y2)
{
  double dx, dy;
  
  dx = x1 - x2;
  dy = y1 - y2;

  if (dx == 0.0)
    return (0.0); /* Infinite ! */

  return (dy/dx);
}

/* Constant of line that goes through x, y with grad lgrad */
static gdouble
line_cons (gdouble x,
	   gdouble y,
	   gdouble lgrad)
{
  return (y - lgrad*x);
}

/*Get grad & const for perpend. line to given points */
static void
line_definition (gdouble  x1,
		 gdouble  y1,
		 gdouble  x2,
		 gdouble  y2,
		 gdouble *lgrad,
		 gdouble *lconst)
{
  double grad1;
  double midx, midy;

  grad1 = line_grad (x1, y1, x2, y2);

  if (grad1 == 0.0)
    {
#ifdef DEBUG
      printf ("Infinite grad....\n");
#endif /* DEBUG */
      return;
    }

  mid_point (x1, y1, x2, y2, &midx, &midy);

  /* Invert grad for perpen gradient */

  *lgrad = -1.0/grad1;
  
  *lconst = line_cons (midx, midy,*lgrad);
}

/* Arch details 
 * Given three points get arc radius and the co-ords 
 * of center point.
 */

static void
arc_details (GdkPoint *vert_a,
	     GdkPoint *vert_b,
	     GdkPoint *vert_c,
	     GdkPoint *center_pnt,
	     gdouble  *radius)
{
  /* Only vertices are in whole numbers - everything else is in doubles */
  double ax, ay;
  double bx, by;
  double cx, cy;

  double len_a, len_b, len_c;
  double sum_sides2;
  double area;
  double circumcircle_R;
  double line1_grad, line1_const;
  double line2_grad, line2_const;
  double inter_x=0.0, inter_y=0.0;
  int got_x=0, got_y=0;

  ax = (double) (vert_a->x);
  ay = (double) (vert_a->y);
  bx = (double) (vert_b->x);
  by = (double) (vert_b->y);
  cx = (double) (vert_c->x);
  cy = (double) (vert_c->y);

#ifdef DEBUG
  printf ("Vertices (%f,%f), (%f,%f), (%f,%f)\n", ax, ay, bx, by, cx, cy);
#endif /* DEBUG */

  len_a = dist (ax, ay, bx, by);
  len_b = dist (bx, by, cx, cy);
  len_c = dist (cx, cy, ax, ay);
#ifdef DEBUG
  printf ("len_a = %f, len_b = %f, len_c = %f\n", len_a, len_b, len_c);
#endif /* DEBUG */


  sum_sides2 = (fabs (len_a) + fabs (len_b) + fabs (len_c))/2;
#ifdef DEBUG
  printf ("Sum sides / 2 = %f\n", sum_sides2);
#endif /* DEBUG */

  /* Area */
  area = sqrt (sum_sides2*(sum_sides2 - len_a)*(sum_sides2 - len_b)*(sum_sides2 - len_c));
#ifdef DEBUG
  printf ("Area of triangle = %f\n", area);
#endif /* DEBUG */
  
  /* Circumcircle */
  circumcircle_R = len_a*len_b*len_c/(4*area);
  *radius = circumcircle_R;
#ifdef DEBUG
  printf ("Circumcircle radius = %f\n", circumcircle_R);
#endif /* DEBUG */

  /* Deal with exceptions - I hate exceptions */

  if (ax == bx || ax == cx || cx == bx)
    {
      /* vert line -> mid point gives inter_x */
      if (ax == bx && bx == cx)
	{
	  /* Straight line */
	  double miny = ay;
	  double maxy = ay;

	  if (by > maxy)
	    maxy = by;
	  
	  if (by < miny)
	    miny = by;

	  if (cy > maxy)
	    maxy = cy;

	  if (cy < miny)
	    miny = cy;

	  inter_y = (maxy - miny)/2 + miny;
	}
      else if (ax == bx)
	{
	  inter_y = (ay - by)/2 + by;
	}
      else if (bx == cx)
	{
	  inter_y = (by - cy)/2 + cy;
	}
      else
	{
	  inter_y = (cy - ay)/2 + ay;
	}
      got_y = 1;
    }

  if (ay == by || by == cy || ay == cy)
    {
      /* Horz line -> midpoint gives inter_y */
      if (ax == bx && bx == cx)
	{
	  /* Straight line */
	  double minx = ax;
	  double maxx = ax;

	  if (bx > maxx)
	    maxx = bx;
	  
	  if (bx < minx)
	    minx = bx;

	  if (cx > maxx)
	    maxx = cx;

	  if (cx < minx)
	    minx = cx;

	  inter_x = (maxx - minx)/2 + minx;
	}
      else if (ay == by)
	{
	  inter_x = (ax - bx)/2 + bx;
	}
      else if (by == cy)
	{
	  inter_x = (bx - cx)/2 + cx;
	}
      else
	{
	  inter_x = (cx - ax)/2 + ax;
	}
      got_x = 1;
    }

  if (!got_x || !got_y)
    {
      /* At least two of the lines are not parallel to the axis */
      /*first line */
      if (ax != bx && ay != by)
	line_definition (ax, ay, bx, by, &line1_grad, &line1_const);
      else
	line_definition (ax, ay, cx, cy, &line1_grad, &line1_const);
      /* second line */
      if (bx != cx && by != cy)
	line_definition (bx, by, cx, cy, &line2_grad, &line2_const);
      else
	line_definition (ax, ay, cx, cy, &line2_grad, &line2_const);
    }

  /* Intersection point */

  if (!got_x)
    inter_x = /*rint*/((line2_const - line1_const)/(line1_grad - line2_grad));
  if (!got_y)
    inter_y = /*rint*/((line1_grad * inter_x + line1_const));

#ifdef DEBUG
  printf ("Intersection point is (%f,%f)\n", inter_x, inter_y);
#endif /* DEBUG */

  center_pnt->x = (gint16)inter_x;
  center_pnt->y = (gint16)inter_y;
}

static gdouble
arc_angle (GdkPoint *pnt,
	   GdkPoint *center)
{
  /* Get angle (in degress) of point given origin of center */
  gint16 shift_x;
  gint16 shift_y;
  gdouble offset_angle;

  shift_x = pnt->x - center->x;
  shift_y = -pnt->y + center->y;
  offset_angle = atan2 (shift_y, shift_x);
#ifdef DEBUG
  printf ("offset_ang = %f\n", offset_angle);
#endif /* DEBUG */
  if (offset_angle < 0)
    offset_angle += 2*G_PI;

  return (offset_angle*360/(2*G_PI));
}

static void
d_save_arc (Dobject *obj,
	    FILE    *to)
{
  DobjPoints * spnt;

  spnt = obj->points;

  if (!spnt)
    return;

  fprintf (to, "<ARC>\n");

  while (spnt)
    {
      fprintf (to, "%d %d\n",
	      (gint)spnt->pnt.x,
	      (gint)spnt->pnt.y);
      spnt = spnt->next;
    }
  
  fprintf (to, "</ARC>\n");
}

/* Load a circle from the specified stream */

static Dobject *
d_load_arc (FILE *from)
{
  Dobject *new_obj = NULL;
  gint xpnt;
  gint ypnt;
  gchar buf[MAX_LOAD_LINE];
  gint num_pnts = 0;

#ifdef DEBUG
  printf ("Load arc called\n");
#endif /* DEBUG */

  while (get_line (buf, MAX_LOAD_LINE, from, 0))
    {
      if (sscanf (buf, "%d %d", &xpnt, &ypnt) != 2)
	{
	  /* Must be the end */
	  if (strcmp ("</ARC>", buf) || num_pnts != 3)
	    {
	      g_warning ("[%d] Internal load error while loading arc",
			line_no);
	      return (NULL);
	    }
	  return (new_obj);
	}
      
      num_pnts++;

      if (!new_obj)
	new_obj = d_new_arc (xpnt, ypnt);
      else
	{
	  d_pnt_add_line (new_obj, xpnt, ypnt,-1);
	}
    }
  g_warning ("[%d] Not enough points for arc", line_no);
  return (NULL);
}

static void
arc_drawing_details (Dobject  *obj,
		     gdouble  *minang,
		     GdkPoint *center_pnt,
		     gdouble  *arcang,
		     gdouble  *radius,
		     gint      draw_cnts,
		     gint      do_scale)
{
  DobjPoints * pnt1 = NULL;
  DobjPoints * pnt2 = NULL;
  DobjPoints * pnt3 = NULL;
  DobjPoints dpnts[3];
  gdouble ang1, ang2, ang3;
  gdouble maxang;

  pnt1 = obj->points;

  if (!pnt1)
    return; /* Not fully drawn */

  pnt2 = pnt1->next;

  if (!pnt2)
    return; /* Not fully drawn */

  pnt3 = pnt2->next;

  if (!pnt3)
    return; /* Still not fully drawn */

  if (draw_cnts)
    {
      draw_sqr (&pnt1->pnt);
      draw_sqr (&pnt2->pnt);
      draw_sqr (&pnt3->pnt);
    }

  if (do_scale)
    {
      /* Adjust pnts for scaling */
      /* Warning struct copies here! and casting to double <-> int */
      /* Too complex fix me - to much hacking */
      gdouble xy[2];
      int j;

      dpnts[0] = *pnt1;
      dpnts[1] = *pnt2;
      dpnts[2] = *pnt3;

      pnt1 = &dpnts[0];
      pnt2 = &dpnts[1];
      pnt3 = &dpnts[2];

      for (j = 0 ; j < 3; j++)
	{
	  xy[0] = dpnts[j].pnt.x;
	  xy[1] = dpnts[j].pnt.y;
	  if (selvals.scaletoimage)
	    scale_to_original_xy (&xy[0], 1);
	  else
	    scale_to_xy (&xy[0], 1);
	  dpnts[j].pnt.x = xy[0];
	  dpnts[j].pnt.y = xy[1];
	}
    }

  arc_details (&pnt1->pnt, &pnt2->pnt, &pnt3->pnt, center_pnt, radius);
  
  ang1 = arc_angle (&pnt1->pnt, center_pnt);
  ang2 = arc_angle (&pnt2->pnt, center_pnt);
  ang3 = arc_angle (&pnt3->pnt, center_pnt);

  /* Find min/max angle */

  maxang = ang1;

  if (ang3 > maxang)
    maxang = ang3;
  
  *minang = ang1;

  if (ang3 < *minang)
    *minang = ang3;

  if (ang2 > *minang && ang2 < maxang)
    *arcang = maxang - *minang;
  else
    *arcang = maxang - *minang - 360;
}

static void
d_draw_arc (Dobject * obj)
{
  GdkPoint center_pnt;
  gdouble radius, minang, arcang;

  g_assert (obj != NULL);

  if (!obj)
    return;

  arc_drawing_details (obj, &minang, &center_pnt, &arcang, &radius, TRUE, FALSE);
  
#ifdef DEBUG
  printf ("Min ang = %f Arc ang = %f\n", minang, arcang);
#endif /* DEBUG */

  if (drawing_pic)
    {
      gdk_draw_arc (pic_preview->window,
		    pic_preview->style->black_gc,
		    0,
		    adjust_pic_coords (center_pnt.x - (gint)radius,
				      preview_width),
		    adjust_pic_coords (center_pnt.y - (gint)radius,
				      preview_height),
		    adjust_pic_coords ((gint) (radius * 2),
				      preview_width),
		    adjust_pic_coords ((gint) (radius * 2),
				      preview_height),
		    (gint) (minang*64),
		    (gint) (arcang*64));
    }
  else
    {
      gdk_draw_arc (gfig_preview->window,
		    gfig_gc,
		    0,
		    gfig_scale_x (center_pnt.x - (gint)radius),
		    gfig_scale_y (center_pnt.y - (gint)radius),
		    gfig_scale_x ((gint) (radius * 2)),
		    gfig_scale_y ((gint) (radius * 2)),
		    (gint) (minang*64),
		    (gint) (arcang*64));
    }
}

static void
d_paint_arc (Dobject *obj)
{
  /* first point center */
  /* Next point is radius */
  gdouble *line_pnts;
  gint seg_count = 0;
  gint i = 0;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble radius;
  gint loop;
  GdkPoint first_pnt, last_pnt;
  gint first = 1;
  GdkPoint center_pnt;
  gdouble minang, arcang;

  g_assert (obj != NULL);

  if (!obj)
    return;

  /* No cnt pnts & must scale */
  arc_drawing_details (obj, &minang, &center_pnt, &arcang, &radius, FALSE, TRUE);

#ifdef DEBUG
  printf ("Paint Min ang = %f Arc ang = %f\n", minang, arcang);
#endif /* DEBUG */

  seg_count = 360; /* Should make a smoth-ish curve */

  /* +3 because we MIGHT do pie selection */
  line_pnts = g_new0 (gdouble, 2 * seg_count + 3);

  /* Lines */
  ang_grid = 2*G_PI/(gdouble)360;

  if (arcang < 0.0)
    {
      /* Swap - since we always draw anti-clock wise */
      minang += arcang;
      arcang = -arcang;
    }

  minang = minang * (2*G_PI/360); /* min ang is in degrees - need in rads*/

  for (loop = 0 ; loop < abs ((gint)arcang) ; loop++)
    {
      gdouble lx, ly;
      GdkPoint calc_pnt;
      
      ang_loop = (gdouble)loop * ang_grid + minang;

      lx = radius * cos (ang_loop);
      ly = -radius * sin (ang_loop); /* y grows down screen and angs measured from x clockwise */

      calc_pnt.x = (gint)RINT (lx + center_pnt.x);
      calc_pnt.y = (gint)RINT (ly + center_pnt.y);

      /* Miss out duped pnts */
      if (!first)
	{
	  if (calc_pnt.x == last_pnt.x && calc_pnt.y == last_pnt.y)
	    {
	      continue;
	    }
	}

      last_pnt.x = line_pnts[i++] = calc_pnt.x;
      last_pnt.y = line_pnts[i++] = calc_pnt.y;

      if (first)
	{
	  first_pnt.x = calc_pnt.x;
	  first_pnt.y = calc_pnt.y;
	  first = 0;
	}
    }

  /* Reverse line if approp */
  if (selvals.reverselines)
    reverse_pairs_list (&line_pnts[0], i/2);

  /* One go */
  if (selvals.painttype == PAINT_BRUSH_TYPE)
    {
      gfig_paint (selvals.brshtype,
		  gfig_drawable,
		  i, line_pnts);
    }
  else
    {
      if (selopt.as_pie)
	{
	  /* Add center point - cause a pie like selection... */
	  line_pnts[i++] = center_pnt.x;
	  line_pnts[i++] = center_pnt.y;
	}

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
d_copy_arc (Dobject * obj)
{
  Dobject *nc;

#if DEBUG
  printf ("Copy ellipse\n");
#endif /*DEBUG*/
  if (!obj)
    return (NULL);

  g_assert (obj->type == ARC);

  nc = d_new_arc (obj->points->pnt.x, obj->points->pnt.y);

  nc->points->next = d_copy_dobjpoints (obj->points->next);

#if DEBUG
  printf ("Arc (%x,%x), (%x,%x), (%x,%x)\n",
	 nc->points->pnt.x, obj->points->pnt.y,
	 nc->points->next->pnt.x, obj->points->next->pnt.y,
	 nc->points->next->next->pnt.x, obj->points->next->next->pnt.y);
  printf ("Done copy\n");
#endif /*DEBUG*/
  return (nc);
}

static Dobject *
d_new_arc (gint x,
	   gint y)
{
  Dobject *nobj;
  DobjPoints *npnt;
 
  /* Get new object and starting point */

  /* Start point */
  npnt = g_new0 (DobjPoints, 1);

#if DEBUG
  printf ("New arc start at (%x,%x)\n", x, y);
#endif /* DEBUG */
  npnt->pnt.x = x;
  npnt->pnt.y = y;

  nobj = g_new0 (Dobject, 1);

  nobj->type = ARC;
  nobj->points = npnt;
  nobj->drawfunc  = d_draw_arc;
  nobj->loadfunc  = d_load_arc;
  nobj->savefunc  = d_save_arc;
  nobj->paintfunc = d_paint_arc;
  nobj->copyfunc  = d_copy_arc;

  return (nobj);
}

static void
d_update_arc (GdkPoint *pnt)
{
  DobjPoints * pnt1 = NULL;
  DobjPoints * pnt2 = NULL;
  DobjPoints * pnt3 = NULL;

  /* First two points as line only become arch when third
   * point is placed on canvas.
   */

  pnt1 = obj_creating->points;

  if (!pnt1 ||
     !(pnt2 = pnt1->next) ||
     !(pnt3 = pnt2->next))
    {
      d_update_line (pnt);
      return; /* Not fully drawn */
    }

  /* Update a real curve */
  /* Nothing to be done ... */
}

static void
d_arc_start (GdkPoint *pnt,
	     gint      shift_down)
{
  /* Draw lines to start with -- then convert to an arc */
  if (!tmp_line)
    draw_sqr (pnt);
  d_line_start (pnt, TRUE); /* TRUE means multiple pointed line */
}

static void
d_arc_end (GdkPoint *pnt,
	   gint      shift_down)
{
  /* Under contrl point */
  if (!tmp_line ||
     !tmp_line->points ||
     !tmp_line->points->next)
    {
      /* No arc created  - yet */
      /* Must have three points */
#ifdef DEBUG
      printf ("No arc created yet\n");
#endif /* DEBUG */
      d_line_end (pnt, TRUE);
    }
  else
    {
      /* Complete arc */
      /* Convert to an arc ... */
      tmp_line->type = ARC;
      tmp_line->drawfunc  = d_draw_arc;
      tmp_line->loadfunc  = d_load_arc;
      tmp_line->savefunc  = d_save_arc;
      tmp_line->paintfunc = d_paint_arc;
      tmp_line->copyfunc  = d_copy_arc;
      d_line_end (pnt, FALSE);
      /*d_draw_line (newarc);  Should undraw line */
      if (need_to_scale)
	{
	  selvals.scaletoimage = 0;
	}
      /*d_draw_arc (newarc);*/
      update_draw_area (gfig_preview, NULL);
      if (need_to_scale)
	{
	  selvals.scaletoimage = 1;
	}

    }
}
/*XXXXXXXXXXXXXXXXXXXXXXX*/
/* Star shape */

static void
d_save_star (Dobject *obj,
	     FILE    *to)
{
  DobjPoints * spnt;
  
  spnt = obj->points;

  if (!spnt)
    return; /* End-of-line */

  fprintf (to, "<STAR>\n");

  while (spnt)
    {
      fprintf (to, "%d %d\n",
	      (gint)spnt->pnt.x,
	      (gint)spnt->pnt.y);
      spnt = spnt->next;
    }
  
  fprintf (to, "<EXTRA>\n");
  fprintf (to, "%d\n</EXTRA>\n", (gint)obj->type_data);
  fprintf (to, "</STAR>\n");
}

/* Load a circle from the specified stream */

static Dobject *
d_load_star (FILE *from)
{
  Dobject *new_obj = NULL;
  gint xpnt;
  gint ypnt;
  gchar buf[MAX_LOAD_LINE];

#ifdef DEBUG
  printf ("Load star called\n");
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
		  g_warning ("[%d] Internal load error while loading star (extra area)",
			    line_no);
		  return (NULL);
		}
	      get_line (buf, MAX_LOAD_LINE, from, 0);
	      if (sscanf (buf, "%d", &nsides) != 1)
		{
		  g_warning ("[%d] Internal load error while loading star (extra area scanf)",
			    line_no);
		  return (NULL);
		}
	      new_obj->type_data = (gpointer)nsides;
	      get_line (buf, MAX_LOAD_LINE, from, 0);
	      if (strcmp ("</EXTRA>", buf))
		{
		  g_warning ("[%d] Internal load error while loading star",
			    line_no);
		  return (NULL);
		} 
	      /* Go around and read the last line */
	      continue;
	    }
	  else if (strcmp ("</STAR>", buf))
	    {
	      g_warning ("[%d] Internal load error while loading star",
			line_no);
	      return (NULL);
	    }
	  return (new_obj);
	}
      
      if (!new_obj)
	new_obj = d_new_star (xpnt, ypnt);
      else
	d_pnt_add_line (new_obj, xpnt, ypnt,-1);
    }
  return (new_obj);
}

static void
d_draw_star (Dobject *obj)
{
  DobjPoints * center_pnt;
  DobjPoints * outer_radius_pnt;
  DobjPoints * inner_radius_pnt;
  gint16 shift_x;
  gint16 shift_y;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble outer_radius;
  gdouble inner_radius;
  gdouble offset_angle;
  gint loop;
  GdkPoint start_pnt;
  GdkPoint first_pnt;
  gint do_line = 0;

  center_pnt = obj->points;

  if (!center_pnt)
    return; /* End-of-line */

  /* First point is the center */
  /* Just draw a control point around it */

  draw_sqr (&center_pnt->pnt);

  /* Next point defines the radius */
  outer_radius_pnt = center_pnt->next; /* this defines the vetices */

  if (!outer_radius_pnt)
    {
#ifdef DEBUG
      g_warning ("Internal error in star - no outer vertice point \n");
#endif /* DEBUG */
      return;
    }

  inner_radius_pnt = outer_radius_pnt->next; /* this defines the vetices */

  if (!inner_radius_pnt)
    {
#ifdef DEBUG
      g_warning ("Internal error in star - no inner vertice point \n");
#endif /* DEBUG */
      return;
    }

  /* Other control points */
  draw_sqr (&outer_radius_pnt->pnt);
  draw_sqr (&inner_radius_pnt->pnt);

  /* Have center and radius - draw star */

  shift_x = outer_radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = outer_radius_pnt->pnt.y - center_pnt->pnt.y;

  outer_radius = sqrt ((shift_x*shift_x) + (shift_y*shift_y));

  /* Lines */
  ang_grid = 2*G_PI/(2.0*(gdouble) (gint)obj->type_data);
  offset_angle = atan2 (shift_y, shift_x);

  shift_x = inner_radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = inner_radius_pnt->pnt.y - center_pnt->pnt.y;

  inner_radius = sqrt ((shift_x*shift_x) + (shift_y*shift_y));

  for (loop = 0 ; loop < 2*(gint)obj->type_data ; loop++)
    {
      gdouble lx, ly;
      GdkPoint calc_pnt;

      ang_loop = (gdouble)loop * ang_grid + offset_angle;
	
      if (loop%2)
	{
	  lx = inner_radius * cos (ang_loop);
	  ly = inner_radius * sin (ang_loop);
	}
      else
	{
	  lx = outer_radius * cos (ang_loop);
	  ly = outer_radius * sin (ang_loop);
	}

      calc_pnt.x = (gint)RINT (lx + center_pnt->pnt.x);
      calc_pnt.y = (gint)RINT (ly + center_pnt->pnt.y);

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

  /* Join up */
  if (drawing_pic)
    {
      gdk_draw_line (pic_preview->window,
		     pic_preview->style->black_gc,
		     adjust_pic_coords (first_pnt.x, preview_width),
		     adjust_pic_coords (first_pnt.y, preview_width),
		     adjust_pic_coords (start_pnt.x, preview_width),
		     adjust_pic_coords (start_pnt.y, preview_width));
    }
  else
    {
      gdk_draw_line (gfig_preview->window,
		     gfig_gc,
		     gfig_scale_x (first_pnt.x),
		     gfig_scale_y (first_pnt.y),
		     gfig_scale_x (start_pnt.x),
		     gfig_scale_y (start_pnt.y));
    }
}

static void
d_paint_star (Dobject *obj)
{
  /* first point center */
  /* Next point is radius */
  gdouble *line_pnts;
  gint seg_count = 0;
  gint i = 0;
  DobjPoints * center_pnt;
  DobjPoints * outer_radius_pnt;
  DobjPoints * inner_radius_pnt;
  gint16 shift_x;
  gint16 shift_y;
  gdouble ang_grid;
  gdouble ang_loop;
  gdouble outer_radius;
  gdouble inner_radius;

  gdouble offset_angle;
  gint loop;
  GdkPoint first_pnt, last_pnt;
  gint first = 1;

  g_assert (obj != NULL);

  /* count - add one to close polygon */
  seg_count = 2*(gint)obj->type_data + 1;

  center_pnt = obj->points;

  if (!center_pnt || !seg_count)
    return; /* no-line */

  line_pnts = g_new0 (gdouble, 2 * seg_count + 1);
  
  /* Go around all the points drawing a line from one to the next */
  /* Next point defines the radius */
  outer_radius_pnt = center_pnt->next; /* this defines the vetices */

  if (!outer_radius_pnt)
    {
#ifdef DEBUG
      g_warning ("Internal error in star - no outer vertice point \n");
#endif /* DEBUG */
      return;
    }

  inner_radius_pnt = outer_radius_pnt->next; /* this defines the vetices */

  if (!inner_radius_pnt)
    {
#ifdef DEBUG
      g_warning ("Internal error in star - no inner vertice point \n");
#endif /* DEBUG */
      return;
    }

  shift_x = outer_radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = outer_radius_pnt->pnt.y - center_pnt->pnt.y;

  outer_radius = sqrt ((shift_x*shift_x) + (shift_y*shift_y));

  /* Lines */
  ang_grid = 2*G_PI/(2.0*(gdouble) (gint)obj->type_data);
  offset_angle = atan2 (shift_y, shift_x);

  shift_x = inner_radius_pnt->pnt.x - center_pnt->pnt.x;
  shift_y = inner_radius_pnt->pnt.y - center_pnt->pnt.y;

  inner_radius = sqrt ((shift_x*shift_x) + (shift_y*shift_y));

  for (loop = 0 ; loop < 2*(gint)obj->type_data ; loop++)
    {
      gdouble lx, ly;
      GdkPoint calc_pnt;
      
      ang_loop = (gdouble)loop * ang_grid + offset_angle;
	
      if (loop%2)
	{
	  lx = inner_radius * cos (ang_loop);
	  ly = inner_radius * sin (ang_loop);
	}
      else
	{
	  lx = outer_radius * cos (ang_loop);
	  ly = outer_radius * sin (ang_loop);
	}

      calc_pnt.x = (gint)RINT (lx + center_pnt->pnt.x);
      calc_pnt.y = (gint)RINT (ly + center_pnt->pnt.y);

      /* Miss out duped pnts */
      if (!first)
	{
	  if (calc_pnt.x == last_pnt.x && calc_pnt.y == last_pnt.y)
	    {
	      continue;
	    }
	}

      last_pnt.x = line_pnts[i++] = calc_pnt.x;
      last_pnt.y = line_pnts[i++] = calc_pnt.y;

      if (first)
	{
	  first_pnt.x = calc_pnt.x;
	  first_pnt.y = calc_pnt.y;
	  first = 0;
	}
    }

  line_pnts[i++] = first_pnt.x;
  line_pnts[i++] = first_pnt.y;

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
d_copy_star (Dobject * obj)
{
  Dobject *np;

#if DEBUG
  printf ("Copy star\n");
#endif /*DEBUG*/
  if (!obj)
    return (NULL);

  g_assert (obj->type == STAR);

  np = d_new_star (obj->points->pnt.x, obj->points->pnt.y);

  np->points->next = d_copy_dobjpoints (obj->points->next);

  np->type_data = obj->type_data;

#if DEBUG
  printf ("Done star copy\n");
#endif /*DEBUG*/
  return (np);
}

static Dobject *
d_new_star (gint x,
	    gint y)
{
  Dobject *nobj;
  DobjPoints *npnt;
 
  /* Get new object and starting point */

  /* Start point */
  npnt = g_new0 (DobjPoints, 1);

#if DEBUG
  printf ("New STAR start at (%x,%x)\n", x, y);
#endif /* DEBUG */
  npnt->pnt.x = x;
  npnt->pnt.y = y;

  nobj = g_new0 (Dobject, 1);

  nobj->type = STAR;
  nobj->type_data = (gpointer)3; /* Default to three sides 6 points*/
  nobj->points = npnt;
  nobj->drawfunc  = d_draw_star;
  nobj->loadfunc  = d_load_star;
  nobj->savefunc  = d_save_star;
  nobj->paintfunc = d_paint_star;
  nobj->copyfunc  = d_copy_star;

  return (nobj);
}

static void
d_update_star (GdkPoint *pnt)
{
  DobjPoints *center_pnt, *inner_pnt, *outer_pnt;
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


  if ((outer_pnt = center_pnt->next))
    {
      /* Undraw */
      inner_pnt = outer_pnt->next;
      draw_circle (&inner_pnt->pnt);
      draw_circle (&outer_pnt->pnt);
      selvals.opts.showcontrol = 0;
      d_draw_star (obj_creating);
      outer_pnt->pnt.x = pnt->x;
      outer_pnt->pnt.y = pnt->y;
      inner_pnt->pnt.x = pnt->x + (2*(center_pnt->pnt.x - pnt->x))/3;
      inner_pnt->pnt.y = pnt->y + (2*(center_pnt->pnt.y - pnt->y))/3;
    }
  else
    {
      /* Radius is a few pixels away */
      /* First edge point */
      d_pnt_add_line (obj_creating, pnt->x, pnt->y,-1);
      outer_pnt = center_pnt->next;
      /* Inner radius */
      d_pnt_add_line (obj_creating,
		      pnt->x + (2*(center_pnt->pnt.x - pnt->x))/3,
		      pnt->y + (2*(center_pnt->pnt.y - pnt->y))/3,
		      -1);
      inner_pnt = outer_pnt->next;
    }

  /* draw it */
  selvals.opts.showcontrol = 0;
  d_draw_star (obj_creating);
  selvals.opts.showcontrol = saved_cnt_pnt;

  /* Realy draw the control points */
  draw_circle (&outer_pnt->pnt);
  draw_circle (&inner_pnt->pnt);
}

/* first point is center 
 * next defines the radius
 */

static void
d_star_start (GdkPoint *pnt,
	      gint      shift_down)
{
  gint16 x, y;
  /* First is center point */
  obj_creating = d_new_star (x = pnt->x, y = pnt->y);
  obj_creating->type_data = (gpointer)star_num_sides;
}

static void
d_star_end (GdkPoint *pnt,
	    gint      shift_down)
{
  draw_circle (pnt);
  add_to_all_obj (current_obj, obj_creating);
  obj_creating = NULL;
}


/* Spiral */

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
	       (gint)spnt->pnt.x,
	       (gint)spnt->pnt.y);
      spnt = spnt->next;
    }
  
  fprintf (to, "<EXTRA>\n");
  fprintf (to, "%d\n</EXTRA>\n", (gint)obj->type_data);
  fprintf (to, "</SPIRAL>\n");

}

/* Load a spiral from the specified stream */

static Dobject *
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
	      new_obj->type_data = (gpointer)nsides;
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

  clock_wise = ((gint)obj->type_data)/(abs ((gint) (obj->type_data)));

  if (offset_angle < 0)
    offset_angle += 2*G_PI;

  sp_cons = radius/((gint)obj->type_data * 2 * G_PI + offset_angle);
  /* Lines */
  ang_grid = 2.0*G_PI/(gdouble)180;


  for (loop = 0 ; loop <= abs ((gint) (obj->type_data)*180) + clock_wise*(gint)RINT (offset_angle/ang_grid) ; loop++)
    {
      gdouble lx, ly;
      GdkPoint calc_pnt;

      ang_loop = (gdouble)loop * ang_grid;
	
      lx = sp_cons * ang_loop * cos (ang_loop)*clock_wise;
      ly = sp_cons * ang_loop * sin (ang_loop);

      calc_pnt.x = (gint)RINT (lx + center_pnt->pnt.x);
      calc_pnt.y = (gint)RINT (ly + center_pnt->pnt.y);

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

  clock_wise = ((gint)obj->type_data)/(abs ((gint) (obj->type_data)));

  offset_angle = atan2 (shift_y, shift_x);

  if (offset_angle < 0)
    offset_angle += 2*G_PI;

  sp_cons = radius/((gint)obj->type_data * 2 * G_PI + offset_angle);
  /* Lines */
  ang_grid = 2.0*G_PI/(gdouble)180;


  /* count - */
  seg_count = abs ((gint) (obj->type_data)*180) + clock_wise*(gint)RINT (offset_angle/ang_grid);

  line_pnts = g_new0 (gdouble, 2 * seg_count + 3);

  for (loop = 0 ; loop <= seg_count; loop++)
    {
      gdouble lx, ly;
      GdkPoint calc_pnt;

      ang_loop = (gdouble)loop * ang_grid;
	
      lx = sp_cons * ang_loop * cos (ang_loop)*clock_wise;
      ly = sp_cons * ang_loop * sin (ang_loop);

      calc_pnt.x = (gint)RINT (lx + center_pnt->pnt.x);
      calc_pnt.y = (gint)RINT (ly + center_pnt->pnt.y);

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
#endif /*DEBUG*/
  if (!obj)
    return (NULL);

  g_assert (obj->type == SPIRAL);

  np = d_new_spiral (obj->points->pnt.x, obj->points->pnt.y);

  np->points->next = d_copy_dobjpoints (obj->points->next);

  np->type_data = obj->type_data;

#if DEBUG
  printf ("Done spiral copy\n");
#endif /*DEBUG*/
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
  nobj->type_data = (gpointer)4; /* Default to four turns */
  nobj->points = npnt;
  nobj->drawfunc  = d_draw_spiral;
  nobj->loadfunc  = d_load_spiral;
  nobj->savefunc  = d_save_spiral;
  nobj->paintfunc = d_paint_spiral;
  nobj->copyfunc  = d_copy_spiral;

  return (nobj);
}

static void
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

static void
d_spiral_start (GdkPoint *pnt,
		gint      shift_down)
{
  gint16 x, y;
  /* First is center point */
  obj_creating = d_new_spiral (x = pnt->x, y = pnt->y);
  obj_creating->type_data =
    (gpointer) (spiral_num_turns * ((spiral_toggle == 0) ? 1 : -1));
}

static void
d_spiral_end (GdkPoint *pnt,
	      gint     shift_down)
{
  draw_circle (pnt);
  add_to_all_obj (current_obj, obj_creating);
  obj_creating = NULL;
}

/* Stuff for bezier curves... */

static void
d_save_bezier (Dobject *obj,
	       FILE    *to)
{
  DobjPoints *spnt;

  spnt = obj->points;

  if (!spnt)
    return; /* End-of-line */

  fprintf (to, "<BEZIER>\n");

  while (spnt)
    {
      fprintf (to, "%d %d\n",
	       (gint)spnt->pnt.x,
	       (gint)spnt->pnt.y);
      spnt = spnt->next;
    }
  
  fprintf (to, "<EXTRA>\n");
  fprintf (to, "%d\n</EXTRA>\n", (gint) obj->type_data);
  fprintf (to, "</BEZIER>\n");
}

/* Load a bezier from the specified stream */

static Dobject *
d_load_bezier (FILE *from)
{
  Dobject *new_obj = NULL;
  gint xpnt;
  gint ypnt;
  gchar buf[MAX_LOAD_LINE];

#ifdef DEBUG
  printf ("Load bezier called\n");
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
	      new_obj->type_data = (gpointer) nsides;
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
      x0 = (gint) fp_pnt_pnts[i];
      y0 = (gint) fp_pnt_pnts[i + 1];
      x1 = (gint) fp_pnt_pnts[i + 2];
      y1 = (gint) fp_pnt_pnts[i + 3];

      if (drawing_pic)
	{
	  gdk_draw_line (pic_preview->window,
			 pic_preview->style->black_gc,
			 adjust_pic_coords ((gint) x0,
					    preview_width),
			 adjust_pic_coords ((gint) y0,
					    preview_height),
			 adjust_pic_coords ((gint) x1,
					    preview_width),
			 adjust_pic_coords ((gint) y1,
					    preview_height));
	}
      else
	{
	  gdk_draw_line (gfig_preview->window,
			 gfig_gc,
			 gfig_scale_x ((gint) x0),
			 gfig_scale_y ((gint) y0),
			 gfig_scale_x ((gint) x1),
			 gfig_scale_y ((gint) y1));
	}
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


static void
d_draw_bezier (Dobject *obj)
{
  DobjPoints * spnt;
  gint seg_count = 0;
  gint i = 0;
  gdouble (*line_pnts)[2];

  spnt = obj->points;

  /* First count the number of points */

  /* count */
  while (spnt)
    {
      seg_count++;
      spnt = spnt->next;
    }

  spnt = obj->points;

  if (!spnt || !seg_count)
    return; /* no-line */

  line_pnts = (fp_pnt) g_new0 (gdouble, 2 * seg_count + 1);

  /* Go around all the points drawing a line from one to the next */
  while (spnt)
    {
      draw_sqr (&spnt->pnt);
      line_pnts[i][0] = spnt->pnt.x;
      line_pnts[i++][1] = spnt->pnt.y;
      spnt = spnt->next;
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

  spnt = obj->points;

  /* First count the number of points */

  /* count */
  while (spnt)
    {
      seg_count++;
      spnt = spnt->next;
    }

  spnt = obj->points;

  if (!spnt || !seg_count)
    return; /* no-line */

  bz_line_pnts = (fp_pnt) g_new0 (gdouble, 2 * seg_count + 1);

  /* Go around all the points drawing a line from one to the next */
  while (spnt)
    {
      bz_line_pnts[i][0] = spnt->pnt.x;
      bz_line_pnts[i++][1] = spnt->pnt.y;
      spnt = spnt->next;
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
d_copy_bezier (Dobject * obj)
{
  Dobject *np;

#if DEBUG
  printf ("Copy bezier\n");
#endif /*DEBUG*/
  if (!obj)
    return (NULL);

  g_assert (obj->type == BEZIER);

  np = d_new_bezier (obj->points->pnt.x, obj->points->pnt.y);

  np->points->next = d_copy_dobjpoints (obj->points->next);

  np->type_data = obj->type_data;

#if DEBUG
  printf ("Done bezier copy\n");
#endif /*DEBUG*/
  return np;
}

static Dobject *
d_new_bezier (gint x, gint y)
{
  Dobject *nobj;
  DobjPoints *npnt;
 
  /* Get new object and starting point */

  /* Start point */
  npnt = g_new0 (DobjPoints, 1);

#if DEBUG
  printf ("New BEZIER start at (%x,%x)\n", x, y);
#endif /* DEBUG */
  npnt->pnt.x = x;
  npnt->pnt.y = y;

  nobj = g_new0 (Dobject, 1);

  nobj->type = BEZIER;
  nobj->type_data = (gpointer)4; /* Default to four turns */
  nobj->points = npnt;
  nobj->drawfunc  = d_draw_bezier;
  nobj->loadfunc  = d_load_bezier;
  nobj->savefunc  = d_save_bezier;
  nobj->paintfunc = d_paint_bezier;
  nobj->copyfunc  = d_copy_bezier;

  return (nobj);
}

static void
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
      l_pnt->pnt.x = pnt->x;
      l_pnt->pnt.y = pnt->y;
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

/* first point is center 
 * next defines the radius
 */

static void
d_bezier_start (GdkPoint *pnt, gint shift_down)
{
  gint16 x, y;
  /* First is center point */
  if (!tmp_bezier)
    {
      /* New curve */
      tmp_bezier = obj_creating = d_new_bezier (x = pnt->x, y = pnt->y);
    }
}

static void
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


/* copy objs */
static DAllObjs *
copy_all_objs (DAllObjs *objs)
{
  DAllObjs *nobj;
  DAllObjs *new_all_objs = NULL;
  DAllObjs *ret = NULL;

  while (objs)
    {
      nobj = g_new0 (DAllObjs, 1);

     if (!ret)
	{
	  ret = new_all_objs = nobj;
	}
      else
	{
	  new_all_objs->next = nobj;
	  new_all_objs = nobj;
	}

      nobj->obj = (Dobject *) objs->obj->copyfunc (objs->obj);

      objs = objs->next;
    }

  return ret;
}

/* Screen refresh */
static void
draw_one_obj (Dobject * obj)
{
  obj->drawfunc (obj);
}

static void
draw_objects (DAllObjs *objs,
	      gint      show_single)
{
  /* Show_single - only one object to draw Unless shift 
   * is down in whcih case show all.
   */

  gint count = 0;

  while (objs)
    {
      if (!show_single || count == obj_show_single || obj_show_single == -1)
	draw_one_obj (objs->obj);
      objs = objs->next;
      count++;
    }
}

static void
prepend_to_all_obj (GFigObj  *fobj,
		    DAllObjs *nobj)
{
  DAllObjs *cobj;

  setup_undo (); /* Remember ME */

  if (!fobj->obj_list)
    {
      fobj->obj_list = nobj;
      return;
    }

  cobj = fobj->obj_list;

  while (cobj->next)
    {
      cobj = cobj->next;
    }

  cobj->next = nobj;
}

static void
add_to_all_obj (GFigObj *fobj,
		Dobject *obj)
{
  DAllObjs *nobj;
  
  nobj = g_new0 (DAllObjs, 1);

  nobj->obj = obj;

  if (need_to_scale)
    scale_obj_points (obj->points, scale_x_factor, scale_y_factor);

  prepend_to_all_obj (fobj, nobj);
}

static void
object_operation_start (GdkPoint *pnt,
			gint      shift_down)
{
  Dobject *new_obj;

  /* Find point in given object list */
  operation_obj = get_nearest_objs (current_obj, pnt);

  /* Special case if shift down && move obj then moving all objs */

  if (shift_down && selvals.otype == MOVE_OBJ)
    {
      move_all_pnt = g_malloc0 (sizeof (*move_all_pnt));
      *move_all_pnt = *pnt; /* Structure copy */
      setup_undo ();
      return;
    }

  if (!operation_obj)
    return;/* None to work on */

  setup_undo ();

  switch (selvals.otype)
    {
    case MOVE_OBJ:
      if (operation_obj->type == BEZIER)
	{
	  d_draw_bezier (operation_obj);
	  tmp_bezier = operation_obj;
	  d_draw_bezier (operation_obj);
	}
      break;
    case MOVE_POINT:
      if (operation_obj->type == BEZIER)
	{
	  d_draw_bezier (operation_obj);
	  tmp_bezier = operation_obj;
	  d_draw_bezier (operation_obj);
	}
      /* If shift is down the break into sep lines */
      if ((operation_obj->type == POLY  
	  || operation_obj->type == STAR)
	 && shift_down)
	{
	  switch (operation_obj->type)
	    {
	    case POLY:
	      d_poly2lines (operation_obj);
	      break;
	    case STAR:
	      d_star2lines (operation_obj);
	      break;
	    default:
	      break;
	    }
	  /* Re calc which object point we are lookin at */
	  scan_obj_points (operation_obj->points, pnt);
	}
      break;
    case COPY_OBJ:
      /* Copy the "operation object" */
      /* Then bung us into "copy/move" mode */
#ifdef DEBUG
      printf ("In copy obj\n");
#endif /* DEBUG */
      new_obj = (Dobject *) operation_obj->copyfunc (operation_obj);
      if (new_obj)
	{
	  scan_obj_points (new_obj->points, pnt);
	  add_to_all_obj (current_obj, new_obj);
	  operation_obj = new_obj;
	  selvals.otype = MOVE_COPY_OBJ;
	  new_obj->drawfunc (new_obj);
	}
      break;
    case DEL_OBJ:
      remove_obj_from_list (current_obj, operation_obj);
      break;
    case MOVE_COPY_OBJ: /* Never when button down */
    default:
      g_warning ("Internal error selvals.otype object operation start");
      break;
    }
}

static void
object_operation_end (GdkPoint *pnt,
		      gint      shift_down)
{
  if (selvals.otype != DEL_OBJ && operation_obj && operation_obj->type == BEZIER)
    {
      d_draw_bezier (operation_obj);
      tmp_bezier = NULL; /* use as switch */
      d_draw_bezier (operation_obj);
    }

  operation_obj = NULL;

  if (move_all_pnt)
    {
      g_free (move_all_pnt);
      move_all_pnt = 0;
    }

  /* Special case - if copying mode MUST be copy when button up received */
  if (selvals.otype == MOVE_COPY_OBJ)
    selvals.otype = COPY_OBJ;
}

/* Move object around */
static void
object_operation (GdkPoint *to_pnt,
		  gint      shift_down)
{
  /* Must do diffent things depending on object type */
  /* but must have object to operate on! */

  /* Special case - if shift own and move_obj then move ALL objects */
  if (move_all_pnt && shift_down && selvals.otype == MOVE_OBJ)
    {
      do_move_all_obj (to_pnt);
      return;
    }

  if (!operation_obj)
    return;

  switch (selvals.otype)
    {
    case MOVE_OBJ:
    case MOVE_COPY_OBJ:
      switch (operation_obj->type)
	{
	case LINE:
	case CIRCLE:
	case ELLIPSE:
	case POLY:
	case ARC:
	case STAR:
	case SPIRAL:
	case BEZIER:
	  do_move_obj (operation_obj, to_pnt);
	  break;
	default:
	  /* Internal error */
	  g_warning ("Internal error in operation_obj->type");
	  break;
	}
      break;
    case MOVE_POINT:
      switch (operation_obj->type)
	{
	case LINE:
	case CIRCLE:
	case ELLIPSE:
	case POLY:
	case ARC:
	case STAR:
	case SPIRAL:
	case BEZIER:
	  do_move_obj_pnt (operation_obj, to_pnt);
	  break;
	default:
	  /* Internal error */
	  g_warning ("Internal error in operation_obj->type");
	  break;
	}
      break;
    case DEL_OBJ:
      break;
    case COPY_OBJ: /* Should have been changed to MOVE_COPY_OBJ */
    default:
      g_warning ("Internal error selvals.otype");
      break;
    }
}

/* First button press -- start drawing object */
static void
object_start (GdkPoint *pnt,
	      gint      shift_down)
{
  /* start for the current object */
  if (!selvals.scaletoimage)
    {
      need_to_scale = 1;
      selvals.scaletoimage = 1;
    }
  else
    {
      need_to_scale = 0;
    }

  switch (selvals.otype)
    {
    case LINE:
      /* Shift means we are still drawing */
      if (!shift_down || !obj_creating)
	draw_sqr (pnt);
      d_line_start (pnt, shift_down);
      break;
    case CIRCLE:
      draw_sqr (pnt);
      d_circle_start (pnt, shift_down);
      break;
    case ELLIPSE:
      draw_sqr (pnt);
      d_ellipse_start (pnt, shift_down);
      break;
    case POLY:
      draw_sqr (pnt);
      d_poly_start (pnt, shift_down);
      break;
    case ARC:
      d_arc_start (pnt, shift_down);
      break;
    case STAR:
      draw_sqr (pnt);
      d_star_start (pnt, shift_down);
      break;
    case SPIRAL:
      draw_sqr (pnt);
      d_spiral_start (pnt, shift_down);
      break;
    case BEZIER:
      if (!tmp_bezier)
	draw_sqr (pnt);
      d_bezier_start (pnt, shift_down);
      break;
    default:
      /* Internal error */
      break;
    }
}
  
/* Real object now !*/
static void
object_end (GdkPoint *pnt,
	    gint      shift_down)
{
  /* end for the current object */
  /* Add onto global object list */

  /* If shift is down may carry on drawing */
  switch (selvals.otype)
    {
    case LINE:
      d_line_end (pnt, shift_down);
      draw_sqr (pnt);
      break;
    case CIRCLE:
      draw_sqr (pnt);
      d_circle_end (pnt, shift_down);
      break;
    case ELLIPSE:
      draw_sqr (pnt);
      d_ellipse_end (pnt, shift_down);
      break;
    case POLY:
      draw_sqr (pnt);
      d_poly_end (pnt, shift_down);
      break;
    case STAR:
      draw_sqr (pnt);
      d_star_end (pnt, shift_down);
      break;
    case ARC:
      draw_sqr (pnt);
      d_arc_end (pnt, shift_down);
      break;
    case SPIRAL:
      draw_sqr (pnt);
      d_spiral_end (pnt, shift_down);
      break;
    case BEZIER:
      d_bezier_end (pnt, shift_down);
      break;
    default:
      /* Internal error */
      break;
    }

  if (need_to_scale)
    {
      need_to_scale = 0;
      selvals.scaletoimage = 0;
    }
}

static void
object_update (GdkPoint *pnt)
{
  /* update for the current object */
  /* New position xy */
  switch (selvals.otype)
    {
    case LINE:
      d_update_line (pnt);
      break;
    case CIRCLE:
      d_update_circle (pnt);
      break;
    case ELLIPSE:
      d_update_ellipse (pnt);
      break;
    case POLY:
      d_update_poly (pnt);
      break;
    case STAR:
      d_update_star (pnt);
      break;
    case ARC:
      d_update_arc (pnt);
      break;
    case SPIRAL:
      d_update_spiral (pnt);
      break;
    case BEZIER:
      d_update_bezier (pnt);
      break;
    default:
      /* Internal error */
      break;
    }
}
