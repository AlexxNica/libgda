/*
 * Copyright (C) 2009 Vivien Malerba
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

#include <string.h>
#include <glib/gi18n-lib.h>
#include "browser-connection.h"
#include <libgda/thread-wrapper/gda-thread-wrapper.h>
#include "support.h"
#include "marshal.h"
#include <sql-parser/gda-sql-parser.h>
#include <libgda/gda-sql-builder.h>

#include "browser-connection-priv.h"

/* code inclusion */
#include "../dict-file-name.c"

typedef struct {
	GObject *result;
	GError  *error;
	GdaSet  *last_inserted_row;
} StatementResult;

static void
statement_result_free (StatementResult *res)
{
	if (res->result)
		g_object_unref (res->result);
	if (res->last_inserted_row)
		g_object_unref (res->last_inserted_row);
	g_clear_error (&(res->error));
	g_free (res);
}

/* signals */
enum {
	BUSY,
	META_CHANGED,
	FAV_CHANGED,
	TRANSACTION_STATUS_CHANGED,
	TABLE_COLUMN_PREF_CHANGED,
	LAST_SIGNAL
};

gint browser_connection_signals [LAST_SIGNAL] = { 0, 0, 0, 0, 0 };

/* wrapper jobs handling */
static gboolean check_for_wrapper_result (BrowserConnection *bcnc);

typedef enum {
	JOB_TYPE_META_STORE_UPDATE,
	JOB_TYPE_META_STRUCT_SYNC,
	JOB_TYPE_STATEMENT_EXECUTE
} JobType;

typedef struct {
	guint    job_id;
	JobType  job_type;
	gchar   *reason;
} WrapperJob;

static void
wrapper_job_free (WrapperJob *wj)
{
	g_free (wj->reason);
	g_free (wj);
}

/*
 * Pushes a job which has been asked to be exected in a sub thread using gda_thread_wrapper_execute()
 */
static void
push_wrapper_job (BrowserConnection *bcnc, guint job_id, JobType job_type, const gchar *reason)
{
	/* setup timer */
	if (bcnc->priv->wrapper_results_timer == 0)
		bcnc->priv->wrapper_results_timer = g_timeout_add (200,
								   (GSourceFunc) check_for_wrapper_result,
								   bcnc);

	/* add WrapperJob structure */
	WrapperJob *wj;
	wj = g_new0 (WrapperJob, 1);
	wj->job_id = job_id;
	wj->job_type = job_type;
	if (reason)
		wj->reason = g_strdup (reason);

	bcnc->priv->wrapper_jobs = g_slist_append (bcnc->priv->wrapper_jobs, wj);

	if (! bcnc->priv->wrapper_jobs->next)
		g_signal_emit (bcnc, browser_connection_signals [BUSY], 0, TRUE, wj->reason);
}

static void
pop_wrapper_job (BrowserConnection *bcnc, WrapperJob *wj)
{
	bcnc->priv->wrapper_jobs = g_slist_remove (bcnc->priv->wrapper_jobs, wj);
	wrapper_job_free (wj);
	g_signal_emit (bcnc, browser_connection_signals [BUSY], 0, FALSE, NULL);
}


/* 
 * Main static functions 
 */
static void browser_connection_class_init (BrowserConnectionClass *klass);
static void browser_connection_init (BrowserConnection *bcnc);
static void browser_connection_dispose (GObject *object);
static void browser_connection_set_property (GObject *object,
					     guint param_id,
					     const GValue *value,
					     GParamSpec *pspec);
static void browser_connection_get_property (GObject *object,
					     guint param_id,
					     GValue *value,
					     GParamSpec *pspec);
/* get a pointer to the parents to be able to call their destructor */
static GObjectClass  *parent_class = NULL;

/* properties */
enum {
        PROP_0,
        PROP_GDA_CNC
};

GType
browser_connection_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		static GStaticMutex registering = G_STATIC_MUTEX_INIT;
		static const GTypeInfo info = {
			sizeof (BrowserConnectionClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) browser_connection_class_init,
			NULL,
			NULL,
			sizeof (BrowserConnection),
			0,
			(GInstanceInitFunc) browser_connection_init
		};

		
		g_static_mutex_lock (&registering);
		if (type == 0)
			type = g_type_register_static (G_TYPE_OBJECT, "BrowserConnection", &info, 0);
		g_static_mutex_unlock (&registering);
	}
	return type;
}

static void
browser_connection_class_init (BrowserConnectionClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	parent_class = g_type_class_peek_parent (klass);

	browser_connection_signals [BUSY] =
		g_signal_new ("busy",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (BrowserConnectionClass, busy),
                              NULL, NULL,
                              _marshal_VOID__BOOLEAN_STRING, G_TYPE_NONE,
                              2, G_TYPE_BOOLEAN, G_TYPE_STRING);
	browser_connection_signals [META_CHANGED] =
		g_signal_new ("meta-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (BrowserConnectionClass, meta_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE,
                              1, GDA_TYPE_META_STRUCT);
	browser_connection_signals [FAV_CHANGED] =
		g_signal_new ("favorites-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (BrowserConnectionClass, favorites_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
                              0);
	browser_connection_signals [TRANSACTION_STATUS_CHANGED] =
		g_signal_new ("transaction-status-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (BrowserConnectionClass, transaction_status_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID, G_TYPE_NONE,
                              0);
	browser_connection_signals [TABLE_COLUMN_PREF_CHANGED] =
		g_signal_new ("table-column-pref-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (BrowserConnectionClass, table_column_pref_changed),
                              NULL, NULL,
			      _marshal_VOID__POINTER_POINTER_STRING_STRING, G_TYPE_NONE,
                              4, G_TYPE_POINTER, G_TYPE_POINTER, G_TYPE_STRING, G_TYPE_STRING);

	klass->busy = browser_connection_set_busy_state;
	klass->meta_changed = NULL;
	klass->favorites_changed = NULL;
	klass->transaction_status_changed = NULL;
	klass->table_column_pref_changed = NULL;

	/* Properties */
        object_class->set_property = browser_connection_set_property;
        object_class->get_property = browser_connection_get_property;
	g_object_class_install_property (object_class, PROP_GDA_CNC,
                                         g_param_spec_object ("gda-connection", NULL, "Connection to use",
                                                              GDA_TYPE_CONNECTION,
                                                              G_PARAM_READABLE | G_PARAM_WRITABLE |
							      G_PARAM_CONSTRUCT_ONLY));

	object_class->dispose = browser_connection_dispose;
}

