/* GDA library
 * Copyright (C) 2009 The GNOME Foundation.
 *
 * AUTHORS:
 *      Vivien Malerba <malerba@gnome-db.org>
 *
 * This Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GDA_TREE_MGR_LABEL_H__
#define __GDA_TREE_MGR_LABEL_H__

#include <libgda/gda-connection.h>
#include "gda-tree-manager.h"

G_BEGIN_DECLS

#define GDA_TYPE_TREE_MGR_LABEL            (gda_tree_mgr_label_get_type())
#define GDA_TREE_MGR_LABEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST (obj, GDA_TYPE_TREE_MGR_LABEL, GdaTreeMgrLabel))
#define GDA_TREE_MGR_LABEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST (klass, GDA_TYPE_TREE_MGR_LABEL, GdaTreeMgrLabelClass))
#define GDA_IS_TREE_MGR_LABEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE(obj, GDA_TYPE_TREE_MGR_LABEL))
#define GDA_IS_TREE_MGR_LABEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GDA_TYPE_TREE_MGR_LABEL))

typedef struct _GdaTreeMgrLabel GdaTreeMgrLabel;
typedef struct _GdaTreeMgrLabelPriv GdaTreeMgrLabelPriv;
typedef struct _GdaTreeMgrLabelClass GdaTreeMgrLabelClass;

struct _GdaTreeMgrLabel {
	GdaTreeManager        object;
	GdaTreeMgrLabelPriv *priv;
};

struct _GdaTreeMgrLabelClass {
	GdaTreeManagerClass   object_class;
};

GType              gda_tree_mgr_label_get_type                 (void) G_GNUC_CONST;
GdaTreeManager*    gda_tree_mgr_label_new                      (const gchar *label);

G_END_DECLS

#endif