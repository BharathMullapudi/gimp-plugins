#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#ifdef __GNUC__
#warning GTK_DISABLE_DEPRECATED
#endif
#undef GTK_DISABLE_DEPRECATED

#include <gtk/gtk.h>

#include "gimpressionist.h"
#include "ppmtool.h"

#include "libgimp/stdplugins-intl.h"

#define NUMVECTYPES 4

static GtkWidget *omwindow;

static GtkWidget *vectorprev;
static GtkWidget *ompreviewprev;
static GtkWidget *prev_button;
static GtkWidget *next_button;
static GtkWidget *add_button;
static GtkWidget *kill_button;

GtkObject *vectprevbrightadjust = NULL;

static GtkObject *angadjust = NULL;
GtkObject *stradjust = NULL;
GtkObject *strexpadjust = NULL;
GtkObject *angoffadjust = NULL;
static GtkWidget *vectypes[NUMVECTYPES];
GtkWidget *orientvoronoi = NULL;

#define OMWIDTH 150
#define OMHEIGHT 150

static vector_t vector[MAXORIENTVECT];
static gint numvect = 0;
static gint vector_type;

static double degtorad(double d)
{
  return d/180.0*G_PI;
}

double radtodeg(double d)
{
  double v = d/G_PI*180.0;
  if(v < 0.0) v += 360;
  return v;
}

double dist(double x, double y, double dx, double dy)
{
  double ax = dx - x;
  double ay = dy - y;
  return sqrt(ax * ax + ay * ay);
}

int pixval(double dir)
{
  while(dir < 0.0) dir += 360.0;
  while(dir >= 360.0) dir -= 360.0;
  return dir * 255.0 / 360.0;
}

double getdir(double x, double y, int from)
{
  int i;
  int n;
  int voronoi;
  double sum, dx, dy, dst;
  vector_t *vec;
  double angoff, strexp;
  int first = 0, last;

  if(from == 0) {
    n = numvect;
    vec = vector;
    angoff = GTK_ADJUSTMENT(angoffadjust)->value;
    strexp = GTK_ADJUSTMENT(strexpadjust)->value;
    voronoi = GTK_TOGGLE_BUTTON(orientvoronoi)->active;
  } else {
    n = pcvals.numorientvector;
    vec = pcvals.orientvector;
    angoff = pcvals.orientangoff;
    strexp = pcvals.orientstrexp;
    voronoi = pcvals.orientvoronoi;
  }

  if(voronoi) {
    double bestdist = -1.0;
    for(i = 0; i < n; i++) {
      dst = dist(x,y,vec[i].x,vec[i].y);
      if((bestdist < 0.0) || (dst < bestdist)) {
	bestdist = dst;
	first = i;
      }
    }
    last = first+1;
  } else {
    first = 0;
    last = n;
  }

  dx = dy = 0.0;
  sum = 0.0;
  for(i = first; i < last; i++) {
    double s = vec[i].str;
    double tx = 0.0, ty = 0.0;
    
    if(vec[i].type == 0) {
      tx = vec[i].dx;
      ty = vec[i].dy;
    } else if(vec[i].type == 1) {
      double a = atan2(vec[i].dy, vec[i].dx);
      a -= atan2(y-vec[i].y, x-vec[i].x);
      tx = sin(a+G_PI_2);
      ty = cos(a+G_PI_2);
    } else if(vec[i].type == 2) {
      double a = atan2(vec[i].dy, vec[i].dx);
      a += atan2(y-vec[i].y, x-vec[i].x);
      tx = sin(a+G_PI_2);
      ty = cos(a+G_PI_2);
    } else if(vec[i].type == 3) {
      double a = atan2(vec[i].dy, vec[i].dx);
      a -= atan2(y-vec[i].y, x-vec[i].x)*2;
      tx = sin(a+G_PI_2);
      ty = cos(a+G_PI_2);
    }

    dst = dist(x,y,vec[i].x,vec[i].y);
    dst = pow(dst, strexp);
    if(dst < 0.0001) dst = 0.0001;
    s = s / dst;

    dx += tx * s;
    dy += ty * s;
    sum += s;
  }
  dx = dx / sum;
  dy = dy / sum;
  return 90-(radtodeg(atan2(dy,dx))+angoff);
}

