/* GNOME DB library
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

#include <glib/gi18n-lib.h>
#include <string.h>
#include "query-result.h"
#include "../browser-window.h"
#include <libgda-ui/libgda-ui.h>
#include <libgda/sql-parser/gda-sql-parser.h>

struct _QueryResultPrivate {
	QueryEditor *history;
	QueryEditorHistoryBatch *hbatch;
	QueryEditorHistoryItem *hitem;

	GHashTable *hash; /* key = a QueryEditorHistoryItem, value = a #GtkWidget, refed here all the times */
	GtkWidget *child;
};

static void query_result_class_init (QueryResultClass *klass);
static void query_result_init       (QueryResult *result, QueryResultClass *klass);
static void query_result_finalize   (GObject *object);

static GObjectClass *parent_class = NULL;

static GtkWidget *make_widget_for_notice (void);
static GtkWidget *make_widget_for_data_model (GdaDataModel *model, QueryResult *qres, const gchar *sql);
static GtkWidget *make_widget_for_set (GdaSet *set);
static GtkWidget *make_widget_for_error (GError *error);


/*
 * QueryResult class implementation
 */
static void
query_result_class_init (QueryResultClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = query_result_finalize;
}

static void
query_result_init (QueryResult *result, QueryResultClass *klass)
{
	GtkWidget *wid;

	/* allocate private structure */
	result->priv = g_new0 (QueryResultPrivate, 1);
	result->priv->history = NULL;
	result->priv->hbatch = NULL;
	result->priv->hitem = NULL;
	result->priv->hash = g_hash_table_new_full (NULL, NULL, NULL, g_object_unref);

	wid = make_widget_for_notice ();
	gtk_box_pack_start (GTK_BOX (result), wid, TRUE, TRUE, 0);
	gtk_widget_show (wid);
	result->priv->child = wid;
}

static void
history_item_removed_cb (QueryEditor *history, QueryEditorHistoryItem *item, QueryResult *result)
{
	g_hash_table_remove (result->priv->hash, item);
}

static void
history_cleared_cb (QueryEditor *history, QueryResult *result)
{
	g_hash_table_remove_all (result->priv->hash);
}

static void
query_result_finalize (GObject *object)
{
	QueryResult *result = (QueryResult *) object;

	g_return_if_fail (IS_QUERY_RESULT (result));

	/* free memory */
	if (result->priv->hash)
		g_hash_table_destroy (result->priv->hash);
	if (result->priv->history) {
		g_signal_handlers_disconnect_by_func (result->priv->history,
						      G_CALLBACK (history_item_removed_cb), result);
		g_signal_handlers_disconnect_by_func (result->priv->history,
						      G_CALLBACK (history_cleared_cb), result);
		g_object_unref (result->priv->history);
	}
	if (result->priv->hbatch)
		query_editor_history_batch_unref (result->priv->hbatch);
	if (result->priv->hitem)
		query_editor_history_item_unref (result->priv->hitem);

	g_free (result->priv);
	result->priv = NULL;

	parent_class->finalize (object);
}

GType
query_result_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static const GTypeInfo info = {
			sizeof (QueryResultClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) query_result_class_init,
			NULL,
			NULL,
			sizeof (QueryResult),
			0,
			(GInstanceInitFunc) query_result_init
		};
		type = g_type_register_static (GTK_TYPE_VBOX, "QueryResult", &info, 0);
	}
	return type;
}

/**
 * query_result_new
 * @history: a #QueryEditor to take history from
 *
 * Create a new #QueryResult widget
 *
 * Returns: the newly created widget.
 */
GtkWidget *
query_result_new (QueryEditor *history)
{
	QueryResult *result;
	g_return_val_if_fail (QUERY_IS_EDITOR (history), NULL);

	result = g_object_new (QUERY_TYPE_RESULT, NULL);
	g_signal_connect (history, "history-item-removed",
			  G_CALLBACK (history_item_removed_cb), result);
	g_signal_connect (history, "history-cleared",
			  G_CALLBACK (history_cleared_cb), result);
	result->priv->history = g_object_ref (history);

	return GTK_WIDGET (result);
}

