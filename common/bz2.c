/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 * Copyright (C) 1997 Daniel Risacher
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

/* bzip2 plug-in for the gimp */
/* this is almost exactly the same as the gz(ip) plugin by */
/* Dan Risacher & Josh, so feel free to go look there. */
/* GZ plugin adapted to BZ2 by Adam. I've left all other */
/* Error checking added by srn. */
/* credits intact since it was only a super-wussy mod. */

/* This is reads and writes bzip2ed image files for the Gimp
 *
 * You need to have bzip2 installed for it to work.
 *
 * It should work with file names of the form
 * filename.foo.bz2 where foo is some already-recognized extension
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#ifdef __EMX__
#include <fcntl.h>
#include <process.h>
#endif

#include <libgimp/gimp.h>

#include "libgimp/stdplugins-intl.h"


static void          query       (void);
static void          run         (gchar       *name,
				  gint         nparams,
				  GimpParam   *param,
				  gint        *nreturn_vals,
				  GimpParam  **return_vals);

static gint32        load_image  (gchar       *filename,
				  gint32       run_mode,
				  GimpPDBStatusType *status /* return value */);
static GimpPDBStatusType save_image  (gchar       *filename,
				      gint32       image_ID,
				      gint32       drawable_ID,
				      gint32       run_mode);

static gboolean   valid_file     (gchar       *filename);
static gchar    * find_extension (gchar       *filename);

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
  static gint nload_args = sizeof (load_args) / sizeof (load_args[0]);
  static gint nload_return_vals = (sizeof (load_return_vals) /
				   sizeof (load_return_vals[0]));

  static GimpParamDef save_args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE, "image", "Input image" },
    { GIMP_PDB_DRAWABLE, "drawable", "Drawable to save" },
    { GIMP_PDB_STRING, "filename", "The name of the file to save the image in" },
    { GIMP_PDB_STRING, "raw_filename", "The name of the file to save the image in" }
  };
  static gint nsave_args = sizeof (save_args) / sizeof (save_args[0]);

  gimp_install_procedure ("file_bz2_load",
                          "loads files compressed with bzip2",
                          "You need to have bzip2 installed.",
                          "Daniel Risacher",
                          "Daniel Risacher, Spencer Kimball and Peter Mattis",
                          "1995-1997",
                          "<Load>/bzip2",
			  NULL,
                          GIMP_PLUGIN,
                          nload_args, nload_return_vals,
                          load_args, load_return_vals);

  gimp_install_procedure ("file_bz2_save",
                          "saves files compressed with bzip2",
                          "You need to have bzip2 installed",
                          "Daniel Risacher",
                          "Daniel Risacher, Spencer Kimball and Peter Mattis",
                          "1995-1997",
                          "<Save>/bzip2",
			  "RGB*, GRAY*, INDEXED*",
                          GIMP_PLUGIN,
                          nsave_args, 0,
                          save_args, NULL);

  gimp_register_magic_load_handler ("file_bz2_load",
				    "xcf.bz2,bz2,xcfbz2",
				    "",
				    "0,string,BZh");
  gimp_register_save_handler ("file_bz2_save",
			      "xcf.bz2,bz2,xcfbz2",
			      "");
}

static void
run (gchar      *name,
     gint        nparams,
     GimpParam  *param,
     gint       *nreturn_vals,
     GimpParam **return_vals)
{
  static GimpParam   values[2];
  GimpRunModeType    run_mode;
  GimpPDBStatusType  status = GIMP_PDB_SUCCESS;
  gint32 image_ID;

  run_mode = param[0].data.d_int32;

  INIT_I18N();

  *nreturn_vals = 1;
  *return_vals  = values;
  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;

  if (strcmp (name, "file_bz2_load") == 0)
    {
      image_ID = load_image (param[1].data.d_string,
			     param[0].data.d_int32,
			     &status);

      if (image_ID != -1 &&
	  status == GIMP_PDB_SUCCESS)
	{
	  *nreturn_vals = 2;
	  values[1].type         = GIMP_PDB_IMAGE;
	  values[1].data.d_image = image_ID;
	}
    }
  else if (strcmp (name, "file_bz2_save") == 0)
    {
      switch (run_mode)
	{
	case GIMP_RUN_INTERACTIVE:
	  break;
	case GIMP_RUN_NONINTERACTIVE:
	  /*  Make sure all the arguments are there!  */
	  if (nparams != 4)
	    status = GIMP_PDB_CALLING_ERROR;
	  break;
	case GIMP_RUN_WITH_LAST_VALS:
	  break;

	default:
	  break;
	}

      if (status == GIMP_PDB_SUCCESS)
	{
	  status = save_image (param[3].data.d_string,
			       param[1].data.d_int32,
			       param[2].data.d_int32,
			       param[0].data.d_int32);
	}
    }
  else
    {
      status = GIMP_PDB_CALLING_ERROR;
    }

  values[0].data.d_status = status;
}

