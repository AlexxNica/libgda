/* gdaui-server-operation.h
 *
 * Copyright (C) 2006 Vivien Malerba
 *
 * This Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifndef __GDAUI_SERVER_OPERATION__
#define __GDAUI_SERVER_OPERATION__

#include <gtk/gtk.h>
#include <libgda/libgda.h>
#ifdef HAVE_LIBGLADE
#include <glade/glade.h>
#endif

G_BEGIN_DECLS

#define GDAUI_TYPE_SERVER_OPERATION          (gdaui_server_operation_get_type())
#define GDAUI_SERVER_OPERATION(obj)          G_TYPE_CHECK_INSTANCE_CAST (obj, gdaui_server_operation_get_type(), GdauiServerOperation)
#define GDAUI_SERVER_OPERATION_CLASS(klass)  G_TYPE_CHECK_CLASS_CAST (klass, gdaui_server_operation_get_type (), GdauiServerOperationClass)
#define GDAUI_IS_SERVER_OPERATION(obj)       G_TYPE_CHECK_INSTANCE_TYPE (obj, gdaui_server_operation_get_type ())


typedef struct _GdauiServerOperation      GdauiServerOperation;
typedef struct _GdauiServerOperationClass GdauiServerOperationClass;
typedef struct _GdauiServerOperationPriv  GdauiServerOperationPriv;

/* struct for the object's data */
struct _GdauiServerOperation
{
	GtkVBox                     object;
	GdauiServerOperationPriv *priv;
};

/* struct for the object's class */
struct _GdauiServerOperationClass
{
	GtkVBoxClass                parent_class;
};

/* 
 * Generic widget's methods 
*/
GType             gdaui_server_operation_get_type      (void) G_GNUC_CONST;
GtkWidget        *gdaui_server_operation_new           (GdaServerOperation *op);
GtkWidget        *gdaui_server_operation_new_in_dialog (GdaServerOperation *op, GtkWindow *parent,
							   const gchar *title, const gchar *header);

G_END_DECLS

#endif