static void
browser_connection_init (BrowserConnection *bcnc)
{
	static guint index = 1;
	bcnc->priv = g_new0 (BrowserConnectionPrivate, 1);
	bcnc->priv->wrapper = gda_thread_wrapper_new ();
	bcnc->priv->wrapper_jobs = NULL;
	bcnc->priv->wrapper_results_timer = 0;
	bcnc->priv->executed_statements = NULL;

	bcnc->priv->name = g_strdup_printf (_("c%u"), index++);
	bcnc->priv->cnc = NULL;
	bcnc->priv->parser = NULL;
	bcnc->priv->variables = NULL;
	memset (&(bcnc->priv->dsn_info), 0, sizeof (GdaDsnInfo));
	bcnc->priv->p_mstruct_mutex = g_mutex_new ();
	bcnc->priv->p_mstruct = NULL;
	bcnc->priv->mstruct = NULL;

	bcnc->priv->bfav = NULL;

	bcnc->priv->store_cnc = NULL;

	bcnc->priv->variables = NULL;
}

static void
transaction_status_changed_cb (GdaConnection *cnc, BrowserConnection *bcnc)
{
	g_signal_emit (bcnc, browser_connection_signals [TRANSACTION_STATUS_CHANGED], 0);
}

/* executed in sub @bcnc->priv->wrapper's thread */
static gpointer
wrapper_meta_store_update (BrowserConnection *bcnc, GError **error)
{
	gboolean retval;
	GdaMetaContext context = {"_tables", 0, NULL, NULL};
	retval = gda_connection_update_meta_store (bcnc->priv->cnc, &context, error);

	return GINT_TO_POINTER (retval ? 2 : 1);
}

/* executed in sub @bcnc->priv->wrapper's thread */
static gpointer
wrapper_meta_struct_sync (BrowserConnection *bcnc, GError **error)
{
	gboolean retval;

	g_mutex_lock (bcnc->priv->p_mstruct_mutex);
	g_assert (bcnc->priv->p_mstruct);
	g_object_ref (G_OBJECT (bcnc->priv->p_mstruct));
	retval = gda_meta_struct_complement_all (bcnc->priv->p_mstruct, error);
	g_object_unref (G_OBJECT (bcnc->priv->p_mstruct));
	g_mutex_unlock (bcnc->priv->p_mstruct_mutex);

	return GINT_TO_POINTER (retval ? 2 : 1);
}

static void
browser_connection_set_property (GObject *object,
				 guint param_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
        BrowserConnection *bcnc;

        bcnc = BROWSER_CONNECTION (object);
        if (bcnc->priv) {
                switch (param_id) {
                case PROP_GDA_CNC:
                        bcnc->priv->cnc = (GdaConnection*) g_value_get_object (value);
                        if (!bcnc->priv->cnc)
				return;

			g_object_ref (bcnc->priv->cnc);
			g_signal_connect (bcnc->priv->cnc, "transaction-status-changed",
					  G_CALLBACK (transaction_status_changed_cb), bcnc);

			/* meta store */
			gchar *dict_file_name = NULL;
			gboolean update_store = FALSE;
			GdaMetaStore *store;
			gchar *cnc_string, *cnc_info;
			
			g_object_get (G_OBJECT (bcnc->priv->cnc),
				      "dsn", &cnc_info,
				      "cnc-string", &cnc_string, NULL);
			dict_file_name = compute_dict_file_name (cnc_info ? gda_config_get_dsn_info (cnc_info) : NULL,
								 cnc_string);
			g_free (cnc_string);
			if (dict_file_name) {
				if (! g_file_test (dict_file_name, G_FILE_TEST_EXISTS))
					update_store = TRUE;
				store = gda_meta_store_new_with_file (dict_file_name);
			}
			else {
				store = gda_meta_store_new (NULL);
				if (store)
					update_store = TRUE;
			}
			bcnc->priv->dict_file_name = dict_file_name;
			g_object_set (G_OBJECT (bcnc->priv->cnc), "meta-store", store, NULL);
			if (update_store) {
				GError *lerror = NULL;
				guint job_id;
				job_id = gda_thread_wrapper_execute (bcnc->priv->wrapper,
								     (GdaThreadWrapperFunc) wrapper_meta_store_update,
								     g_object_ref (bcnc), g_object_unref, &lerror);
				if (job_id > 0)
					push_wrapper_job (bcnc, job_id, JOB_TYPE_META_STORE_UPDATE,
							  _("Getting database schema information"));
				else if (lerror) {
					browser_show_error (NULL, _("Error while fetching meta data from the connection: %s"),
							    lerror->message ? lerror->message : _("No detail"));
					g_error_free (lerror);
				}
			}
			else {
				guint job_id;
				GError *lerror = NULL;
				
				bcnc->priv->p_mstruct = gda_meta_struct_new (store, GDA_META_STRUCT_FEATURE_ALL);
				job_id = gda_thread_wrapper_execute (bcnc->priv->wrapper,
								     (GdaThreadWrapperFunc) wrapper_meta_struct_sync,
								     g_object_ref (bcnc), g_object_unref, &lerror);
				if (job_id > 0)
					push_wrapper_job (bcnc, job_id, JOB_TYPE_META_STRUCT_SYNC,
							  _("Analysing database schema"));
				else if (lerror) {
					browser_show_error (NULL, _("Error while fetching meta data from the connection: %s"),
							    lerror->message ? lerror->message : _("No detail"));
					g_error_free (lerror);
				}
				g_object_unref (store);
			}
                        break;
                }
        }
}

static void
browser_connection_get_property (GObject *object,
				 guint param_id,
				 GValue *value,
				 GParamSpec *pspec)
{
        BrowserConnection *bcnc;

        bcnc = BROWSER_CONNECTION (object);
        if (bcnc->priv) {
                switch (param_id) {
                case PROP_GDA_CNC:
                        g_value_set_object (value, bcnc->priv->cnc);
                        break;
                }
        }
}

static void
clear_dsn_info (BrowserConnection *bcnc)
{
        g_free (bcnc->priv->dsn_info.name);
        bcnc->priv->dsn_info.name = NULL;

        g_free (bcnc->priv->dsn_info.provider);
        bcnc->priv->dsn_info.provider = NULL;

        g_free (bcnc->priv->dsn_info.description);
        bcnc->priv->dsn_info.description = NULL;

        g_free (bcnc->priv->dsn_info.cnc_string);
        bcnc->priv->dsn_info.cnc_string = NULL;

        g_free (bcnc->priv->dsn_info.auth_string);
        bcnc->priv->dsn_info.auth_string = NULL;
}

