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

#include "imap_cmd_copy.h"
#include "imap_main.h"

static void object_down_command_destruct(Command_t *parent);
static gboolean object_down_command_execute(Command_t *parent);
static void object_down_command_undo(Command_t *parent);

CommandClass_t object_down_command_class = {
   object_down_command_destruct,
   object_down_command_execute,
   object_down_command_undo,
   NULL				/* object_down_command_redo */
};

typedef struct {
   Command_t parent;
   ObjectList_t *list;
   Object_t *obj;
} ObjectDownCommand_t;

Command_t* 
object_down_command_new(ObjectList_t *list, Object_t *obj)
{
   ObjectDownCommand_t *command = g_new(ObjectDownCommand_t, 1);
   command->list = list;
   command->obj = object_ref(obj);
   return command_init(&command->parent, "Move Down", 
		       &object_down_command_class);
}

static void
object_down_command_destruct(Command_t *parent)
{
   ObjectDownCommand_t *command = (ObjectDownCommand_t*) parent;
   object_unref(command->obj);
}

static gboolean
object_down_command_execute(Command_t *parent)
{
   ObjectDownCommand_t *command = (ObjectDownCommand_t*) parent;
   object_list_move_down(command->list, command->obj);
   redraw_preview();		/* fix me! */
   return TRUE;
}

static void
object_down_command_undo(Command_t *parent)
{
   ObjectDownCommand_t *command = (ObjectDownCommand_t*) parent;
   object_list_move_up(command->list, command->obj);
   redraw_preview();		/* fix me! */
}
