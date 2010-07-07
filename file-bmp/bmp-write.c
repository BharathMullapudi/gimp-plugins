/* bmpwrite.c   Writes Bitmap files. Even RLE encoded ones.      */
/*              (Windows (TM) doesn't read all of those, but who */
/*              cares? ;-)                                       */
/*              I changed a few things over the time, so perhaps */
/*              it dos now, but now there's no Windows left on   */
/*              my computer...                                   */

/* Alexander.Schulz@stud.uni-karlsruhe.de                        */

/*
 * GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * ----------------------------------------------------------------------------
 */

#include "config.h"

#include <errno.h>
#include <string.h>

#include <glib/gstdio.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "bmp.h"

#include "libgimp/stdplugins-intl.h"

typedef enum
{
  RGB_565,
  RGBA_5551,
  RGB_555,
  RGB_888,
  RGBA_8888,
  RGBX_8888
} RGBMode;

static struct
{
  RGBMode rgb_format;
  gint    encoded;
} BMPSaveData;

static gint    cur_progress = 0;
static gint    max_progress = 0;


static  void      write_image     (FILE   *f,
                                   guchar *src,
                                   gint    width,
                                   gint    height,
                                   gint    encoded,
                                   gint    channels,
                                   gint    bpp,
                                   gint    spzeile,
                                   gint    MapSize,
                                   RGBMode rgb_format);

static  gboolean  save_dialog     (gint    channels);


static void
FromL (gint32  wert,
       guchar *bopuffer)
{
  bopuffer[0] = (wert & 0x000000ff)>>0x00;
  bopuffer[1] = (wert & 0x0000ff00)>>0x08;
  bopuffer[2] = (wert & 0x00ff0000)>>0x10;
  bopuffer[3] = (wert & 0xff000000)>>0x18;
}

static void
FromS (gint16  wert,
       guchar *bopuffer)
{
  bopuffer[0] = (wert & 0x00ff)>>0x00;
  bopuffer[1] = (wert & 0xff00)>>0x08;
}

static void
write_color_map (FILE *f,
                 gint  red[MAXCOLORS],
                 gint  green[MAXCOLORS],
                 gint  blue[MAXCOLORS],
                 gint  size)
{
  gchar trgb[4];
  gint  i;

  size /= 4;
  trgb[3] = 0;
  for (i = 0; i < size; i++)
    {
      trgb[0] = (guchar) blue[i];
      trgb[1] = (guchar) green[i];
      trgb[2] = (guchar) red[i];
      Write (f, trgb, 4);
    }
}

static gboolean
warning_dialog (const gchar *primary,
                const gchar *secondary)
{
  GtkWidget *dialog;
  gboolean   ok;

  dialog = gtk_message_dialog_new (NULL, 0,
                                   GTK_MESSAGE_WARNING, GTK_BUTTONS_OK_CANCEL,
                                   "%s", primary);

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            "%s", secondary);

  gimp_window_set_transient (GTK_WINDOW (dialog));

  ok = (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK);

  gtk_widget_destroy (dialog);

  return ok;
}

