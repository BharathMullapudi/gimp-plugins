/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * GTM plug-in --- GIMP Table Magic
 * Allows images to be saved as HTML tables with different colored cells.
 * It doesn't  have very much practical use other than being able to
 * easily design a table by "painting" it in GIMP, or to make small HTML
 * table images/icons.
 *
 * Copyright (C) 1997 Daniel Dunbar
 * Email: ddunbar@diads.com
 * WWW:   http://millennium.diads.com/gimp/
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

/* Version 1.0:
 * Once I first found out that it was possible to have pixel level control
 * of HTML tables I instantly realized that it would be possible, however
 * pointless, to save an image as a, albeit huge, HTML table.
 *
 * One night when I was feeling in an adventourously stupid programming mood
 * I decided to write a program to do it.
 *
 * At first I just wrote a really ugly hack to do it, which I then planned
 * on using once just to see how it worked, and then posting a URL and 
 * laughing about it on #gimp.  As it turns out, tigert thought it actually
 * had potential to be a useful plugin, so I started adding features and
 * and making a nice UI.
 *
 * It's still not very useful, but I did manage to significantly improve my
 * C programming skills in the process, so it was worth it.
 *
 * If you happen to find it usefull I would appreciate any email about it.
 *                                     - Daniel Dunbar
 *                                       ddunbar@diads.com
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"

#include "pixmaps/eek.xpm"


/* Typedefs */

typedef struct
{
  gchar captiontxt[256];
  gchar cellcontent[256];
  gchar clwidth[256];
  gchar clheight[256];
  gint  fulldoc;
  gint  caption;
  gint  border;
  gint  spantags;
  gint  tdcomp;
  gint  cellpadding;
  gint  cellspacing;
} GTMValues;

typedef struct
{
  gint run;
} GTMInterface;

/* Variables */

static GTMInterface bint =
{
  FALSE  /* run */
};

static GTMValues gtmvals =
{
  "Made with GIMP Table Magic",  /* caption text */
  "&nbsp;",  /* cellcontent text */
  "",    /* cell width text */
  "",    /* cell height text */
  1,     /* fulldoc */
  0,     /* caption */
  2,     /* border */
  0,     /* spantags */
  0,     /* tdcomp */
  4,     /* cellpadding */
  0      /* cellspacing */
};

/* Declare some local functions */

static void   query      (void);
static void   run        (gchar   *name,
                          gint     nparams,
                          GimpParam  *param,
                          gint    *nreturn_vals,
                          GimpParam **return_vals);

static gint   save_image  (gchar     *filename,
			   GimpDrawable *drawable);
static gint   save_dialog (gint32     image_ID);

static gint   color_comp               (guchar    *buffer,
					guchar    *buf2);
static void   save_ok_callback         (GtkWidget *widget,
					gpointer   data);
static void   gtm_caption_callback     (GtkWidget *widget,
					gpointer   data);
static void   gtm_cellcontent_callback (GtkWidget *widget,
					gpointer   data);
static void   gtm_clwidth_callback     (GtkWidget *widget,
					gpointer   data);
static void   gtm_clheight_callback    (GtkWidget *widget,
					gpointer   data);

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};


MAIN ()

static void
query (void)
{
  static GimpParamDef save_args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive" },
    { GIMP_PDB_IMAGE, "image", "Input image" },
    { GIMP_PDB_DRAWABLE, "drawable", "Drawable to save" },
    { GIMP_PDB_STRING, "filename", "The name of the file to save the image in" },
    { GIMP_PDB_STRING, "raw_filename", "The name of the file to save the image in" }
  };
  static gint nsave_args = sizeof (save_args) / sizeof (save_args[0]);

  gimp_install_procedure ("file_GTM_save",
                          "GIMP Table Magic",
                          "Allows you to draw an HTML table in GIMP. See help for more info.",
                          "Daniel Dunbar",
                          "Daniel Dunbar",
                          "1998",
                          "<Save>/HTML",
			  "RGB*, GRAY*",
                          GIMP_PLUGIN,
                          nsave_args, 0,
                          save_args, NULL);

  gimp_register_save_handler ("file_GTM_save",
			      "html,htm",
			      "");
}