static void
fav_changed_cb (BrowserFavorites *bfav, BrowserConnection *bcnc)
{
	g_signal_emit (bcnc, browser_connection_signals [FAV_CHANGED], 0);
}

static void
browser_connection_dispose (GObject *object)
{
	BrowserConnection *bcnc;

	g_return_if_fail (object != NULL);
	g_return_if_fail (BROWSER_IS_CONNECTION (object));

	bcnc = BROWSER_CONNECTION (object);
	if (bcnc->priv) {
		if (bcnc->priv->variables)
			g_object_unref (bcnc->priv->variables);

		if (bcnc->priv->store_cnc)
			g_object_unref (bcnc->priv->store_cnc);

		if (bcnc->priv->executed_statements)
			g_hash_table_destroy (bcnc->priv->executed_statements);

		clear_dsn_info (bcnc);

		g_free (bcnc->priv->dict_file_name);

		if (bcnc->priv->wrapper_jobs) {
			g_slist_foreach (bcnc->priv->wrapper_jobs, (GFunc) wrapper_job_free, NULL);
			g_slist_free (bcnc->priv->wrapper_jobs);
		}

		if (bcnc->priv->wrapper_results_timer > 0)
			g_source_remove (bcnc->priv->wrapper_results_timer);

		g_object_unref (bcnc->priv->wrapper);
		g_free (bcnc->priv->name);
		if (bcnc->priv->mstruct)
			g_object_unref (bcnc->priv->mstruct);
		if (bcnc->priv->p_mstruct)
			g_object_unref (bcnc->priv->p_mstruct);
		if (bcnc->priv->p_mstruct_mutex)
			g_mutex_free (bcnc->priv->p_mstruct_mutex);
		if (bcnc->priv->cnc) {
			g_signal_handlers_disconnect_by_func (bcnc->priv->cnc,
							      G_CALLBACK (transaction_status_changed_cb),
							      bcnc);
			g_object_unref (bcnc->priv->cnc);
		}
		if (bcnc->priv->parser)
			g_object_unref (bcnc->priv->parser);
		if (bcnc->priv->bfav) {
			g_signal_handlers_disconnect_by_func (bcnc->priv->bfav,
							      G_CALLBACK (fav_changed_cb), bcnc);
			g_object_unref (bcnc->priv->bfav);
		}
		browser_connection_set_busy_state (bcnc, FALSE, NULL);

		g_free (bcnc->priv);
		bcnc->priv = NULL;
	}

	/* parent class */
	parent_class->dispose (object);
}

static gboolean
check_for_wrapper_result (BrowserConnection *bcnc)
{
	GError *lerror = NULL;
	gpointer exec_res = NULL;
	WrapperJob *wj;

	if (!bcnc->priv->wrapper_jobs)
		goto out;

	wj = (WrapperJob*) bcnc->priv->wrapper_jobs->data;
	exec_res = gda_thread_wrapper_fetch_result (bcnc->priv->wrapper,
						    FALSE, 
						    wj->job_id, &lerror);
	if (exec_res) {
		switch (wj->job_type) {
		case JOB_TYPE_META_STORE_UPDATE: {
			if (GPOINTER_TO_INT (exec_res) == 1) {
				browser_show_error (NULL, _("Error while analysing database schema: %s"),
						    lerror && lerror->message ? lerror->message : _("No detail"));
				g_clear_error (&lerror);
			}
			else {
				guint job_id;
				
				g_mutex_lock (bcnc->priv->p_mstruct_mutex);
				if (bcnc->priv->p_mstruct)
					g_object_unref (G_OBJECT (bcnc->priv->p_mstruct));
				bcnc->priv->p_mstruct = gda_meta_struct_new (gda_connection_get_meta_store (bcnc->priv->cnc),
									     GDA_META_STRUCT_FEATURE_ALL);
				g_mutex_unlock (bcnc->priv->p_mstruct_mutex);
				job_id = gda_thread_wrapper_execute (bcnc->priv->wrapper,
								     (GdaThreadWrapperFunc) wrapper_meta_struct_sync,
								     g_object_ref (bcnc), g_object_unref, &lerror);
				if (job_id > 0)
					push_wrapper_job (bcnc, job_id, JOB_TYPE_META_STRUCT_SYNC,
							  _("Analysing database schema"));
				else if (lerror) {
					browser_show_error (NULL, _("Error while fetching meta data from the connection: %s"),
							    lerror->message ? lerror->message : _("No detail"));
					g_error_free (lerror);
				}
			}
			break;
		}
		case JOB_TYPE_META_STRUCT_SYNC: {
			if (GPOINTER_TO_INT (exec_res) == 1) {
				browser_show_error (NULL, _("Error while analysing database schema: %s"),
						    lerror && lerror->message ? lerror->message : _("No detail"));
				g_clear_error (&lerror);
			}
			else {
				g_mutex_lock (bcnc->priv->p_mstruct_mutex);
				GdaMetaStruct *old_mstruct;
				old_mstruct = bcnc->priv->mstruct;
				bcnc->priv->mstruct = bcnc->priv->p_mstruct;
				bcnc->priv->p_mstruct = NULL;
				if (old_mstruct)
					g_object_unref (old_mstruct);
#ifdef GDA_DEBUG_NO
				GSList *all, *list;
				all = gda_meta_struct_get_all_db_objects (bcnc->priv->mstruct);
				for (list = all; list; list = list->next) {
					GdaMetaDbObject *dbo = (GdaMetaDbObject *) list->data;
					g_print ("DBO, Type %d: short=>[%s] schema=>[%s] full=>[%s]\n", dbo->obj_type,
						 dbo->obj_short_name, dbo->obj_schema, dbo->obj_full_name);
				}
				g_slist_free (all);
#endif

				g_signal_emit (bcnc, browser_connection_signals [META_CHANGED], 0, bcnc->priv->mstruct);
				g_mutex_unlock (bcnc->priv->p_mstruct_mutex);
			}
			break;
		}
		case JOB_TYPE_STATEMENT_EXECUTE: {
			guint *id;
			StatementResult *res;

			if (! bcnc->priv->executed_statements)
				bcnc->priv->executed_statements = g_hash_table_new_full (g_int_hash, g_int_equal,
											 g_free,
											 (GDestroyNotify) statement_result_free);
			id = g_new (guint, 1);
			*id = wj->job_id;
			res = g_new0 (StatementResult, 1);
			if (exec_res == (gpointer) 0x01)
				res->error = lerror;
			else {
				res->result = G_OBJECT (exec_res);
				res->last_inserted_row = g_object_get_data (exec_res, "__bcnc_last_inserted_row");
				if (res->last_inserted_row)
					g_object_set_data (exec_res, "__bcnc_last_inserted_row", NULL);
			}
			g_hash_table_insert (bcnc->priv->executed_statements, id, res);
			break;
		}
		}

		pop_wrapper_job (bcnc, wj);
	}

 out:
	if (!bcnc->priv->wrapper_jobs) {
		bcnc->priv->wrapper_results_timer = 0;
		return FALSE;
	}
	else {
		wj = (WrapperJob*) bcnc->priv->wrapper_jobs->data;
		if (exec_res)
			g_signal_emit (bcnc, browser_connection_signals [BUSY], 0, TRUE, wj->reason);
		return TRUE;
	}
}

