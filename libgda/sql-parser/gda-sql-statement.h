/*
 * Copyright (C) 2000 Reinhard Müller <reinhard@src.gnome.org>
 * Copyright (C) 2000 - 2002 Rodrigo Moya <rodrigo@gnome-db.org>
 * Copyright (C) 2001 Carlos Perell� Mar�n <carlos@gnome-db.org>
 * Copyright (C) 2001 - 2008 Vivien Malerba <malerba@gnome-db.org>
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

#ifndef __GDA_SQL_STATEMENT_H__
#define __GDA_SQL_STATEMENT_H__

G_BEGIN_DECLS

#include <sql-parser/gda-statement-struct-select.h>
#include <sql-parser/gda-statement-struct-insert.h>
#include <sql-parser/gda-statement-struct-update.h>
#include <sql-parser/gda-statement-struct-delete.h>
#include <sql-parser/gda-statement-struct-compound.h>
#include <sql-parser/gda-statement-struct-trans.h>
#include <sql-parser/gda-statement-struct-unknown.h>
#include <sql-parser/gda-statement-struct-parts.h>

G_END_DECLS

#endif