GimpPDBStatusType
WriteBMP (const gchar  *filename,
          gint32        image,
          gint32        drawable_ID,
          GError      **error)
{
  FILE          *outfile;
  gint           Red[MAXCOLORS];
  gint           Green[MAXCOLORS];
  gint           Blue[MAXCOLORS];
  guchar        *cmap;
  gint           rows, cols, Spcols, channels, MapSize, SpZeile;
  glong          BitsPerPixel;
  gint           colors;
  guchar        *pixels;
  GimpPixelRgn   pixel_rgn;
  GimpDrawable  *drawable;
  GimpImageType  drawable_type;
  guchar         puffer[50];
  gint           i;
  gint           mask_info_size;
  guint32        Mask[4];

  drawable = gimp_drawable_get (drawable_ID);
  drawable_type = gimp_drawable_type (drawable_ID);

  gimp_pixel_rgn_init (&pixel_rgn, drawable,
                       0, 0, drawable->width, drawable->height, FALSE, FALSE);

  switch (drawable_type)
    {
    case GIMP_RGBA_IMAGE:
      colors       = 0;
      BitsPerPixel = 32;
      MapSize      = 0;
      channels     = 4;
      BMPSaveData.rgb_format = RGBA_8888;
      break;

    case GIMP_RGB_IMAGE:
      colors       = 0;
      BitsPerPixel = 24;
      MapSize      = 0;
      channels     = 3;
      BMPSaveData.rgb_format = RGB_888;
      break;

    case GIMP_GRAYA_IMAGE:
      if (interactive && !warning_dialog (_("Cannot save indexed image with "
    					    "transparency in BMP file format."),
                                          _("Alpha channel will be ignored.")))
          return GIMP_PDB_CANCEL;

     /* fallthrough */

    case GIMP_GRAY_IMAGE:
      colors       = 256;
      BitsPerPixel = 8;
      MapSize      = 1024;
      if (drawable_type == GIMP_GRAY_IMAGE) channels = 1;
      else channels = 2;
      for (i = 0; i < colors; i++)
        {
          Red[i]   = i;
          Green[i] = i;
          Blue[i]  = i;
        }
      break;

    case GIMP_INDEXEDA_IMAGE:
      if (interactive && !warning_dialog (_("Cannot save indexed image with "
    			                    "transparency in BMP file format."),
                                          _("Alpha channel will be ignored.")))
          return GIMP_PDB_CANCEL;

     /* fallthrough */

    case GIMP_INDEXED_IMAGE:
      cmap     = gimp_image_get_colormap (image, &colors);
      MapSize  = 4 * colors;
      if (drawable_type == GIMP_INDEXED_IMAGE) channels = 1;
      else channels = 2;

      if (colors > 16)
        BitsPerPixel = 8;
      else if (colors > 2)
        BitsPerPixel = 4;
      else
        BitsPerPixel = 1;

      for (i = 0; i < colors; i++)
        {
          Red[i]   = *cmap++;
          Green[i] = *cmap++;
          Blue[i]  = *cmap++;
        }
      break;

    default:
      g_assert_not_reached ();
    }

  /* Perhaps someone wants RLE encoded Bitmaps */
  BMPSaveData.encoded = 0;
  mask_info_size = 0;

  if (!interactive && lastvals)
    {
      gimp_get_data (SAVE_PROC, &BMPSaveData);
    }

  if ((BitsPerPixel == 8 || BitsPerPixel == 4) && interactive)
    {
      if (! save_dialog (1))
        return GIMP_PDB_CANCEL;
    }
  else if ((BitsPerPixel == 24 || BitsPerPixel == 32))
    {
      if (interactive && !save_dialog (channels))
        return GIMP_PDB_CANCEL;

      switch (BMPSaveData.rgb_format)
        {
        case RGB_888:
          BitsPerPixel = 24;
          break;
        case RGBA_8888:
          BitsPerPixel = 32;
          break;
        case RGBX_8888:
          BitsPerPixel = 32;
          mask_info_size = 16;
          break;
        case RGB_565:
          BitsPerPixel = 16;
          mask_info_size = 16;
          break;
        case RGBA_5551:
          BitsPerPixel = 16;
          mask_info_size = 16;
          break;
        case RGB_555:
          BitsPerPixel = 16;
          break;
        default:
          g_return_val_if_reached (GIMP_PDB_EXECUTION_ERROR);
        }
    }

  gimp_set_data (SAVE_PROC, &BMPSaveData, sizeof (BMPSaveData));

  /* Let's take some file */
  outfile = g_fopen (filename, "wb");
  if (!outfile)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   _("Could not open '%s' for writing: %s"),
                   gimp_filename_to_utf8 (filename), g_strerror (errno));
      return GIMP_PDB_EXECUTION_ERROR;
    }

  /* fetch the image */
  pixels = g_new (guchar, drawable->width * drawable->height * channels);
  gimp_pixel_rgn_get_rect (&pixel_rgn, pixels,
                           0, 0, drawable->width, drawable->height);

  /* And let's begin the progress */
  gimp_progress_init_printf (_("Saving '%s'"),
                             gimp_filename_to_utf8 (filename));

  cur_progress = 0;
  max_progress = drawable->height;

  /* Now, we need some further information ... */
  cols = drawable->width;
  rows = drawable->height;

  /* ... that we write to our headers. */
  if ((BitsPerPixel <= 8) && (cols % (8 / BitsPerPixel)))
    Spcols = (((cols / (8 / BitsPerPixel)) + 1) * (8 / BitsPerPixel));
  else
    Spcols = cols;

  if ((((Spcols * BitsPerPixel) / 8) % 4) == 0)
    SpZeile = ((Spcols * BitsPerPixel) / 8);
  else
    SpZeile = ((gint) (((Spcols * BitsPerPixel) / 8) / 4) + 1) * 4;

  Bitmap_File_Head.bfSize    = 0x36 + MapSize + (rows * SpZeile) + mask_info_size;
  Bitmap_File_Head.zzHotX    = 0;
  Bitmap_File_Head.zzHotY    = 0;
  Bitmap_File_Head.bfOffs    = 0x36 + MapSize + mask_info_size;
  Bitmap_File_Head.biSize    = 40 + (mask_info_size > 12 ? mask_info_size : 0);

  Bitmap_Head.biWidth  = cols;
  Bitmap_Head.biHeight = rows;
  Bitmap_Head.biPlanes = 1;
  Bitmap_Head.biBitCnt = BitsPerPixel;

  if (BMPSaveData.encoded == 0)
  {
    if (!mask_info_size) Bitmap_Head.biCompr = 0;
    else Bitmap_Head.biCompr = 3;
  }
  else if (BitsPerPixel == 8)
    Bitmap_Head.biCompr = 1;
  else if (BitsPerPixel == 4)
    Bitmap_Head.biCompr = 2;
  else
    Bitmap_Head.biCompr = 0;

  Bitmap_Head.biSizeIm = SpZeile * rows;

  {
    gdouble xresolution;
    gdouble yresolution;
    gimp_image_get_resolution (image, &xresolution, &yresolution);

    if (xresolution > GIMP_MIN_RESOLUTION &&
        yresolution > GIMP_MIN_RESOLUTION)
      {
        /*
         * xresolution and yresolution are in dots per inch.
         * the BMP spec says that biXPels and biYPels are in
         * pixels per meter as long ints (actually, "DWORDS"),
         * so...
         *    n dots    inch     100 cm   m dots
         *    ------ * ------- * ------ = ------
         *     inch    2.54 cm     m       inch
         *
         * We add 0.5 for proper rounding.
         */
        Bitmap_Head.biXPels = (long int) (xresolution * 100.0 / 2.54 + 0.5);
        Bitmap_Head.biYPels = (long int) (yresolution * 100.0 / 2.54 + 0.5);
      }
  }

  if (BitsPerPixel <= 8)
    Bitmap_Head.biClrUsed = colors;
  else
    Bitmap_Head.biClrUsed = 0;

  Bitmap_Head.biClrImp = Bitmap_Head.biClrUsed;