/**
 * browser_connection_new
 * @cnc: a #GdaConnection
 *
 * Creates a new #BrowserConnection object wrapping @cnc. The browser_core_take_connection() method
 * must be called on the new object to mahe it managed by the browser.
 *
 * To close the new connection, use browser_core_close_connection().
 *
 * Returns: a new object
 */
BrowserConnection*
browser_connection_new (GdaConnection *cnc)
{
	BrowserConnection *bcnc;

	g_return_val_if_fail (GDA_IS_CONNECTION (cnc), NULL);

	bcnc = BROWSER_CONNECTION (g_object_new (BROWSER_TYPE_CONNECTION, "gda-connection", cnc, NULL));

	return bcnc;
}

/**
 * browser_connection_get_name
 * @bcnc: a #BrowserConnection
 *
 * Returns: @bcnc's name
 */
const gchar *
browser_connection_get_name (BrowserConnection *bcnc)
{
	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), NULL);
	return bcnc->priv->name;
}

/**
 * browser_connection_get_information
 * @bcnc: a #BrowserConnection
 *
 * Get some information about the connection
 *
 * Returns: a pointer to the associated #GdaDsnInfo
 */
const GdaDsnInfo *
browser_connection_get_information (BrowserConnection *bcnc)
{
	gboolean is_wrapper;
	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), NULL);

	clear_dsn_info (bcnc);
	if (!bcnc->priv->cnc)
		return NULL;
	
	g_object_get (G_OBJECT (bcnc->priv->cnc), "is-wrapper", &is_wrapper, NULL);

	if (!is_wrapper && gda_connection_get_provider_name (bcnc->priv->cnc))
		bcnc->priv->dsn_info.provider = g_strdup (gda_connection_get_provider_name (bcnc->priv->cnc));
	if (gda_connection_get_dsn (bcnc->priv->cnc)) {
		bcnc->priv->dsn_info.name = g_strdup (gda_connection_get_dsn (bcnc->priv->cnc));
		if (! bcnc->priv->dsn_info.provider) {
			GdaDsnInfo *cinfo;
			cinfo = gda_config_get_dsn_info (bcnc->priv->dsn_info.name);
			if (cinfo && cinfo->provider)
				bcnc->priv->dsn_info.provider = g_strdup (cinfo->provider);
		}
	}
	if (gda_connection_get_cnc_string (bcnc->priv->cnc))
		bcnc->priv->dsn_info.cnc_string = g_strdup (gda_connection_get_cnc_string (bcnc->priv->cnc));
	if (is_wrapper && bcnc->priv->dsn_info.cnc_string) {
		GdaQuarkList *ql;
		const gchar *prov;
		ql = gda_quark_list_new_from_string (bcnc->priv->dsn_info.cnc_string);
		prov = gda_quark_list_find (ql, "PROVIDER_NAME");
		if (prov)
			bcnc->priv->dsn_info.provider = g_strdup (prov);
		gda_quark_list_free (ql);
	}
	if (gda_connection_get_authentication (bcnc->priv->cnc))
		bcnc->priv->dsn_info.auth_string = g_strdup (gda_connection_get_authentication (bcnc->priv->cnc));

	return &(bcnc->priv->dsn_info);
}

/**
 * browser_connection_is_busy
 * @bcnc: a #BrowserConnection
 * @out_reason: a pointer to store a copy of the reason @bcnc is busy (will be set 
 *              to %NULL if @bcnc is not busy), or %NULL
 *
 * Tells if @bcnc is currently busy or not.
 *
 * Returns: %TRUE if @bcnc is busy
 */
gboolean
browser_connection_is_busy (BrowserConnection *bcnc, gchar **out_reason)
{
	if (out_reason)
		*out_reason = NULL;
	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), FALSE);

	if (out_reason && bcnc->priv->busy_reason)
		*out_reason = g_strdup (bcnc->priv->busy_reason);

	return bcnc->priv->busy;
}

/**
 * browser_connection_update_meta_data
 * @bcnc: a #BrowserConnection
 *
 * Make @bcnc update its meta store in the background.
 */
void
browser_connection_update_meta_data (BrowserConnection *bcnc)
{
	g_return_if_fail (BROWSER_IS_CONNECTION (bcnc));

	if (bcnc->priv->wrapper_jobs) {
		WrapperJob *wj;
		wj = (WrapperJob*) g_slist_last (bcnc->priv->wrapper_jobs)->data;
		if (wj->job_type == JOB_TYPE_META_STORE_UPDATE) {
			/* nothing to do */
			return;
		}
	}

	guint job_id;
	GError *lerror = NULL;
	job_id = gda_thread_wrapper_execute (bcnc->priv->wrapper,
					     (GdaThreadWrapperFunc) wrapper_meta_store_update,
					     g_object_ref (bcnc), g_object_unref, &lerror);
	if (job_id > 0)
		push_wrapper_job (bcnc, job_id, JOB_TYPE_META_STORE_UPDATE,
				  _("Getting database schema information"));
	else if (lerror) {
		browser_show_error (NULL, _("Error while fetching meta data from the connection: %s"),
				    lerror->message ? lerror->message : _("No detail"));
		g_error_free (lerror);
	}
}

/**
 * browser_connection_get_meta_struct
 * @bcnc: a #BrowserConnection
 *
 * Get the #GdaMetaStruct maintained up to date by @bcnc.
 *
 * Returns: a #GdaMetaStruct, the caller does not have any reference to it.
 */
GdaMetaStruct *
browser_connection_get_meta_struct (BrowserConnection *bcnc)
{
	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), NULL);
	return bcnc->priv->mstruct;
}

/**
 * browser_connection_get_meta_store
 * @bcnc: a #BrowserConnection
 *
 * Returns: @bcnc's #GdaMetaStore, the caller does not have any reference to it.
 */
GdaMetaStore *
browser_connection_get_meta_store (BrowserConnection *bcnc)
{
	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), NULL);
	return gda_connection_get_meta_store (bcnc->priv->cnc);
}

