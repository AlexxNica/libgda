/* libgda library
 *
 * Copyright (C) 2001 Carlos Perell� Mar�n <carlos@gnome-db.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Library General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __gda_report_data_header_h__
#define __gda_report_data_header_h__

#ifdef HAVE_GOBJECT
#include <glib-object.h>
#else
#include <gtk/gtk.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#include <GDA_Report.h>
#include <gda-report-defs.h>

typedef struct _GdaReportDataHeader      GdaReportDataHeader;
typedef struct _GdaReportDataHeaderClass  GdaReportDataHeaderClass;

struct _GdaReportDataHeader {
	GdaReportSection	parent;
		
	GDA_ReportDataHeader	corba_reportdataheader;
	GList*			errors_head;
};

struct _GdaReportDataHeaderClass {
	GdaReportSectionClass	parent_class;
	
	void (* warning) (GdaReportDataHeader* object, GList* errors);
	void (* error)   (GdaReportDataHeader* object, GList* errors);
};

#define GDA_TYPE_REPORT_DATA_HEADER          (gda_report_data_header_get_type())
#ifdef HAVE_GOBJECT
#define GDA_REPORT_DATA_HEADER(obj) \
		G_TYPE_CHECK_INSTANCE_CAST (obj, GDA_TYPE_REPORT_DATA_HEADER, GdaReportDataHeader)
#define GDA_REPORT_DATA_HEADER_CLASS(klass) \
		G_TYPE_CHECK_CLASS_CAST (klass, GDA_TYPE_REPORT_DATA_HEADER, GdaReportDataHeaderClass)
#define GDA_IS_REPORT_DATA_HEADER(obj) \
		G_TYPE_CHECK_INSTANCE_TYPE (obj, GDA_TYPE_REPORT_DATA_HEADER)
#define GDA_IS_REPORT_DATA_HEADER_CLASS(klass) \
		G_TYPE_CHECK_CLASS_TYPE ((klass), GDA_TYPE_REPORT_DATA_HEADER)
#else
#define GDA_REPORT_DATA_HEADER(obj) \
		GTK_CHECK_CAST(obj, GDA_TYPE_REPORT_DATA_HEADER, GdaReportDataHeader)
#define GDA_REPORT_DATA_HEADER_CLASS(klass) \
		GTK_CHECK_CLASS_CAST(klass, GDA_TYPE_REPORT_DATA_HEADER, GdaReportDataHeaderClass)
#define GDA_IS_REPORT_DATA_HEADER(obj) \
		GTK_CHECK_TYPE(obj, GDA_TYPE_REPORT_DATA_HEADER)
#define GDA_IS_REPORT_DATA_HEADER_CLASS(klass) \
		GTK_CHECK_CLASS_TYPE((klass), GDA_TYPE_REPORT_DATA_HEADER))
#endif


#ifdef HAVE_GOBJECT
GType			gda_report_data_header_get_type	(void);
#else
GtkType			gda_report_data_header_get_type	(void);
#endif

GdaReportDataHeader*	gda_report_data_header_new	();
void			gda_report_data_header_free	(GdaReportDataHeader* object);

#if defined(__cplusplus)
}
#endif

#endif