/**
 * query_result_show_history_batch
 * @qres: a #QueryResult widget
 * @hbatch: the #QueryEditorHistoryBatch to display, or %NULL
 *
 * 
 */
void
query_result_show_history_batch (QueryResult *qres, QueryEditorHistoryBatch *hbatch)
{
	GSList *list;
	GtkWidget *child;	
	GtkWidget *vbox;
	gchar *str;

	g_return_if_fail (IS_QUERY_RESULT (qres));
	if (qres->priv->child)
		gtk_container_remove (GTK_CONTAINER (qres), qres->priv->child);

	if (!hbatch) {
		child = make_widget_for_notice ();
		gtk_box_pack_start (GTK_BOX (qres), child, TRUE, TRUE, 10);
		gtk_widget_show (child);
		qres->priv->child = child;
		return;
	}

	vbox = gtk_vbox_new (FALSE, 0);

	child = query_editor_new ();
	query_editor_set_mode (QUERY_EDITOR (child), QUERY_EDITOR_READONLY);
	gtk_box_pack_start (GTK_BOX (vbox), child, TRUE, TRUE, 0);
	for (list = hbatch->hist_items; list; list = list->next) {
		QueryEditorHistoryItem *hitem;
		GString *string;
		hitem = (QueryEditorHistoryItem *) list->data;
		if (list != hbatch->hist_items)
			query_editor_append_text (QUERY_EDITOR (child), "\n");
		query_editor_append_note (QUERY_EDITOR (child), _("Statement:"), 0);
		query_editor_append_text (QUERY_EDITOR (child), hitem->sql);

		string = g_string_new ("");
		if (hitem->result) {
			if (GDA_IS_DATA_MODEL (hitem->result)) {
				gint n, c;
				gchar *tmp1, *tmp2;
				n = gda_data_model_get_n_rows (GDA_DATA_MODEL (hitem->result));
				c = gda_data_model_get_n_columns (GDA_DATA_MODEL (hitem->result));
				tmp1 = g_strdup_printf (ngettext ("%d row", "%d rows", n), n);
				tmp2 = g_strdup_printf (ngettext ("%d column", "%d columns", c), c);
				g_string_append_printf (string, 
							_("Data set with %s and %s"),
							tmp1, tmp2);
				g_free (tmp1);
				g_free (tmp2);
			}
			else if (GDA_IS_SET (hitem->result)) {
				GdaSet *set;
				GSList *list;
				set = GDA_SET (hitem->result);
				for (list = set->holders; list; list = list->next) {
					GdaHolder *h;
					const GValue *value;
					const gchar *cstr;
					gchar *tmp;
					h = GDA_HOLDER (list->data);
					
					if (list != set->holders)
						g_string_append_c (string, '\n');
					
					cstr = gda_holder_get_id (h);
					if (!strcmp (cstr, "IMPACTED_ROWS"))
						g_string_append (string, _("Number of rows impacted"));
					else
						g_string_append (string, cstr);
					
					g_string_append (string, ": ");
					value = gda_holder_get_value (h);
					tmp = gda_value_stringify (value);
					g_string_append_printf (string, "%s", tmp);
					g_free (tmp);
				}
			}
			else
				g_assert_not_reached ();
		}
		else 
			g_string_append_printf (string, _("Error: %s"),
						hitem->exec_error && hitem->exec_error->message ?
						hitem->exec_error->message : _("No detail"));
		query_editor_append_note (QUERY_EDITOR (child), string->str, 1);
		g_string_free (string, TRUE);
	}

	if (hbatch->params) {
		GtkWidget *exp, *form;
		str = g_strdup_printf ("<b>%s:</b>", _("Execution Parameters"));
		exp = gtk_expander_new (str);
		g_free (str);
		gtk_expander_set_use_markup (GTK_EXPANDER (exp), TRUE);
		gtk_box_pack_start (GTK_BOX (vbox), exp, FALSE, FALSE, 0);

		form = gdaui_basic_form_new (hbatch->params);
		gdaui_basic_form_entry_set_editable (GDAUI_BASIC_FORM (form), NULL, FALSE);
		gtk_container_add (GTK_CONTAINER (exp), form);
	}

	gtk_box_pack_start (GTK_BOX (qres), vbox, TRUE, TRUE, 0);
	gtk_widget_show_all (vbox);
	qres->priv->child = vbox;
}