#ifdef __EMX__
static gint
spawn_bz (gchar *filename,
	  gchar *tmpname,
	  gchar *parms,
	  gint  *pid)
{
  FILE *f;
  gint tfd;
  
  if (!(f = fopen(filename,"wb")))
    {
      g_message ("bz: fopen failed: %s\n", g_strerror (errno));
      return -1;
    }

  /* save fileno(stdout) */
  tfd = dup (fileno (stdout));
  /* make stdout for this process be the output file */
  if (dup2 (fileno (f), fileno (stdout)) == -1)
    {
      g_message ("bz: dup2 failed: %s\n", g_strerror (errno));
      close (tfd);
      return -1;
    }
  fcntl (tfd, F_SETFD, FD_CLOEXEC);
  *pid = spawnlp (P_NOWAIT, "bzip2", "bzip2", parms, tmpname, NULL);
  fclose (f);
  /* restore fileno(stdout) */
  dup2 (tfd, fileno (stdout));
  close (tfd);
  if (*pid == -1)
    {
      g_message ("bz: spawn failed: %s\n", g_strerror (errno));
      return -1;
    }
  return 0;  
}
#endif

static GimpPDBStatusType
save_image (gchar  *filename,
	    gint32  image_ID,
	    gint32  drawable_ID,
	    gint32  run_mode)
{
  FILE  *f;
  gchar *ext;
  gchar *tmpname;
  gint   pid;
  gint   wpid;
  gint   process_status;

  if (NULL == (ext = find_extension (filename)))
    {
      g_message (_("bz2: can't open bzip2ed file without a "
		   "sensible extension\n"));
      return GIMP_PDB_CALLING_ERROR;
    }

  /* get a temp name with the right extension and save into it. */
  tmpname = gimp_temp_name (ext + 1);
  
  if (! (gimp_file_save (run_mode,
			 image_ID,
			 drawable_ID,
			 tmpname, 
			 tmpname) && valid_file (tmpname)) )
    {
      unlink (tmpname);
      g_free (tmpname);
      return GIMP_PDB_EXECUTION_ERROR;
    }

#ifndef __EMX__
  /* fork off a bzip2 process */
  if ((pid = fork ()) < 0)
    {
      g_message ("bz2: fork failed: %s\n", g_strerror (errno));
      g_free (tmpname);
      return GIMP_PDB_EXECUTION_ERROR;
    }
  else if (pid == 0)
    {
      if (!(f = fopen (filename, "w")))
	{
	  g_message ("bz2: fopen failed: %s\n", g_strerror (errno));
	  g_free (tmpname);
	  _exit(127);
	}

      /* make stdout for this process be the output file */
      if (-1 == dup2 (fileno (f), fileno (stdout)))
	g_message ("bz2: dup2 failed: %s\n", g_strerror (errno));

      /* and bzip2 into it */
      execlp ("bzip2", "bzip2", "-cf", tmpname, NULL);
      g_message ("bz2: exec failed: bzip2: %s\n", g_strerror (errno));
      g_free (tmpname);
      _exit (127);
    }
  else
#else /* __EMX__ */
  if (spawn_bz (filename, tmpname, "-cf", &pid) == -1)
    {
      g_free (tmpname);
      return GIMP_PDB_EXECUTION_ERROR;
    }
#endif
    {
      wpid = waitpid (pid, &process_status, 0);

      if ((wpid < 0)
	  || !WIFEXITED (process_status)
	  || (WEXITSTATUS (process_status) != 0))
	{
	  g_message ("bz2: bzip2 exited abnormally on file %s\n", tmpname);
	  g_free (tmpname);
	  return GIMP_PDB_EXECUTION_ERROR;
	}
    }

  unlink (tmpname);
  g_free (tmpname);

  return GIMP_PDB_SUCCESS;
}