/**
 * browser_connection_get_dictionary_file
 * @bcnc: a #BrowserConnection
 *
 * Returns: the dictionary file name used by @bcnc, or %NULL
 */
const gchar *
browser_connection_get_dictionary_file (BrowserConnection *bcnc)
{
	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), NULL);
	return bcnc->priv->dict_file_name;
}

/**
 * browser_connection_get_transaction_status
 * @bcnc: a #BrowserConnection
 *
 * Retuns: the #GdaTransactionStatus of the connection, or %NULL
 */
GdaTransactionStatus *
browser_connection_get_transaction_status (BrowserConnection *bcnc)
{
	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), NULL);
	return gda_connection_get_transaction_status (bcnc->priv->cnc);
}

/**
 * browser_connection_begin
 * @bcnc: a #BrowserConnection
 * @error: a place to store errors, or %NULL
 *
 * Begins a transaction
 */
gboolean
browser_connection_begin (BrowserConnection *bcnc, GError **error)
{
	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), FALSE);
	return gda_connection_begin_transaction (bcnc->priv->cnc, NULL,
						 GDA_TRANSACTION_ISOLATION_UNKNOWN, error);
}

/**
 * browser_connection_commit
 * @bcnc: a #BrowserConnection
 * @error: a place to store errors, or %NULL
 *
 * Commits a transaction
 */
gboolean
browser_connection_commit (BrowserConnection *bcnc, GError **error)
{
	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), FALSE);
	return gda_connection_commit_transaction (bcnc->priv->cnc, NULL, error);
}

/**
 * browser_connection_rollback
 * @bcnc: a #BrowserConnection
 * @error: a place to store errors, or %NULL
 *
 * Rolls back a transaction
 */
gboolean
browser_connection_rollback (BrowserConnection *bcnc, GError **error)
{
	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), FALSE);
	return gda_connection_rollback_transaction (bcnc->priv->cnc, NULL, error);
}

/**
 * browser_connection_get_favorites
 * @bcnc: a #BrowserConnection
 *
 * Get @bcnc's favorites handler
 *
 * Returns: the #BrowserFavorites used by @bcnc
 */
BrowserFavorites *
browser_connection_get_favorites (BrowserConnection *bcnc)
{
	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), NULL);
	if (!bcnc->priv->bfav) {
		bcnc->priv->bfav = browser_favorites_new (gda_connection_get_meta_store (bcnc->priv->cnc));
		g_signal_connect (bcnc->priv->bfav, "favorites-changed",
				  G_CALLBACK (fav_changed_cb), bcnc);
	}
	return bcnc->priv->bfav;
}

/**
 * browser_connection_get_completions
 * @bcnc: a #BrowserConnection
 * @sql:
 * @start:
 * @end:
 *
 * See gda_completion_list_get()
 *
 * Returns: a new array of strings, or NULL (use g_strfreev() to free the returned array)
 */
gchar **
browser_connection_get_completions (BrowserConnection *bcnc, const gchar *sql,
				    gint start, gint end)
{
	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), NULL);
	return gda_completion_list_get (bcnc->priv->cnc, sql, start, end);
}


/**
 * browser_connection_create_parser
 * @bcnc: a #BrowserConnection
 *
 * Get a new #GdaSqlParser object for @bcnc
 *
 * Returns: a new #GdaSqlParser
 */
GdaSqlParser *
browser_connection_create_parser (BrowserConnection *bcnc)
{
	GdaSqlParser *parser;
	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), NULL);
	
	parser = gda_connection_create_parser (bcnc->priv->cnc);
	if (!parser)
		parser = gda_sql_parser_new ();
	return parser;
}

/**
 * browser_connection_render_pretty_sql
 * @bcnc: a #BrowserConnection
 * @stmt: a #GdaStatement
 *
 * Renders @stmt as SQL well indented
 *
 * Returns: a new string
 */
gchar *
browser_connection_render_pretty_sql (BrowserConnection *bcnc, GdaStatement *stmt)
{
	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), NULL);
	g_return_val_if_fail (GDA_IS_STATEMENT (stmt), NULL);

	return gda_statement_to_sql_extended (stmt, bcnc->priv->cnc, NULL,
					      GDA_STATEMENT_SQL_PRETTY |
					      GDA_STATEMENT_SQL_PARAMS_SHORT,
					      NULL, NULL);
}

typedef struct {
	GdaConnection *cnc;
	GdaStatement *stmt;
	GdaSet *params;
	GdaStatementModelUsage model_usage;
	gboolean need_last_insert_row;
} StmtExecData;

/* executed in sub @bcnc->priv->wrapper's thread */
static gpointer
wrapper_statement_execute (StmtExecData *data, GError **error)
{
	GObject *obj;
	GdaSet *last_insert_row = NULL;
	obj = gda_connection_statement_execute (data->cnc, data->stmt,
						data->params, data->model_usage,
						data->need_last_insert_row ? &last_insert_row : NULL,
						error);
	if (obj) {
		if (GDA_IS_DATA_MODEL (obj))
			/* force loading of rows if necessary */
			gda_data_model_get_n_rows ((GdaDataModel*) obj);
		else if (last_insert_row)
			g_object_set_data (obj, "__bcnc_last_inserted_row", last_insert_row);
	}
	return obj ? obj : (gpointer) 0x01;
}

/**
 * browser_connection_execute_statement
 * @bcnc: a #BrowserConnection
 * @stmt: a #GdaStatement
 * @params: a #GdaSet as parameters, or %NULL
 * @model_usage: how the returned data model (if any) will be used
 * @need_last_insert_row: %TRUE if the values of the last interted row must be computed
 * @error: a place to store errors, or %NULL
 *
 * Executes @stmt by @bcnc
 *
 * Returns: a job ID, to be used with browser_connection_execution_get_result(), or %0 if an
 * error occurred
 */
guint
browser_connection_execute_statement (BrowserConnection *bcnc,
				      GdaStatement *stmt,
				      GdaSet *params,
				      GdaStatementModelUsage model_usage,
				      gboolean need_last_insert_row,
				      GError **error)
{
	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), 0);
	g_return_val_if_fail (GDA_IS_STATEMENT (stmt), 0);
	g_return_val_if_fail (!params || GDA_IS_SET (params), 0);

	StmtExecData *data;
	guint job_id;

	data = g_new0 (StmtExecData, 1);
	data->cnc = bcnc->priv->cnc;
	data->stmt = stmt;
	data->params = params;
	data->model_usage = model_usage;
	data->need_last_insert_row = need_last_insert_row;

	job_id = gda_thread_wrapper_execute (bcnc->priv->wrapper,
					     (GdaThreadWrapperFunc) wrapper_statement_execute,
					     data, (GDestroyNotify) g_free, error);
	if (job_id > 0)
		push_wrapper_job (bcnc, job_id, JOB_TYPE_STATEMENT_EXECUTE,
				  _("Executing a query"));

	return job_id;
}

