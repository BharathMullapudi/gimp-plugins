/* WMF plug-in for The GIMP
 * Copyright (C) 1998 Tor Lillqvist
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
 * See http://www.iki.fi/tml/gimp/wmf/
 */

#define VERSION "1999-10-30"

/* #define DEBUG */

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __GNUC__
#warning FIXME: GDK_DISABLE_DEPRECATED
#warning FIXME: GDK_MULTIHEAD_SAFE
#endif

#undef GDK_DISABLE_DEPRECATED
#undef GDK_MULTIHEAD_SAFE

#include <gtk/gtk.h>

#define GDK_DISABLE_DEPRECATED

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


typedef guchar  BYTE;
typedef guint16 WORD;
typedef guint32 DWORD;

typedef gint16 SHORT;
typedef gint32 LONG;

/* The following information is from O'Reilly's updates to the
 * Encyclopedia of Graphics File Formats,
 * http://www.ora.com/centers/gff/formats/micmeta/index.htm
 */

typedef struct _WindowsMetaHeader
{
  WORD  FileType;       /* Type of metafile (0=memory, 1=disk) */
  WORD  HeaderSize;     /* Size of header in WORDS (always 9) */
  WORD  Version;        /* Version of Microsoft Windows used */
  DWORD FileSize;       /* Total size of the metafile in WORDs */
  WORD  NumOfObjects;   /* Number of objects in the file */
  DWORD MaxRecordSize;  /* The size of largest record in WORDs */
  WORD  NumOfParams;    /* Not Used (always 0) */
} WMFHEAD;

#define SIZE_WMFHEAD 18

typedef struct _PlaceableMetaHeader
{
  DWORD Key;           /* Magic number (always 9AC6CDD7h) */
  WORD  Handle;        /* Metafile HANDLE number (always 0) */
  SHORT Left;          /* Left coordinate in metafile units */
  SHORT Top;           /* Top coordinate in metafile units */
  SHORT Right;         /* Right coordinate in metafile units */
  SHORT Bottom;        /* Bottom coordinate in metafile units */
  WORD  Inch;          /* Number of metafile units per inch */
  DWORD Reserved;      /* Reserved (always 0) */
  WORD  Checksum;      /* Checksum value for previous 10 WORDs */
} PLACEABLEMETAHEADER;

#define SIZE_PLACEABLEMETAHEADER 22

typedef struct _Clipboard16MetaHeader
{
  SHORT MappingMode; /* Units used to playback metafile */
  SHORT Width;       /* Width of the metafile */
  SHORT Height;      /* Height of the metafile */
  WORD  Handle;      /* Handle to the metafile in memory */
} CLIPBOARD16METAHEADER;

#define SIZE_CLIPBOARD16METAHEADER 8

typedef struct _Clipboard32MetaHeader
{
  LONG  MappingMode; /* Units used to playback metafile */
  LONG  Width;       /* Width of the metafile */
  LONG  Height;      /* Height of the metafile */
  DWORD Handle;      /* Handle to the metafile in memory */
} CLIPBOARD32METAHEADER;

#define SIZE_CLIPBOARD32METAHEADER 16

typedef struct _EnhancedMetaHeader
{
  DWORD RecordType;       /* Record type */
  DWORD RecordSize;       /* Size of the record in bytes */
  LONG  BoundsLeft;       /* Left inclusive bounds */
  LONG  BoundsRight;      /* Right inclusive bounds */
  LONG  BoundsTop;        /* Top inclusive bounds */
  LONG  BoundsBottom;     /* Bottom inclusive bounds */
  LONG  FrameLeft;        /* Left side of inclusive picture frame */
  LONG  FrameRight;       /* Right side of inclusive picture frame */
  LONG  FrameTop;         /* Top side of inclusive picture frame */
  LONG  FrameBottom;      /* Bottom side of inclusive picture frame */
  DWORD Signature;        /* Signature ID (always 0x464D4520) */
  DWORD Version;          /* Version of the metafile */
  DWORD Size;             /* Size of the metafile in bytes */
  DWORD NumOfRecords;     /* Number of records in the metafile */
  WORD  NumOfHandles;     /* Number of handles in the handle table */
  WORD  Reserved;         /* Not used (always 0) */
  DWORD SizeOfDescrip;    /* Size of description string in WORDs */
  DWORD OffsOfDescrip;    /* Offset of description string in metafile */
  DWORD NumPalEntries;    /* Number of color palette entries */
  LONG  WidthDevPixels;   /* Width of reference device in pixels */
  LONG  HeightDevPixels;  /* Height of reference device in pixels */
  LONG  WidthDevMM;       /* Width of reference device in millimeters */
  LONG  HeightDevMM;      /* Height of reference device in millimeters */
} ENHANCEDMETAHEADER;

#define SIZE_ENHANCEDMETAHEADER 88

typedef struct _StandardMetaRecord
{
  DWORD Size;          /* Total size of the record in WORDs */
  WORD  Function;      /* Function number (defined in WINDOWS.H) */
#if DOCUMENTATION_ONLY_ILLEGAL_C
  WORD  Parameters[]; /* Parameter values passed to function */
#endif
} WMFRECORD;

#define SIZE_WMFRECORD 6
#define WORDSIZE_WMFRECORD 3

#define EndOfFile		0x0000

#define AbortDoc		0x0052
#define Arc			0x0817
#define Chord			0x0830
#define Ellipse			0x0418
#define EndDoc			0x005E
#define EndPage			0x0050
#define ExcludeClipRect		0x0415
#define ExtFloodFill		0x0548
#define FillRegion		0x0228
#define FloodFill		0x0419
#define FrameRegion		0x0429
#define IntersectClipRect	0x0416
#define InvertRegion		0x012A
#define LineTo			0x0213
#define MoveTo			0x0214
#define OffsetClipRgn		0x0220
#define OffsetViewportOrg	0x0211
#define OffsetWindowOrg		0x020F
#define PaintRegion		0x012B
#define PatBlt			0x061D
#define Pie			0x081A
#define RealizePalette		0x0035
#define Rectangle		0x041B
#define ResetDc			0x014C
#define ResizePalette		0x0139
#define RestoreDC		0x0127
#define RoundRect		0x061C
#define SaveDC			0x001E
#define ScaleViewportExt	0x0412
#define ScaleWindowExt		0x0410
#define SelectClipRegion	0x012C
#define SelectObject		0x012D
#define SelectPalette		0x0234
#define SetBkColor		0x0201
#define SetBkMode		0x0102
#define SetDibToDev		0x0d33
#define SetMapMode		0x0103
#define SetMapperFlags		0x0231
#define SetPalEntries		0x0037
#define SetPixel		0x041F
#define SetPolyFillMode		0x0106
#define SetRelabs		0x0105
#define SetROP2			0x0104
#define SetStretchBltMode	0x0107
#define SetTextAlign		0x012E
#define SetTextCharExtra	0x0108
#define SetTextColor		0x0209
#define SetTextJustification	0x020A
#define SetViewportExt		0x020E
#define SetViewportOrg		0x020D
#define SetWindowExt		0x020C
#define SetWindowOrg		0x020B
#define StartDoc		0x014D
#define StartPage		0x004F

#define AnimatePalette		0x0436
#define BitBlt			0x0922
#define CreateBitmap		0x06FE
#define CreateBitmapIndirect	0x02FD
#define CreateBrush		0x00F8
#define CreateBrushIndirect	0x02FC
#define CreateFontIndirect	0x02FB
#define CreatePalette		0x00F7
#define CreatePatternBrush	0x01F9
#define CreatePenIndirect	0x02FA
#define CreateRegion		0x06FF
#define DeleteObject		0x01F0
#define DibBitblt		0x0940
#define DibCreatePatternBrush	0x0142
#define DibStretchBlt		0x0B41
#define DrawText		0x062F
#define Escape			0x0626
#define ExtTextOut		0x0A32
#define Polygon			0x0324
#define PolyPolygon		0x0538
#define Polyline		0x0325
#define TextOut			0x0521
#define StretchBlt		0x0B23
#define StretchDIBits		0x0F43

typedef struct _RGBTriple
{
  BYTE Red;
  BYTE Green;
  BYTE Blue;
} RGBTRIPLE;

typedef struct _BitBltRecord
{
  DWORD     Size;             /* Total size of the record in WORDs */
  WORD      Function;         /* Function number (0x0922) */
  WORD      RasterOp;         /* High-order word for the raster operation */
  WORD      YSrcOrigin;       /* Y-coordinate of the source origin */
  WORD      XSrcOrigin;       /* X-coordinate of the source origin */
  WORD      YDest;            /* Destination width */
  WORD      XDest;            /* Destination height */
  WORD      YDestOrigin;      /* Y-coordinate of the destination origin */
  WORD      XDestOrigin;      /* X-coordinate of the destination origin */
  /* DDB Bitmap */
  DWORD     Width;            /* Width of bitmap in pixels */
  DWORD     Height;           /* Height of bitmap in scan lines */
  DWORD     BytesPerLine;     /* Number of bytes in each scan line */
  WORD      NumColorPlanes;   /* Number of color planes in the bitmap */
  WORD      BitsPerPixel;     /* Number of bits in each pixel */
#if DOCUMENTATION_ONLY_ILLEGAL_C
  RGBTRIPLE Bitmap[];         /* Bitmap data */
#endif
} BITBLTRECORD;

typedef struct _RGBQuad
{
  BYTE Red;
  BYTE Green;
  BYTE Blue;
  BYTE Reserved;
} RGBQUAD;