static void updateompreviewprev(void)
{
  int x, y;
  static ppm_t nbuffer = {0,0,NULL};
  guchar black[3] = {0,0,0};
  guchar gray[3] = {120,120,120};
  guchar white[3] = {255,255,255};

  if(!nbuffer.col) {
    newppm(&nbuffer,OMWIDTH,OMHEIGHT);
  }
  fill(&nbuffer, black);

  for(y = 6; y < OMHEIGHT-4; y += 10)
    for(x = 6; x < OMWIDTH-4; x += 10) {
      double dir = degtorad(getdir(x/(double)OMWIDTH,y/(double)OMHEIGHT,0));
      double xo = sin(dir)*4.0;
      double yo = cos(dir)*4.0;
      drawline(&nbuffer, x-xo, y-yo, x+xo, y+yo, gray);
      putrgb(&nbuffer, x-xo, y-yo, white);
    }

  for(y = 0; y < OMHEIGHT; y++)
    gtk_preview_draw_row(GTK_PREVIEW(ompreviewprev), (guchar *)nbuffer.col + y * OMWIDTH * 3, 0, y, OMWIDTH);
  gtk_widget_draw(ompreviewprev,NULL);

  gtk_widget_set_sensitive (prev_button, (numvect > 1));  
  gtk_widget_set_sensitive (next_button, (numvect > 1));  
  gtk_widget_set_sensitive (add_button, (numvect < MAXORIENTVECT));
  gtk_widget_set_sensitive (kill_button, (numvect > 1));  
}

static int selectedvector = 0;

static void updatevectorprev(void)
{
  static ppm_t backup = {0,0,NULL};
  static ppm_t buffer = {0,0,NULL};
  static int ok = 0;
  int i, x, y;
  double dir, xo, yo;
  double val;
  static double lastval = 0.0;
  guchar gray[3] = {120,120,120};
  guchar red[3] = {255,0,0};
  guchar white[3] = {255,255,255};

  if(vectprevbrightadjust) val = 1.0 - GTK_ADJUSTMENT(vectprevbrightadjust)->value / 100.0;
  else val = 0.5;

  if(!ok || (val != lastval)) {
    if(!infile.col)
      updatepreviewprev(NULL, (void *)2); /* Force grabarea() */
    copyppm(&infile, &backup);
    ppmbrightness(&backup, val, 1,1,1);
    if((backup.width != OMWIDTH) || (backup.height != OMHEIGHT))
      resize_fast(&backup, OMWIDTH, OMHEIGHT);
    ok = 1;
  }
  copyppm(&backup, &buffer);

  for(i = 0; i < numvect; i++) {
    double s;
    x = vector[i].x * OMWIDTH;
    y = vector[i].y * OMHEIGHT;
    dir = degtorad(vector[i].dir);
    s = degtorad(vector[i].str);
    xo = sin(dir)*(6.0+100*s);
    yo = cos(dir)*(6.0+100*s);
    if(i == selectedvector)
      drawline(&buffer, x-xo, y-yo, x+xo, y+yo, red);
    else
      drawline(&buffer, x-xo, y-yo, x+xo, y+yo, gray);
    putrgb(&buffer, x-xo, y-yo, white);
  }

  for(y = 0; y < OMHEIGHT; y++)
    gtk_preview_draw_row(GTK_PREVIEW(vectorprev), (guchar *)buffer.col + y * OMWIDTH * 3, 0, y, OMWIDTH);
  gtk_widget_draw(vectorprev,NULL);

}

static gboolean adjignore = FALSE;