static void
run (gchar   *name,
     gint     nparams,
     GimpParam  *param,
     gint    *nreturn_vals,
     GimpParam **return_vals)
{
  static GimpParam values[2];
  GimpPDBStatusType   status = GIMP_PDB_SUCCESS;
  GimpDrawable *drawable;

  INIT_I18N_UI();

  drawable = gimp_drawable_get (param[2].data.d_int32);

  *nreturn_vals = 1;
  *return_vals  = values;
  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;

  gimp_get_data ("file_GTM_save", &gtmvals);

  if (save_dialog (param[1].data.d_int32))
    {
      if (save_image (param[3].data.d_string, drawable))
	{
	  gimp_set_data ("file_GTM_save", &gtmvals, sizeof (GTMValues));
	}
      else
	{
	  status = GIMP_PDB_EXECUTION_ERROR;
	}
    }
  else
    {
      status = GIMP_PDB_CANCEL;
    }

  values[0].data.d_status = status;
}

static gint
save_image (gchar     *filename,
	    GimpDrawable *drawable)
{
  int row,col, cols, rows, x, y;
  int colcount, colspan, rowspan;
  /* This works only in gcc - not allowed according */
  /* to ANSI C */
  /*int palloc[drawable->width][drawable->height];*/
  int *palloc;
  guchar *buffer, *buf2;
  gchar *width, *height;
  GimpPixelRgn pixel_rgn;
  char *name;

  FILE *fp, *fopen();

  palloc = malloc(drawable->width * drawable->height * sizeof(int));

  fp = fopen(filename, "w");
  if (gtmvals.fulldoc) {
    fprintf (fp,"<HTML>\n<HEAD><TITLE>%s</TITLE></HEAD>\n<BODY>\n",filename);
    fprintf (fp,"<H1>%s</H1>\n",filename);
  }
  fprintf (fp,"<TABLE BORDER=%d CELLPADDING=%d CELLSPACING=%d>\n",gtmvals.border,gtmvals.cellpadding,gtmvals.cellspacing);
  if (gtmvals.caption)
    fprintf (fp,"<CAPTION>%s</CAPTION>\n",gtmvals.captiontxt); 

  name = g_strdup_printf (_("Saving %s:"), filename);
  gimp_progress_init (name);
  g_free (name);

  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0, drawable->width, drawable->height, FALSE, FALSE);

  cols = drawable->width;
  rows = drawable->height;
  buffer = g_new(guchar,drawable->bpp);
  buf2 = g_new(guchar,drawable->bpp);

  width = malloc (2);
  height = malloc (2);
  sprintf(width," ");
  sprintf(height," ");
  if (strcmp (gtmvals.clwidth, "") != 0) {
    width = malloc (strlen (gtmvals.clwidth) + 11);
    sprintf(width," WIDTH=\"%s\"",gtmvals.clwidth);
  }
  if (strcmp (gtmvals.clheight, "") != 0) {
    height = malloc (strlen (gtmvals.clheight) + 13);
    sprintf(height," HEIGHT=\"%s\" ",gtmvals.clheight);
  }  
  
  /* Initialize array to hold ROWSPAN and COLSPAN cell allocation table */

  for (row=0; row < rows; row++)
    for (col=0; col < cols; col++)
      palloc[drawable->width * row + col]=1;

  colspan=0;
  rowspan=0;

  for (y = 0; y < rows; y++) {
    fprintf (fp,"   <TR>\n");
    for (x = 0; x < cols; x++) {
      gimp_pixel_rgn_get_pixel(&pixel_rgn, buffer, x, y);

      /* Determine ROWSPAN and COLSPAN */

      if (gtmvals.spantags) { 
	col=x;
	row=y;
	colcount=0;
	colspan=0;
	rowspan=0;
	gimp_pixel_rgn_get_pixel(&pixel_rgn, buf2, col, row);
	
	while (color_comp(buffer,buf2) && palloc[drawable->width * row + col] == 1 && row < drawable->height) {
	  while (color_comp(buffer,buf2) && palloc[drawable->width * row + col] == 1 && col < drawable->width ) {
	    colcount++;
	    col++;
	    gimp_pixel_rgn_get_pixel(&pixel_rgn, buf2, col, row);
	  }
	  
	  if (colcount != 0) {
	    row++;
	    rowspan++;
	  }
	  
	  if (colcount < colspan || colspan == 0)
	    colspan=colcount;
	  
	  col=x;
	  colcount=0;
	  gimp_pixel_rgn_get_pixel(&pixel_rgn, buf2, col, row);
	}
	
	if (colspan > 1 || rowspan > 1) {
	  for (row=0; row < rowspan; row++)
	    for (col=0; col < colspan; col++)
	      palloc[drawable->width * (row+y) + (col+x)]=0;
	  palloc[drawable->width * y + x]=2;
	}
      }

      if (palloc[drawable->width * y + x]==1)
	fprintf (fp,"      <TD%s%sBGCOLOR=#%02x%02x%02x>",width,height,buffer[0],buffer[1],buffer[2]);

      if (palloc[drawable->width * y + x]==2)
	fprintf (fp,"      <TD ROWSPAN=\"%d\" COLSPAN=\"%d\"%s%sBGCOLOR=#%02x%02x%02x>",rowspan,colspan,width,height,buffer[0],buffer[1],buffer[2]);

      if (palloc[drawable->width * y + x]!=0) {
	if (gtmvals.tdcomp)
	  fprintf (fp,"%s</TD>\n",gtmvals.cellcontent);
	else 
	  fprintf (fp,"\n      %s\n      </TD>\n",gtmvals.cellcontent);
      }
    }
    fprintf (fp,"   </TR>\n");
    gimp_progress_update ((double) y / (double) rows);
  }

  if (gtmvals.fulldoc)
    fprintf (fp,"</TABLE></BODY></HTML>\n");  
  else fprintf (fp,"</TABLE>\n");
  fclose(fp);
  gimp_drawable_detach (drawable);
  free(width);
  free(height);

  free(palloc);

  return 1;
}