typedef struct _DibBitBltRecord
{
  DWORD   Size;             /* Total size of the record in WORDs */
  WORD    Function;         /* Function number (0x0940) */
  WORD    RasterOp;         /* High-order word for the raster operation */
  WORD    YSrcOrigin;       /* Y-coordinate of the source origin */
  WORD    XSrcOrigin;       /* X-coordinate of the source origin */
  WORD    YDest;            /* Destination width */
  WORD    XDest;            /* Destination height */
  WORD    YDestOrigin;      /* Y-coordinate of the destination origin */
  WORD    XDestOrigin;      /* X-coordinate of the destination origin */
  /* DIB Bitmap */
  DWORD   Width;            /* Width of bitmap in pixels */
  DWORD   Height;           /* Height of bitmap in scan lines */
  DWORD   BytesPerLine;     /* Number of bytes in each scan line */
  WORD    NumColorPlanes;   /* Number of color planes in the bitmap */
  WORD    BitsPerPixel;     /* Number of bits in each pixel */
  DWORD   Compression;      /* Compression type */
  DWORD   SizeImage;        /* Size of bitmap in bytes */
  LONG    XPelsPerMeter;    /* Width of image in pixels per meter */
  LONG    YPelsPerMeter;    /* Height of image in pixels per meter */
  DWORD   ClrUsed;          /* Number of colors used */
  DWORD   ClrImportant;     /* Number of important colors */
#if DOCUMENTATION_ONLY_ILLEGAL_C
  RGBQUAD Bitmap[];         /* Bitmap data */
#endif
} DIBBITBLTRECORD;

typedef struct _EnhancedMetaRecord
{
  DWORD Function;      /* Function number (defined in WINGDI.H) */
  DWORD Size;          /* Total size of the record in WORDs */
#if DOCUMENTATION_ONLY_ILLEGAL_C
  DWORD Parameters[];   /* Parameter values passed to GDI function */
#endif
} EMFRECORD;

#define EMR_ABORTPATH		68
#define EMR_POLYLINE		4
#define EMR_ANGLEARC		41
#define EMR_POLYLINE16		87
#define EMR_ARC			45
#define EMR_POLYLINETO		6
#define EMR_ARCTO		55
#define EMR_POLYLINETO16	89
#define EMR_BEGINPATH		59
#define EMR_POLYPOLYGON		8
#define EMR_BITBLT		76
#define EMR_POLYPOLYGON16	91
#define EMR_CHORD		46
#define EMR_POLYPOLYLINE	7
#define EMR_CLOSEFIGURE		61
#define EMR_POLYPOLYLINE16	90
#define EMR_CREATEBRUSHINDIRECT	39
#define EMR_POLYTEXTOUTA	96
#define EMR_CREATEDIBPATTERNBRUSHPT 94
#define EMR_POLYTEXTOUTW	97
#define EMR_CREATEMONOBRUSH	93
#define EMR_REALIZEPALETTE	52
#define EMR_CREATEPALETTE	49
#define EMR_RECTANGLE		43

#define EMR_CREATEPEN		38
#define EMR_RESIZEPALETTE	51
#define EMR_DELETEOBJECT	40
#define EMR_RESTOREDC		34
#define EMR_ELLIPSE		42
#define EMR_ROUNDRECT		44
#define EMR_ENDPATH		60
#define EMR_SAVEDC		33
#define EMR_EOF			14
#define EMR_SCALEVIEWPORTEXTEX	31
#define EMR_EXCLUDECLIPRECT	29
#define EMR_SCALEWINDOWEXTEX	32
#define EMR_EXTCREATEFONTINDIRECTW 82
#define EMR_SELECTCLIPPATH	67
#define EMR_EXTCREATEPEN	95
#define EMR_SELECTOBJECT	37
#define EMR_EXTFLOODFILL	53
#define EMR_SELECTPALETTE	48
#define EMR_EXTSELECTCLIPRGN	75
#define EMR_SETARCDIRECTION	57
#define EMR_EXTTEXTOUTA		83
#define EMR_SETBKCOLOR		25

#define EMR_EXTTEXTOUTW		84
#define EMR_SETBKMODE		18
#define EMR_FILLPATH		62
#define EMR_SETBRUSHORGEX	13
#define EMR_FILLRGN		71
#define EMR_SETCOLORADJUSTMENT	23
#define EMR_FLATTENPATH		65
#define EMR_SETDIBITSTODEVICE	80
#define EMR_FRAMERGN		72
#define EMR_SETMAPMODE		17
#define EMR_GDICOMMENT		70
#define EMR_SETMAPPERFLAGS	16
#define EMR_HEADER		1
#define EMR_SETMETARGN		28
#define EMR_INTERSECTCLIPRECT	30
#define EMR_SETMITERLIMIT	58
#define EMR_INVERTRGN		73
#define EMR_SETPALETTEENTRIES	50
#define EMR_LINETO		54
#define EMR_SETPIXELV		15
#define EMR_MASKBLT		78
#define EMR_SETPOLYFILLMODE	19
#define EMR_MODIFYWORLDTRANSFORM 36
#define EMR_SETROP2		20

#define EMR_MOVETOEX		27
#define EMR_SETSTRETCHBLTMODE	21
#define EMR_OFFSETCLIPRGN	26
#define EMR_SETTEXTALIGN	22
#define EMR_PAINTRGN		74
#define EMR_SETTEXTCOLOR	24
#define EMR_PIE			47
#define EMR_SETVIEWPORTEXTEX	11
#define EMR_PLGBLT		79
#define EMR_SETVIEWPORTORGEX	12
#define EMR_POLYBEZIER		2
#define EMR_SETWINDOWEXTEX	9
#define EMR_POLYBEZIER16	85
#define EMR_SETWINDOWORGEX	10
#define EMR_POLYBEZIERTO	5
#define EMR_SETWORLDTRANSFORM	35
#define EMR_POLYBEZIERTO16	88
#define EMR_STRETCHBLT		77
#define EMR_POLYDRAW		56
#define EMR_STRETCHDIBITS	81
#define EMR_POLYDRAW16		92
#define EMR_STROKEANDFILLPATH	63

#define EMR_POLYGON		3
#define EMR_STROKEPATH		64
#define EMR_POLYGON16		86
#define EMR_WIDENPATH		66

typedef struct _PaletteEntry
{
  BYTE Red;       /* Red component value */
  BYTE Green;     /* Green component value */
  BYTE Blue;      /* Blue component value */
  BYTE Flags;     /* Flag values */
} PALENT;

typedef struct _EndOfRecord
{
  DWORD  Function;        /* End Of Record ID (14) */
  DWORD  Size;            /* Total size of the record in WORDs */
  DWORD  NumPalEntries;   /* Number of color palette entries */
  DWORD  OffPalEntries;   /* Offset of color palette entries */
#if DOCUMENTATION_ONLY_ILLEGAL_C
  PALENT Palette[];       /* The color palette data */
  DWORD  OffToEOF;        /* Offset to beginning of this record */
#endif
} ENDOFRECORD;

typedef struct _GdiCommentRecord
{
  DWORD   Function;      /* GDI Comment ID (70) */
  DWORD   Size;          /* Total size of the record in WORDs */
  DWORD   SizeOfData;    /* Size of comment data in bytes */
#if DOCUMENTATION_ONLY_ILLEGAL_C
  BYTE    Data[];        /* Comment data */
#endif
} GDICOMMENTRECORD;

typedef struct _GdiCommentMetafile
{
  DWORD Identifier;       /* Comment ID (0x43494447) */
  DWORD Comment;          /* Metafile ID (0x80000001) */
  DWORD Version;          /* Version of the metafile */
  DWORD Checksum;         /* Checksum value of the metafile */
  DWORD Flags;            /* Flags (always 0) */
  DWORD Size;             /* Size of the metafile data in bytes */
} GDICOMMENTMETAFILE;

typedef struct _GdiCommentBeginGroup
{
  DWORD Identifier;       /* Comment ID (0x43494447) */
  DWORD Comment;          /* BeginGroup ID (0x00000002) */
  LONG  BoundsLeft;       /* Left side of bounding rectangle */
  LONG  BoundsRight;      /* Right side of bounding rectangle */
  LONG  BoundsTop;        /* Top side of bounding rectangle */
  LONG  BoundsBottom;     /* Bottom side of bounding rectangle */
  DWORD SizeOfDescrip;    /* Number of characters in the description */
} GDICOMMENTBEGINGROUP;

typedef struct _GdiCommentEndGroup
{
  DWORD Identifier;       /* Comment ID (0x43494447) */
  DWORD Comment;          /* EndGroup ID (0x00000003) */
} GDICOMMENTENDGROUP;

typedef struct _EmrFormat
{
  DWORD Signature;    /* Format signature */
  DWORD Version;      /* Format version number */
  DWORD Data;         /* Size of data in bytes */
  DWORD OffsetToData; /* Offset to data */
} EMRFORMAT;

typedef struct _GdiCommentMultiFormats
{
  DWORD Identifier;       /* Comment ID (0x43494447) */
  DWORD Comment;          /* Multiformats ID (0x40000004) */
  LONG  BoundsLeft;       /* Left side of bounding rectangle */
  LONG  BoundsRight;      /* Right side of bounding rectangle */
  LONG  BoundsTop;        /* Top side of bounding rectangle */
  LONG  BoundsBottom;     /* Bottom side of bounding rectangle */
  DWORD NumFormats;       /* Number of formats in comment */
#if DOCUMENTATION_ONLY_ILLEGAL_C
  EMRFORMAT Data[];       /* Array of comment data */
#endif
} GDICOMMENTMULTIFORMATS;

/* Binary raster ops */
#define R2_BLACK            1
#define R2_NOTMERGEPEN      2
#define R2_MASKNOTPEN       3
#define R2_NOTCOPYPEN       4
#define R2_MASKPENNOT       5
#define R2_NOT              6
#define R2_XORPEN           7
#define R2_NOTMASKPEN       8
#define R2_MASKPEN          9
#define R2_NOTXORPEN       10
#define R2_NOP             11
#define R2_MERGENOTPEN     12
#define R2_COPYPEN         13
#define R2_MERGEPENNOT     14
#define R2_MERGEPEN        15
#define R2_WHITE           16

/* Background mix modes */
#define TRANSPARENT	    1
#define OPAQUE		    2

/* Brush styles */
#define BS_SOLID            0
#define BS_NULL             1
#define BS_HATCHED          2
#define BS_PATTERN          3
#define BS_DIBPATTERN       5
#define BS_DIBPATTERNPT     6
#define BS_PATTERN8X8       7
#define BS_DIBPATTERN8X8    8
#define BS_MONOPATTERN      9

/* Pen styles */
#define PS_SOLID            0
#define PS_DASH             1
#define PS_DOT              2
#define PS_DASHDOT          3
#define PS_DASHDOTDOT       4
#define PS_NULL             5
#define PS_INSIDEFRAME      6

/* Polygon fill modes */
#define ALTERNATE	    1
#define WINDING		    2