#ifdef DEBUG
  printf("\nSize: %u, Colors: %u, Bits: %u, Width: %u, Height: %u, Comp: %u, Zeile: %u\n",
         (int)Bitmap_File_Head.bfSize,(int)Bitmap_Head.biClrUsed,Bitmap_Head.biBitCnt,(int)Bitmap_Head.biWidth,
         (int)Bitmap_Head.biHeight, (int)Bitmap_Head.biCompr,SpZeile);
#endif

  /* And now write the header and the colormap (if any) to disk */

  Write (outfile, "BM", 2);

  FromL (Bitmap_File_Head.bfSize, &puffer[0x00]);
  FromS (Bitmap_File_Head.zzHotX, &puffer[0x04]);
  FromS (Bitmap_File_Head.zzHotY, &puffer[0x06]);
  FromL (Bitmap_File_Head.bfOffs, &puffer[0x08]);
  FromL (Bitmap_File_Head.biSize, &puffer[0x0C]);

  Write (outfile, puffer, 16);

  FromL (Bitmap_Head.biWidth, &puffer[0x00]);
  FromL (Bitmap_Head.biHeight, &puffer[0x04]);
  FromS (Bitmap_Head.biPlanes, &puffer[0x08]);
  FromS (Bitmap_Head.biBitCnt, &puffer[0x0A]);
  FromL (Bitmap_Head.biCompr, &puffer[0x0C]);
  FromL (Bitmap_Head.biSizeIm, &puffer[0x10]);
  FromL (Bitmap_Head.biXPels, &puffer[0x14]);
  FromL (Bitmap_Head.biYPels, &puffer[0x18]);
  FromL (Bitmap_Head.biClrUsed, &puffer[0x1C]);
  FromL (Bitmap_Head.biClrImp, &puffer[0x20]);

  Write (outfile, puffer, 36);
  write_color_map (outfile, Red, Green, Blue, MapSize);

  if (mask_info_size)
    {
      switch (BMPSaveData.rgb_format)
        {
        default:
        case RGB_888:
        case RGBX_8888:
          Mask[0] = 0xff000000;
          Mask[1] = 0x00ff0000;
          Mask[2] = 0x0000ff00;
          Mask[3] = 0x00000000;
          break;

        case RGBA_8888:
          Mask[0] = 0xff000000;
          Mask[1] = 0x00ff0000;
          Mask[2] = 0x0000ff00;
          Mask[3] = 0x000000ff;
          break;

        case RGB_565:
          Mask[0] = 0xf800;
          Mask[1] = 0x7e0;
          Mask[2] = 0x1f;
          Mask[3] = 0x0;
          break;

        case RGBA_5551:
          Mask[0] = 0x7c00;
          Mask[1] = 0x3e0;
          Mask[2] = 0x1f;
          Mask[3] = 0x8000;
          break;

        case RGB_555:
          Mask[0] = 0x7c00;
          Mask[1] = 0x3e0;
          Mask[2] = 0x1f;
          Mask[3] = 0x0;
          break;
        }

      FromL (Mask[0], &puffer[0x00]);
      FromL (Mask[1], &puffer[0x04]);
      FromL (Mask[2], &puffer[0x08]);
      FromL (Mask[3], &puffer[0x0C]);
      Write (outfile, puffer, mask_info_size);
    }

  /* After that is done, we write the image ... */

  write_image (outfile,
               pixels, cols, rows,
               BMPSaveData.encoded, channels, BitsPerPixel, SpZeile, MapSize,
               BMPSaveData.rgb_format);

  /* ... and exit normally */

  fclose (outfile);
  gimp_drawable_detach (drawable);
  g_free (pixels);

  return GIMP_PDB_SUCCESS;
}

