#include "config.h"

#include <string.h> /* memcpy, strcpy, strlen */

#include "siod.h"
#include "siod-wrapper.h"
#include <glib.h>
#include "libgimp/gimp.h"
#include "script-fu-constants.h"
#include "script-fu-enums.h"
#include "script-fu-scripts.h"
#include "script-fu-server.h"

/* global variables declared by the scheme interpreter */
extern FILE* siod_output;
extern int siod_verbose_level;
extern char siod_err_msg[];
extern LISP repl_return_val;


/* defined in regex.c. not exported by regex.h */
extern void  init_regex   (void);

/* defined in siodp.h but this file cannot be imported... */
extern long nlength (LISP obj);
extern LISP leval_define (LISP args, LISP env);


/* wrapper functions */
FILE *
siod_get_output_file (void)
{
  return siod_output;
}

void 
siod_set_output_file (FILE *file)
{
  siod_output = file;
}

int 
siod_get_verbose_level (void)
{
  return siod_verbose_level;
}


void 
siod_set_verbose_level (int verbose_level)
{
  siod_verbose_level = verbose_level;
}


void
siod_print_welcome (void)
{
  print_welcome ();
}


int
siod_interpret_string (const char *expr)
{
  return repl_c_string ((char *)expr, 0, 0, 1);
}


const char *
siod_get_error_msg (void)
{
  return siod_err_msg;
}

const char *
siod_get_success_msg (void)
{
  char *response;

  if (TYPEP (repl_return_val, tc_string))
    response = get_c_string (repl_return_val);
  else
    response = "Success";

  return (const char *) response;
}


static void  init_constants           (void);
static void  init_procedures          (void);

static gboolean register_scripts = FALSE;

void 
siod_init (gint local_register_scripts)
{
  char *siod_argv[] =
  {
    "siod",
    "-h100000:10",
    "-g0",
    "-o1000",
    "-s200000",
    "-n2048",
    "-v0",
  };

  register_scripts = local_register_scripts;

  /* init the interpreter */
  process_cla (G_N_ELEMENTS (siod_argv), siod_argv, 1);
  init_storage ();
  init_subrs ();
  init_trace ();
  init_regex ();

  /* register in the interpreter the gimp functions and types. */
  init_procedures ();
  init_constants ();

}

static void  convert_string           (char *str);
static gint  sputs_fcn                (char *st,
				       void  *dest);
static LISP  lprin1s                  (LISP   exp,
				       char *dest);
static LISP  marshall_proc_db_call    (LISP a);
static LISP  script_fu_register_call  (LISP a);
static LISP  script_fu_quit_call      (LISP a);


/*********
	  
	  Below can be found the functions responsible for registering the gimp functions
	  and types against the scheme interpreter.
	  
********/


static void
init_procedures (void)
{
  gchar          **proc_list;
  gchar           *proc_name;
  gchar           *arg_name;
  gchar           *proc_blurb;
  gchar           *proc_help;
  gchar           *proc_author;
  gchar           *proc_copyright;
  gchar           *proc_date;
  GimpPDBProcType  proc_type;
  gint             nparams;
  gint             nreturn_vals;
  GimpParamDef    *params;
  GimpParamDef    *return_vals;
  gint             num_procs;
  gint             i;

  /*  register the database execution procedure  */
  init_lsubr ("gimp-proc-db-call",  marshall_proc_db_call);
  init_lsubr ("script-fu-register", script_fu_register_call);
  init_lsubr ("script-fu-quit",     script_fu_quit_call);

  gimp_procedural_db_query (".*", ".*", ".*", ".*", ".*", ".*", ".*", 
			    &num_procs, &proc_list);

  /*  Register each procedure as a scheme func  */
  for (i = 0; i < num_procs; i++)
    {
      proc_name = g_strdup (proc_list[i]);

      /*  lookup the procedure  */
      if (gimp_procedural_db_proc_info (proc_name, 
					&proc_blurb, 
					&proc_help, 
					&proc_author,
					&proc_copyright, 
					&proc_date, 
					&proc_type, 
					&nparams, &nreturn_vals,
					&params, &return_vals))
	{
	  LISP args = NIL;
	  LISP code = NIL;
	  gint j;

	  /*  convert the names to scheme-like naming conventions  */
	  convert_string (proc_name);

	  /*  create a new scheme func that calls gimp-proc-db-call  */
	  for (j = 0; j < nparams; j++)
	    {
	      arg_name = g_strdup (params[j].name);
	      convert_string (arg_name);
	      args = cons (cintern (arg_name), args);
	      code = cons (cintern (arg_name), code);
	    }

	  /*  reverse the list  */
	  args = nreverse (args);
	  code = nreverse (code);

	  /*  set the scheme-based procedure name  */
	  args = cons (cintern (proc_name), args);

	  /*  set the acture pdb procedure name  */
	  code = cons (cons (cintern ("quote"), 
			     cons (cintern (proc_list[i]), NIL)), 
		       code);
	  code = cons (cintern ("gimp-proc-db-call"), code);

	  leval_define (cons (args, cons (code, NIL)), NIL);

	  /*  free the queried information  */
	  g_free (proc_blurb);
	  g_free (proc_help);
	  g_free (proc_author);
	  g_free (proc_copyright);
	  g_free (proc_date);
	  gimp_destroy_paramdefs (params, nparams);
	  gimp_destroy_paramdefs (return_vals, nreturn_vals);
	}
    }

  g_free (proc_list);
}