/* Modes for SetMapMode */
#define MM_TEXT             1
#define MM_LOMETRIC         2
#define MM_HIMETRIC         3
#define MM_LOENGLISH        4
#define MM_HIENGLISH        5
#define MM_TWIPS            6
#define MM_ISOTROPIC        7
#define MM_ANISOTROPIC      8


#define ReadOK(file,buffer,len) (fread(buffer, len, 1, file) != 0)

#define NPARMWORDS 16


typedef struct
{
  gdouble scale;
} WMFLoadVals;

static WMFLoadVals load_vals =
{
  1.0				/* scale */
};

typedef struct
{
  gboolean  run;
} WMFLoadInterface;

static WMFLoadInterface load_interface =
{
  FALSE
};

typedef struct
{
  GtkWidget     *dialog;
  GtkAdjustment *scale;
} LoadDialogVals;

typedef enum
{
  OBJ_BITMAP,
  OBJ_BRUSH,
  OBJ_PATTERNBRUSH,
  OBJ_FONT,
  OBJ_PEN,
  OBJ_REGION,
  OBJ_PALETTE
} ObjectType;

typedef struct
{
  int dummy;
} BitmapObject;

typedef struct
{
  GdkColor color;
  gboolean invisible;
  guint style;
  glong hatch;
} BrushObject;

typedef struct
{
  int dummy;
} PatternBrushObject;

typedef struct
{
  GdkColor color;
  gboolean invisible;
  gushort width;
  GdkLineStyle style;
} PenObject;

typedef struct
{
  GdkFont *font;
} FontObject;

typedef struct
{
  int dummy;
} PaletteObject;

typedef struct
{
  ObjectType type;
  union
  {
    BitmapObject bitmap;
    BrushObject brush;
    PatternBrushObject pbrush;
    PenObject pen;
    FontObject font;
    PaletteObject palette;
  } u;
} Object;

typedef struct
{
  GdkGC *gc;
  GdkColor bg;
  BrushObject *brush;
  PenObject *pen;
  FontObject *font;
  GdkColor textColor;
  gint tag;
} DC;

static gint saved_dc_tag = 1;

typedef struct
{
  GdkPixmap *pixmap;
  DC dc;
  GSList *dc_stack;
  GdkColormap *colormap;
  guint width, height;
  double scalex, scaley;
  double curx, cury;
} Canvas;

typedef struct
{
  gboolean valid;
  gint org_x, org_y;
  gint ext_x, ext_y;
} OrgAndExt;

static void   query      (void);
static void   run        (const gchar      *name,
                          gint              nparams,
                          const GimpParam  *param,
                          gint             *nreturn_vals,
                          GimpParam       **return_vals);
static gint32 load_image (const gchar      *filename);

static gint readparams (DWORD  size,
			guint  nparams,
			FILE  *fd,
			WORD  *params);

static void sync_record (DWORD  size,
			 guint  nparams,
			 FILE  *fd);

GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};

static GimpRunMode l_run_mode;

static int pixs_per_in;

static void
load_ok_callback (GtkWidget *widget,
                  gpointer   data)

{
  LoadDialogVals *vals = (LoadDialogVals *)data;

  /* Read scale */
  load_vals.scale = pow (2.0, vals->scale->value);

  load_interface.run = TRUE;
  gtk_widget_destroy (GTK_WIDGET (vals->dialog));
}

static gint
load_dialog (gchar *file_name)
{
  LoadDialogVals *vals;
  GtkWidget *frame;
  GtkWidget *vbox;
  GtkWidget *label;
  GtkWidget *table;
  GtkWidget *slider;

  gimp_ui_init ("wmf", FALSE);

  vals = g_new (LoadDialogVals, 1);

  vals->dialog = gimp_dialog_new ( _("Load Windows Metafile"), "wmf",
				  gimp_standard_help_func, "filters/wmf.html",
				  GTK_WIN_POS_MOUSE,
				  FALSE, TRUE, FALSE,

				  GTK_STOCK_CANCEL, gtk_widget_destroy,
				  NULL, 1, NULL, FALSE, TRUE,

				  GTK_STOCK_OK, load_ok_callback,
				  vals, NULL, NULL, TRUE, FALSE,

				  NULL);

  g_signal_connect (vals->dialog, "destroy",
                    G_CALLBACK (gtk_main_quit),
                    NULL);

  /* Rendering */
  frame = gtk_frame_new (g_strdup_printf ( _("Rendering %s"), file_name));
  gtk_container_set_border_width (GTK_CONTAINER (frame), 6);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (vals->dialog)->vbox), frame,
		      TRUE, TRUE, 0);

  vbox = gtk_vbox_new (FALSE, 4);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 4);
  gtk_container_add (GTK_CONTAINER (frame), vbox);

  /* Scale label */
  table = gtk_table_new (1, 2, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  label = gtk_label_new ( _("Scale (log 2):"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 1.0);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (label);

  /* Scale slider */
  vals->scale = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, -2.0, 2.0, 0.2, 0.2, 0.0));
  slider = gtk_hscale_new (vals->scale);
  gtk_table_attach (GTK_TABLE (table), slider, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
  gtk_scale_set_value_pos (GTK_SCALE (slider), GTK_POS_TOP);
  gtk_range_set_update_policy (GTK_RANGE (slider), GTK_UPDATE_DELAYED);
  gtk_widget_show (slider);

  gtk_widget_show (vbox);
  gtk_widget_show (frame);

  gtk_widget_show (vals->dialog);

  gtk_main ();
  gdk_flush ();

  g_free (vals);

  return load_interface.run;
}

static void
check_load_vals (void)
{
  if (load_vals.scale < 0.01)
    load_vals.scale = 0.01;
  else if (load_vals.scale > 100.)
    load_vals.scale = 100.;
}

MAIN ()

static void
query (void)
{
  static GimpParamDef load_args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_STRING, "filename", "The name of the file to load" },
    { GIMP_PDB_STRING, "raw_filename", "The name entered" }
  };

  static GimpParamDef load_return_vals[] =
  {
    { GIMP_PDB_IMAGE, "image", "Output image" }
  };

  static GimpParamDef load_setargs_args[] =
  {
    { GIMP_PDB_FLOAT, "scale", "Scale in which to load image" }
  };

  gimp_install_procedure ("file_wmf_load",
                          "loads files of the Windows(tm) metafile file format",
                          "FIXME: write help for file_wmf_load",
                          "Tor Lillqvist <tml@iki.fi>",
                          "Tor Lillqvist",
                          "1998",
                          "<Load>/WMF",
			  NULL,
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (load_args),
                          G_N_ELEMENTS (load_return_vals),
                          load_args, load_return_vals);

  gimp_install_procedure ("file_wmf_load_setargs",
			  "set additional parameters for the procedure file_wmf_load",
			  "set additional parameters for the procedure file_wmf_load",
			  "Tor Lillqvist <tml@iki.fi>",
                          "Tor Lillqvist",
                          "1998",
			  NULL,
			  NULL,
			  GIMP_PLUGIN,
			  G_N_ELEMENTS (load_setargs_args), 0,
			  load_setargs_args, NULL);

  gimp_register_magic_load_handler ("file_wmf_load",
				    "wmf,apm",
				    "",
				    "0,string,\\327\\315\\306\\232");
}

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  static GimpParam  values[2];
  GimpPDBStatusType status = GIMP_PDB_SUCCESS;
  gint32            image_ID;

  l_run_mode = param[0].data.d_int32;

  INIT_I18N ();

  *nreturn_vals = 1;
  *return_vals  = values;

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;

  if (strcmp (name, "file_wmf_load") == 0)
    {
      switch (l_run_mode)
	{
	case GIMP_RUN_INTERACTIVE:
	  gimp_get_data ("file_wmf_load", &load_vals);

	  if (!load_dialog (param[1].data.d_string))
	    status = GIMP_PDB_CANCEL;
	  break;

	case GIMP_RUN_NONINTERACTIVE:
	case GIMP_RUN_WITH_LAST_VALS:
	  gimp_get_data ("file_wmf_load", &load_vals);

	default:
	  break;
	}

      if (status == GIMP_PDB_SUCCESS)
	{
	  check_load_vals ();

	  image_ID = load_image (param[1].data.d_string);

	  if (image_ID != -1)
	    {
	      gimp_set_data ("file_wmf_load", &load_vals, sizeof (load_vals));

	      *nreturn_vals = 2;
	      values[1].type         = GIMP_PDB_IMAGE;
	      values[1].data.d_image = image_ID;
	    }
	  else
	    {
	      status = GIMP_PDB_EXECUTION_ERROR;
	    }
	}
    }
  else
    {
      status = GIMP_PDB_CALLING_ERROR;
    }

  values[0].data.d_status = status;
}

static Object *
new_object (ObjectType type,
	    Object **objects,
	    const int nobjects)
{
  gint i;
  Object *result = NULL;

  for (i = 0; i < nobjects; i++)
    if (objects[i] == NULL)
      {
	objects[i] = result = g_new (Object, 1);
	result->type = type;
	break;
      }
  if (i == nobjects)
    g_message ("Creating too many objects");

  return result;
}