static void updatesliders(void)
{
  gint type;
  adjignore = TRUE;
  gtk_adjustment_set_value(GTK_ADJUSTMENT(angadjust),
			   vector[selectedvector].dir);
  gtk_adjustment_set_value(GTK_ADJUSTMENT(stradjust),
			   vector[selectedvector].str);
  type = vector[selectedvector].type;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vectypes[type]), TRUE);
  adjignore = FALSE;
}

static void prevclick(GtkWidget *w, gpointer data)
{
  selectedvector--;
  if(selectedvector < 0) selectedvector = numvect-1;
  updatesliders();
  updatevectorprev();
}

static void nextclick(GtkWidget *w, gpointer data)
{
  selectedvector++;
  if(selectedvector == numvect) selectedvector = 0;
  updatesliders();
  updatevectorprev();
}

static void add_new_vector (gdouble x, gdouble y)
{
  vector[numvect].x = x;
  vector[numvect].y = y;
  vector[numvect].dir = 0.0;
  vector[numvect].dx = sin(degtorad(0.0));
  vector[numvect].dy = cos(degtorad(0.0));
  vector[numvect].str = 1.0;
  vector[numvect].type = 0;
  selectedvector = numvect;
  numvect++;
}

static void addclick(GtkWidget *w, gpointer data)
{
  add_new_vector (0.5, 0.5);
  updatesliders();
  updatevectorprev();
  updateompreviewprev();
}

static void deleteclick(GtkWidget *w, gpointer data)
{
  int i;

  for (i = selectedvector; i < numvect-1; i++) {
    vector[i] = vector[i + 1];
  }
  numvect--;

  if (selectedvector >= numvect) selectedvector = 0;
  updatesliders();
  updatevectorprev();
  updateompreviewprev();
}

static void mapclick(GtkWidget *w, GdkEventButton *event)
{
  if (event->button == 1) {
    vector[selectedvector].x = event->x / (double)OMWIDTH;
    vector[selectedvector].y = event->y / (double)OMHEIGHT;

  } else if (event->button == 2) {
    if (numvect + 1 == MAXORIENTVECT) return;
    add_new_vector (event->x / (double)OMWIDTH, event->y / (double)OMHEIGHT);
    updatesliders();

  } else if (event->button == 3) {
    double d;
    d = atan2(OMWIDTH * vector[selectedvector].x - event->x,
	      OMHEIGHT * vector[selectedvector].y - event->y);
    vector[selectedvector].dir = radtodeg(d);
    vector[selectedvector].dx = sin(d);
    vector[selectedvector].dy = cos(d);
    updatesliders();
  }
  updatevectorprev();
  updateompreviewprev();
}

static void angadjmove(GtkWidget *w, gpointer data)
{
  if (adjignore) return;
  vector[selectedvector].dir = GTK_ADJUSTMENT(angadjust)->value;
  vector[selectedvector].dx = sin(degtorad(vector[selectedvector].dir));
  vector[selectedvector].dy = cos(degtorad(vector[selectedvector].dir));
  updatevectorprev();
  updateompreviewprev();
}

static void stradjmove(GtkWidget *w, gpointer data)
{
  if (adjignore) return;
  vector[selectedvector].str = GTK_ADJUSTMENT(stradjust)->value;
  updatevectorprev();
  updateompreviewprev();
}

static void strexpadjmove(GtkWidget *w, gpointer data)
{
  if (adjignore) return;
  updatevectorprev();
  updateompreviewprev();
}

static void angoffadjmove(GtkWidget *w, gpointer data)
{
  if (adjignore) return;
  updatevectorprev();
  updateompreviewprev();
}

static void vectypeclick(GtkWidget *w, gpointer data)
{
  if (adjignore) return;

  gimp_radio_button_update (w, data);
  vector[selectedvector].type = vector_type;
  updatevectorprev();
  updateompreviewprev();
}