typedef struct {
	GdaConnection *cnc;
	GdaDataModel *model;
} RerunSelectData;

/* executed in @bcnc->priv->wrapper's sub thread */
static gpointer
wrapper_rerun_select (RerunSelectData *data, GError **error)
{
	gboolean retval;

	retval = gda_data_select_rerun (GDA_DATA_SELECT (data->model), error);
	return retval ? data->model : (gpointer) 0x01;
}

/**
 * browser_connection_rerun_select
 */
guint
browser_connection_rerun_select (BrowserConnection *bcnc,
				 GdaDataModel *model,
				 GError **error)
{
	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), 0);
	g_return_val_if_fail (GDA_IS_DATA_SELECT (model), 0);

	RerunSelectData *data;
	guint job_id;

	data = g_new0 (RerunSelectData, 1);
	data->cnc = bcnc->priv->cnc;
	data->model = model;

	job_id = gda_thread_wrapper_execute (bcnc->priv->wrapper,
					     (GdaThreadWrapperFunc) wrapper_rerun_select,
					     data, (GDestroyNotify) g_free, error);
	if (job_id > 0)
		push_wrapper_job (bcnc, job_id, JOB_TYPE_STATEMENT_EXECUTE,
				  _("Executing a query"));

	return job_id;
}


/**
 * browser_connection_execution_get_result
 * @bcnc: a #BrowserConnection
 * @exec_id: the ID of the excution
 * @last_insert_row: a place to store the last inserted row, if any, or %NULL
 * @error: a place to store errors, or %NULL
 *
 * Pick up the result of the @exec_id's execution.
 *
 * Returns: the execution result, or %NULL if either an error occurred or the result is not yet ready
 */
GObject *
browser_connection_execution_get_result (BrowserConnection *bcnc, guint exec_id,
					 GdaSet **last_insert_row, GError **error)
{
	StatementResult *res;
	guint id;
	GObject *retval;

	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), NULL);
	g_return_val_if_fail (exec_id > 0, NULL);

	if (! bcnc->priv->executed_statements)
		return NULL;

	id = exec_id;
	res = g_hash_table_lookup (bcnc->priv->executed_statements, &id);
	if (!res)
		return NULL;

	retval = res->result;
	res->result = NULL;

	if (last_insert_row) {
		*last_insert_row = res->last_inserted_row;
		res->last_inserted_row = NULL;
	}
	
	if (res->error) {
		g_propagate_error (error, res->error);
		res->error = NULL;
	}

	g_hash_table_remove (bcnc->priv->executed_statements, &id);
	return retval;
}

/**
 * browser_connection_normalize_sql_statement
 * @bcnc: a #BrowserConnection
 * @sqlst: a #GdaSqlStatement
 * @error: a place to store errors, or %NULL
 *
 * See gda_sql_statement_normalize().
 *
 * Returns: %TRUE if no error occurred
 */
gboolean
browser_connection_normalize_sql_statement (BrowserConnection *bcnc,
					    GdaSqlStatement *sqlst, GError **error)
{
	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), FALSE);
	
	return gda_sql_statement_normalize (sqlst, bcnc->priv->cnc, error);
}


/*
 * DOES NOT emit any signal
 */
void
browser_connection_set_busy_state (BrowserConnection *bcnc, gboolean busy, const gchar *busy_reason)
{
	if (bcnc->priv->busy_reason) {
		g_free (bcnc->priv->busy_reason);
		bcnc->priv->busy_reason = NULL;
	}

	bcnc->priv->busy = busy;
	if (busy_reason)
		bcnc->priv->busy_reason = g_strdup (busy_reason);
}

/*
 *
 * Preferences
 *
 */
#define DBTABLE_PREFERENCES_TABLE_NAME "gda_sql_dbtable_preferences"
#define DBTABLE_PREFERENCES_TABLE_DESC \
        "<table name=\"" DBTABLE_PREFERENCES_TABLE_NAME "\"> "                            \
        "   <column name=\"table_schema\" pkey=\"TRUE\"/>"             \
        "   <column name=\"table_name\" pkey=\"TRUE\"/>"                              \
        "   <column name=\"table_column\" nullok=\"TRUE\" pkey=\"TRUE\"/>"                              \
        "   <column name=\"att_name\"/>"                          \
        "   <column name=\"att_value\"/>"                           \
        "</table>"

static gboolean
meta_store_addons_init (BrowserConnection *bcnc, GError **error)
{
	GError *lerror = NULL;
	GdaMetaStore *store;

	if (!bcnc->priv->cnc) {
		g_set_error (error, 0, 0,
			     _("Connection not yet opened"));
		return FALSE;
	}
	store = gda_connection_get_meta_store (bcnc->priv->cnc);
	if (!gda_meta_store_schema_add_custom_object (store, DBTABLE_PREFERENCES_TABLE_DESC, &lerror)) {
                g_set_error (error, 0, 0, "%s",
                             _("Can't initialize dictionary to store table preferences"));
		g_warning ("Can't initialize dictionary to store dbtable_preferences :%s",
			   lerror && lerror->message ? lerror->message : "No detail");
		if (lerror)
			g_error_free (lerror);
                return FALSE;
        }

	bcnc->priv->store_cnc = g_object_ref (gda_meta_store_get_internal_connection (store));
	return TRUE;
}


/**
 * browser_connection_set_table_column_attribute
 * @bcnc:
 * @dbo:
 * @column:
 * @attr_name: attribute name, not %NULL
 * @value: value to set, or %NULL to unset
 * @error:
 *
 *
 * Returns: %TRUE if no error occurred
 */