static void
init_constants (void)
{
  gchar *gimp_plugin_dir;

  setvar (cintern ("gimp-data-dir"), 
	  strcons (-1, (gchar *) gimp_data_directory ()), NIL);

  gimp_plugin_dir = gimp_gimprc_query ("gimp_plugin_dir");
  if (gimp_plugin_dir)
    {
      setvar (cintern ("gimp-plugin-dir"), 
	      strcons (-1, gimp_plugin_dir), NIL);
      g_free (gimp_plugin_dir);
    }
  
  /* Generated constants */
  init_generated_constants ();

  /* These are for backwards compatibility; they should be removed sometime */
  setvar (cintern ("NORMAL"),         flocons (GIMP_NORMAL_MODE),       NIL);
  setvar (cintern ("DISSOLVE"),       flocons (GIMP_DISSOLVE_MODE),     NIL);
  setvar (cintern ("BEHIND"),         flocons (GIMP_BEHIND_MODE),       NIL);
  setvar (cintern ("MULTIPLY"),       flocons (GIMP_MULTIPLY_MODE),     NIL);
  setvar (cintern ("SCREEN"),         flocons (GIMP_SCREEN_MODE),       NIL);
  setvar (cintern ("OVERLAY"),        flocons (GIMP_OVERLAY_MODE),      NIL);
  setvar (cintern ("DIFFERENCE"),     flocons (GIMP_DIFFERENCE_MODE),   NIL);
  setvar (cintern ("ADDITION"),       flocons (GIMP_ADDITION_MODE),     NIL);
  setvar (cintern ("SUBTRACT"),       flocons (GIMP_SUBTRACT_MODE),     NIL);
  setvar (cintern ("DARKEN-ONLY"),    flocons (GIMP_DARKEN_ONLY_MODE),  NIL);
  setvar (cintern ("LIGHTEN-ONLY"),   flocons (GIMP_LIGHTEN_ONLY_MODE), NIL);
  setvar (cintern ("HUE"),            flocons (GIMP_HUE_MODE),          NIL);
  setvar (cintern ("SATURATION"),     flocons (GIMP_SATURATION_MODE),   NIL);
  setvar (cintern ("COLOR"),          flocons (GIMP_COLOR_MODE),        NIL);
  setvar (cintern ("VALUE"),          flocons (GIMP_VALUE_MODE),        NIL);
  setvar (cintern ("DIVIDE"),         flocons (GIMP_DIVIDE_MODE),       NIL);

  setvar (cintern ("BLUR"),           flocons (GIMP_BLUR_CONVOLVE),     NIL);
  setvar (cintern ("SHARPEN"),        flocons (GIMP_SHARPEN_CONVOLVE),  NIL);

  setvar (cintern ("RGB_IMAGE"),      flocons (GIMP_RGB_IMAGE),         NIL);
  setvar (cintern ("RGBA_IMAGE"),     flocons (GIMP_RGBA_IMAGE),        NIL);
  setvar (cintern ("GRAY_IMAGE"),     flocons (GIMP_GRAY_IMAGE),        NIL);
  setvar (cintern ("GRAYA_IMAGE"),    flocons (GIMP_GRAYA_IMAGE),       NIL);
  setvar (cintern ("INDEXED_IMAGE"),  flocons (GIMP_INDEXED_IMAGE),     NIL);
  setvar (cintern ("INDEXEDA_IMAGE"), flocons (GIMP_INDEXEDA_IMAGE),    NIL);

  /* Useful misc stuff */
  setvar (cintern ("TRUE"),           flocons (TRUE),  NIL);
  setvar (cintern ("FALSE"),          flocons (FALSE), NIL);

  /*  Script-fu types  */
  setvar (cintern ("SF-IMAGE"),       flocons (SF_IMAGE),      NIL);
  setvar (cintern ("SF-DRAWABLE"),    flocons (SF_DRAWABLE),   NIL);
  setvar (cintern ("SF-LAYER"),       flocons (SF_LAYER),      NIL);
  setvar (cintern ("SF-CHANNEL"),     flocons (SF_CHANNEL),    NIL);
  setvar (cintern ("SF-COLOR"),       flocons (SF_COLOR),      NIL);
  setvar (cintern ("SF-TOGGLE"),      flocons (SF_TOGGLE),     NIL);
  setvar (cintern ("SF-VALUE"),       flocons (SF_VALUE),      NIL);
  setvar (cintern ("SF-STRING"),      flocons (SF_STRING),     NIL);
  setvar (cintern ("SF-FILENAME"),    flocons (SF_FILENAME),   NIL);
  setvar (cintern ("SF-DIRNAME"),     flocons (SF_DIRNAME),    NIL);
  setvar (cintern ("SF-ADJUSTMENT"),  flocons (SF_ADJUSTMENT), NIL);
  setvar (cintern ("SF-FONT"),        flocons (SF_FONT),       NIL);
  setvar (cintern ("SF-PATTERN"),     flocons (SF_PATTERN),    NIL);
  setvar (cintern ("SF-BRUSH"),       flocons (SF_BRUSH),      NIL);
  setvar (cintern ("SF-GRADIENT"),    flocons (SF_GRADIENT),   NIL);
  setvar (cintern ("SF-OPTION"),      flocons (SF_OPTION),     NIL);

  /* for SF_ADJUSTMENT */
  setvar (cintern ("SF-SLIDER"),      flocons (SF_SLIDER),     NIL);
  setvar (cintern ("SF-SPINNER"),     flocons (SF_SPINNER),    NIL);
}