static Canvas *
make_canvas (OrgAndExt *window,
	     OrgAndExt *viewport,
	     gboolean have_bbox,
	     GdkRectangle *bbox,
	     guint units_per_in)
{
  Canvas *canvas = g_new (Canvas, 1);

  if (!window->valid)
    {
#ifdef DEBUG
      g_print ("make_canvas: !window->valid\n");
#endif
      if (have_bbox)
	{
#ifdef DEBUG
	  g_print ("make_canvas: have_bbox\n");
#endif
	  window->org_x = bbox->x;
	  window->ext_x = bbox->width;
	  window->org_y = bbox->y;
	  window->ext_y = bbox->height;
	}
      else
	{
	  window->org_x = window->org_y = 0;
	  /* Just pick a size. */
	  window->ext_x = units_per_in * 4;
	  window->ext_y = units_per_in * 3;
	}
      window->valid = TRUE;
    }

  canvas->scalex = canvas->scaley = load_vals.scale;

  if (!viewport->valid)
    {
#ifdef DEBUG
      g_print ("make_canvas: !viewport->valid\n");
#endif
      viewport->org_x = viewport->org_y = 0;
      viewport->ext_x = canvas->scalex * fabs (window->ext_x) / units_per_in * pixs_per_in;
      viewport->ext_y = canvas->scaley * fabs (window->ext_y) / units_per_in * pixs_per_in;
      viewport->valid = TRUE;
    }
#ifdef DEBUG
  g_print ("make_canvas: w: (%d,%d)--(%d,%d), vp: (%d,%d)--(%d,%d)\n",
	   window->org_x, window->org_y, window->org_x + window->ext_x, window->org_y + window->ext_y,
	   viewport->org_x, viewport->org_y,
	   viewport->org_x + viewport->ext_x, viewport->org_y + viewport->ext_y);
#endif

  canvas->colormap = gdk_screen_get_system_colormap (gdk_screen_get_default ());

  canvas->width = viewport->ext_x;
  canvas->height = viewport->ext_y;

  canvas->pixmap = gdk_pixmap_new (NULL, viewport->ext_x, viewport->ext_y,
				   gdk_colormap_get_visual (canvas->colormap)->depth);

  canvas->dc.gc = gdk_gc_new (canvas->pixmap);

  canvas->dc.bg.red =
    canvas->dc.bg.green =
    canvas->dc.bg.blue = 0xFFFF;
  gdk_color_alloc (canvas->colormap, &canvas->dc.bg);

  canvas->dc.brush = g_new (BrushObject, 1);
  canvas->dc.brush->invisible = FALSE;
  canvas->dc.brush->color.red =
    canvas->dc.brush->color.green =
    canvas->dc.brush->color.blue = 0xFFFF;
  gdk_color_alloc (canvas->colormap, &canvas->dc.brush->color);

  canvas->dc.pen = g_new (PenObject, 1);
  canvas->dc.pen->invisible = FALSE;
  canvas->dc.pen->color.red =
    canvas->dc.pen->color.green =
    canvas->dc.pen->color.blue = 0;
  gdk_color_alloc (canvas->colormap, &canvas->dc.pen->color);

  canvas->dc.font = g_new (FontObject, 1);
  canvas->dc.font->font = gdk_font_load ("-adobe-helvetica-medium-r-normal--*-120-*-*-*-*-*-*");

  canvas->dc.textColor.red =
    canvas->dc.textColor.green =
    canvas->dc.textColor.blue = 0;
  gdk_color_alloc (canvas->colormap, &canvas->dc.textColor);

  canvas->dc_stack = g_slist_alloc ();

  gdk_gc_set_foreground (canvas->dc.gc, &canvas->dc.brush->color);
  gdk_draw_rectangle (canvas->pixmap, canvas->dc.gc, TRUE, 0, 0,
		      viewport->ext_x, viewport->ext_y);

  canvas->curx = canvas->cury = 0.0;

  return canvas;
}

static void
set_color (WORD *params,
	   GdkColor *color)
{
  color->red = ((GUINT16_FROM_LE (params[0]) & 0x00FF) * 65535) / 255;
  color->green = (((GUINT16_FROM_LE (params[0]) & 0xFF00) >> 8) * 65535) / 255;
  color->blue = ((GUINT16_FROM_LE (params[1]) & 0x00FF) * 65535) / 255;
}