static gint
save_dialog (image_ID)
{
  GtkWidget *dlg;
  GtkWidget *main_vbox;
  GtkWidget *frame;
  GtkWidget *vbox;
  GtkWidget *table;
  GtkWidget *spinbutton;
  GtkObject *adj;
  GtkWidget *entry;
  GtkWidget *toggle;

  bint.run = FALSE;

  gimp_ui_init ("gtm", FALSE);

  dlg = gimp_dialog_new (_("GIMP Table Magic"), "gtm",
			 gimp_standard_help_func, "filters/gtm.html",
			 GTK_WIN_POS_MOUSE,
			 FALSE, TRUE, FALSE,

			 GTK_STOCK_CANCEL, gtk_widget_destroy,
			 NULL, 1, NULL, FALSE, TRUE,
			 GTK_STOCK_OK, save_ok_callback,
			 NULL, NULL, NULL, TRUE, FALSE,

			 NULL);

  gtk_signal_connect (GTK_OBJECT (dlg), "destroy",
                      GTK_SIGNAL_FUNC (gtk_main_quit),
                      NULL);

  /* Initialize Tooltips */
  gimp_help_init ();

  main_vbox = gtk_vbox_new (FALSE, 4);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), main_vbox,
		      TRUE, TRUE, 0);

  if (gimp_image_width (image_ID) * gimp_image_height (image_ID) > 4096)
    {
      GtkWidget *eek;
      GtkWidget *label;
      GtkWidget *hbox;

      frame = gtk_frame_new (_("Warning"));
      gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
      gtk_box_pack_start (GTK_BOX (main_vbox), frame, FALSE, FALSE, 0);

      hbox = gtk_hbox_new (FALSE, 4);
      gtk_container_set_border_width (GTK_CONTAINER (hbox), 4);
      gtk_container_add (GTK_CONTAINER (frame), hbox);
      
      eek = gimp_pixmap_new (eek_xpm);
      gtk_box_pack_start (GTK_BOX (hbox), eek, FALSE, FALSE, 4);

      label = gtk_label_new (_("Are you crazy?\n\n"
			       "You are about to create a huge\n"
			       "HTML file which will most likely\n"
			       "crash your browser."));
      gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);

      gtk_widget_show_all (frame);
    }

  /* HTML Page Options */
  frame = gtk_frame_new (_("HTML Page Options"));
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
  gtk_box_pack_start (GTK_BOX (main_vbox), frame, FALSE, FALSE, 0);

  vbox = gtk_vbox_new (FALSE, 2);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 4);
  gtk_container_add (GTK_CONTAINER (frame), vbox);
  gtk_widget_show (vbox);

  toggle = gtk_check_button_new_with_label (_("Generate Full HTML Document"));
  gtk_box_pack_start (GTK_BOX (vbox), toggle, FALSE, FALSE, 0);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &gtmvals.fulldoc);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), gtmvals.fulldoc);
  gtk_widget_show (toggle);
  gimp_help_set_help_data (toggle,
			   _("If checked GTM will output a full HTML document "
			     "with <HTML>, <BODY>, etc. tags instead of just "
			     "the table html."),
			   NULL);

  gtk_widget_show (main_vbox);
  gtk_widget_show (frame);

  /* HTML Table Creation Options */
  frame = gtk_frame_new (_("Table Creation Options"));
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
  gtk_box_pack_start (GTK_BOX (main_vbox), frame, FALSE, FALSE, 0);

  table = gtk_table_new (4, 2, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_container_set_border_width (GTK_CONTAINER (table), 4);
  gtk_container_add (GTK_CONTAINER (frame), table);

  toggle = gtk_check_button_new_with_label (_("Use Cellspan"));
  gtk_table_attach (GTK_TABLE (table), toggle, 0, 2, 0, 1, GTK_FILL, 0, 0, 0);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &gtmvals.spantags);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), gtmvals.spantags);
  gtk_widget_show (toggle);
  gimp_help_set_help_data (toggle,
			   _("If checked GTM will replace any rectangular "
			     "sections of identically colored blocks with one "
			     "large cell with ROWSPAN and COLSPAN values."),
			   NULL);

  toggle = gtk_check_button_new_with_label (_("Compress TD tags"));
  gtk_table_attach (GTK_TABLE (table), toggle, 0, 2, 1, 2, GTK_FILL, 0, 0, 0);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &gtmvals.tdcomp);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), gtmvals.tdcomp);
  gtk_widget_show (toggle);
  gimp_help_set_help_data (toggle,
			   _("Checking this tag will cause GTM to leave no "
			     "whitespace between the TD tags and the "
			     "cellcontent.  This is only necessary for pixel "
			     "level positioning control."),
			   NULL);

  toggle = gtk_check_button_new_with_label (_("Caption"));
  gtk_table_attach (GTK_TABLE (table), toggle, 0, 1, 2, 3, GTK_FILL, 0, 0, 0);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &gtmvals.caption);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), gtmvals.caption);
  gtk_widget_show (toggle);
  gimp_help_set_help_data (toggle,
			   _("Check if you would like to have the table "
			     "captioned."),
			   NULL);

  entry = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 2, 3,
		    GTK_FILL | GTK_EXPAND, 0, 0, 0);
  gtk_widget_set_size_request (entry, 200, -1);
  gtk_signal_connect (GTK_OBJECT (entry), "changed",
		      GTK_SIGNAL_FUNC (gtm_caption_callback),
		      NULL);
  gtk_entry_set_text (GTK_ENTRY (entry), gtmvals.captiontxt);
  gtk_widget_show (entry);
  gimp_help_set_help_data (entry, _("The text for the table caption."), NULL);

  gtk_object_set_data (GTK_OBJECT (toggle), "set_sensitive", entry);
  gtk_widget_set_sensitive (entry, gtmvals.caption);

  entry = gtk_entry_new ();
  gtk_widget_set_size_request (entry, 200, -1);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 3,
			     _("Cell Content:"), 1.0, 0.5,
			     entry, 1, FALSE);
  gtk_signal_connect (GTK_OBJECT (entry), "changed",
		      GTK_SIGNAL_FUNC (gtm_cellcontent_callback),
		      NULL);
  gtk_entry_set_text (GTK_ENTRY (entry), gtmvals.cellcontent);
  gtk_widget_show (entry);
  gimp_help_set_help_data (entry, _("The text to go into each cell."), NULL);

  gtk_widget_show (table);
  gtk_widget_show (frame);

  /* HTML Table Options */
  frame = gtk_frame_new (_("Table Options"));
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
  gtk_box_pack_start (GTK_BOX (main_vbox), frame, FALSE, FALSE, 0);

  table = gtk_table_new (5, 2, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_container_set_border_width (GTK_CONTAINER (table), 4);
  gtk_container_add (GTK_CONTAINER (frame), table);

  spinbutton = gimp_spin_button_new (&adj, gtmvals.border,
				     0, 1000, 1, 10, 0, 1, 0);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 0,
			     _("Border:"), 1.0, 0.5,
			     spinbutton, 1, TRUE);
  gtk_signal_connect (GTK_OBJECT (adj), "value_changed",
                      GTK_SIGNAL_FUNC (gimp_int_adjustment_update),
                      &gtmvals.border);
  gimp_help_set_help_data (spinbutton,
			   _("The number of pixels in the table border."),
			   NULL);

  entry = gtk_entry_new ();
  gtk_widget_set_size_request (entry, 60, -1);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 1,
			     _("Width:"), 1.0, 0.5,
			     entry, 1, TRUE);
  gtk_signal_connect (GTK_OBJECT (entry), "changed",
                      GTK_SIGNAL_FUNC (gtm_clwidth_callback),
                      NULL);
  gtk_entry_set_text (GTK_ENTRY (entry), gtmvals.clwidth);
  gimp_help_set_help_data (entry,
			   _("The width for each table cell.  "
			     "Can be a number or a percent."),
			   NULL);

  entry = gtk_entry_new ();
  gtk_widget_set_size_request (entry, 60, -1);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 2,
			     _("Height:"), 1.0, 0.5,
			     entry, 1, TRUE);
  gtk_signal_connect (GTK_OBJECT (entry), "changed",
                      GTK_SIGNAL_FUNC (gtm_clheight_callback),
                      NULL);
  gtk_entry_set_text (GTK_ENTRY (entry), gtmvals.clheight);
  gimp_help_set_help_data (entry,
			   _("The height for each table cell.  "
			     "Can be a number or a percent."),
			   NULL);

  spinbutton = gimp_spin_button_new (&adj, gtmvals.cellpadding,
				     0, 1000, 1, 10, 0, 1, 0);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 3,
			     _("Cell-Padding:"), 1.0, 0.5,
			     spinbutton, 1, TRUE);
  gtk_signal_connect (GTK_OBJECT (adj), "value_changed",
                      GTK_SIGNAL_FUNC (gimp_int_adjustment_update),
                      &gtmvals.cellpadding);
  gimp_help_set_help_data (spinbutton,
			   _("The amount of cellpadding."), NULL);

  spinbutton = gimp_spin_button_new (&adj, gtmvals.cellspacing,
				     0, 1000, 1, 10, 0, 1, 0);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 4,
			     _("Cell-Spacing:"), 1.0, 0.5,
			     spinbutton, 1, TRUE);
  gtk_signal_connect (GTK_OBJECT (adj), "value_changed",
                      GTK_SIGNAL_FUNC (gimp_int_adjustment_update),
                      &gtmvals.cellspacing);
  gimp_help_set_help_data (spinbutton,
			   _("The amount of cellspacing."), NULL);

  gtk_widget_show (table);
  gtk_widget_show (frame);

  gtk_widget_show (dlg);

  gtk_main ();
  gdk_flush ();

  return bint.run;
}

static gint
color_comp (guchar *buffer,
	    guchar *buf2)
{
  if (buffer[0] == buf2[0] && buffer[1] == buf2[1] && buffer[2] == buf2[2])
    return 1;
  else
    return 0;
}  

/*  Save interface functions  */

static void
save_ok_callback (GtkWidget *widget,
		  gpointer   data)
{
  bint.run = TRUE;

  gtk_widget_destroy (GTK_WIDGET (data));
}

static void
gtm_caption_callback (GtkWidget *widget,
		      gpointer   data)
{
  strcpy (gtmvals.captiontxt, gtk_entry_get_text (GTK_ENTRY (widget)));
}

static void
gtm_cellcontent_callback (GtkWidget *widget,
			  gpointer   data)
{
  strcpy (gtmvals.cellcontent, gtk_entry_get_text (GTK_ENTRY (widget)));
}

static void
gtm_clwidth_callback (GtkWidget *widget,
		      gpointer   data)
{
  strcpy (gtmvals.clwidth, gtk_entry_get_text (GTK_ENTRY (widget)));
}

static void
gtm_clheight_callback (GtkWidget *widget,
		       gpointer   data)
{
  strcpy (gtmvals.clheight, gtk_entry_get_text (GTK_ENTRY (widget)));
}