static void omapplyclick(GtkWidget *w, gpointer data)
{
  int i;
  for(i = 0; i < numvect; i++) {
    pcvals.orientvector[i] = vector[i];
  }
  pcvals.numorientvector = numvect;
  pcvals.orientstrexp = GTK_ADJUSTMENT(strexpadjust)->value;
  pcvals.orientangoff = GTK_ADJUSTMENT(angoffadjust)->value;
  pcvals.orientvoronoi = GTK_TOGGLE_BUTTON(orientvoronoi)->active;
}

static void omokclick(GtkWidget *w, gpointer data)
{
  omapplyclick(NULL, NULL);
  gtk_widget_hide(w);
}

static void initvectors(void)
{
  if (pcvals.numorientvector) {
    int i;

    numvect = pcvals.numorientvector;
    for(i = 0; i < numvect; i++) {
      vector[i] = pcvals.orientvector[i];
    }
  } else {/* Shouldn't happen */
    numvect = 0;
    add_new_vector (0.5, 0.5);
  }
  if (selectedvector >= numvect)
    selectedvector = numvect-1;
}

void update_orientmap_dialog(void)
{
  if(!omwindow) return;

  initvectors();

  gtk_adjustment_set_value(GTK_ADJUSTMENT(strexpadjust), pcvals.orientstrexp);
  gtk_adjustment_set_value(GTK_ADJUSTMENT(angoffadjust), pcvals.orientangoff);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(orientvoronoi), pcvals.orientvoronoi);

  updatevectorprev();
  updateompreviewprev();
}

