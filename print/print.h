/*
 * "$Id$"
 *
 *   Print plug-in header file for the GIMP.
 *
 *   Copyright 1997 Michael Sweet (mike@easysw.com)
 *
 *   This program is free software; you can redistribute it and/or modify it
 *   under the terms of the GNU General Public License as published by the Free
 *   Software Foundation; either version 2 of the License, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *   for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Revision History:
 *
 *   $Log$
 *   Revision 1.1  1997/11/24 22:04:34  sopwith
 *   Initial revision
 *
 *   Revision 1.3  1997/11/18 03:04:27  nobody
 *   fixed ugly comment-bugs introduced by evil darkwing
 *   keep out configuration empty dirs
 *   	--darkwing
 *
 *   Revision 1.2  1997/11/17 05:43:57  nobody
 *   updated ChangeLog
 *   dropped non-working doc/Makefile entries
 *   applied many fixes from the registry as well as the devel ML
 *   applied missing patches by Art Haas
 *
 *   	--darkwing
 *
 *   Revision 1.8  1997/11/14  17:17:59  mike
 *   Updated to dynamically allocate return params in the run() function.
 *
 *   Revision 1.7  1997/10/02  17:57:26  mike
 *   Added gamma/dot gain correction values for all printers.
 *
 *   Revision 1.7  1997/10/02  17:57:26  mike
 *   Added gamma/dot gain correction values for all printers.
 *
 *   Revision 1.6  1997/07/30  20:33:05  mike
 *   Final changes for 1.1 release.
 *
 *   Revision 1.5  1997/07/30  18:47:39  mike
 *   Added scaling, orientation, and offset options.
 *
 *   Revision 1.4  1997/07/26  18:43:04  mike
 *   Updated version number to 1.1.
 *
 *   Revision 1.3  1997/07/03  13:26:46  mike
 *   Updated documentation for 1.0 release.
 *
 *   Revision 1.2  1997/07/02  13:51:53  mike
 *   Added rgb_to_rgb and gray_to_gray conversion functions.
 *   Standardized calling args to conversion functions.
 *
 *   Revision 1.2  1997/07/02  13:51:53  mike
 *   Added rgb_to_rgb and gray_to_gray conversion functions.
 *   Standardized calling args to conversion functions.
 *
 *   Revision 1.1  1997/06/19  02:18:15  mike
 *   Initial revision
 */

/*
 * Include necessary header files...
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <libgimp/gimp.h>


/*
 * Constants...
 */

#define PLUG_IN_VERSION		"1.2.2 - 14 November 1997"
#define PLUG_IN_NAME		"Print"

#define MEDIA_LETTER		0	/* 8.5x11" a.k.a. "A" size */
#define MEDIA_LEGAL		1	/* 8.5x14" */
#define MEDIA_TABLOID		2	/* 11x17" a.k.a. "B" size */
#define MEDIA_A4		3	/* 8.27x11.69" (210x297mm) */
#define MEDIA_A3		4	/* 11.69x16.54" (297x420mm) */

#define OUTPUT_GRAY		0	/* Grayscale output */
#define OUTPUT_COLOR		1	/* Color output */

#define ORIENT_AUTO		-1	/* Best orientation */
#define ORIENT_PORTRAIT		0	/* Portrait orientation */
#define ORIENT_LANDSCAPE	1	/* Landscape orientation */

#ifndef MIN
#  define MIN(a,b)		((a) < (b) ? (a) : (b))
#  define MAX(a,b)		((a) > (b) ? (a) : (b))
#endif /* !MIN */


/*
 * Printer driver control structure.  See "print.c" for the actual list...
 */

typedef struct
{
  char	*long_name,			/* Long name for UI */
	*short_name;			/* Short name for printrc file */
  int	xdpi,				/* X resolution */
	ydpi,				/* Y resolution */
	large_sizes,			/* TRUE if supports large sizes */
	color,				/* TRUE if supports color */
	model;				/* Model number */
  float	gamma,				/* Gamma correction */
	density;			/* Ink "density" or black level */
  void	(*print)(FILE *prn, GDrawable *drawable, int media_size,
	         int xdpi, int ydpi, int output_type, int model,
	         guchar *lut, guchar *cmap, int orientation,
	         int scaling, int left, int top);
					/* Print function */
} printer_t;

typedef void (*convert_t)(guchar *in, guchar *out, int width, int bpp, guchar *lut, guchar *cmap);


/*
 * Prototypes...
 */

extern void	dither_black(guchar *, int, int, int, unsigned char *);
extern void	dither_cmyk(guchar *, int, int, int, unsigned char *,
		            unsigned char *, unsigned char *, unsigned char *);
extern void	gray_to_gray(guchar *, guchar *, int, int, guchar *, guchar *);
extern void	indexed_to_gray(guchar *, guchar *, int, int, guchar *, guchar *);
extern void	indexed_to_rgb(guchar *, guchar *, int, int, guchar *, guchar *);
extern int	media_width(int, int);
extern int	media_height(int, int);
extern void	rgb_to_gray(guchar *, guchar *, int, int, guchar *, guchar *);
extern void	rgb_to_rgb(guchar *, guchar *, int, int, guchar *, guchar *);

extern void	escp2_print(FILE *, GDrawable *, int, int, int, int, int, guchar *, guchar *, int, int, int, int);
extern void	pcl_print(FILE *, GDrawable *, int, int, int, int, int, guchar *, guchar *, int, int, int, int);
extern void	ps_print(FILE *, GDrawable *, int, int, int, int, int, guchar *, guchar *, int, int, int, int);


/*
 * End of "$Id$".
 */