static inline void Make565(guchar r, guchar g, guchar b, guchar *buf)
{
    gint p;
    p = (((gint)(r / 255.0 * 31.0 + 0.5))<<11) |
        (((gint)(g / 255.0 * 63.0 + 0.5))<<5)  |
         ((gint)(b / 255.0 * 31.0 + 0.5));
    buf[0] = (guchar)(p & 0xff);
    buf[1] = (guchar)(p>>8);
}

static inline void Make5551(guchar r, guchar g, guchar b, guchar a, guchar *buf)
{
    gint p;
    p = (((gint)(r / 255.0 * 31.0 + 0.5))<<10) |
        (((gint)(g / 255.0 * 31.0 + 0.5))<<5)  |
         ((gint)(b / 255.0 * 31.0 + 0.5))      |
         ((gint)(a / 255.0 + 0.5)<<15);
    buf[0] = (guchar)(p & 0xff);
    buf[1] = (guchar)(p>>8);
}

static void
write_image (FILE   *f,
             guchar *src,
             gint    width,
             gint    height,
             gint    encoded,
             gint    channels,
             gint    bpp,
             gint    spzeile,
             gint    MapSize,
             RGBMode rgb_format)
{
  guchar  buf[16] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0 };
  guchar  puffer[8];
  guchar *temp, v;
  guchar *row, *ketten;
  gint    xpos, ypos, i, j, rowstride, length, thiswidth;
  gint    breite, k;
  guchar  n, r, g, b, a;

  xpos = 0;
  rowstride = width * channels;

  /* We'll begin with the 16/24/32 bit Bitmaps, they are easy :-) */

  if (bpp > 8)
    {
      for (ypos = height - 1; ypos >= 0; ypos--)  /* for each row   */
        {
          for (i = 0; i < width; i++)  /* for each pixel */
            {
              temp = src + (ypos * rowstride) + (xpos * channels);
              switch (rgb_format)
                {
                default:
                case RGB_888:
                  buf[2] = *temp++;
                  buf[1] = *temp++;
                  buf[0] = *temp++;
                  xpos++;
                  if (channels > 3 && (guchar) *temp == 0)
                    buf[0] = buf[1] = buf[2] = 0xff;
                  Write (f, buf, 3);
                  break;
                case RGBX_8888:
                  buf[0] = 0;
                  buf[3] = *temp++;
                  buf[2] = *temp++;
                  buf[1] = *temp++;
                  xpos++;
                  if (channels > 3 && (guchar) *temp == 0)
                    buf[0] = buf[1] = buf[2] = 0xff;
                  Write (f, buf, 4);
                  break;
                case RGBA_8888:
                  buf[2] = *temp++;
                  buf[1] = *temp++;
                  buf[0] = *temp++;
                  buf[3] = *temp;
                  xpos++;
                  Write (f, buf, 4);
                  break;
                case RGB_565:
                  r = *temp++;
                  g = *temp++;
                  b = *temp++;
                  if (channels > 3 && (guchar) *temp == 0)
                    r = g = b = 0xff;
                  Make565 (r, g, b, buf);
                  xpos++;
                  Write (f, buf, 2);
                  break;
                case RGB_555:
                  r = *temp++;
                  g = *temp++;
                  b = *temp++;
                  if (channels > 3 && (guchar) *temp == 0)
                    r = g = b = 0xff;
                  Make5551 (r, g, b, 0x0, buf);
                  xpos++;
                  Write (f, buf, 2);
                  break;
                case RGBA_5551:
                  r = *temp++;
                  g = *temp++;
                  b = *temp++;
                  a = *temp;
                  Make5551 (r, g, b, a, buf);
                  xpos++;
                  Write (f, buf, 2);
                  break;
                }
            }

          Write (f, &buf[4], spzeile - (width * (bpp/8)));

          cur_progress++;
          if ((cur_progress % 5) == 0)
            gimp_progress_update ((gdouble) cur_progress /
                                  (gdouble) max_progress);

          xpos = 0;
        }
    }
  else
    {
      switch (encoded)  /* now it gets more difficult */
        {               /* uncompressed 1,4 and 8 bit */
        case 0:
          {
            thiswidth = (width / (8 / bpp));
            if (width % (8 / bpp))
              thiswidth++;

            for (ypos = height - 1; ypos >= 0; ypos--) /* for each row */
              {
                for (xpos = 0; xpos < width;)  /* for each _byte_ */
                  {
                    v = 0;
                    for (i = 1;
                         (i <= (8 / bpp)) && (xpos < width);
                         i++, xpos++)  /* for each pixel */
                      {
                        temp = src + (ypos * rowstride) + (xpos * channels);
                        if (channels > 1 && *(temp+1) == 0) *temp = 0x0;
                        v=v | ((guchar) *temp << (8 - (i * bpp)));
                      }
                    Write (f, &v, 1);
                  }
                Write (f, &buf[3], spzeile - thiswidth);
                xpos = 0;

                cur_progress++;
                if ((cur_progress % 5) == 0)
                  gimp_progress_update ((gdouble) cur_progress /
                                        (gdouble) max_progress);
              }
            break;
          }
        default:
          {              /* Save RLE encoded file, quite difficult */
            length = 0;
            buf[12] = 0;
            buf[13] = 1;
            buf[14] = 0;
            buf[15] = 0;
            row = g_new (guchar, width / (8 / bpp) + 10);
            ketten = g_new (guchar, width / (8 / bpp) + 10);
            for (ypos = height - 1; ypos >= 0; ypos--)
              { /* each row separately */
                j = 0;
                /* first copy the pixels to a buffer,
                 * making one byte from two 4bit pixels
                 */
                for (xpos = 0; xpos < width;)
                  {
                    v = 0;
                    for (i = 1;
                         (i <= (8 / bpp)) && (xpos < width);
                         i++, xpos++)
                      { /* for each pixel */
                        temp = src + (ypos * rowstride) + (xpos * channels);
                        if (channels > 1 && *(temp+1) == 0) *temp = 0x0;
                        v = v | ((guchar) * temp << (8 - (i * bpp)));
                      }
                    row[j++] = v;
                  }
                breite = width / (8 / bpp);
                if (width % (8 / bpp))
                  breite++;
                /* then check for strings of equal bytes */
                for (i = 0; i < breite; i += j)
                  {
                    j = 0;
                    while ((i + j < breite) &&
                           (j < (255 / (8 / bpp))) &&
                           (row[i + j] == row[i]))
                      j++;

                    ketten[i] = j;
                  }

                /* then write the strings and the other pixels to the file */
                for (i = 0; i < breite;)
                  {
                    if (ketten[i] < 3)
                      /* strings of different pixels ... */
                      {
                        j = 0;
                        while ((i + j < breite) &&
                               (j < (255 / (8 / bpp))) &&
                               (ketten[i + j] < 3))
                          j += ketten[i + j];

                        /* this can only happen if j jumps over
                         * the end with a 2 in ketten[i+j]
                         */
                        if (j > (255 / (8 / bpp)))
                          j -= 2;
                        /* 00 01 and 00 02 are reserved */
                        if (j > 2)
                          {
                            Write (f, &buf[12], 1);
                            n = j * (8 / bpp);
                            if (n + i * (8 / bpp) > width)
                              n--;
                            Write (f, &n, 1);
                            length += 2;
                            Write (f, &row[i], j);
                            length += j;
                            if ((j) % 2)
                              {
                                Write (f, &buf[12], 1);
                                length++;
                              }
                          }
                        else
                          {
                            for (k = i; k < i + j; k++)
                              {
                                n = (8 / bpp);
                                if (n + i * (8 / bpp) > width)
                                  n--;
                                Write (f, &n, 1);
                                Write (f, &row[k], 1);
                                /*printf("%i.#|",n); */
                                length += 2;
                              }
                          }
                        i += j;
                      }
                    else
                      /* strings of equal pixels */
                      {
                        n = ketten[i] * (8 / bpp);
                        if (n + i * (8 / bpp) > width)
                          n--;
                        Write (f, &n, 1);
                        Write (f, &row[i], 1);
                        i += ketten[i];
                        length += 2;
                      }
                  }
                Write (f, &buf[14], 2);          /* End of row */
                length += 2;

                cur_progress++;
                if ((cur_progress % 5) == 0)
                  gimp_progress_update ((gdouble) cur_progress /
                                        (gdouble) max_progress);
              }
            fseek (f, -2, SEEK_CUR);     /* Overwrite last End of row ... */
            Write (f, &buf[12], 2);      /* ... with End of file */

            fseek (f, 0x22, SEEK_SET);            /* Write length of image */
            FromL (length, puffer);
            Write (f, puffer, 4);
            fseek (f, 0x02, SEEK_SET);            /* Write length of file */
            length += (0x36 + MapSize);
            FromL (length, puffer);
            Write (f, puffer, 4);
            g_free (ketten);
            g_free (row);
            break;
          }
        }
    }

  gimp_progress_update (1);
}