static gint32
load_image (gchar             *filename,
	    gint32             run_mode,
	    GimpPDBStatusType *status /* return value */)
{
  gint32  image_ID;
  gchar  *ext;
  gchar  *tmpname;
  gint    pid;
  gint    wpid;
  gint    process_status;

  if (NULL == (ext = find_extension (filename)))
    {
      g_message (_("bz2: can't open bzip2ed file without a "
		   "sensible extension\n"));
      *status = GIMP_PDB_CALLING_ERROR;
      return -1;
    }

  /* find a temp name */
  tmpname = gimp_temp_name (ext + 1);

#ifndef __EMX__
  /* fork off a bzip2 and wait for it */
  if ((pid = fork ()) < 0)
    {
      g_message ("bz2: fork failed: %s\n", g_strerror (errno));
      g_free (tmpname);
      *status = GIMP_PDB_EXECUTION_ERROR;
      return -1;
    }
  else if (pid == 0)  /* child process */
    {
      FILE *f;
       if (!(f = fopen (tmpname,"w")))
	 {
	   g_message ("bz2: fopen failed: %s\n", g_strerror (errno));
	   g_free (tmpname);
	   _exit (127);
	 }

      /* make stdout for this child process be the temp file */
      if (-1 == dup2 (fileno (f), fileno (stdout)))
	g_message ("bz2: dup2 failed: %s\n", g_strerror (errno));

      /* and unzip into it */
      execlp ("bzip2", "bzip2", "-cfd", filename, NULL);
      g_message ("bz2: exec failed: bunzip2: %s\n", g_strerror (errno));
      g_free (tmpname);
      _exit (127);
    }
  else  /* parent process */
#else /* __EMX__ */
  if (spawn_bz (tmpname, filename,"-cfd", &pid) == -1) 
    {
      g_free (tmpname);
      *status = GIMP_PDB_EXECUTION_ERROR;
      return -1;
    }
#endif
    {
      wpid = waitpid (pid, &process_status, 0);

      if ((wpid < 0)
	  || !WIFEXITED (process_status)
	  || (WEXITSTATUS (process_status) != 0))
	{
	  g_message ("bz2: bzip2 exited abnormally on file %s\n", filename);
	  g_free (tmpname);
	  *status = GIMP_PDB_EXECUTION_ERROR;
	  return -1;
	}
    }

  /* now that we un-bzip2ed it, load the temp file */

  image_ID = gimp_file_load (run_mode, tmpname, tmpname);
  
  unlink (tmpname);
  g_free (tmpname);

  if (image_ID != -1)
    {
      *status = GIMP_PDB_SUCCESS;
      gimp_image_set_filename (image_ID, filename);
    }
  else
    *status = GIMP_PDB_EXECUTION_ERROR;

  return image_ID;
}

static gboolean
valid_file (gchar* filename)
{
  gint stat_res;
  struct stat buf;

  stat_res = stat (filename, &buf);

  if ((0 == stat_res) && (buf.st_size > 0))
    return TRUE;
  else
    return FALSE;
}

static gchar *
find_extension (gchar* filename)
{
  gchar *filename_copy;
  gchar *ext;

  /* we never free this copy - aren't we evil! */
  filename_copy = g_strdup (filename);

  /* find the extension, boy! */
  ext = strrchr (filename_copy, '.');

  while (TRUE)
    {
      if (!ext || ext[1] == '\0' || strchr (ext, '/'))
	{
	  return NULL;
	}
      if (0 == g_ascii_strcasecmp (ext, ".xcfbz2"))
	{
	  return ".xcf";  /* we've found it */
	}
      if (0 != g_ascii_strcasecmp (ext, ".bz2"))
	{
	  return ext;
	}
      else
	{
	  /* we found ".bz2" so strip it, loop back, and look again */
	  *ext = '\0';
	  ext = strrchr (filename_copy, '.');
	}
    }
}