/**
 * query_result_show_history_item
 * @qres: a #QueryResult widget
 * @hitem: the #QueryEditorHistoryItem to display, or %NULL
 *
 * 
 */
void
query_result_show_history_item (QueryResult *qres, QueryEditorHistoryItem *hitem)
{
	GtkWidget *child;

	g_return_if_fail (IS_QUERY_RESULT (qres));
	if (qres->priv->child)
		gtk_container_remove (GTK_CONTAINER (qres), qres->priv->child);
	
	if (!hitem)
		child = make_widget_for_notice ();
	else {
		child = g_hash_table_lookup (qres->priv->hash, hitem);
		if (!child) {
			if (hitem->result) {
				if (GDA_IS_DATA_MODEL (hitem->result))
					child = make_widget_for_data_model (GDA_DATA_MODEL (hitem->result),
									    qres, hitem->sql);
				else if (GDA_IS_SET (hitem->result))
					child = make_widget_for_set (GDA_SET (hitem->result));
				else
					g_assert_not_reached ();
			}
			else 
				child = make_widget_for_error (hitem->exec_error);

			g_hash_table_insert (qres->priv->hash, hitem, g_object_ref_sink (G_OBJECT (child)));
		}
	}

	gtk_box_pack_start (GTK_BOX (qres), child, TRUE, TRUE, 0);
	gtk_widget_show (child);
	qres->priv->child = child;
}

static GtkWidget *
make_widget_for_notice (void)
{
	GtkWidget *label;
	label = gtk_label_new (_("No result selected"));
	return label;
}

static GtkWidget *
make_widget_for_data_model (GdaDataModel *model, QueryResult *qres, const gchar *sql)
{
	GtkWidget *grid;
	grid = gdaui_grid_new (model);
	gdaui_grid_set_sample_size (GDAUI_GRID (grid), 300);
	g_object_set (G_OBJECT (grid), "info-flags",
		      GDAUI_DATA_PROXY_INFO_CHUNCK_CHANGE_BUTTONS | 
		      GDAUI_DATA_PROXY_INFO_CURRENT_ROW, NULL);

	if (sql) {
		BrowserConnection *bcnc;
		bcnc = browser_window_get_connection ((BrowserWindow*) gtk_widget_get_toplevel ((GtkWidget*) qres));
		if (!bcnc)
			goto out;

		GdaSqlParser *parser;
		GdaStatement *stmt;
		parser = browser_connection_create_parser (bcnc);
		stmt = gda_sql_parser_parse_string (parser, sql, NULL, NULL);
		g_object_unref (parser);
		if (!stmt)
			goto out;

		GdaSqlStatement *sqlst;
		g_object_get ((GObject*) stmt, "structure", &sqlst, NULL);
		g_object_unref (stmt);
		
		if ((sqlst->stmt_type != GDA_SQL_STATEMENT_SELECT) ||
		    !browser_connection_normalize_sql_statement (bcnc, sqlst, NULL)) {
			gda_sql_statement_free (sqlst);
			goto out;
		}

		GdaSet *set;
		set = (GdaSet*) gdaui_data_selector_get_data_set (GDAUI_DATA_SELECTOR (grid));

		GdaSqlStatementSelect *sel;
		GSList *list;
		gint pos;
		sel = (GdaSqlStatementSelect*) sqlst->contents;
		for (pos = 0, list = sel->expr_list; list; pos ++, list = list->next) {
			GdaSqlSelectField *field = (GdaSqlSelectField*) list->data;
			if (! field->validity_meta_object ||
			    (field->validity_meta_object->obj_type != GDA_META_DB_TABLE) ||
			    !field->validity_meta_table_column)
				continue;

			gchar *plugin;
			plugin = browser_connection_get_table_column_attribute (bcnc,
										GDA_META_TABLE (field->validity_meta_object),
										field->validity_meta_table_column,
										BROWSER_CONNECTION_COLUMN_PLUGIN, NULL);
			if (!plugin)
				continue;

			GdaHolder *holder;
			holder = gda_set_get_nth_holder (set, pos);
			if (holder) {
				GValue *value;
				value = gda_value_new_from_string (plugin, G_TYPE_STRING);
				gda_holder_set_attribute_static (holder, GDAUI_ATTRIBUTE_PLUGIN, value);
				gda_value_free (value);
			}
				
		}

		gda_sql_statement_free (sqlst);
	}
 out:
	return grid;
}