static void
format_callback (GtkToggleButton *toggle,
                 gpointer         data)
{
  if (gtk_toggle_button_get_active (toggle))
    BMPSaveData.rgb_format = GPOINTER_TO_INT (data);
}

static gboolean
save_dialog (gint channels)
{
  GtkWidget *dialog;
  GtkWidget *toggle;
  GtkWidget *vbox_main;
  GtkWidget *vbox;
  GtkWidget *vbox2;
  GtkWidget *expander;
  GtkWidget *frame;
  GSList    *group;
  gboolean   run;

  dialog = gimp_export_dialog_new (_("BMP"), PLUG_IN_BINARY, SAVE_PROC);

  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

  vbox_main = gtk_vbox_new (FALSE, 12);
  gtk_container_set_border_width (GTK_CONTAINER (vbox_main), 12);
  gtk_container_add (GTK_CONTAINER (gimp_export_dialog_get_content_area (dialog)),
                     vbox_main);
  gtk_widget_show (vbox_main);

  toggle = gtk_check_button_new_with_mnemonic (_("_Run-Length Encoded"));
  gtk_box_pack_start (GTK_BOX (vbox_main), toggle, FALSE, FALSE, 0);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle),
                                BMPSaveData.encoded);
  gtk_widget_show (toggle);
  if (channels > 1)
    gtk_widget_set_sensitive (toggle, FALSE);

  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &BMPSaveData.encoded);

  expander = gtk_expander_new_with_mnemonic (_("_Advanced Options"));

  gtk_box_pack_start (GTK_BOX (vbox_main), expander, TRUE, TRUE, 0);
  gtk_widget_show (expander);

  if (channels < 3)
    gtk_widget_set_sensitive (expander, FALSE);

  vbox2 = gtk_vbox_new (FALSE, 12);
  gtk_container_set_border_width (GTK_CONTAINER (vbox2), 12);
  gtk_container_add (GTK_CONTAINER (expander), vbox2);
  gtk_widget_show (vbox2);

  group = NULL;

  frame = gimp_frame_new (_("16 bits"));
  gtk_box_pack_start (GTK_BOX (vbox2), frame, TRUE, TRUE, 0);
  gtk_widget_show (frame);

  vbox = gtk_vbox_new (FALSE, 6);
  gtk_container_add (GTK_CONTAINER (frame), vbox);
  gtk_widget_show (vbox);

  toggle = gtk_radio_button_new_with_label (group, "R5 G6 B5");
  group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (toggle));
  gtk_box_pack_start (GTK_BOX (vbox), toggle, FALSE, FALSE, 0);
  gtk_widget_show (toggle);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (format_callback),
                    GINT_TO_POINTER (RGB_565));

  toggle = gtk_radio_button_new_with_label (group, "A1 R5 G5 B5");
  group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (toggle));
  gtk_box_pack_start (GTK_BOX (vbox), toggle, FALSE, FALSE, 0);

  if (channels < 4)
    gtk_widget_set_sensitive (toggle, FALSE);

  gtk_widget_show (toggle);

  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (format_callback),
                    GINT_TO_POINTER (RGBA_5551));
  toggle = gtk_radio_button_new_with_label (group, "X1 R5 G5 B5");
  group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (toggle));
  gtk_box_pack_start (GTK_BOX (vbox), toggle, FALSE, FALSE, 0);
  gtk_widget_show (toggle);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (format_callback),
                    GINT_TO_POINTER (RGB_555));

  frame = gimp_frame_new (_("24 bits"));
  gtk_box_pack_start (GTK_BOX (vbox2), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  toggle = gtk_radio_button_new_with_label (group, "R8 G8 B8");
  group = gtk_radio_button_get_group (GTK_RADIO_BUTTON(toggle));
  gtk_container_add (GTK_CONTAINER (frame), toggle);
  gtk_widget_show (toggle);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (format_callback),
                    GINT_TO_POINTER (RGB_888));
  if (channels < 4)
    {
      BMPSaveData.rgb_format = RGB_888;
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), TRUE);
    }

  frame = gimp_frame_new (_("32 bits"));
  gtk_box_pack_start (GTK_BOX (vbox2), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  vbox = gtk_vbox_new (FALSE, 6);
  gtk_container_add (GTK_CONTAINER (frame), vbox);
  gtk_widget_show (vbox);

  toggle = gtk_radio_button_new_with_label (group, "A8 R8 G8 B8");
  group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (toggle));
  gtk_container_add (GTK_CONTAINER (vbox), toggle);
  gtk_widget_show (toggle);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (format_callback),
                    GINT_TO_POINTER (RGBA_8888));


  if (channels < 4)
    {
      gtk_widget_set_sensitive (toggle, FALSE);
    }
  else
    {
      BMPSaveData.rgb_format = RGBA_8888;
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), TRUE);
    }

  toggle = gtk_radio_button_new_with_label (group, "X8 R8 G8 B8");
  group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (toggle));
  gtk_container_add (GTK_CONTAINER (vbox), toggle);
  gtk_widget_show (toggle);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (format_callback),
                    GINT_TO_POINTER (RGBX_8888));

  gtk_widget_show (dialog);

  run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

  gtk_widget_destroy (dialog);

  return run;
}
