#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#define HAVE_DIRENT_H
#define HAVE_UNISTD_H
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <gtk/gtk.h>

#include <libgimp/gimp.h>

#include "gimpressionist.h"
#include "ppmtool.h"

#include <libgimp/stdplugins-intl.h>

static gimpressionist_vals_t runningvals;

static void prepbrush(ppm_t *p)
{
  int x, y;
  int rowstride = p->width * 3;

  for (y = 0; y< p->height; y++) {
    for (x = 0; x < p->width; x++) {
      p->col[y*rowstride+x*3+1] = 0;
    }
  }

  for (y = 1; y< p->height; y++) {
    for (x = 1; x < p->width; x++) {
      int v = p->col[y*rowstride+x*3] - p->col[(y-1)*rowstride+(x-1)*3];
      if(v < 0) v = 0;
      p->col[y*rowstride+x*3+1] = v;
    }
  }
}

static double sumbrush(ppm_t *p)
{
  double sum = 0;
  int i;

  for(i = 0; i < p->width*3*p->height; i += 3)
    sum += p->col[i];
  return sum;
}

static int gethue(guchar *rgb)
{
  double h, v, temp, diff;
  if((rgb[0] == rgb[1]) && (rgb[0] == rgb[2])) /* Gray */
    return 0;
  v = (rgb[0] > rgb[1] ? rgb[0] : rgb[1]);     /* v = st<F8>rste verdi */
  if(rgb[2] > v) v = rgb[2];
  temp = (rgb[0] > rgb[1] ? rgb[1] : rgb[0] ); /* temp = minste */
  if(rgb[2] < temp) temp = rgb[2];
  diff = v - temp;

  if(v == rgb[0])
    h = ((double)rgb[1] - rgb[2]) / diff;
  else if(v == rgb[1])
    h = ((double)rgb[2] - rgb[0]) / diff + 2;
  else /* v == rgb[2] */
    h = ((double)rgb[0] - rgb[1]) / diff + 4;
  if(h < 0) h += 6;
  return h * 255.0 / 6.0;
}

static int bestbrush(ppm_t *p, ppm_t *a, int tx, int ty,
		     ppm_t *brushes, int numbrush, double *brushsum,
		     int start, int step)
{
  double dev, thissum;
  double bestdev = 0.0;
  double r, g, b;
  int best = -1;
  int x, y, h;
  long i;
  GList *brlist = NULL;

  for(i = start; i < numbrush; i += step) {
    ppm_t *brush = &brushes[i];
    /* thissum = 0.0; */
    thissum = brushsum[i];

    r = g = b = 0.0;
    for(y = 0; y < brush->height; y++) {
      guchar *row = p->col + (ty+y)*p->width*3;
      for(x = 0; x < brush->width; x++) {
	int k = (tx+x)*3;
	double v;
	if((h = brush->col[(y*brush->width*3)+x*3])) {
	  /* thissum += h; */
	  v = h / 255.0;
	  r += row[k+0] * v;
	  g += row[k+1] * v;
	  b += row[k+2] * v;
	}
      }
    }
    r = r * 255.0 / thissum;
    g = g * 255.0 / thissum;
    b = b * 255.0 / thissum;

    dev = 0.0;
    for(y = 0; y < brush->height; y++) {
      guchar *row = p->col + (ty+y)*p->width*3;
      for(x = 0; x < brush->width; x++) {
	int k = (tx+x)*3;
	double v;
	if((h = brush->col[(y*brush->width*3)+x*3])) {
	  v = h / 255.0;
	  dev += abs(row[k+0] - r) * v;
	  dev += abs(row[k+1] - g) * v;
	  dev += abs(row[k+2] - b) * v;
	  if(img_has_alpha)
	    dev += a->col[(ty+y)*a->width*3+(tx+x)*3] * v;
	}
      }
    }
    dev /= thissum;

    if((best == -1) || (dev < bestdev)) {
      if(brlist)
	g_list_free(brlist);
      brlist = NULL;
    }
    
    if(dev <= bestdev || best < 0) {
      best = i;
      bestdev = dev;
      brlist = g_list_append(brlist, (void *)i);
    }
    if(dev < runningvals.devthresh) break;
  }

  if(!brlist) {
    fprintf(stderr, "What!? No brushes?!\n");
    return 0;
  }

  i = g_rand_int_range (gr, 0, g_list_length(brlist));
  best = (long)((g_list_nth(brlist,i))->data);
  g_list_free(brlist);

  return best;
}