static GtkWidget *
make_widget_for_set (GdaSet *set)
{
	GtkWidget *hbox, *img;
	hbox = gtk_hbox_new (FALSE, 5);
	
	img = gtk_image_new_from_stock (GTK_STOCK_DIALOG_INFO, GTK_ICON_SIZE_DIALOG);
	gtk_misc_set_alignment (GTK_MISC (img), 0., 0.);
	gtk_box_pack_start (GTK_BOX (hbox), img, FALSE, FALSE, 0);

	GtkWidget *label;
	GString *string;
	GSList *list;

	label = gtk_label_new ("");
	string = g_string_new ("");
	for (list = set->holders; list; list = list->next) {
		GdaHolder *h;
		const GValue *value;
		gchar *tmp;
		const gchar *cstr;
		h = GDA_HOLDER (list->data);

		if (list != set->holders)
			g_string_append_c (string, '\n');
		
		cstr = gda_holder_get_id (h);
		if (!strcmp (cstr, "IMPACTED_ROWS"))
			g_string_append_printf (string, "<b>%s</b>  ",
						_("Number of rows impacted:"));
		else {
			tmp = g_markup_escape_text (cstr, -1);
			g_string_append_printf (string, "<b>%s</b>", tmp);
			g_free (tmp);
		}

		value = gda_holder_get_value (h);
		tmp = gda_value_stringify (value);
		g_string_append_printf (string, "%s", tmp);
		g_free (tmp);
	}
	gtk_label_set_markup (GTK_LABEL (label), string->str);
	gtk_misc_set_alignment (GTK_MISC (label), 0., 0.);
	g_string_free (string, TRUE);
	gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);

	gtk_widget_show_all (hbox);
	gtk_widget_hide (hbox);

	return hbox;
}

static GtkWidget *
make_widget_for_error (GError *error)
{
	GtkWidget *hbox, *img;
	hbox = gtk_hbox_new (FALSE, 5);
	
	img = gtk_image_new_from_stock (GTK_STOCK_DIALOG_ERROR, GTK_ICON_SIZE_DIALOG);
	gtk_misc_set_alignment (GTK_MISC (img), 0., 0.);
	gtk_box_pack_start (GTK_BOX (hbox), img, FALSE, FALSE, 0);

	GtkWidget *label;
	GString *string;
	gchar *tmp;

	label = gtk_label_new ("");
	string = g_string_new ("");
	g_string_append_printf (string, "<b>%s</b>  ", _("Execution error:\n"));
	if (error && error->message) {
		tmp = g_markup_escape_text (error->message, -1);
		g_string_append (string, tmp);
		g_free (tmp);
	}
	else
		g_string_append (string, _("No detail"));

	gtk_label_set_markup (GTK_LABEL (label), string->str);
	gtk_misc_set_alignment (GTK_MISC (label), 0., 0.);
	g_string_free (string, TRUE);
	gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);	

	gtk_widget_show_all (hbox);
	gtk_widget_hide (hbox);
	return hbox;
}