static void
convert_string (gchar *str)
{
  while (*str)
    {
      if (*str == '_') *str = '-';
      str++;
    }
}

static gboolean
sputs_fcn (gchar    *st,
	   gpointer  dest)
{
  strcpy (*((gchar**)dest), st);
  *((gchar**)dest) += strlen (st);

  return TRUE;
}

static LISP
lprin1s (LISP   exp,
	 gchar *dest)
{
  struct gen_printio s;

  s.putc_fcn    = NULL;
  s.puts_fcn    = sputs_fcn;
  s.cb_argument = &dest;

  lprin1g (exp, &s);

  return (NIL);
}


static LISP
marshall_proc_db_call (LISP a)
{
  GimpParam       *args;
  GimpParam       *values = NULL;
  gint             nvalues;
  gchar           *proc_name;
  gchar           *proc_blurb;
  gchar           *proc_help;
  gchar           *proc_author;
  gchar           *proc_copyright;
  gchar           *proc_date;
  GimpPDBProcType  proc_type;
  gint             nparams;
  gint             nreturn_vals;
  GimpParamDef    *params;
  GimpParamDef    *return_vals;
  gchar  error_str[256];
  gint   i;
  gint   success = TRUE;
  LISP   color_list;
  LISP   intermediate_val;
  LISP   return_val = NIL;
  gchar *string;
  gint   string_len;
  LISP   a_saved;

  /* Save a in case it is needed for an error message. */
  a_saved = a;

  /*  Make sure there are arguments  */
  if (a == NIL)
    return my_err ("Procedure database argument marshaller was called with no arguments. "
		   "The procedure to be executed and the arguments it requires "
		   "(possibly none) must be specified.", NIL);

  /*  Derive the pdb procedure name from the argument 
      or first argument of a list  */
  if (TYPEP (a, tc_cons))
    proc_name = get_c_string (car (a));
  else
    proc_name = get_c_string (a);

  /*  report the current command  */
  script_fu_report_cc (proc_name);

  /*  Attempt to fetch the procedure from the database  */
  if (! gimp_procedural_db_proc_info (proc_name, 
				      &proc_blurb, 
				      &proc_help, 
				      &proc_author,
				      &proc_copyright,
				      &proc_date,
				      &proc_type,
				      &nparams, &nreturn_vals,
				      &params, &return_vals))
    return my_err ("Invalid procedure name specified.", NIL);


  /*  Check the supplied number of arguments  */
  if ((nlength (a) - 1) != nparams)
    {
      g_snprintf (error_str, sizeof (error_str), 
		  "Invalid arguments supplied to %s--(# args: %ld, expecting: %d)",
		  proc_name, (nlength (a) - 1), nparams);
      return my_err (error_str, NIL);
    }

  /*  Marshall the supplied arguments  */
  if (nparams)
    args = g_new (GimpParam, nparams);
  else
    args = NULL;

  a = cdr (a);
  for (i = 0; i < nparams; i++)
    {
      switch (params[i].type)
	{
	case GIMP_PDB_INT32:
	  if (!TYPEP (car (a), tc_flonum))
	    success = FALSE;
	  if (success)
	    {
	      args[i].type = GIMP_PDB_INT32;
	      args[i].data.d_int32 = get_c_long (car (a));
	    }
	  break;

	case GIMP_PDB_INT16:
	  if (!TYPEP (car (a), tc_flonum))
	    success = FALSE;
	  if (success)
	    {
	      args[i].type = GIMP_PDB_INT16;
	      args[i].data.d_int16 = (gint16) get_c_long (car (a));
	    }
	  break;

	case GIMP_PDB_INT8:
	  if (!TYPEP (car (a), tc_flonum))
	    success = FALSE;
	  if (success)
	    {
	      args[i].type = GIMP_PDB_INT8;
	      args[i].data.d_int8 = (gint8) get_c_long (car (a));
	    }
	  break;

	case GIMP_PDB_FLOAT:
	  if (!TYPEP (car (a), tc_flonum))
	    success = FALSE;
	  if (success)
	    {
	      args[i].type = GIMP_PDB_FLOAT;
	      args[i].data.d_float = get_c_double (car (a));
	    }
	  break;

	case GIMP_PDB_STRING:
	  if (!TYPEP (car (a), tc_string))
	    success = FALSE;
	  if (success)
	    {
	      args[i].type = GIMP_PDB_STRING;
	      args[i].data.d_string = get_c_string (car (a));
	    }
	  break;

	case GIMP_PDB_INT32ARRAY:
	  if (!TYPEP (car (a), tc_long_array))
	    success = FALSE;
	  if (success)
	    {
	      args[i].type = GIMP_PDB_INT32ARRAY;
	      args[i].data.d_int32array = 
		(gint32*) (car (a))->storage_as.long_array.data;
	    }
	  break;

	case GIMP_PDB_INT16ARRAY:
	  if (!TYPEP (car (a), tc_long_array))
	    success = FALSE;
	  if (success)
	    {
	      args[i].type = GIMP_PDB_INT16ARRAY;
	      args[i].data.d_int16array = 
		(gint16*) (car (a))->storage_as.long_array.data;
	    }
	  break;

	case GIMP_PDB_INT8ARRAY:
	  if (!TYPEP (car (a), tc_byte_array))
	    success = FALSE;
	  if (success)
	    {
	      args[i].type = GIMP_PDB_INT8ARRAY;
	      args[i].data.d_int8array = 
		(gint8*) (car (a))->storage_as.string.data;
	    }
	  break;

	case GIMP_PDB_FLOATARRAY:
	  if (!TYPEP (car (a), tc_double_array))
	    success = FALSE;
	  if (success)
	    {
	      args[i].type = GIMP_PDB_FLOATARRAY;
	      args[i].data.d_floatarray = 
		(car (a))->storage_as.double_array.data;
	    }
	  break;

	case GIMP_PDB_STRINGARRAY:
	  if (!TYPEP (car (a), tc_cons))
	    success = FALSE;
	  if (success)
	    {
	      args[i].type = GIMP_PDB_STRINGARRAY;

	      /*  Set the array  */
	      {
		gint j;
		gint num_strings;
		gchar **array;
		LISP list;

		list = car (a);
		num_strings = args[i - 1].data.d_int32;
		if (nlength (list) != num_strings)
		  return my_err ("String array argument has incorrectly specified length", NIL);
		array = args[i].data.d_stringarray = 
		  g_new (char *, num_strings);

		for (j = 0; j < num_strings; j++)
		  {
		    array[j] = get_c_string (car (list));
		    list = cdr (list);
		  }
	      }
	    }
	  break;

	case GIMP_PDB_COLOR:
	  if (!TYPEP (car (a), tc_cons))
	    success = FALSE;
	  if (success)
	    {
	      guchar color[3];

	      args[i].type = GIMP_PDB_COLOR;
	      color_list = car (a);
	      color[0] = get_c_long (car (color_list));
	      color[1] = get_c_long (car (color_list));
	      color[2] = get_c_long (car (color_list));

	      gimp_rgb_set_uchar (&args[i].data.d_color, 
				  color[0], color[1], color[2]);
	    }
	  break;

	case GIMP_PDB_REGION:
	  return my_err ("Regions are currently unsupported as arguments", 
			 car (a));
	  break;

	case GIMP_PDB_DISPLAY:
	  if (!TYPEP (car (a), tc_flonum))
	    success = FALSE;
	  if (success)
	    {
	      args[i].type = GIMP_PDB_DISPLAY;
	      args[i].data.d_int32 = get_c_long (car (a));
	    }
	  break;

	case GIMP_PDB_IMAGE:
	  if (!TYPEP (car (a), tc_flonum))
	    success = FALSE;
	  if (success)
	    {
	      args[i].type = GIMP_PDB_IMAGE;
	      args[i].data.d_int32 = get_c_long (car (a));
	    }
	  break;

	case GIMP_PDB_LAYER:
	  if (!TYPEP (car (a), tc_flonum))
	    success = FALSE;
	  if (success)
	    {
	      args[i].type = GIMP_PDB_LAYER;
	      args[i].data.d_int32 = get_c_long (car (a));
	    }
	  break;

	case GIMP_PDB_CHANNEL:
	  if (!TYPEP (car (a), tc_flonum))
	    success = FALSE;
	  if (success)
	    {
	      args[i].type = GIMP_PDB_CHANNEL;
	      args[i].data.d_int32 = get_c_long (car (a));
	    }
	  break;

	case GIMP_PDB_DRAWABLE:
	  if (!TYPEP (car (a), tc_flonum))
	    success = FALSE;
	  if (success)
	    {
	      args[i].type = GIMP_PDB_DRAWABLE;
	      args[i].data.d_int32 = get_c_long (car (a));
	    }
	  break;

	case GIMP_PDB_SELECTION:
	  if (!TYPEP (car (a), tc_flonum))
	    success = FALSE;
	  if (success)
	    {
	      args[i].type = GIMP_PDB_SELECTION;
	      args[i].data.d_int32 = get_c_long (car (a));
	    }
	  break;

	case GIMP_PDB_BOUNDARY:
	  return my_err ("Boundaries are currently unsupported as arguments", 
			 car (a));
	  break;

	case GIMP_PDB_PATH:
	  return my_err ("Paths are currently unsupported as arguments", 
			 car (a));
	  break;

	case GIMP_PDB_PARASITE:
	  if (!TYPEP (car (a), tc_cons))
	    success = FALSE;
	  if (success)
	    {
	      args[i].type = GIMP_PDB_PARASITE;
	      /* parasite->name */
	      intermediate_val = car (a);
	      args[i].data.d_parasite.name = 
		get_c_string (car (intermediate_val));
	      
	      /* parasite->flags */
	      intermediate_val = cdr (intermediate_val);
	      args[i].data.d_parasite.flags = get_c_long (car(intermediate_val));

	      /* parasite->size */
	      intermediate_val = cdr (intermediate_val);
	      args[i].data.d_parasite.size =
		(car (intermediate_val))->storage_as.string.dim;

	      /* parasite->data */
	      args[i].data.d_parasite.data =
		(void*) (car (intermediate_val))->storage_as.string.data;
	    }
	  break;

	case GIMP_PDB_STATUS:
	  return my_err ("Status is for return types, not arguments", car (a));
	  break;

	default:
	  return my_err ("Unknown argument type", NIL);
	}

      a = cdr (a);
    }

  if (success)
    values = gimp_run_procedure2 (proc_name, &nvalues, nparams, args);
  else
    return my_err ("Invalid types specified for arguments", NIL);

  /*  Check the return status  */
  if (! values)
    {
      strcpy (error_str, "Procedural database execution did not return a status:\n    ");
      lprin1s (a_saved, error_str + strlen(error_str));
      
      return my_err (error_str, NIL);
    }

  switch (values[0].data.d_status)
    {
    case GIMP_PDB_EXECUTION_ERROR:
	  strcpy (error_str, "Procedural database execution failed:\n    ");
	  lprin1s (a_saved, error_str + strlen(error_str));
      return my_err (error_str, NIL);
      break;

    case GIMP_PDB_CALLING_ERROR:
	  strcpy (error_str, "Procedural database execution failed on invalid input arguments:\n    ");
	  lprin1s (a_saved, error_str + strlen(error_str));
      return my_err (error_str, NIL);
      break;

    case GIMP_PDB_SUCCESS:
      return_val = NIL;

      for (i = 0; i < nvalues - 1; i++)
	{
	  switch (return_vals[i].type)
	    {
	    case GIMP_PDB_INT32:
	      return_val = cons (flocons (values[i + 1].data.d_int32), 
				 return_val);
	      break;

	    case GIMP_PDB_INT16:
	      return_val = cons (flocons (values[i + 1].data.d_int32), 
				 return_val);
	      break;

	    case GIMP_PDB_INT8:
	      return_val = cons (flocons (values[i + 1].data.d_int32), 
				 return_val);
	      break;

	    case GIMP_PDB_FLOAT:
	      return_val = cons (flocons (values[i + 1].data.d_float), 
				 return_val);
	      break;

	    case GIMP_PDB_STRING:
	      string = (gchar *) values[i + 1].data.d_string;
	      string_len = strlen (string);
	      return_val = cons (strcons (string_len, string), return_val);
	      break;

	    case GIMP_PDB_INT32ARRAY:
	      {
		LISP array;
		gint j;

		array = arcons (tc_long_array, values[i].data.d_int32, 0);
		for (j = 0; j < values[i].data.d_int32; j++)
		  {
		    array->storage_as.long_array.data[j] = 
		      values[i + 1].data.d_int32array[j];
		  }
		return_val = cons (array, return_val);
	      }
	      break;

	    case GIMP_PDB_INT16ARRAY:
	      return my_err ("Arrays are currently unsupported as return values", NIL);
	      break;

	    case GIMP_PDB_INT8ARRAY:
	      {
		LISP array;
		gint j;

		array = arcons (tc_byte_array, values[i].data.d_int32, 0);
		for (j = 0; j < values[i].data.d_int32; j++)
		  {
		    array->storage_as.string.data[j] = 
		      values[i + 1].data.d_int8array[j];
		  }
		return_val = cons (array, return_val);
	      }
	      break;

	    case GIMP_PDB_FLOATARRAY:
	      {
		LISP array;
		gint j;

		array = arcons (tc_double_array, values[i].data.d_int32, 0);
		for (j = 0; j < values[i].data.d_int32; j++)
		  {
		    array->storage_as.double_array.data[j] = 
		      values[i + 1].data.d_floatarray[j];
		  }
		return_val = cons (array, return_val);
	      }
	      break;

	    case GIMP_PDB_STRINGARRAY:
	      /*  string arrays are always implemented such that the previous
	       *  return value contains the number of strings in the array
	       */
	      {
		gint    j;
		gint    num_strings  = values[i].data.d_int32;
		LISP    string_array = NIL;
		gchar **array  = (gchar **) values[i + 1].data.d_stringarray;

		for (j = 0; j < num_strings; j++)
		  {
		    string_len = strlen (array[j]);
		    string_array = cons (strcons (string_len, array[j]), 
					 string_array);
		  }

		return_val = cons (nreverse (string_array), return_val);
	      }
	      break;

	    case GIMP_PDB_COLOR:
	      {
		guchar color[3];
		gimp_rgb_get_uchar (&values[i + 1].data.d_color, color, color + 1, color + 2);
		intermediate_val = cons (flocons ((int) color[0]),
					 cons (flocons ((int) color[1]),
					       cons (flocons ((int) color[2]),
						     NIL)));
		return_val = cons (intermediate_val, return_val);
		break;
	      }

	    case GIMP_PDB_REGION:
	      return my_err ("Regions are currently unsupported as return values", NIL);
	      break;

	    case GIMP_PDB_DISPLAY:
	      return_val = cons (flocons (values[i + 1].data.d_int32), 
				 return_val);
	      break;

	    case GIMP_PDB_IMAGE:
	      return_val = cons (flocons (values[i + 1].data.d_int32), 
				 return_val);
	      break;

	    case GIMP_PDB_LAYER:
	      return_val = cons (flocons (values[i + 1].data.d_int32), 
				 return_val);
	      break;

	    case GIMP_PDB_CHANNEL:
	      return_val = cons (flocons (values[i + 1].data.d_int32), 
				 return_val);
	      break;

	    case GIMP_PDB_DRAWABLE:
	      return_val = cons (flocons (values[i + 1].data.d_int32), 
				 return_val);
	      break;

	    case GIMP_PDB_SELECTION:
	      return_val = cons (flocons (values[i + 1].data.d_int32), 
				 return_val);
	      break;

	    case GIMP_PDB_BOUNDARY:
	      return my_err ("Boundaries are currently unsupported as return values", NIL);
	      break;

	    case GIMP_PDB_PATH:
	      return my_err ("Paths are currently unsupported as return values", NIL);
	      break;

	    case GIMP_PDB_PARASITE:
	      {
		LISP name, flags, data;

		if (values[i + 1].data.d_parasite.name == NULL)
		  {
		    return_val = my_err("Error: null parasite", NIL);
		  }
		else
		  {
		    string_len = strlen (values[i + 1].data.d_parasite.name);
		    name    = strcons (string_len,
				       values[i + 1].data.d_parasite.name);
		    
		    flags   = flocons (values[i + 1].data.d_parasite.flags);
		    data    = arcons (tc_byte_array,
				      values[i+1].data.d_parasite.size, 0);
		    memcpy(data->storage_as.string.data,
			   values[i+1].data.d_parasite.data, 
			   values[i+1].data.d_parasite.size); 
		    
		    intermediate_val = cons (name, 
					     cons(flags, cons(data, NIL)));
		    return_val = cons (intermediate_val, return_val);
		  }
	      }
	      break;

	    case GIMP_PDB_STATUS:
	      return my_err ("Procedural database execution returned multiple status values", NIL);
	      break;

	    default:
	      return my_err ("Unknown return type", NIL);
	    }
	}
      break;

    case GIMP_PDB_PASS_THROUGH:
    case GIMP_PDB_CANCEL:   /*  should we do something here?  */
      break;
    }

  /*  free up the executed procedure return values  */
  gimp_destroy_params (values, nvalues);

  /*  free up arguments and values  */
  g_free (args);

  /*  free the query information  */
  g_free (proc_blurb);
  g_free (proc_help);
  g_free (proc_author);
  g_free (proc_copyright);
  g_free (proc_date);
  g_free (params);
  g_free (return_vals);

  /*  reverse the return values  */
  return_val = nreverse (return_val);
#ifndef G_OS_WIN32
  /*  if we're in server mode, listen for additional commands for 10 ms  */
  if (script_fu_server_get_mode ())
    script_fu_server_listen (10);
#endif

#ifdef GDK_WINDOWING_WIN32
  /* This seems to help a lot on Windoze. */
  while (gtk_events_pending ())
    gtk_main_iteration ();
#endif

  return return_val;
}

static LISP
script_fu_register_call (LISP a)
{
  if (register_scripts)
    return script_fu_add_script (a);
  else
    return NIL;
}

static LISP
script_fu_quit_call (LISP a)
{
#ifdef G_OS_WIN32
  g_warning ("script_fu_server not available.");
#else
  script_fu_server_quit ();
#endif

  return NIL;
}