static void applybrush(ppm_t *brush,
		       ppm_t *shadow,
		       ppm_t *p, ppm_t *a,
		       int tx, int ty, int r, int g, int b)
{
  ppm_t tmp;
  ppm_t atmp;
  double v, h;
  int x, y;
  double edgedarken = 1.0 - runningvals.generaldarkedge;
  double relief = runningvals.brushrelief / 100.0;
  int shadowdepth = pcvals.generalshadowdepth;
  int shadowblur = pcvals.generalshadowblur;

  tmp = *p;
  if(img_has_alpha)
    atmp = *a;

  if (shadow) {
    int sx = tx + shadowdepth - shadowblur*2;
    int sy = ty + shadowdepth - shadowblur*2;
    for(y = 0; y < shadow->height; y++) {
      guchar *row, *arow = NULL;
      if((sy + y) < 0) continue;
      if((sy + y) >= tmp.height) break;
      row = tmp.col + (sy+y) * tmp.width * 3;
      if(img_has_alpha) arow = atmp.col + (sy+y) * atmp.width * 3;
      for(x = 0; x < shadow->width; x++) {
	int k = (sx + x) * 3;
	if((sx + x) < 0) continue;
	if((sx + x) >= tmp.width) break;
	h = shadow->col[y*shadow->width*3+x*3+2];
	if(!h) continue;
	v = 1.0 - (h / 255.0 * runningvals.generalshadowdarkness / 100.0);
	row[k+0] *= v;
	row[k+1] *= v;
	row[k+2] *= v;
	if(img_has_alpha) arow[k] *= v;
      }
    }
  }

  for(y = 0; y < brush->height; y++) {
    guchar *row = tmp.col + (ty+y)*tmp.width*3;
    guchar *arow = NULL;
    if(img_has_alpha) arow = atmp.col + (ty+y)*atmp.width*3;
    for(x = 0; x < brush->width; x++) {
      int k = (tx + x) * 3;
      h = brush->col[y*brush->width*3+x*3];
      if(!h) continue;

      if(runningvals.colorbrushes) {
	v = 1.0 - brush->col[y*brush->width*3+x*3+2] / 255.0;
	row[k+0] *= v;
	row[k+1] *= v;
	row[k+2] *= v;
	if(img_has_alpha) arow[(tx+x)*3] *= v;
      }
      v = (1.0 - h / 255.0) * edgedarken;
      row[k+0] *= v;
      row[k+1] *= v;
      row[k+2] *= v;
      if(img_has_alpha) arow[k] *= v;

      v = h / 255.0;
      row[k+0] += r * v;
      row[k+1] += g * v;
      row[k+2] += b * v;
    }
  }

  if(relief > 0.001) {
    for(y = 1; y < brush->height; y++) {
      guchar *row = tmp.col + (ty+y)*tmp.width*3;
      for(x = 1; x < brush->width; x++) {
	int k = (tx + x) * 3;
	h = brush->col[y*brush->width*3+x*3+1] * relief;
	if(h < 0.001) continue;
	if(h > 255) h = 255;
	row[k+0] = (row[k+0] * (255-h) + 255 * h) / 255;
	row[k+1] = (row[k+1] * (255-h) + 255 * h) / 255;
	row[k+2] = (row[k+2] * (255-h) + 255 * h) / 255;
      }
    }
  }
}