static gint32
load_image (const gchar *filename)
{
  FILE *fp;
  char *name_buf;
  guchar buffer[100];
  gboolean warned_unhandled = FALSE;
  gboolean warned_opaque = FALSE;
  gboolean warned_orientation = FALSE;
  WMFHEAD wmf_head;
  PLACEABLEMETAHEADER apm_head;
  WMFRECORD record;
  WORD params[NPARMWORDS];
  Object **objects, *objp = NULL;
  guint nobjects;
  guint units_per_in;

  guint i, j, jj, k;
  gint ix;
  gchar *string;
  guint record_counter = 0;
  GdkRectangle bbox;
  gboolean have_bbox = FALSE;
  OrgAndExt window, viewport;

#define XSCALE(value) ((value) * (double) viewport.ext_x / window.ext_x)
#define YSCALE(value) ((value) * (double) viewport.ext_y / window.ext_y)

#define XMAPPAR(param) (XSCALE (GINT16_FROM_LE (param) - window.org_x) + viewport.org_x)
#define YMAPPAR(param) (YSCALE (GINT16_FROM_LE (param) - window.org_y) + viewport.org_y)
#define XMAPPARPLUS1(param) (XSCALE ((GINT16_FROM_LE (param) + 1) - window.org_x) + viewport.org_x)
#define YMAPPARPLUS1(param) (YSCALE ((GINT16_FROM_LE (param) + 1) - window.org_y) + viewport.org_y)

#define XIMAPPAR(param) ((gint) XMAPPAR (param))
#define YIMAPPAR(param) ((gint) YMAPPAR (param))
#define XIMAPPARPLUS1(param) ((gint) XMAPPARPLUS1 (param))
#define YIMAPPARPLUS1(param) ((gint) YMAPPARPLUS1 (param))

  Canvas *canvas = NULL;
  GdkGCValues gc_values;
  DC *dc;
  GdkVisual *visual;
  GdkPoint *points;
  double x, y;
  guint npoints;
  guint npolys;
  guint *nppoints;
  GdkImage *image;
  guint options;

  GimpPixelRgn pixel_rgn;
  gint32 image_ID = -1;
  gint32 layer_ID;
  GimpDrawable *drawable;
  guchar *pixelp;
  gulong pixel;
  guint start, end, scanlines;
  guchar *buf, *bufp;
  GdkColor *colors;
  guchar *rtbl, *gtbl, *btbl;
  guint rmask, gmask, bmask, rshift, gshift, bshift;
  gdouble xres, yres;

  int argc;
  char **argv;

  argc = 1;
  argv = g_new (char*, 1);
  argv[0] = g_strdup ("wmf");

  gdk_init (&argc, &argv);

  fp = fopen (filename, "rb");
  if (!fp)
    {
      g_message ("Can't open '%s':%s", filename, g_strerror (errno));
      return -1;
    }

  name_buf = g_strdup_printf ( _("Interpreting %s:"), filename);
  gimp_progress_init (name_buf);
  g_free (name_buf);

  if (!ReadOK (fp, buffer, SIZE_WMFHEAD))
    {
      g_message ("Failed to read metafile header");
      return -1;
    }

  g_memmove (&apm_head.Key, buffer, 4);

  if (GUINT32_FROM_LE (apm_head.Key) == 0x9ac6cdd7)
    {
      if (!ReadOK (fp, buffer + SIZE_WMFHEAD,
		   SIZE_PLACEABLEMETAHEADER - SIZE_WMFHEAD))
	{
	  g_message ("Failed to read placeable metafile header");
	  return -1;
	}
      g_memmove (&apm_head.Left, buffer + 6, 2);
      g_memmove (&apm_head.Top, buffer + 8, 2);
      g_memmove (&apm_head.Right, buffer + 10, 2);
      g_memmove (&apm_head.Bottom, buffer + 12, 2);
      g_memmove (&apm_head.Inch, buffer + 14, 2);
      bbox.x = GINT16_FROM_LE (apm_head.Left);
      bbox.y = GINT16_FROM_LE (apm_head.Top);
      bbox.width = GUINT16_FROM_LE (apm_head.Right) - bbox.x;
      bbox.height = GUINT16_FROM_LE (apm_head.Bottom) - bbox.y;
      have_bbox = TRUE;
      units_per_in = GUINT16_FROM_LE (apm_head.Inch);

#ifdef DEBUG
      g_print ("placeable metafile header: (%d,%d)--(%d,%d), bbox: %dx%d@+%d+%d\n",
	       apm_head.Left, apm_head.Top, apm_head.Right, apm_head.Bottom,
	       bbox.width, bbox.height, bbox.x, bbox.y);
      g_print ("units_per_in: %d\n", units_per_in);
#endif
      if (!ReadOK (fp, buffer, SIZE_WMFHEAD))
	{
	  g_message ("Failed to read metafile header");
	  return -1;
	}
    }
  else
    {
      units_per_in = 1440;
    }
  viewport.org_x = viewport.org_y = 0;
  viewport.valid = FALSE;
  window.org_x = window.org_y = 0;
  window.valid = FALSE;

  gimp_get_monitor_resolution (&xres, &yres);
  pixs_per_in = (int) ((xres + yres) / 2.0);

#ifdef DEBUG
  g_print ("pixs_per_in: %d\n", pixs_per_in);
#endif
  g_memmove (&wmf_head.Version, buffer + 4, 2);
  g_memmove (&wmf_head.FileSize, buffer + 6, 4);
  g_memmove (&wmf_head.NumOfObjects, buffer + 10, 2);

  if (GUINT16_FROM_LE (wmf_head.Version) != 0x0300)
    {
      g_message ("Metafile has wrong version, got %#x, expected 0x300",
		 GUINT16_FROM_LE (wmf_head.Version));
      return -1;
    }

  nobjects = GUINT16_FROM_LE (wmf_head.NumOfObjects);
  objects = g_new (Object*, nobjects);
  for (i = 0; i < nobjects; i++)
    objects[i] = NULL;

  while (1)
    {
      if (!ReadOK (fp, buffer, SIZE_WMFRECORD))
	{
	  g_message ("Failed to read metafile record");
	  return -1;
	}
      g_memmove (&record.Size, buffer, 4);
      g_memmove (&record.Function, buffer + 4, 2);
      record_counter++;
#ifdef DEBUG
      g_print ("%#x %d\n", GUINT16_FROM_LE (record.Function), GUINT32_FROM_LE (record.Size));
#endif
      switch (GUINT16_FROM_LE (record.Function))
	{
	case SetWindowOrg:
	  if (!readparams (record.Size, 2, fp, params))
	    return -1;
	  window.org_y = GINT16_FROM_LE (params[0]);
	  window.org_x = GINT16_FROM_LE (params[1]);
	  sync_record (record.Size, 2, fp);
	  break;

	case SetViewportOrg:
	  if (!readparams (record.Size, 2, fp, params))
	    return -1;
	  viewport.org_y = GINT16_FROM_LE (params[0]);
	  viewport.org_x = GINT16_FROM_LE (params[1]);
	  sync_record (record.Size, 2, fp);
	  break;

	case SetWindowExt:
	  if (!readparams (record.Size, 2, fp, params))
	    return -1;
	  window.ext_y = GINT16_FROM_LE (params[0]);
	  window.ext_x = GINT16_FROM_LE (params[1]);
	  window.valid = TRUE;
	  sync_record (record.Size, 2, fp);
	  break;

	case SetViewportExt:
	  if (!readparams (record.Size, 2, fp, params))
	    return -1;
	  viewport.ext_y = GINT16_FROM_LE (params[0]);
	  viewport.ext_x = GINT16_FROM_LE (params[1]);
	  viewport.valid = TRUE;
	  sync_record (record.Size, 2, fp);
	  break;

	case IntersectClipRect:
	  if (!readparams (record.Size, 4, fp, params))
	    return -1;
	  /* XXX */
	  sync_record (record.Size, 4, fp);
	  break;

	case SaveDC:
	  dc = g_new (DC, 1);
	  if (canvas == NULL)
	    canvas = make_canvas (&window, &viewport, have_bbox, &bbox, units_per_in);
	  *dc = canvas->dc;
	  dc->gc = gdk_gc_new (canvas->pixmap);
	  dc->tag = saved_dc_tag++;
	  gdk_gc_copy (dc->gc, canvas->dc.gc);
	  gdk_font_ref (dc->font->font);
	  canvas->dc_stack = g_slist_prepend (canvas->dc_stack, dc);
	  sync_record (record.Size, 0, fp);
	  break;

	case RestoreDC:
	  if (!readparams (record.Size, 1, fp, params))
	    return -1;
	  ix = GINT16_FROM_LE (params[0]);
	  if (ix >= 0)
	    {
	      fclose (fp);
	      g_message ("RestoreDC with positive argument (%d)?", ix);
	      return -1;
	    }
	  while (ix++ < 0)
	    {
	      if (canvas->dc_stack == NULL)
		{
		  fclose (fp);
		  g_message ("DC stack underflow");
		  return -1;
		}
	      gdk_gc_unref (canvas->dc.gc);
	      gdk_font_unref (canvas->dc.font->font);
	      canvas->dc = *((DC *) canvas->dc_stack->data);
	      canvas->dc_stack = g_slist_next (canvas->dc_stack);
	    }
	  sync_record (record.Size, 1, fp);
	  break;

	case SetBkColor:
	  if (!readparams (record.Size, 2, fp, params))
	    return -1;
	  if (canvas == NULL)
	    canvas = make_canvas (&window, &viewport, have_bbox, &bbox, units_per_in);
	  set_color (params + 0, &canvas->dc.bg);
#ifdef DEBUG
	  g_print ("SetBkColor: %d %d %d\n", canvas->dc.bg.red,
		   canvas->dc.bg.green, canvas->dc.bg.blue);
#endif
	  if (!gdk_color_alloc (canvas->colormap, &canvas->dc.bg))
	    {
	      fclose (fp);
	      g_message ("Couldn't allocate color");
	      return -1;
	    }
#ifdef DEBUG
	  g_print ("...allocated color %#.06lx\n", canvas->dc.bg.pixel);
#endif
	  sync_record (record.Size, 2, fp);
	  break;

	case SetBkMode:
	  if (!readparams (record.Size, 1, fp, params))
	    return -1;
	  switch (GINT16_FROM_LE (params[0]))
	    {
	    case TRANSPARENT:
	      break;
	    case OPAQUE:
	      if (!warned_opaque)
		{
		  g_message ("The WMF file contains SetBkMode (OPAQUE).\n"
			     "This is not supported, sorry. The resulting\n"
			     "image might not look quite right.");
		  warned_opaque = TRUE;
		}
	      break;
	    default:
	      fclose (fp);
	      g_message ("Invalid case %d at line %d",
			 GINT16_FROM_LE (params[0]), __LINE__);
	      break;
	    }
	  sync_record (record.Size, 1, fp);
	  break;

	case SetTextColor:
	  if (!readparams (record.Size, 2, fp, params))
	    return -1;
	  if (canvas == NULL)
	    canvas = make_canvas (&window, &viewport, have_bbox, &bbox, units_per_in);
	  set_color (params + 0, &canvas->dc.textColor);
#ifdef DEBUG
	  g_print ("SetTextColor: %d %d %d\n", canvas->dc.textColor.red,
		   canvas->dc.textColor.green, canvas->dc.textColor.blue);
#endif
	  if (!gdk_color_alloc (canvas->colormap, &canvas->dc.textColor))
	    {
	      fclose (fp);
	      g_message ("Couldn't allocate color");
	      return -1;
	    }
#ifdef DEBUG
	  g_print ("...allocated color %#.06lx\n", canvas->dc.textColor.pixel);
#endif
	  sync_record (record.Size, 2, fp);
	  break;

	case SetTextAlign:
	  if (!readparams (record.Size, 1, fp, params))
	    return -1;
	  /* XXX */
	  sync_record (record.Size, 1, fp);
	  break;

	case SetROP2:
	  if (!readparams (record.Size, 1, fp, params))
	    return -1;
	  if (canvas == NULL)
	    canvas = make_canvas (&window, &viewport, have_bbox, &bbox, units_per_in);
	  switch (GUINT16_FROM_LE (params[0]))
	    {
	    case R2_COPYPEN:
	      gdk_gc_set_function (canvas->dc.gc, GDK_COPY); break;
	    case R2_NOT:
	      gdk_gc_set_function (canvas->dc.gc, GDK_INVERT); break;
	    case R2_XORPEN:
	      gdk_gc_set_function (canvas->dc.gc, GDK_XOR); break;
#ifdef GDK_CLEAR		/* Other ROPs not in gdk 1.0 */
	    case R2_BLACK:
	      gdk_gc_set_function (canvas->dc.gc, GDK_CLEAR); break;
	    case R2_MASKPEN:
	      gdk_gc_set_function (canvas->dc.gc, GDK_AND); break;
	    case R2_MASKPENNOT:
	      gdk_gc_set_function (canvas->dc.gc, GDK_AND_REVERSE); break;
	    case R2_MASKNOTPEN:
	      gdk_gc_set_function (canvas->dc.gc, GDK_AND_INVERT); break;
	    case R2_NOP:
	      gdk_gc_set_function (canvas->dc.gc, GDK_NOOP); break;
	    case R2_MERGEPEN:
	      gdk_gc_set_function (canvas->dc.gc, GDK_OR); break;
	    case R2_NOTXORPEN:
	      gdk_gc_set_function (canvas->dc.gc, GDK_EQUIV); break;
	    case R2_MERGEPENNOT:
	      gdk_gc_set_function (canvas->dc.gc, GDK_OR_REVERSE); break;
	    case R2_NOTCOPYPEN:
	      gdk_gc_set_function (canvas->dc.gc, GDK_COPY_INVERT); break;
	    case R2_MERGENOTPEN:
	      gdk_gc_set_function (canvas->dc.gc, GDK_OR_INVERT); break;
	    case R2_NOTMASKPEN:
	      gdk_gc_set_function (canvas->dc.gc, GDK_NAND); break;
	    case R2_WHITE:
	      gdk_gc_set_function (canvas->dc.gc, GDK_SET); break;
#endif
	    default:
	      fclose (fp);
	      g_message ("Invalid ROP2");
	      return -1;
	    }
	  sync_record (record.Size, 1, fp);
	  break;

	case SetStretchBltMode:
	  if (!readparams (record.Size, 1, fp, params))
	    return -1;
	  /* XXX */
	  sync_record (record.Size, 1, fp);
	  break;

	case SetPolyFillMode:
	  if (!readparams (record.Size, 1, fp, params))
	    return -1;
	  /* GDK has no way to set the fill rule of a GdkGC! */
	  /* XXX */
	  sync_record (record.Size, 1, fp);
	  break;

	case SetMapMode:
	  if (!readparams (record.Size, 1, fp, params))
	    return -1;
	  switch (GUINT16_FROM_LE (params[0]))
	    {
	    case MM_ANISOTROPIC:
	    case MM_ISOTROPIC:
	      break;

	    case MM_HIENGLISH:
	      units_per_in = 1000;
	      goto set_window_and_viewport;

	    case MM_HIMETRIC:
	      units_per_in = 2540;
	      goto set_window_and_viewport;

	    case MM_LOENGLISH:
	      units_per_in = 100;
	      goto set_window_and_viewport;

	    case MM_LOMETRIC:
	      units_per_in = 254;
	      goto set_window_and_viewport;

	    case MM_TEXT:
	      units_per_in = pixs_per_in;
	      goto set_window_and_viewport;

	    case MM_TWIPS:
	      units_per_in = 1440;

	    set_window_and_viewport:
	      window.valid = TRUE;
	      viewport.valid = TRUE;
	      window.org_x = window.org_y =
		viewport.org_x = viewport.org_y = 0;
	      window.ext_x = units_per_in * 4;
	      window.ext_y = units_per_in * 3;
	      viewport.ext_x = load_vals.scale * fabs (window.ext_x) / units_per_in * pixs_per_in;
	      viewport.ext_y = load_vals.scale * fabs (window.ext_y) / units_per_in * pixs_per_in;
	      break;

	    default:
	      fclose (fp);
	      g_message ("Invalid case %d at line %d",
			 GUINT16_FROM_LE (params[0]), __LINE__);
	      return -1;
	    }
	  sync_record (record.Size, 1, fp);
	  break;

	case SetRelabs:
	  if (!readparams (record.Size, 1, fp, params))
	    return -1;
	  /* XXX */
	  sync_record (record.Size, 1, fp);
	  break;

	case CreatePenIndirect:
	  if (!readparams (record.Size, 5, fp, params))
	    return -1;
	  if ((objp = new_object (OBJ_PEN, objects, nobjects)) == NULL)
	    {
	      fclose (fp);
	      return -1;
	    }
	  objp->u.pen.invisible = FALSE;
	  switch (GUINT16_FROM_LE (params[0]))
	    {
	    case PS_SOLID:
	    case PS_INSIDEFRAME:
	      objp->u.pen.style = GDK_LINE_SOLID;
	      break;

	    case PS_DASH:
	    case PS_DOT:
	    case PS_DASHDOT:
	    case PS_DASHDOTDOT:
	      objp->u.pen.style = GDK_LINE_ON_OFF_DASH;
	      break;

	    case PS_NULL:
	      objp->u.pen.style = GDK_LINE_SOLID;
	      objp->u.pen.invisible = TRUE;
	      break;

	    default:
	      g_message ("Unrecognized pen style %#x",
			 GUINT16_FROM_LE (params[0]));
	      fclose (fp);
	      return -1;
	    }
	  if (canvas == NULL)
	    canvas = make_canvas (&window, &viewport, have_bbox, &bbox, units_per_in);
	  objp->u.pen.width = (int) XSCALE (GUINT16_FROM_LE (params[1]) + (GUINT16_FROM_LE (params[2]) << 16));
	  set_color (params+3, &objp->u.pen.color);
#ifdef DEBUG
	  g_print ("CreatePenIndirect: %#x width %d%s %s color: %d %d %d\n",
		   objp,
		   objp->u.pen.width,
		   (objp->u.pen.invisible ? " invisible" : ""),
		   (objp->u.pen.style == GDK_LINE_SOLID ? "solid" :
		    (objp->u.pen.style == GDK_LINE_ON_OFF_DASH ? "on-off-dash" :
		     (objp->u.pen.style == GDK_LINE_DOUBLE_DASH ? "double-dash" :
		      "???"))),
		   objp->u.pen.color.red,
		   objp->u.pen.color.green,
		   objp->u.pen.color.blue);
#endif
	  if (!gdk_color_alloc (canvas->colormap, &objp->u.pen.color))
	    {
	      g_message ("Couldn't allocate color");
	      fclose (fp);
	      return -1;
	    }
#ifdef DEBUG
	  g_print ("...allocated color %#.06lx\n", objp->u.pen.color.pixel);
#endif
	  /* CreatePenIndirect records sometimes have junk padding? */
	  sync_record (record.Size, 5, fp);
	  break;

	case CreateBrushIndirect:
	  if (!readparams (record.Size, 4, fp, params))
	    return -1;
	  if ((objp = new_object (OBJ_BRUSH, objects, nobjects)) == NULL)
	    {
	      fclose (fp);
	      return -1;
	    }
	  objp->u.brush.style = GUINT16_FROM_LE (params[0]);
	  objp->u.brush.invisible = FALSE;
	  if (objp->u.brush.style == BS_NULL)
	    objp->u.brush.invisible = TRUE;
	  set_color (params+1, &objp->u.brush.color);
#ifdef DEBUG
	  g_print ("CreateBrushIndirect: %#x%s color: %d %d %d\n",
		   objp,
		   (objp->u.brush.invisible ? " invisible" : ""),
		   objp->u.brush.color.red,
		   objp->u.brush.color.green,
		   objp->u.brush.color.blue);
#endif
	  if (canvas == NULL)
	    canvas = make_canvas (&window, &viewport, have_bbox, &bbox, units_per_in);
	  if (!gdk_color_alloc (canvas->colormap, &objp->u.brush.color))
	    {
	      g_message ("Couldn't allocate color");
	      fclose (fp);
	      return -1;
	    }
#ifdef DEBUG
	  g_print ("...allocated color %#.06lx\n", objp->u.brush.color.pixel);
#endif
	  objp->u.brush.hatch = GUINT16_FROM_LE (params[3]);
	  sync_record (record.Size, 4, fp);
	  break;

	case DibCreatePatternBrush:
	  if ((objp = new_object (OBJ_PATTERNBRUSH, objects, nobjects)) == NULL)
	    {
	      fclose (fp);
	      return -1;
	    }
#ifdef DEBUG
	  g_print ("DibCreatePatternBrush: %#x\n", objp);
#endif
	  /* Ignored for now */
	  sync_record (record.Size, 0, fp);
	  break;

	case CreatePalette:
	  if ((objp = new_object (OBJ_PALETTE, objects, nobjects)) == NULL)
	    {
	      fclose (fp);
	      return -1;
	    }
#ifdef DEBUG
	  g_print ("CreatePalette: %#x\n", objp);
#endif
	  /* XXX */
	  sync_record (record.Size, 0, fp);
	  break;

	case CreateFontIndirect:
	  if (!readparams (record.Size, 9, fp, params))
	    return -1;
	  if ((objp = new_object (OBJ_FONT, objects, nobjects)) == NULL)
	    {
	      fclose (fp);
	      return -1;
	    }
	  if (canvas == NULL)
	    canvas = make_canvas (&window, &viewport, have_bbox, &bbox, units_per_in);
	  {
	    gint height, orientation, weight, italic, pitch_family;
	    char *pitch, *slant, *fontname, *name, *name2;

	    height = ABS (GINT16_FROM_LE (params[0]));
	    if (height == 0)
	      height = 12;
	    /* Orientation ignored for now. GDK doesn't support
	     * tilted text anyway. We could of course rotate it by hand.
	     */
	    orientation = GUINT16_FROM_LE (params[2]);
	    if (orientation != 0 && !warned_orientation)
	      {
		g_message ("The WMF file contains non-horizontal fonts.\n"
			   "This is not supported, sorry. The resulting\n"
			   "image will not look quite right.");
		warned_orientation = TRUE;
	      }
	    weight = GUINT16_FROM_LE (params[4]);
	    italic = (GUINT16_FROM_LE (params[5]) & 0xFF);
#if 0
	    pitch_family = ((GUINT16_FROM_LE (params[8]) >> 8) & 0xFF);
#else
	    pitch_family = (GUINT16_FROM_LE (params[8]) & 0xFF);
#endif
	    if ((pitch_family & 0x03) == 1)
	      pitch = "m";
	    else if ((pitch_family & 0x03) == 2)
	      pitch = "p";
	    else
	      pitch = "*";

	    if (italic)
	      {
		if ((pitch_family & 0x3) == 1)
		  slant = "o";
		else
		  slant = "i";
	      }
	    else
	      slant = "r";

	    k = GUINT32_FROM_LE (record.Size) - 9 - WORDSIZE_WMFRECORD;
	    name = g_malloc (k*2 + 1);
	    for (i = 0; i < k*2; i++)
	      {
		if ((i & 1) == 0)
		  {
		    if (!readparams (0, 1, fp, params))
		      return -1;
		    name[i] = (params[0] & 0xFF);
		  }
		else
		  name[i] = ((params[0] >> 8) & 0xFF);
	      }
	    name[k*2] = '\0';

#ifdef DEBUG
	    g_print ("CreateFontIndirect: %#x %s\n", objp, name);
#endif
	    /* Very rough mapping from typical Windows fonts to
	     * typical X11 fonts. If you run GIMP on Win32,
	     * they will be mapped back to typical Windows
	     * fonts, sigh...
	     */
	    name = g_ascii_strdown (name, -1);

	    if (strcmp (name, "system") == 0
		|| strcmp (name, "fixedsys") == 0)
	      name2 = "courier";
	    else if (strncmp (name, "arial", 5) == 0)
	      name2 = "helvetica";
	    else if (strncmp (name, "courier", 7) == 0)
	      name2 = "courier";
	    else
	      name2 = name;
	    fontname =
	      g_strdup_printf ("-*-%s-%s-%s-%s-*-%d-*-*-*-%s-*-*-*",
			       name2,
			       (weight >= 700 ? "bold" :
				(weight >= 400 ? "medium" :
				 "*")),
			       slant,
			       "*",
			       (int) YSCALE (height),
			       pitch);
#ifdef DEBUG
	    g_print ("...XLFD font: %s\n", fontname);
#endif
	    objp->u.font.font = gdk_font_load (fontname);
	    if (objp->u.font.font == NULL)
	      {
		g_free (fontname);
		fontname =
		  g_strdup_printf ("-*-%s-%s-%s-%s-*-%d-*-*-*-%s-*-*-*",
				   "*",
				   (weight >= 700 ? "bold" :
				    (weight >= 400 ? "medium" :
				     "*")),
				   "*",
				   "*",
				   (int) YSCALE (height),
				   "*");
#ifdef DEBUG
		g_print ("...another XLFD font: %s\n", fontname);
#endif
		objp->u.font.font = gdk_font_load (fontname);
		if (objp->u.font.font == NULL)
		  {
		    fclose (fp);
		    g_message ("Cannot load suitable font, not even %s",
			       fontname);
		    return -1;
		  }
	      }
	    g_free (name);
	    g_free (fontname);
	  }
	  break;

	case SelectObject:
	  if (!readparams (record.Size, 1, fp, params))
	    return -1;
	  k = GUINT16_FROM_LE (params[0]);
	  if (k >= nobjects)
	    {
	      fclose (fp);
	      g_message ("Selecting out of bounds object index");
	      return -1;
	    }
	  objp = objects[k];
	  if (objp == NULL)
	    {
	      fclose (fp);
	      g_message ("Selecting NULL object");
	      return -1;
	    }
#ifdef DEBUG
	  g_print ("SelectObject: %#x %s\n",
		   objp,
		   (objp->type == OBJ_BRUSH ? "brush" :
		    (objp->type == OBJ_PEN ? "pen" :
		     (objp->type == OBJ_PATTERNBRUSH ? "patternbrush" :
		      (objp->type == OBJ_FONT ? "font" :
		       "???")))));
#endif
	  switch (objp->type)
	    {
	    case OBJ_BRUSH:
	      if (canvas == NULL)
		canvas = make_canvas (&window, &viewport, have_bbox, &bbox, units_per_in);
	      canvas->dc.brush = &objp->u.brush;
	      break;

	    case OBJ_PEN:
	      if (canvas == NULL)
		canvas = make_canvas (&window, &viewport, have_bbox, &bbox, units_per_in);
	      canvas->dc.pen = &objp->u.pen;
	      gdk_gc_get_values (canvas->dc.gc, &gc_values);
	      gdk_gc_set_line_attributes
		(canvas->dc.gc, objp->u.pen.width, objp->u.pen.style,
		 gc_values.cap_style, gc_values.join_style);
	      break;

	    case OBJ_PATTERNBRUSH:
	      /* XXX */
	      break;

	    case OBJ_FONT:
	      gdk_font_unref (canvas->dc.font->font);
	      canvas->dc.font = &objp->u.font;
	      gdk_font_ref (canvas->dc.font->font);
	      break;

	    default:
	      fclose (fp);
	      g_message ("Unhandled case %d at line %d",
			 objp->type, __LINE__);
	      return -1;
	    }
	  sync_record (record.Size, 1, fp);
	  break;

	case SelectPalette:
	  if (!readparams (record.Size, 1, fp, params))
	    return -1;
	  k = GUINT16_FROM_LE (params[0]);
	  if (k >= nobjects)
	    {
	      fclose (fp);
	      g_message ("Selecting out of bounds palette index");
	      return -1;
	    }
	  objp = objects[k];
	  if (objp == NULL)
	    {
	      fclose (fp);
	      g_message ("Selecting NULL palette");
	      return -1;
	    }
	  if (objp->type != OBJ_PALETTE)
	    {
	      fclose (fp);
	      g_message ("SelectPalette selects non-palette");
	      return -1;
	    }
	  /* XXX */
	  sync_record (record.Size, 1, fp);
	  break;

	case RealizePalette:
	  /* XXX */
	  sync_record (record.Size, 0, fp);
	  break;

	case DeleteObject:
	  if (!readparams (record.Size, 1, fp, params))
	    return -1;
	  k = GUINT16_FROM_LE (params[0]);
	  if (k >= nobjects)
	    {
	      fclose (fp);
	      g_message ("Deleting out of bounds object index");
	      return -1;
	    }
	  objp = objects[k];
	  if (objp == NULL)
	    {
	      fclose (fp);
	      g_message ("Deleting already deleted object");
	      return -1;
	    }
	  if (objp->type == OBJ_FONT)
	    gdk_font_unref (objp->u.font.font);
	  g_free (objp);
	  objects[k] = NULL;
	  break;

	case MoveTo:
	  if (!readparams (record.Size, 2, fp, params))
	    return -1;
	  if (canvas == NULL)
	    canvas = make_canvas (&window, &viewport, have_bbox, &bbox, units_per_in);
	  canvas->curx = XMAPPAR (params[1]);
	  canvas->cury = YMAPPAR (params[0]);
	  sync_record (record.Size, 2, fp);
	  break;

	case LineTo:
	  if (!readparams (record.Size, 2, fp, params))
	    return -1;
	  if (canvas == NULL)
	    canvas = make_canvas (&window, &viewport, have_bbox, &bbox, units_per_in);
	  x = XMAPPAR (params[1]);
	  y = YMAPPAR (params[0]);
#ifdef DEBUG
	  g_print ("LineTo: (%d,%d)--(%d,%d)\n",
		   (gint) canvas->curx, (gint) canvas->cury,
		   (gint) x, (gint) y);
#endif
	  gdk_gc_set_foreground (canvas->dc.gc, &canvas->dc.pen->color);
	  gdk_draw_line (canvas->pixmap, canvas->dc.gc,
			 (gint) canvas->curx, (gint) canvas->cury,
			 (gint) x, (gint) y);
	  canvas->curx = x;
	  canvas->cury = y;
	  sync_record (record.Size, 2, fp);
	  break;

	case Polyline:
	  if (!readparams (record.Size, 1, fp, params))
	    return -1;
	  npoints = GUINT16_FROM_LE (params[0]);
	  points = g_new (GdkPoint, npoints);
	  for (i = 0; i < npoints; i++)
	    {
	      if (!readparams (0, 2, fp, params))
		return -1;
	      points[i].x = XIMAPPAR (params[0]);
	      points[i].y = YIMAPPAR (params[1]);
	    }
	  if (canvas == NULL)
	    canvas = make_canvas (&window, &viewport, have_bbox, &bbox, units_per_in);
	  gdk_gc_set_foreground (canvas->dc.gc, &canvas->dc.pen->color);
	  gdk_draw_lines (canvas->pixmap, canvas->dc.gc, points, npoints);
	  g_free (points);
	  break;

	case Rectangle:
	  if (!readparams (record.Size, 4, fp, params))
	    return -1;
	  if (canvas == NULL)
	    canvas = make_canvas (&window, &viewport, have_bbox, &bbox, units_per_in);
	  if (!canvas->dc.brush->invisible)
	    {
	      gdk_gc_set_foreground (canvas->dc.gc, &canvas->dc.brush->color);
	      gdk_draw_rectangle (canvas->pixmap, canvas->dc.gc, TRUE,
				  XIMAPPAR (params[3]),
				  YIMAPPAR (params[2]),
				  XIMAPPARPLUS1 (params[1]) - XIMAPPAR (params[3]),
				  YIMAPPARPLUS1 (params[0]) - YIMAPPAR (params[2]));
	    }
	  if (!canvas->dc.pen->invisible)
	    {
	      gdk_gc_set_foreground (canvas->dc.gc, &canvas->dc.pen->color);
	      gdk_draw_rectangle (canvas->pixmap, canvas->dc.gc, FALSE,
				  XIMAPPAR (params[3]),
				  YIMAPPAR (params[2]),
				  XIMAPPARPLUS1 (params[1]) - XIMAPPAR (params[3]),
				  YIMAPPARPLUS1 (params[0]) - YIMAPPAR (params[2]));
	    }
	  sync_record (record.Size, 4, fp);
	  break;

	case Ellipse:
	  if (!readparams (record.Size, 4, fp, params))
	    return -1;
	  if (canvas == NULL)
	    canvas = make_canvas (&window, &viewport, have_bbox, &bbox, units_per_in);
	  if (!canvas->dc.brush->invisible)
	    {
	      gdk_gc_set_foreground (canvas->dc.gc, &canvas->dc.brush->color);
	      gdk_draw_arc (canvas->pixmap, canvas->dc.gc, TRUE,
			    XIMAPPAR (params[3]),
			    YIMAPPAR (params[2]),
			    XIMAPPARPLUS1 (params[1]) - XIMAPPAR (params[3]),
			    YIMAPPARPLUS1 (params[0]) - YIMAPPAR (params[2]),
			    0, 360 * 64);
	    }
	  if (!canvas->dc.pen->invisible)
	    {
	      gdk_gc_set_foreground (canvas->dc.gc, &canvas->dc.pen->color);
	      gdk_draw_arc (canvas->pixmap, canvas->dc.gc, FALSE,
			    XIMAPPAR (params[3]),
			    YIMAPPAR (params[2]),
			    XIMAPPARPLUS1 (params[1]) - XIMAPPAR (params[3]),
			    YIMAPPARPLUS1 (params[0]) - YIMAPPAR (params[2]),
			    0, 360 * 64);
	    }
	  sync_record (record.Size, 4, fp);
	  break;

	case Polygon:
	  if (!readparams (record.Size, 1, fp, params))
	    return -1;
	  npoints = GUINT16_FROM_LE (params[0]);
	  points = g_new (GdkPoint, npoints);
	  for (i = 0; i < npoints; i++)
	    {
	      if (!readparams (0, 2, fp, params))
		return -1;
	      points[i].x = XIMAPPAR (params[0]);
	      points[i].y = YIMAPPAR (params[1]);
	    }
	  if (canvas == NULL)
	    canvas = make_canvas (&window, &viewport, have_bbox, &bbox, units_per_in);
	  if (!canvas->dc.brush->invisible)
	    {
	      gdk_gc_set_foreground (canvas->dc.gc, &canvas->dc.brush->color);
	      gdk_draw_polygon (canvas->pixmap, canvas->dc.gc, TRUE, points, npoints);
	    }
	  if (!canvas->dc.pen->invisible)
	    {
	      gdk_gc_set_foreground (canvas->dc.gc, &canvas->dc.pen->color);
	      gdk_draw_polygon (canvas->pixmap, canvas->dc.gc, FALSE, points, npoints);
	    }
	  g_free (points);
	  break;

	case PolyPolygon:
	  if (!readparams (record.Size, 1, fp, params))
	    return -1;
	  if (canvas == NULL)
	    canvas = make_canvas (&window, &viewport, have_bbox, &bbox, units_per_in);
	  /* Number of polygons */
	  npolys = GUINT16_FROM_LE (params[0]);
	  nppoints = g_new (guint, npolys);
	  for (i = 0; i < npolys; i++)
	    {
	      if (!readparams (0, 1, fp, params))
		return -1;
	      nppoints[i] = GUINT16_FROM_LE (params[0]);
	    }
	  for (i = 0; i < npolys; i++)
	    {
	      points = g_new (GdkPoint, nppoints[i]);
	      for (j = 0; j < nppoints[i]; j++)
		{
		  if (!readparams (0, 2, fp, params))
		    return -1;
		  points[j].x = XIMAPPAR (params[0]);
		  points[j].y = YIMAPPAR (params[1]);
		}
	      if (!canvas->dc.brush->invisible)
		{
		  gdk_gc_set_foreground (canvas->dc.gc, &canvas->dc.brush->color);
		  gdk_draw_polygon (canvas->pixmap, canvas->dc.gc,
				    TRUE, points, nppoints[i]);
		}
	      if (!canvas->dc.pen->invisible)
		{
		  gdk_gc_set_foreground (canvas->dc.gc, &canvas->dc.pen->color);
		  gdk_draw_polygon (canvas->pixmap, canvas->dc.gc,
				    TRUE, points, nppoints[i]);
		}
	      g_free (points);
	    }
	  g_free (nppoints);
	  break;

	case TextOut:
	  if (!readparams (record.Size, 1, fp, params))
	    return -1;
	  k = GUINT16_FROM_LE (params[0]);
	  string = g_malloc (k);
	  for (i = 0; i < k; i++)
	    {
	      if ((i & 1) == 0)
		{
		  if (!readparams (0, 1, fp, params))
		    return -1;
		  j++;
		  string[i] = (params[0] & 0xFF);
		}
	      else
		string[i] = ((params[0] >> 8) & 0xFF);
	    }
	  if (!readparams (0, 2, fp, params))
	    return -1;
	  x = XMAPPAR (params[1]);
	  y = YMAPPAR (params[0]);
	  gdk_gc_set_foreground (canvas->dc.gc, &canvas->dc.textColor);
	  gdk_draw_text (canvas->pixmap, canvas->dc.font->font,
			 canvas->dc.gc, (gint) x, (gint) y, string, k);
	  g_free (string);
	  break;

	case ExtTextOut:
	  if (!readparams (record.Size, 4, fp, params))
	    return -1;
	  /* Count extra words read */
	  j = 4;
	  x = XMAPPAR (params[1]);
	  y = YMAPPAR (params[0]);
	  /* String length */
	  k = GUINT16_FROM_LE (params[2]);
	  options = GUINT16_FROM_LE (params[3]);
	  /* Clipping or opaquing? */
	  if ((options & 0x04) || (options & 0x02))
	    {
	      GdkRectangle r;
	      if (!readparams (0, 4, fp, params))
		return -1;
	      j += 4;
	      r.x = XIMAPPARPLUS1 (params[0]);
	      r.y = YIMAPPARPLUS1 (params[1]);
	      r.width = XIMAPPAR (params[2]) - r.x;
	      r.height = YIMAPPARPLUS1 (params[3]) - r.y;
	      if (options & 0x04)
		gdk_gc_set_clip_rectangle (canvas->dc.gc, &r);
	    }
	  string = g_malloc (k);
	  for (i = 0; i < k; i++)
	    {
	      if ((i & 1) == 0)
		{
		  if (!readparams (0, 1, fp, params))
		    return -1;
		  j++;
		  string[i] = (params[0] & 0xFF);
		}
	      else
		string[i] = ((params[0] >> 8) & 0xFF);
	    }
	  gdk_gc_set_foreground (canvas->dc.gc, &canvas->dc.textColor);
#if 0	  /* ExtTextOut records can have an optional list of distances
	   * between characters. But as we don't have the exact same font
	   * metrics anyway, we ignore it.
	   */
	  if (j < GUINT16_FROM_LE (record.Size))
	    for (i = 0; i < k; i++)
	      {
		gdk_draw_text (canvas->pixmap, canvas->dc.font->font,
			       canvas->dc.gc, (gint) x, (gint) y,
			       string + i, 1);
		if (j < GUINT16_FROM_LE (record.Size))
		  {
		    if (!readparams (0, 1, fp, params))
		      return -1;
		    j++;
		  }
		x += XSCALE (GINT16_FROM_LE (params[0]));
	      }
	  else
#endif
	    gdk_draw_text (canvas->pixmap, canvas->dc.font->font,
			   canvas->dc.gc, (gint) x, (gint) y + canvas->dc.font->font->ascent, string, k);
	  g_free (string);
	  if (options & 0x04)
	    gdk_gc_set_clip_rectangle (canvas->dc.gc, NULL);
	  sync_record (record.Size, j, fp);
	  break;

	case EndOfFile:
	  fclose (fp);
	  gimp_progress_update (1.0);

	  if (canvas->height >= 100)
	    gimp_progress_init (_("Transferring image"));

	  image_ID = gimp_image_new (canvas->width, canvas->height, GIMP_RGB);
	  gimp_image_set_filename (image_ID, filename);
	  layer_ID = gimp_layer_new (image_ID, _("Background"),
				     canvas->width, canvas->height, GIMP_RGB_IMAGE,
				     100, GIMP_NORMAL_MODE);
	  gimp_image_add_layer (image_ID, layer_ID, 0);
	  drawable = gimp_drawable_get (layer_ID);
	  image = gdk_image_get (canvas->pixmap, 0, 0,
				 canvas->width, canvas->height);
	  gimp_pixel_rgn_init (&pixel_rgn, drawable, 0, 0,
			       drawable->width, drawable->height,
			       TRUE, FALSE);
	  if (pixel_rgn.bpp != 3)
	    abort ();
	  visual = gdk_screen_get_system_visual (gdk_screen_get_default ());
	  switch (visual->type)
	    {
	    case GDK_VISUAL_PSEUDO_COLOR:
	      buf = g_malloc (gimp_tile_height() * canvas->width * 3);
	      colors = canvas->colormap->colors;
	      for (j = 0; j < canvas->height;)
		{
		  start = j;
		  end = MIN (j + gimp_tile_height (), canvas->height);
		  scanlines = end - start;
		  bufp = buf;

		  for (jj = 0; jj < scanlines; jj++)
		    {
		      pixelp = ((guchar *) image->mem) + (j + jj) * image->bpl;
		      for (i = 0; i < canvas->width; i++)
			{
			  *bufp++ = colors[*pixelp].red >> 8;
			  *bufp++ = colors[*pixelp].green >> 8;
			  *bufp++ = colors[*pixelp].blue >> 8;
			  pixelp++;
			}
		    }

		  gimp_pixel_rgn_set_rect (&pixel_rgn, buf, 0, j, canvas->width, scanlines);
		  if (canvas->height >= 100)
		    gimp_progress_update ((double) j / canvas->height);
		  j += scanlines;
		}
	      if (canvas->height >= 100)
		gimp_progress_update (1.0);
	      g_free (buf);
	      break;

	    case GDK_VISUAL_TRUE_COLOR:
	      buf = g_malloc (gimp_tile_height () * canvas->width * 3);

	      /* Set up mappings from subfield ranges to full 0..255 range */
	      k = 1 << visual->red_prec;
	      rtbl = g_malloc (k);
	      for (i = 0; i < k; i++)
		rtbl[i] = (i * 255) / (k-1);
	      k = 1 << visual->green_prec;
	      gtbl = g_malloc (k);
	      for (i = 0; i < k; i++)
		gtbl[i] = (i * 255) / (k-1);
	      k = 1 << visual->blue_prec;
	      btbl = g_malloc (k);
	      for (i = 0; i < k; i++)
		btbl[i] = (i * 255) / (k-1);
#if 0
	      g_print ("R: %.08x, %d, %d\n", visual->red_mask, visual->red_shift, visual->red_prec);
	      g_print ("G: %.08x, %d, %d\n", visual->green_mask, visual->green_shift, visual->green_prec);
	      g_print ("B: %.08x, %d, %d\n", visual->blue_mask, visual->blue_shift, visual->blue_prec);
	      g_print ("byte order: %s\n", (visual->byte_order == GDK_LSB_FIRST ? "LSB_FIRST" : (visual->byte_order == GDK_MSB_FIRST ? "MSB_FIRST" : "???")));
#endif
	      rmask = visual->red_mask;
	      gmask = visual->green_mask;
	      bmask = visual->blue_mask;
	      rshift = visual->red_shift;
	      gshift = visual->green_shift;
	      bshift = visual->blue_shift;

	      if ((image->depth > 8 && image->bpp == 1)
		  || image->bpp > 4)
		{
		  /* Workaround for bugs in GDK */
		  if (image->bpp > 4)
		    /* GDK has set image->bpp to bits-per-pixel,
		     * correct it to bytes-per-pixel.
		     */
		    image->bpp = (image->bpp + 7) / 8;
		  else if (image->depth > 24)
		    image->bpp = 4;
		  else if (image->depth > 16)
		    image->bpp = 3;
		  else
		    image->bpp = 2;
		}

	      for (j = 0; j < canvas->height;)
		{
		  start = j;
		  end = MIN (j + gimp_tile_height (), canvas->height);
		  scanlines = end - start;
		  bufp = buf;
		  for (jj = 0; jj < scanlines; jj++)
		    {
		      pixelp = ((guchar *) image->mem) + (j + jj) * image->bpl;
		      for (i = 0; i < canvas->width; i++)
			{
			  pixel = 0;
			  if (visual->byte_order == GDK_LSB_FIRST)
#if 1
			    {
			      k = image->bpp - 1;
			      switch (k)
				{
				case 3:
				  pixel |= (pixelp[3] << 24);
				  /* Fallthrough on purpose */
				case 2:
				  pixel |= (pixelp[2] << 16);
				  /* ditto */
				case 1:
				  pixel |= (pixelp[1] << 8);
				  /* ditto */
				case 0:
				  pixel |= (pixelp[0]);
				}
			    }
#else
			    for (k = 0; k < image->bpp; k++)
			      pixel |= (pixelp[k] << (k*8));
#endif
			  else
			    for (k = 0; k < image->bpp; k++)
			      pixel |= (pixelp[image->bpp - k - 1] << (k*8));

			  *bufp++ = rtbl[(pixel & rmask) >> rshift];
			  *bufp++ = gtbl[(pixel & gmask) >> gshift];
			  *bufp++ = btbl[(pixel & bmask) >> bshift];
			  pixelp += image->bpp;
			}
		    }
		  gimp_pixel_rgn_set_rect (&pixel_rgn, buf, 0, j, canvas->width, scanlines);
		  if (canvas->height >= 100)
		    gimp_progress_update ((double) j / canvas->height);
		  j += scanlines;
		}
	      if (canvas->height >= 100)
		gimp_progress_update (1.0);
	      g_free (buf);
	      g_free (rtbl);
	      g_free (gtbl);
	      g_free (btbl);
	      break;

	    default:
	      g_message ("Unsupported image visual");
	      return -1;
	    }
	  gimp_drawable_flush (drawable);

	  return image_ID;

	default:
	  if (!warned_unhandled)
	    {
	      g_message ("Unhandled operation %#x.",
			 GUINT16_FROM_LE (record.Function));
	      warned_unhandled = TRUE;
	    }
	  sync_record (record.Size, 0, fp);
	}

      if (record_counter % 10 == 0)
	gimp_progress_update (((double) ftell (fp) / 2)
			      / GUINT32_FROM_LE (wmf_head.FileSize));
    }

  /*NOTREACHED*/
  fclose (fp);
  return image_ID;
}

static gint
readparams (DWORD size,
	    guint nparams,
	    FILE *fp,
	    WORD *params)
{
  gulong nwords;

  if (size != 0)
    {
      nwords = GUINT32_FROM_LE (size) - WORDSIZE_WMFRECORD;

      if (nwords < nparams)
	{
	  fclose (fp);
	  g_message ("Too small record?");
	  return 0;
	}
    }

  if (nparams > NPARMWORDS)
    {
      fclose (fp);
      g_message ("Too large record?");
      return 0;
    }

  if (nparams > 0 && !ReadOK (fp, params, nparams  * sizeof (WORD)))
    {
      fclose (fp);
      g_message ("Read failed");
      return 0;
    }

  return 1;
}

static void
sync_record (DWORD size,
	     guint nparams,
	     FILE *fp)
{
  gulong nwords;

  nwords = GUINT32_FROM_LE (size) - WORDSIZE_WMFRECORD;
  if (nwords > nparams)
    fseek (fp, (nwords - nparams) * 2, SEEK_CUR);
}
