#include "config.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <gtk/gtk.h>

#include <libgimp/gimpui.h>

#include "gimpressionist.h"

#include "libgimp/stdplugins-intl.h"


#define NUMPLACERADIO 2

static GtkWidget *placeradio[NUMPLACERADIO];
GtkWidget *placecenter = NULL;
GtkObject *brushdensityadjust = NULL;

void placechange(int num)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(placeradio[num]), TRUE);
}

void create_placementpage(GtkNotebook *notebook)
{
  GtkWidget *vbox, *hbox, *thispage;
  GtkWidget *label, *tmpw, *table, *frame;

  label = gtk_label_new_with_mnemonic (_("Pl_acement"));

  thispage = gtk_vbox_new(FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (thispage), 5);
  gtk_widget_show(thispage);

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start(GTK_BOX(thispage), vbox,FALSE,FALSE,0);
  gtk_widget_show (vbox);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  frame = gimp_radio_group_new2 (TRUE, _("Placement"),
				 G_CALLBACK (gimp_radio_button_update),
				 &pcvals.placetype, (gpointer) 0,

				 _("Randomly"), 0, &placeradio[0],
				 _("Evenly distributed"), 1, &placeradio[1],
				 NULL);

  gtk_tooltips_set_tip(GTK_TOOLTIPS(tooltips), placeradio[0], 
		       _("Place strokes randomly around the image"), NULL);
  gtk_tooltips_set_tip(GTK_TOOLTIPS(tooltips), placeradio[1], 
		       _("The strokes are evenly distributed across the image")
		       , NULL);
  gtk_box_pack_start(GTK_BOX(hbox), frame, FALSE, FALSE, 0);
  gtk_widget_show(frame);

  gtk_toggle_button_set_active 
    (GTK_TOGGLE_BUTTON (placeradio[pcvals.placetype]), TRUE);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  table = gtk_table_new (1, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE(table), 4);
  gtk_box_pack_start(GTK_BOX(hbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  brushdensityadjust = 
    gimp_scale_entry_new (GTK_TABLE(table), 0, 0, 
			  _("Stroke _density:"),
			  100, -1, pcvals.brushdensity, 
			  1.0, 50.0, 1.0, 5.0, 0, 
			  TRUE, 0, 0,
			  _("The relative density of the brush strokes"),
			  NULL);
  g_signal_connect (brushdensityadjust, "value_changed",
                    G_CALLBACK (gimp_double_adjustment_update),
                    &pcvals.brushdensity);

  placecenter = tmpw = gtk_check_button_new_with_mnemonic( _("Centerize"));
  gtk_box_pack_start(GTK_BOX(vbox), tmpw, FALSE, FALSE, 0);
  gtk_widget_show (tmpw);
  gtk_tooltips_set_tip(GTK_TOOLTIPS(tooltips), tmpw, _("Focus the brush strokes around the center of the image"), NULL);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tmpw), pcvals.placecenter);
    
  gtk_notebook_append_page_menu (notebook, thispage, label, NULL);
}