void repaint(ppm_t *p, ppm_t *a)
{
  int x, y;
  int tx = 0, ty = 0;
  ppm_t tmp = {0,0,NULL};
  ppm_t atmp = {0,0,NULL};
  int r, g, b, n, h, i, j, on, sn;
  int numbrush, maxbrushwidth, maxbrushheight;
  guchar back[3] = {0,0,0};
  ppm_t *brushes, *shadows;
  ppm_t *brush, *shadow = NULL;
  double *brushsum;
  int cx, cy, maxdist;
  double scale, relief, startangle, anglespan, density, bgamma;
  double thissum;
  int max_progress;
  ppm_t paperppm = {0,0,NULL};
  ppm_t dirmap = {0,0,NULL};
  ppm_t sizmap = {0,0,NULL};
  int *xpos = NULL, *ypos = NULL;
  int step = 1;
  int progstep;
  static int running = 0;

  int dropshadow = pcvals.generaldropshadow;
  int shadowblur = pcvals.generalshadowblur;

  if (running)
    return;
  running++;

  runningvals = pcvals;

  /* Shouldn't be necessary, but... */
  if(img_has_alpha)
    if((p->width != a->width) || (p->height != a->height)) {
      fprintf(stderr, "Huh? Image size != alpha size?\n");
      return;
    }

  numbrush = runningvals.orientnum * runningvals.sizenum;
  startangle = runningvals.orientfirst;
  anglespan = runningvals.orientlast;

  density = runningvals.brushdensity;

  if(runningvals.placetype == 1) density /= 3.0;

  bgamma = runningvals.brushgamma;

  brushes = g_malloc (numbrush * sizeof(ppm_t));
  brushsum = g_malloc (numbrush * sizeof(double));

  if(dropshadow)
    shadows = g_malloc (numbrush * sizeof(ppm_t));
  else
    shadows = NULL;

  brushes[0].col = NULL;
  if(brushfile)
    reloadbrush(runningvals.selectedbrush, &brushes[0]);
  else
    copyppm(&brushppm, &brushes[0]);

  resize(&brushes[0], brushes[0].width, brushes[0].height * pow(10,runningvals.brushaspect));
  scale = runningvals.sizelast / MAX(brushes[0].width, brushes[0].height);

  if(bgamma != 1.0)
    ppmgamma(&brushes[0], 1.0/bgamma, 1,1,1);
  
  resize(&brushes[0], brushes[0].width * scale, brushes[0].height * scale);
  i = 1 + sqrt(brushes[0].width * brushes[0].width +
	       brushes[0].height * brushes[0].height);
  pad(&brushes[0], i-brushes[0].width, i-brushes[0].width,
      i-brushes[0].height, i-brushes[0].height, back);
  
  for(i = 1; i < numbrush; i++) {
    brushes[i].col = NULL;
    copyppm(&brushes[0], &brushes[i]);
  }

  for(i = 0; i < runningvals.sizenum; i++) {
    double sv;
    if(runningvals.sizenum > 1)
      sv = i / (runningvals.sizenum - 1.0);
    else sv = 1.0;
    for(j = 0; j < runningvals.orientnum; j++) {
      h = j + i * runningvals.orientnum;
      freerotate(&brushes[h],
		 startangle + j * anglespan / runningvals.orientnum);
      rescale(&brushes[h], (sv * runningvals.sizefirst + (1.0-sv) * runningvals.sizelast) / runningvals.sizelast);
      autocrop(&brushes[h],1);
    } 
  }

  /* Brush-debugging */
  /*
  for(i = 0; i < numbrush; i++) {
    char tmp[1000];
    sprintf(tmp, "/tmp/_brush%03d.ppm", i);
    saveppm(&brushes[i], tmp);
  }
  */

  for(i = 0; i < numbrush; i++) {
    if(!runningvals.colorbrushes)
      prepbrush(&brushes[i]);
    brushsum[i] = sumbrush(&brushes[i]);
  }

  brush = &brushes[0];
  thissum = brushsum[0];

  maxbrushwidth = maxbrushheight = 0;
  for(i = 0; i < numbrush; i++) {
    if(brushes[i].width > maxbrushwidth) maxbrushwidth = brushes[i].width;
    if(brushes[i].height > maxbrushheight) maxbrushheight = brushes[i].height;
  }
  
  for(i = 0; i < numbrush; i++) {
    int xp, yp;
    guchar blk[3] = {0,0,0};

    xp = maxbrushwidth - brushes[i].width;
    yp = maxbrushheight - brushes[i].height;
    if(xp || yp)
      pad(&brushes[i], xp/2, xp-xp/2, yp/2, yp-yp/2, blk);
  }

  if(dropshadow) {
    for(i = 0; i < numbrush; i++) {
      shadows[i].col = NULL;
      copyppm(&brushes[i], &shadows[i]);
      ppmgamma(&shadows[i], 0, 1,1,0);
      pad(&shadows[i], shadowblur*2, shadowblur*2, 
	  shadowblur*2, shadowblur*2, back);
      for(j = 0; j < shadowblur; j++)
	blur(&shadows[i], 2, 2);
      /* autocrop(&shadows[i],1); */
    }
    /*
    maxbrushwidth += shadowdepth*3;
    maxbrushheight += shadowdepth*3;
    */
  }
  
  /* For extra annoying debugging :-) */
  /*
  saveppm(brushes, "/tmp/__brush.ppm");
  if(shadows) saveppm(shadows, "/tmp/__shadow.ppm");
  system("xv /tmp/__brush.ppm & xv /tmp/__shadow.ppm & ");
  */

  if(runningvals.generalpaintedges) {
    edgepad(p, maxbrushwidth, maxbrushwidth, maxbrushheight, maxbrushheight);
    if(img_has_alpha)
      edgepad(a, maxbrushwidth, maxbrushwidth, maxbrushheight, maxbrushheight);
  }

  if(img_has_alpha) {
    /* Initially fully transparent */
    if(runningvals.generalbgtype == 3) {
      guchar tmpcol[3] = {255,255,255};
      newppm(&atmp, a->width, a->height);
      fill(&atmp, tmpcol);
    } else {
      copyppm(a, &atmp);
    }
  }

  if(runningvals.generalbgtype == 0) {
    guchar tmpcol[3];
    newppm(&tmp, p->width, p->height);
    gimp_rgb_get_uchar(&runningvals.color, &tmpcol[0], &tmpcol[1], &tmpcol[2]);
    fill(&tmp, tmpcol);
  } else if(runningvals.generalbgtype == 1) {
    copyppm(p, &tmp);
  } else {
    scale = runningvals.paperscale / 100.0;
    newppm(&tmp, p->width, p->height);
    loadppm(runningvals.selectedpaper, &paperppm);
    resize(&paperppm, paperppm.width * scale, paperppm.height * scale);
    if(runningvals.paperinvert)
      ppmgamma(&paperppm, -1.0, 1, 1, 1);
    for(x = 0; x < tmp.width; x++) {
      int rx = x % paperppm.width;
      for(y = 0; y < tmp.height; y++) {
	int ry = y % paperppm.height;
	memcpy(&tmp.col[y*tmp.width*3+x*3], &paperppm.col[ry*paperppm.width*3+rx*3], 3);
      }
    }
  }

  cx = p->width / 2;
  cy = p->height / 2;
  maxdist = sqrt(cx*cx+cy*cy);

  if(runningvals.orienttype == 0) { /* Value */
    newppm(&dirmap, p->width, p->height);
    for(y = 0; y < dirmap.height; y++) {
      guchar *dstrow = &dirmap.col[y*dirmap.width*3];
      guchar *srcrow = &p->col[y*p->width*3];
      for(x = 0; x < dirmap.width; x++) {
	dstrow[x*3] = (srcrow[x*3] + srcrow[x*3+1] + srcrow[x*3+2]) / 3;
      }
    }
  } else if(runningvals.orienttype == 1) { /* Radius */
    newppm(&dirmap, p->width, p->height);
    for(y = 0; y < dirmap.height; y++) {
      guchar *dstrow = &dirmap.col[y*dirmap.width*3];
      double ysqr = (cy-y)*(cy-y);
      for(x = 0; x < dirmap.width; x++) {
	dstrow[x*3] = sqrt((cx-x)*(cx-x)+ysqr) * 255 / maxdist;
      }
    }
  } else if(runningvals.orienttype == 3) { /* Radial */
    newppm(&dirmap, p->width, p->height);
    for(y = 0; y < dirmap.height; y++) {
      guchar *dstrow = &dirmap.col[y*dirmap.width*3];
      for(x = 0; x < dirmap.width; x++) {
	dstrow[x*3] = (G_PI + atan2(cy-y, cx-x)) * 255.0 / (G_PI*2);
      }
    }
  } else if(runningvals.orienttype == 4) { /* Flowing */
    newppm(&dirmap, p->width / 6 + 5, p->height / 6 + 5);
    mkgrayplasma(&dirmap, 15);
    blur(&dirmap, 2, 2);
    blur(&dirmap, 2, 2);
    resize(&dirmap, p->width, p->height);
    blur(&dirmap, 2, 2);
    if(runningvals.generalpaintedges)
      edgepad(&dirmap, maxbrushwidth, maxbrushheight,maxbrushwidth, maxbrushheight);
  } else if(runningvals.orienttype == 5) { /* Hue */
    newppm(&dirmap, p->width, p->height);
    for(y = 0; y < dirmap.height; y++) {
      guchar *dstrow = &dirmap.col[y*dirmap.width*3];
      guchar *srcrow = &p->col[y*p->width*3];
      for(x = 0; x < dirmap.width; x++) {
	dstrow[x*3] = gethue(&srcrow[x*3]);
      }
    }
  } else if(runningvals.orienttype == 6) {
    guchar tmpcol[3] = {0,0,0};
    newppm(&dirmap, p->width, p->height);
    fill(&dirmap, tmpcol);

  } else if(runningvals.orienttype == 7) { /* Manual */
    newppm(&dirmap, p->width-maxbrushwidth*2, p->height-maxbrushheight*2);
    for(y = 0; y < dirmap.height; y++) {
      guchar *dstrow = &dirmap.col[y*dirmap.width*3];
      double tmpy = y / (double)dirmap.height;
      for(x = 0; x < dirmap.width; x++) {
	dstrow[x*3] = pixval(90-getdir(x / (double)dirmap.width, tmpy, 1));
      }
    }
    edgepad(&dirmap, maxbrushwidth, maxbrushwidth, maxbrushheight, maxbrushheight);
  }

  if(runningvals.sizetype == 0) { /* Value */
    newppm(&sizmap, p->width, p->height);
    for(y = 0; y < sizmap.height; y++) {
      guchar *dstrow = &sizmap.col[y*sizmap.width*3];
      guchar *srcrow = &p->col[y*p->width*3];
      for(x = 0; x < sizmap.width; x++) {
	dstrow[x*3] = (srcrow[x*3] + srcrow[x*3+1] + srcrow[x*3+2]) / 3;
      }
    }
  } else if(runningvals.sizetype == 1) { /* Radius */
    newppm(&sizmap, p->width, p->height);
    for(y = 0; y < sizmap.height; y++) {
      guchar *dstrow = &sizmap.col[y*sizmap.width*3];
      double ysqr = (cy-y)*(cy-y);
      for(x = 0; x < sizmap.width; x++) {
	dstrow[x*3] = sqrt((cx-x)*(cx-x)+ysqr) * 255 / maxdist;
      }
    }
  } else if(runningvals.sizetype == 3) { /* Radial */
    newppm(&sizmap, p->width, p->height);
    for(y = 0; y < sizmap.height; y++) {
      guchar *dstrow = &sizmap.col[y*sizmap.width*3];
      for(x = 0; x < sizmap.width; x++) {
	dstrow[x*3] = (G_PI + atan2(cy-y, cx-x)) * 255.0 / (G_PI*2);
      }
    }
  } else if(runningvals.sizetype == 4) { /* Flowing */
    newppm(&sizmap, p->width / 6 + 5, p->height / 6 + 5);
    mkgrayplasma(&sizmap, 15);
    blur(&sizmap, 2, 2);
    blur(&sizmap, 2, 2);
    resize(&sizmap, p->width, p->height);
    blur(&sizmap, 2, 2);
    if(runningvals.generalpaintedges)
      edgepad(&sizmap, maxbrushwidth, maxbrushheight,maxbrushwidth, maxbrushheight);
  } else if(runningvals.sizetype == 5) { /* Hue */
    newppm(&sizmap, p->width, p->height);
    for(y = 0; y < sizmap.height; y++) {
      guchar *dstrow = &sizmap.col[y*sizmap.width*3];
      guchar *srcrow = &p->col[y*p->width*3];
      for(x = 0; x < sizmap.width; x++) {
	dstrow[x*3] = gethue(&srcrow[x*3]);
      }
    }
  } else if(runningvals.sizetype == 6) {
    guchar tmpcol[3] = {0,0,0};
    newppm(&sizmap, p->width, p->height);
    fill(&sizmap, tmpcol);

  } else if(runningvals.sizetype == 7) { /* Manual */
    newppm(&sizmap, p->width-maxbrushwidth*2, p->height-maxbrushheight*2);
    for(y = 0; y < sizmap.height; y++) {
      guchar *dstrow = &sizmap.col[y*sizmap.width*3];
      double tmpy = y / (double)sizmap.height;
      for(x = 0; x < sizmap.width; x++) {
	dstrow[x*3] = 255 * (1.0 - getsiz(x / (double)sizmap.width, tmpy, 1));
      }
    }
    edgepad(&sizmap, maxbrushwidth, maxbrushwidth, maxbrushheight, maxbrushheight);
  }
  /*
  saveppm(&sizmap, "/tmp/_sizmap.ppm");
  */
  if(runningvals.placetype == 0) {
    i = tmp.width * tmp.height / (maxbrushwidth * maxbrushheight);
    i *= density;
  } else if(runningvals.placetype == 1) {
    i = (int)(tmp.width * density / maxbrushwidth) *
      (int)(tmp.height * density / maxbrushheight);
    step = i;
    /* fprintf(stderr, "step=%d i=%d\n", step, i); */
  }
  if(i < 1) i = 1;

  max_progress = i;
  progstep = max_progress / 30;
  if(progstep < 10) progstep = 10;

  if(runningvals.placetype == 1) {
    int j;
    xpos = g_new (int, i);
    ypos = g_new (int, i);
    for(j = 0; j < i; j++) {
      int factor = (int)(tmp.width * density / maxbrushwidth + 0.5);
      if(factor < 1) factor = 1;
      xpos[j] = maxbrushwidth/2 + (j % factor) * maxbrushwidth / density;
      ypos[j] = maxbrushheight/2 + (j / factor) * maxbrushheight / density;
    }
    for(j = 0; j < i; j++) {
      int a, b;
      a = g_rand_int_range (gr, 0, i);
      b = xpos[j]; xpos[j] = xpos[a]; xpos[a] = b;
      b = ypos[j]; ypos[j] = ypos[a]; ypos[a] = b;
    }
  }

  for(; i; i--) {
    if(i % progstep == 0) {
      if(runningvals.run) {
	if(!standalone)
	  gimp_progress_update(0.8 - 0.8*((double)i / max_progress));
	else {
	  fprintf(stderr, "."); fflush(stderr);
	}
      } else {
	char tmps[40];
	sprintf(tmps, "%.1f %%", 100 * (1.0 - ((double)i / max_progress)));
	gtk_label_set_text(GTK_LABEL(GTK_BIN(previewbutton)->child), tmps);
	while(gtk_events_pending())
	  gtk_main_iteration();
      }
    }

    if(runningvals.placetype == 0) {
      tx = g_rand_int_range (gr, maxbrushwidth/2, 
                             tmp.width - maxbrushwidth/2);
      ty = g_rand_int_range (gr, maxbrushheight/2, 
                             tmp.height - maxbrushheight/2);
    } else if(runningvals.placetype == 1) {
      tx = xpos[i-1];
      ty = ypos[i-1];
    }
    if(runningvals.placecenter) {
      double z = g_rand_double_range (gr, 0, 0.75);
      tx = tx * (1.0-z) + tmp.width/2 * z;
      ty = ty * (1.0-z) + tmp.height/2 * z;
    }

    if((tx < maxbrushwidth/2) || (ty < maxbrushwidth/2) ||
       (tx + maxbrushwidth/2 >= p->width) ||
       (ty + maxbrushheight/2 >= p->height)) {
      /*
      fprintf(stderr, "Internal Error; invalid coords: (%d,%d) i=%d\n", tx, ty, i);
      */
      continue;
    }

    if(img_has_alpha) {
      if(a->col[ty*a->width*3+tx*3] > 128)
	continue;
    }

    n = sn = on = 0;

    switch(runningvals.orienttype) {
    case 2: /* Random */
      on = g_rand_int_range (gr, 0, runningvals.orientnum);
      break;
    case 0: /* Value */
    case 1: /* Radius */
    case 3: /* Radial */
    case 4: /* Flowing */
    case 5: /* Hue */
    case 7: /* Manual */
      on = runningvals.orientnum * dirmap.col[ty*dirmap.width*3+tx*3] / 256;
      break;
    case 6: /* Adaptive */
      break; /* Handled below */
    default:
      fprintf(stderr, "Internal error; Unknown orientationtype\n");
      on = 0;
      break;
    }

    switch(runningvals.sizetype) {
    case 2: /* Random */
      sn = g_rand_int_range (gr, 0, runningvals.sizenum);
      break;
    case 0: /* Value */
    case 1: /* Radius */
    case 3: /* Radial */
    case 4: /* Flowing */
    case 5: /* Hue */
    case 7: /* Manual */
      sn = runningvals.sizenum * sizmap.col[ty*sizmap.width*3+tx*3] / 256;
      break;
    case 6: /* Adaptive */
      break; /* Handled below */
    default:
      fprintf(stderr, "Internal error; Unknown sizetype\n");
      sn = 0;
      break;
    }

    /* Handle Adaptive selections */
    if((runningvals.orienttype == 6) && (runningvals.sizetype == 6)) {
      n = bestbrush(p, a, tx-maxbrushwidth/2, ty-maxbrushheight/2,
		    brushes, numbrush, brushsum, 0, 1);
    } else if(runningvals.orienttype == 6) {
      int st = sn * runningvals.orientnum;
      n = bestbrush(p, a, tx-maxbrushwidth/2, ty-maxbrushheight/2,
		    brushes, st+runningvals.orientnum, brushsum, st, 1);
    } else if(runningvals.sizetype == 6) {
      n = bestbrush(p, a, tx-maxbrushwidth/2, ty-maxbrushheight/2,
		    brushes, numbrush, brushsum, on, runningvals.orientnum);
    } else {
      n = sn * runningvals.orientnum + on;
    }

    /* Should never happen, but hey... */
    if(n < 0) n = 0;
    else if(n >= numbrush) n = numbrush - 1;

    tx -= maxbrushwidth/2;
    ty -= maxbrushheight/2;

    brush = &brushes[n];
    if(dropshadow)
      shadow = &shadows[n];
    thissum = brushsum[n];

    /* Calculate color - avg. of in-brush pixels */
    if(runningvals.colortype == 0) {
      r = g = b = 0;
      for(y = 0; y < brush->height; y++) {
	guchar *row = &p->col[(ty+y)*p->width*3];
	for(x = 0; x < brush->width; x++) {
	  int k = (tx+x) * 3;
	  double v;
	  if((h = brush->col[y*brush->width*3+x*3])) {
	    v = h / 255.0;
	    r += row[k+0] * v;
	    g += row[k+1] * v;
	    b += row[k+2] * v;
	  }
	}
      }
      r = r * 255.0 / thissum;
      g = g * 255.0 / thissum;
      b = b * 255.0 / thissum;
    } else if(runningvals.colortype == 1) {
      guchar *pixel;
      y = ty + (brush->height / 2);
      x = tx + (brush->width / 2);
      pixel = &p->col[y*p->width*3 + x*3];
      r = pixel[0];
      g = pixel[1];
      b = pixel[2];
    } else {
      /* No such colortype! */
      r = g = b = 0;
    }
    if(runningvals.colornoise > 0.0) {
      double v = runningvals.colornoise;
      r = r + g_rand_double_range (gr, -v/2.0, v/2.0);
      g = g + g_rand_double_range (gr, -v/2.0, v/2.0);
      b = b + g_rand_double_range (gr, -v/2.0, v/2.0);
      if(r < 0) r = 0; else if(r > 255) r = 255;
      if(g < 0) g = 0; else if(g > 255) g = 255;
      if(b < 0) b = 0; else if(b > 255) b = 255;
    }

    applybrush(brush, shadow, &tmp, &atmp, tx,ty, r,g,b);
    if(runningvals.generaltileable && runningvals.generalpaintedges) {
      int origwidth = tmp.width - 2 * maxbrushwidth;
      int origheight = tmp.height - 2 * maxbrushheight;
      int dox = 0, doy = 0;
      if(tx < maxbrushwidth) {
	applybrush(brush, shadow, &tmp, &atmp, tx+origwidth,ty, r,g,b);
	dox = -1;
      } else if(tx > origwidth) {
	applybrush(brush, shadow, &tmp, &atmp, tx-origwidth,ty, r,g,b);
	dox = 1;
      }
      if(ty < maxbrushheight) {
	applybrush(brush, shadow, &tmp, &atmp, tx,ty+origheight, r,g,b);
	doy = 1;
      } else if(ty > origheight) {
	applybrush(brush, shadow, &tmp, &atmp, tx,ty-origheight, r,g,b);
	doy = -1;
      }
      if(doy) {
	if(dox < 0)
	  applybrush(brush, shadow, &tmp, &atmp, tx+origwidth,ty+doy*origheight,r,g,b);
	if(dox > 0)
	  applybrush(brush, shadow, &tmp, &atmp, tx-origwidth,ty+doy*origheight,r,g,b);
      }
    }
  }
  for(i = 0; i < numbrush; i++) {
    killppm(&brushes[i]);
  }
  g_free(brushes);
  g_free(shadows);
  g_free(brushsum);

  g_free (xpos);
  g_free (ypos);

  if(runningvals.generalpaintedges) {
    crop(&tmp, maxbrushwidth, maxbrushheight, tmp.width - maxbrushwidth, tmp.height - maxbrushheight);
    if(img_has_alpha)
      crop(&atmp, maxbrushwidth, maxbrushheight, atmp.width - maxbrushwidth, atmp.height - maxbrushheight);
  }

  killppm(p);
  p->width = tmp.width;
  p->height = tmp.height;
  p->col = tmp.col;

  if(img_has_alpha) {
    killppm(a);
    a->width = atmp.width;
    a->height = atmp.height;
    a->col = atmp.col;
  }

  relief = runningvals.paperrelief / 100.0;
  if(relief > 0.001) {
    scale = runningvals.paperscale / 100.0;

    if(paperppm.col) {
      tmp = paperppm;
      paperppm.col = NULL;
    } else {
      tmp.col = NULL;
      loadppm(runningvals.selectedpaper, &tmp);
      resize(&tmp, tmp.width * scale, tmp.height * scale);
      if(runningvals.paperinvert)
	ppmgamma(&tmp, -1.0, 1,1,1);
    }
    for(x = 0; x < p->width; x++) {
      double h, v;
      int px = x % tmp.width, py;
      for(y = 0; y < p->height; y++) {
	int k = y * p->width * 3 + x * 3;
	py = y % tmp.height;
	if(runningvals.paperoverlay)
	  h = (tmp.col[py*tmp.width*3+px*3]-128) * relief;
	else
	  h = (tmp.col[py*tmp.width*3+px*3] - (int)tmp.col[((py+1)%tmp.height)*tmp.width*3+((px+1)%tmp.width)*3]) / -2.0 * relief;
	if(h <= 0.0) {
	  v = 1.0 + h/128.0;
	  if(v < 0.0) v = 0.0; else if(v > 1.0) v = 1.0;
	  p->col[k+0] *= v;
	  p->col[k+1] *= v;
	  p->col[k+2] *= v;
	} else {
	  v = h/128.0;
	  if(v < 0.0) v = 0.0; else if(v > 1.0) v = 1.0;
	  p->col[k+0] = p->col[k+0] * (1.0-v) + 255 * v;
	  p->col[k+1] = p->col[k+1] * (1.0-v) + 255 * v;
	  p->col[k+2] = p->col[k+2] * (1.0-v) + 255 * v;
	}
      }
    }
    killppm(&tmp);
  }

  if(paperppm.col) killppm(&paperppm);
  if(dirmap.col) killppm(&dirmap);
  if(sizmap.col) killppm(&sizmap);
  if(runningvals.run) {
    if(!standalone)
      gimp_progress_update(0.8);
    else {
      fprintf(stderr, ".\n"); fflush(stderr);
    }
  } else {
    gtk_label_set_text(GTK_LABEL(GTK_BIN(previewbutton)->child), _("Update"));
  }
  running = 0;
}