gboolean
browser_connection_set_table_column_attribute (BrowserConnection *bcnc,
					       GdaMetaTable *table,
					       GdaMetaTableColumn *column,
					       const gchar *attr_name,
					       const gchar *value, GError **error)
{
	GdaConnection *store_cnc;

	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), FALSE);
	g_return_val_if_fail (table, FALSE);
	g_return_val_if_fail (column, FALSE);
	g_return_val_if_fail (attr_name, FALSE);

	if (! bcnc->priv->store_cnc &&
	    ! meta_store_addons_init (bcnc, error))
		return FALSE;

	store_cnc = bcnc->priv->store_cnc;
	if (! gda_lockable_trylock (GDA_LOCKABLE (store_cnc))) {
		g_set_error (error, 0, 0, "%s",
                             _("Can't initialize transaction to access favorites"));
		return FALSE;
	}
	/* begin a transaction */
	if (! gda_connection_begin_transaction (store_cnc, NULL, GDA_TRANSACTION_ISOLATION_UNKNOWN, NULL)) {
		g_set_error (error, 0, 0, "%s",
                             _("Can't initialize transaction to access favorites"));
		gda_lockable_unlock (GDA_LOCKABLE (store_cnc));
                return FALSE;
	}

	/* delete existing attribute */
	GdaStatement *stmt;
	GdaSqlBuilder *builder;
	GdaSet *params;
	GdaSqlBuilderId op_ids[4];
	GdaMetaDbObject *dbo = (GdaMetaDbObject *) table;

	params = gda_set_new_inline (5, "schema", G_TYPE_STRING, dbo->obj_schema,
				     "name", G_TYPE_STRING, dbo->obj_name,
				     "column", G_TYPE_STRING, column->column_name,
				     "attname", G_TYPE_STRING, attr_name,
				     "attvalue", G_TYPE_STRING, value);

	builder = gda_sql_builder_new (GDA_SQL_STATEMENT_DELETE);
	gda_sql_builder_set_table (builder, DBTABLE_PREFERENCES_TABLE_NAME);
	op_ids[0] = gda_sql_builder_add_cond (builder, GDA_SQL_OPERATOR_TYPE_EQ,
					      gda_sql_builder_add_id (builder, "table_schema"),
					      gda_sql_builder_add_param (builder, "schema", G_TYPE_STRING,
									 FALSE), 0);
	op_ids[1] = gda_sql_builder_add_cond (builder, GDA_SQL_OPERATOR_TYPE_EQ,
					      gda_sql_builder_add_id (builder, "table_name"),
					      gda_sql_builder_add_param (builder, "name", G_TYPE_STRING,
									 FALSE), 0);
	op_ids[2] = gda_sql_builder_add_cond (builder, GDA_SQL_OPERATOR_TYPE_EQ,
					      gda_sql_builder_add_id (builder, "table_column"),
					      gda_sql_builder_add_param (builder, "column", G_TYPE_STRING,
									 FALSE), 0);
	op_ids[3] = gda_sql_builder_add_cond (builder, GDA_SQL_OPERATOR_TYPE_EQ,
					      gda_sql_builder_add_id (builder, "att_name"),
					      gda_sql_builder_add_param (builder, "attname", G_TYPE_STRING,
									 FALSE), 0);
	gda_sql_builder_set_where (builder,
				   gda_sql_builder_add_cond_v (builder, GDA_SQL_OPERATOR_TYPE_AND,
							       op_ids, 4));
	stmt = gda_sql_builder_get_statement (builder, error);
	g_object_unref (G_OBJECT (builder));
	if (!stmt)
		goto err;
	if (gda_connection_statement_execute_non_select (store_cnc, stmt, params, NULL, error) == -1) {
		g_object_unref (stmt);
		goto err;
	}
	g_object_unref (stmt);		

	/* insert new attribute if necessary */
	if (value) {
		builder = gda_sql_builder_new (GDA_SQL_STATEMENT_INSERT);
		gda_sql_builder_set_table (builder, DBTABLE_PREFERENCES_TABLE_NAME);
		gda_sql_builder_add_field_value_id (builder,
					      gda_sql_builder_add_id (builder, "table_schema"),
					      gda_sql_builder_add_param (builder, "schema", G_TYPE_STRING, FALSE));
		gda_sql_builder_add_field_value_id (builder,
					      gda_sql_builder_add_id (builder, "table_name"),
					      gda_sql_builder_add_param (builder, "name", G_TYPE_STRING, FALSE));
		gda_sql_builder_add_field_value_id (builder,
					      gda_sql_builder_add_id (builder, "table_column"),
					      gda_sql_builder_add_param (builder, "column", G_TYPE_STRING, FALSE));
		gda_sql_builder_add_field_value_id (builder,
					      gda_sql_builder_add_id (builder, "att_name"),
					      gda_sql_builder_add_param (builder, "attname", G_TYPE_STRING, FALSE));
		gda_sql_builder_add_field_value_id (builder,
					      gda_sql_builder_add_id (builder, "att_value"),
					      gda_sql_builder_add_param (builder, "attvalue", G_TYPE_STRING, FALSE));
		stmt = gda_sql_builder_get_statement (builder, error);
		g_object_unref (G_OBJECT (builder));
		if (!stmt)
			goto err;
		if (gda_connection_statement_execute_non_select (store_cnc, stmt, params, NULL, error) == -1) {
			g_object_unref (stmt);
			goto err;
		}
		g_object_unref (stmt);
	}

	if (! gda_connection_commit_transaction (store_cnc, NULL, NULL)) {
		g_set_error (error, 0, 0, "%s",
                             _("Can't commit transaction to access favorites"));
		goto err;
	}

	g_object_unref (params);
	gda_lockable_unlock (GDA_LOCKABLE (store_cnc));
	/*
	g_print ("%s(table=>%s, column=>%s, value=>%s)\n", __FUNCTION__, GDA_META_DB_OBJECT (table)->obj_full_name,
		 column->column_name, value);
	*/
	g_signal_emit (bcnc, browser_connection_signals [TABLE_COLUMN_PREF_CHANGED], 0,
		       table, column, attr_name, value);

	return TRUE;

 err:
	g_object_unref (params);
	gda_lockable_unlock (GDA_LOCKABLE (store_cnc));
	gda_connection_rollback_transaction (store_cnc, NULL, NULL);
	return FALSE;
}

/**
 * browser_connection_get_table_column_attribute
 * @bcnc:
 * @dbo:
 * @column: may be %NULL
 * @attr_name: attribute name, not %NULL
 * @error:
 *
 *
 * Returns: the requested attribute, or %NULL if not set or if an error occurred
 */