void create_orientmap_dialog(void)
{
  GtkWidget *tmpw, *tmpw2;
  GtkWidget *table1, *table2;
  GtkWidget *frame;
  GtkWidget *ebox, *hbox, *vbox;

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (orientradio[7]), TRUE);

  initvectors();

  if (omwindow) {
    updatevectorprev();
    updateompreviewprev();
    gtk_widget_show(omwindow);
    return;
  }

  omwindow =
    gimp_dialog_new (_("Orientation Map Editor"), "gimpressionist",
		     gimp_standard_help_func, "filters/gimpressionst.html",
		     GTK_WIN_POS_MOUSE,
		     FALSE, TRUE, FALSE,

		     GTK_STOCK_APPLY, omapplyclick,
		     NULL, NULL, NULL, FALSE, FALSE,

		     GTK_STOCK_CANCEL, gtk_widget_hide,
		     NULL, 1, NULL, FALSE, FALSE,

		     GTK_STOCK_OK, omokclick,
		     NULL, 1, NULL, TRUE, FALSE,

		     NULL);

  g_signal_connect (G_OBJECT(omwindow), "destroy",
		    G_CALLBACK(gtk_widget_destroyed), &omwindow);
  g_signal_connect (G_OBJECT(omwindow), "delete_event",
		    G_CALLBACK(gtk_widget_hide_on_delete), &omwindow);

  table1 = gtk_table_new(2, 5, FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (table1), 6);
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (omwindow)->vbox), table1);
  gtk_widget_show(table1);

  frame = gtk_frame_new( _("Vectors"));
  gtk_container_set_border_width (GTK_CONTAINER (frame), 2);
  gtk_table_attach(GTK_TABLE(table1), frame, 0, 1, 0, 1,
		   GTK_EXPAND,GTK_EXPAND, 0, 0);
  gtk_widget_show(frame);

  hbox = gtk_hbox_new(FALSE, 0);
  gtk_container_add(GTK_CONTAINER(frame), hbox);
  gtk_widget_show(hbox);

  ebox = gtk_event_box_new();
  gtk_tooltips_set_tip(GTK_TOOLTIPS(tooltips), ebox, _("The vector-field. Left-click to move selected vector, Right-click to point it towards mouse, Middle-click to add a new vector."), NULL);
  gtk_box_pack_start(GTK_BOX(hbox), ebox, FALSE, FALSE, 0);

  tmpw = vectorprev = gtk_preview_new(GTK_PREVIEW_COLOR);
  gtk_preview_size(GTK_PREVIEW(tmpw), OMWIDTH, OMHEIGHT);
  gtk_container_add(GTK_CONTAINER(ebox), tmpw);
  gtk_widget_show(tmpw);
  gtk_widget_set_events(ebox, GDK_BUTTON_PRESS_MASK);
  gtk_signal_connect(GTK_OBJECT(ebox), "button_press_event",
                     GTK_SIGNAL_FUNC(mapclick), NULL);
  gtk_widget_realize(ebox);
  gtk_widget_show(ebox);

  vectprevbrightadjust = gtk_adjustment_new(50.0, 0.0, 100.0, 1.0, 1.0, 1.0);
  tmpw = gtk_vscale_new(GTK_ADJUSTMENT(vectprevbrightadjust));
  gtk_scale_set_draw_value (GTK_SCALE (tmpw), FALSE);
  gtk_box_pack_start(GTK_BOX(hbox), tmpw,FALSE,FALSE,0);
  gtk_widget_show(tmpw);
  gtk_signal_connect(GTK_OBJECT(vectprevbrightadjust), "value_changed",
                     (GtkSignalFunc)updatevectorprev, NULL);
  gtk_tooltips_set_tip(GTK_TOOLTIPS(tooltips), tmpw, 
		       _("Adjust the preview's brightness"), NULL);

  tmpw2 = tmpw = gtk_frame_new( _("Preview"));
  gtk_container_set_border_width (GTK_CONTAINER (tmpw), 2);
  gtk_table_attach(GTK_TABLE(table1), tmpw, 1,2,0,1,GTK_EXPAND,GTK_EXPAND,0,0);
  gtk_widget_show(tmpw);

  tmpw = ompreviewprev = gtk_preview_new(GTK_PREVIEW_COLOR);
  gtk_preview_size(GTK_PREVIEW(tmpw), OMWIDTH, OMHEIGHT);
  gtk_container_add(GTK_CONTAINER(tmpw2), tmpw);
  gtk_widget_show(tmpw);

  hbox = tmpw = gtk_hbox_new(TRUE,0);
  gtk_container_set_border_width (GTK_CONTAINER (tmpw), 2);
  gtk_table_attach_defaults(GTK_TABLE(table1), tmpw, 0,1,1,2);
  gtk_widget_show(tmpw);

  prev_button = tmpw = gtk_button_new_with_mnemonic("_<<");
  gtk_box_pack_start(GTK_BOX(hbox),tmpw,FALSE,TRUE,0);
  gtk_widget_show(tmpw);
  g_signal_connect (G_OBJECT(tmpw), "clicked", G_CALLBACK(prevclick), NULL);
  gtk_tooltips_set_tip(GTK_TOOLTIPS(tooltips), tmpw, 
		       _("Select previous vector"), NULL);

  next_button = tmpw = gtk_button_new_with_mnemonic("_>>");
  gtk_box_pack_start(GTK_BOX(hbox),tmpw,FALSE,TRUE,0);
  gtk_widget_show(tmpw);
  g_signal_connect (G_OBJECT(tmpw), "clicked", G_CALLBACK(nextclick), NULL);
  gtk_tooltips_set_tip(GTK_TOOLTIPS(tooltips), tmpw, 
		       _("Select next vector"), NULL);

  add_button = tmpw = gtk_button_new_with_mnemonic( _("A_dd"));
  gtk_box_pack_start(GTK_BOX(hbox), tmpw, FALSE, TRUE, 0);
  gtk_widget_show(tmpw);
  g_signal_connect (G_OBJECT(tmpw), "clicked", G_CALLBACK(addclick), NULL);
  gtk_tooltips_set_tip(GTK_TOOLTIPS(tooltips), tmpw, 
		       _("Add new vector"), NULL);

  kill_button = tmpw = gtk_button_new_with_mnemonic( _("_Kill"));
  gtk_box_pack_start(GTK_BOX(hbox), tmpw, FALSE, TRUE, 0);
  gtk_widget_show(tmpw);
  g_signal_connect (G_OBJECT(tmpw), "clicked", G_CALLBACK(deleteclick), NULL);
  gtk_tooltips_set_tip(GTK_TOOLTIPS(tooltips), tmpw, 
		       _("Delete selected vector"), NULL);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_set_spacing (GTK_BOX(hbox), 12);
  gtk_table_attach_defaults(GTK_TABLE(table1), hbox, 0, 2, 2, 3);
  gtk_widget_show (hbox);

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add(GTK_CONTAINER(hbox), vbox);
  gtk_widget_show (vbox);

  frame = gimp_radio_group_new2 (TRUE, _("Type"),
				 G_CALLBACK (vectypeclick),
				 &vector_type, (gpointer) 0,

				 _("_Normal"), 0, &vectypes[0],
				 _("Vorte_x"), 1, &vectypes[1],
				 _("Vortex_2"), 2, &vectypes[2],
				 _("Vortex_3"), 3, &vectypes[3],
				 NULL);
  gtk_container_add(GTK_CONTAINER(vbox), frame);
  gtk_widget_show(frame);

  orientvoronoi = tmpw = gtk_check_button_new_with_mnemonic( _("_Voronoi"));
  gtk_container_add(GTK_CONTAINER(vbox), tmpw);
  gtk_widget_show (tmpw);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tmpw), pcvals.orientvoronoi);
  g_signal_connect(G_OBJECT(tmpw), "clicked", G_CALLBACK(angoffadjmove), NULL);
  gtk_tooltips_set_tip(GTK_TOOLTIPS(tooltips), tmpw, _("Voronoi-mode makes only the vector closest to the given point have any influence"), NULL);

  table2 = gtk_table_new(4, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE(table2), 4);
  gtk_container_add(GTK_CONTAINER(hbox), table2);
  gtk_widget_show(table2);

  angadjust = 
    gimp_scale_entry_new (GTK_TABLE(table2), 0, 0, 
			  _("A_ngle:"),
			  150, 6, 0.0, 
			  0.0, 360.0, 1.0, 10.0, 1, 
			  TRUE, 0, 0,
			  _("Change the angle of the selected vector"),
			  NULL);
  g_signal_connect (angadjust, "value_changed", G_CALLBACK (angadjmove), NULL);

  angoffadjust = 
    gimp_scale_entry_new (GTK_TABLE(table2), 0, 1, 
			  _("Ang_le offset:"),
			  150, 6, 0.0, 
			  0.0, 360.0, 1.0, 10.0, 1, 
			  TRUE, 0, 0,
			  _("Offset all vectors with a given angle"),
			  NULL);
  g_signal_connect (angoffadjust, "value_changed", 
		    G_CALLBACK (angoffadjmove), NULL);

  stradjust = 
    gimp_scale_entry_new (GTK_TABLE(table2), 0, 2, 
			  _("_Strength:"),
			  150, 6, 1.0, 
			  0.1, 5.0, 0.1, 1.0, 1,
			  TRUE, 0, 0,
			  _("Change the strength of the selected vector"),
			  NULL);
  g_signal_connect (stradjust, "value_changed", G_CALLBACK (stradjmove), NULL);

  strexpadjust = 
    gimp_scale_entry_new (GTK_TABLE(table2), 0, 3, 
			  _("S_trength exp.:"),
			  150, 6, 1.0, 
			  0.1, 10.9, 0.1, 1.0, 1,
			  TRUE, 0, 0,
			  _("Change the exponent of the strength"),
			  NULL);
  g_signal_connect (strexpadjust, "value_changed", 
		    G_CALLBACK (strexpadjmove), NULL);

  gtk_widget_show(omwindow);

  updatevectorprev();
  updateompreviewprev();
}
