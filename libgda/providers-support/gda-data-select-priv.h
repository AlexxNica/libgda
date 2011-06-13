/*
 * Copyright (C) 2000 Reinhard Müller <reinhard@src.gnome.org>
 * Copyright (C) 2000 - 2002 Rodrigo Moya <rodrigo@gnome-db.org>
 * Copyright (C) 2001 Carlos Perell� Mar�n <carlos@gnome-db.org>
 * Copyright (C) 2001 - 2011 Vivien Malerba <malerba@gnome-db.org>
 * Copyright (C) 2002 Gonzalo Paniagua Javier <gonzalo@src.gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#ifndef __GDA_DATA_SELECT_PRIV_H__
#define __GDA_DATA_SELECT_PRIV_H__

#include <glib-object.h>
#include <libgda/gda-row.h>
#include <libgda/providers-support/gda-pstmt.h>
#include <sql-parser/gda-sql-statement.h>
#include <libgda/gda-data-select.h>

G_BEGIN_DECLS


GType          gda_data_select_get_type                     (void) G_GNUC_CONST;

/* API reserved to provider's implementations */
void           gda_data_select_take_row                     (GdaDataSelect *model, GdaRow *row, gint rownum);
GdaRow        *gda_data_select_get_stored_row               (GdaDataSelect *model, gint rownum);
GdaConnection *gda_data_select_get_connection               (GdaDataSelect *model);
void           gda_data_select_set_columns                  (GdaDataSelect *model, GSList *columns);

void           gda_data_select_add_exception                (GdaDataSelect *model, GError *error);

/* internal API */
void           _gda_data_select_share_private_data (GdaDataSelect *master, GdaDataSelect *slave);

G_END_DECLS

#endif