gchar *
browser_connection_get_table_column_attribute  (BrowserConnection *bcnc,
						GdaMetaTable *table,
						GdaMetaTableColumn *column,
						const gchar *attr_name,
						GError **error)
{
	GdaConnection *store_cnc;
	gchar *retval = NULL;

	g_return_val_if_fail (BROWSER_IS_CONNECTION (bcnc), FALSE);
	g_return_val_if_fail (table, FALSE);
	g_return_val_if_fail (column, FALSE);
	g_return_val_if_fail (attr_name, FALSE);

	if (! bcnc->priv->store_cnc &&
	    ! meta_store_addons_init (bcnc, error))
		return FALSE;

	store_cnc = bcnc->priv->store_cnc;
	if (! gda_lockable_trylock (GDA_LOCKABLE (store_cnc))) {
		g_set_error (error, 0, 0, "%s",
                             _("Can't initialize transaction to access favorites"));
		return FALSE;
	}

	/* SELECT */
	GdaStatement *stmt;
	GdaSqlBuilder *builder;
	GdaSet *params;
	GdaSqlBuilderId op_ids[4];
	GdaDataModel *model;
	const GValue *cvalue;
	GdaMetaDbObject *dbo = (GdaMetaDbObject *) table;

	params = gda_set_new_inline (4, "schema", G_TYPE_STRING, dbo->obj_schema,
				     "name", G_TYPE_STRING, dbo->obj_name,
				     "column", G_TYPE_STRING, column->column_name,
				     "attname", G_TYPE_STRING, attr_name);

	builder = gda_sql_builder_new (GDA_SQL_STATEMENT_SELECT);
	gda_sql_builder_select_add_target_id (builder,
					      gda_sql_builder_add_id (builder, DBTABLE_PREFERENCES_TABLE_NAME),
					      NULL);
	gda_sql_builder_select_add_field (builder, "att_value", NULL, NULL);
	op_ids[0] = gda_sql_builder_add_cond (builder, GDA_SQL_OPERATOR_TYPE_EQ,
					      gda_sql_builder_add_id (builder, "table_schema"),
					      gda_sql_builder_add_param (builder, "schema", G_TYPE_STRING,
									 FALSE), 0);
	op_ids[1] = gda_sql_builder_add_cond (builder, GDA_SQL_OPERATOR_TYPE_EQ,
					      gda_sql_builder_add_id (builder, "table_name"),
					      gda_sql_builder_add_param (builder, "name", G_TYPE_STRING,
									 FALSE), 0);
	op_ids[2] = gda_sql_builder_add_cond (builder, GDA_SQL_OPERATOR_TYPE_EQ,
					      gda_sql_builder_add_id (builder, "table_column"),
					      gda_sql_builder_add_param (builder, "column", G_TYPE_STRING,
									 FALSE), 0);
	op_ids[3] = gda_sql_builder_add_cond (builder, GDA_SQL_OPERATOR_TYPE_EQ,
					      gda_sql_builder_add_id (builder, "att_name"),
					      gda_sql_builder_add_param (builder, "attname", G_TYPE_STRING,
									 FALSE), 0);
	gda_sql_builder_set_where (builder,
				   gda_sql_builder_add_cond_v (builder, GDA_SQL_OPERATOR_TYPE_AND,
							       op_ids, 4));
	stmt = gda_sql_builder_get_statement (builder, error);
	g_object_unref (G_OBJECT (builder));
	if (!stmt)
		goto out;

	model = gda_connection_statement_execute_select (store_cnc, stmt, params, error);
	g_object_unref (stmt);
	if (!model)
		goto out;

	/*gda_data_model_dump (model, NULL);*/
	if (gda_data_model_get_n_rows (model) == 0)
		goto out;

	cvalue = gda_data_model_get_value_at (model, 0, 0, error);
	if (cvalue)
		retval = g_value_dup_string (cvalue);

 out:
	if (model)
		g_object_unref (model);
	g_object_unref (params);
	gda_lockable_unlock (GDA_LOCKABLE (store_cnc));

	return retval;
}

/**
 * browser_connection_keep_variables
 * @bcnc: a #BrowserConnection object
 * @set: a #GdaSet containing variables for which a copy has to be done
 *
 * Makes a copy of the variables in @set and keep them in @bcnc. Retreive them
 * using browser_connection_load_variables()
 */
void
browser_connection_keep_variables (BrowserConnection *bcnc, GdaSet *set)
{
	g_return_if_fail (BROWSER_IS_CONNECTION (bcnc));
	if (!set)
		return;
	g_return_if_fail (GDA_IS_SET (set));

	if (! bcnc->priv->variables) {
		bcnc->priv->variables = gda_set_copy (set);
		return;
	}

	GSList *list;
	for (list = set->holders; list; list = list->next) {
		GdaHolder *nh, *eh;
		nh = GDA_HOLDER (list->data);
		eh = gda_set_get_holder (bcnc->priv->variables, gda_holder_get_id (nh));
		if (eh) {
			if (gda_holder_get_g_type (nh) == gda_holder_get_g_type (eh)) {
				const GValue *cvalue;
				cvalue = gda_holder_get_value (nh);
				gda_holder_set_value (eh, cvalue, NULL);
			}
			else {
				gda_set_remove_holder (bcnc->priv->variables, eh);
				eh = gda_holder_copy (nh);
				gda_set_add_holder (bcnc->priv->variables, eh);
				g_object_unref (eh);
			}
		}
		else {
			eh = gda_holder_copy (nh);
			gda_set_add_holder (bcnc->priv->variables, eh);
			g_object_unref (eh);
		}
	}
}

/**
 * browser_connection_load_variables
 * @bcnc: a #BrowserConnection object
 * @set: a #GdaSet which will in the end contain (if any) variables stored in @bcnc
 *
 * For each #GdaHolder in @set, set the value if one is available in @bcnc.
 */
void
browser_connection_load_variables (BrowserConnection *bcnc, GdaSet *set)
{
	g_return_if_fail (BROWSER_IS_CONNECTION (bcnc));
	if (!set)
		return;
	g_return_if_fail (GDA_IS_SET (set));

	if (! bcnc->priv->variables)
		return;

	GSList *list;
	for (list = set->holders; list; list = list->next) {
		GdaHolder *nh, *eh;
		nh = GDA_HOLDER (list->data);
		eh = gda_set_get_holder (bcnc->priv->variables, gda_holder_get_id (nh));
		if (eh) {
			if (gda_holder_get_g_type (nh) == gda_holder_get_g_type (eh)) {
				const GValue *cvalue;
				cvalue = gda_holder_get_value (eh);
				gda_holder_set_value (nh, cvalue, NULL);
			}
			else if (g_value_type_transformable (gda_holder_get_g_type (eh),
							     gda_holder_get_g_type (nh))) {
				const GValue *evalue;
				GValue *nvalue;
				evalue = gda_holder_get_value (eh);
				nvalue = gda_value_new (gda_holder_get_g_type (nh));
				if (g_value_transform (evalue, nvalue))
					gda_holder_take_value (nh, nvalue, NULL);
				else
					gda_value_free (nvalue);
			}
		}
	}	
}