/*
 * This is a plug-in for the GIMP.
 *
 * Generates clickable image maps.
 *
 * Copyright (C) 1998-1999 Maurits Rijk  lpeek.mrijk@consunet.nl
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
 */

#include "imap_cmd_unselect.h"

#include "imap_main.h"

static void unselect_command_destruct(Command_t *parent);
static gboolean unselect_command_execute(Command_t *parent);
static void unselect_command_undo(Command_t *parent);

static CommandClass_t unselect_command_class = {
   unselect_command_destruct,
   unselect_command_execute,
   unselect_command_undo,
   NULL				/* unselect_command_redo */
};

typedef struct {
   Command_t parent;
   Object_t *obj;
} UnselectCommand_t;

Command_t* 
unselect_command_new(Object_t *obj)
{
   UnselectCommand_t *command = g_new(UnselectCommand_t, 1);
   command->obj = object_ref(obj);
   return command_init(&command->parent, "Unselect", 
		       &unselect_command_class);
}

static void
unselect_command_destruct(Command_t *command)
{
   UnselectCommand_t *unselect_command = (UnselectCommand_t*) command;
   object_unref(unselect_command->obj);
}

static gboolean
unselect_command_execute(Command_t *command)
{
   UnselectCommand_t *unselect_command = (UnselectCommand_t*) command;
   object_unselect(unselect_command->obj);
   return TRUE;
}

static void
unselect_command_undo(Command_t *command)
{
   UnselectCommand_t *unselect_command = (UnselectCommand_t*) command;
   object_select(unselect_command->obj);
}
