/*****************************************************************************

Copyright (c) 2016, MariaDB Corporation.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA

*****************************************************************************/

/**************************************************//**
@file dict/dict0tableoptions.cc
Function implementations for the system table SYS_TABLE_OPTIONS

Created 22/01/2016 Jan Lindström
*******************************************************/

#include "mysql_version.h"
#include "btr0pcur.h"
#include "btr0btr.h"
#include "page0page.h"
#include "mach0data.h"
#include "dict0dict.h"
#include "dict0boot.h"
#include "dict0stats.h"
#include "dict0mem.h"
#include "rem0cmp.h"
#include "srv0start.h"
#include "srv0srv.h"
#include "dict0crea.h"
#include "dict0priv.h"
#include "ha_prototypes.h" /* innobase_casedn_str() */
#include "fts0priv.h"
#include "dict0tableoptions.h"
#include "dict0load.h"
#include "row0mysql.h"

/********************************************************************//**
This function parses a SYS_TABLE_OPTIONS record, extracts necessary
information from the record and returns it to the caller.
@return error message or NULL if successfull */
UNIV_INTERN
const char*
dict_process_sys_tableoptions(
/*==========================*/
	mem_heap_t*	heap,		/*!< in/out: heap memory */
	const rec_t*	rec,		/*!< in: current SYS_TABLE_OPTIONS rec */
	dict_tableoptions_t* table_options) /*!< out: table options */
{
	const byte*	field;
	ulint		len=0;

	if (rec_get_deleted_flag(rec, 0)) {
		return (NULL);
	}

	if (rec_get_n_fields_old(rec) != DICT_NUM_FIELDS__SYS_TABLEOPTIONS) {
		return("wrong number of columns in SYS_TABLE_OPTIONS record");
	}

	field = rec_get_nth_field_old(
		rec, DICT_FLD__SYS_TABLEOPTIONS__TABLE_ID, &len);
	if (len != 8) {
err_len:
		return("incorrect column length in SYS_TABLE_OPTIONS");
	}

	table_options->table_id = mach_read_from_8(field);

	rec_get_nth_field_offs_old(
		rec, DICT_FLD__SYS_TABLEOPTIONS__DB_TRX_ID, &len);
	if (len != DATA_TRX_ID_LEN && len != UNIV_SQL_NULL) {
		goto err_len;
	}

	rec_get_nth_field_offs_old(
		rec, DICT_FLD__SYS_TABLEOPTIONS__DB_ROLL_PTR, &len);
	if (len != DATA_ROLL_PTR_LEN && len != UNIV_SQL_NULL) {
		goto err_len;
	}

	field = rec_get_nth_field_old(
		rec, DICT_FLD__SYS_TABLEOPTIONS__PAGE_COMPRESSED, &len);
	if (len != 4 && len != UNIV_SQL_NULL) {
		goto err_len;
	}

	table_options->page_compressed = mach_read_from_4(field);

	field = rec_get_nth_field_old(
		rec, DICT_FLD__SYS_TABLEOPTIONS__PAGE_COMPRESSION_LEVEL, &len);
	if (len != 4 && len != UNIV_SQL_NULL) {
		goto err_len;
	}

	table_options->page_compression_level = mach_read_from_4(field);

	field = rec_get_nth_field_old(
		rec, DICT_FLD__SYS_TABLEOPTIONS__ENCRYPTED, &len);
	if (len != 4 && len != UNIV_SQL_NULL) {
		goto err_len;
	}

	table_options->encryption = (fil_encryption_t)mach_read_from_4(field);

	field = rec_get_nth_field_old(
		rec, DICT_FLD__SYS_TABLEOPTIONS__ENCRYPTION_KEY_ID, &len);
	if (len != 4 && len != UNIV_SQL_NULL) {
		goto err_len;
	}

	table_options->encryption_key_id = mach_read_from_4(field);

	field = rec_get_nth_field_old(
		rec, DICT_FLD__SYS_TABLEOPTIONS__IS_SHARED, &len);
	if (len != 4 && len != UNIV_SQL_NULL) {
		goto err_len;
	}

	table_options->is_shared = mach_read_from_4(field);

	field = rec_get_nth_field_old(
		rec, DICT_FLD__SYS_TABLEOPTIONS__IS_TEMPORARY, &len);
	if (len != 4 && len != UNIV_SQL_NULL) {
		goto err_len;
	}

	table_options->is_temporary = mach_read_from_4(field);
	table_options->is_stored = true;

	return(NULL);
}

/********************************************************************//**
Load the table options from SYS_TABLE_OPTIONS based on table_id
@return true if found, false if not */
UNIV_INTERN
bool
dict_load_table_options(
/*====================*/
	dict_table_t*	table,
	mem_heap_t*	heap)
{
	mtr_t		mtr;
	dict_table_t*	sys_tableoptions;
	dict_index_t*	sys_index;
	dtuple_t*	tuple;
	dfield_t*	dfield;
	byte*		buf;
	btr_pcur_t	pcur;
	const rec_t*	rec;
	bool		found = false;

	mtr_start(&mtr);

	ut_ad(mutex_own(&(dict_sys->mutex)));

	sys_tableoptions = dict_table_get_low("SYS_TABLE_OPTIONS");
	sys_index = UT_LIST_GET_FIRST(sys_tableoptions->indexes);
	ut_ad(!dict_table_is_comp(sys_tableoptions));

	tuple = dtuple_create(heap, 1);
	dfield = dtuple_get_nth_field(tuple, DICT_FLD__SYS_TABLEOPTIONS__TABLE_ID);

	buf = static_cast<byte*>(mem_heap_alloc(heap, 8));
	mach_write_to_8(buf, table->id);

	dfield_set_data(dfield, buf, 8);
	dict_index_copy_types(tuple, sys_index, 1);

	btr_pcur_open_on_user_rec(sys_index, tuple, PAGE_CUR_GE,
				  BTR_SEARCH_LEAF, &pcur, &mtr);

	rec = btr_pcur_get_rec(&pcur);

	/* If the file-per-table tablespace was created with
	an earlier version of InnoDB, then this record is not
	in SYS_TABLE_OPTIONS.*/

	if (btr_pcur_is_on_user_rec(&pcur)) {
		const char* msg = dict_process_sys_tableoptions(heap, rec, table->table_options);

		if (msg) {
			ib::error() << "Processing record in "
				    << "SYS_TABLEOPTIONS failed with error:" << msg;
		} else {
			table->table_options->is_stored = true;
			table->table_options->need_stored = true;
			found = true;
		}
	}

	btr_pcur_close(&pcur);
	mtr_commit(&mtr);

	return(found);
}

/********************************************************************//**
Insert record into SYS_TABLE_OPTIONS
@return	DB_SUCCESS if OK, dberr_t if the insert failed */
UNIV_INTERN
dberr_t
dict_insert_tableoptions(
/*=====================*/
	const dict_table_t*	table,	/*!< in: table object */
	bool			fixed,	/*!< in: is dict already
					fixed ? */
	trx_t*			trx, 	/*!< in: trx */
	bool			commit) /*!< in: true => commit
					transaction */
{
	dberr_t		err = DB_SUCCESS;
	mtr_t		mtr;
	pars_info_t*	info = NULL;

	if (!trx) {
		trx = trx_allocate_for_background();
		trx->dict_operation_lock_mode = RW_X_LATCH;
		trx_start_for_ddl(trx, TRX_DICT_OP_INDEX);
	}

	if (!fixed) {
		rw_lock_x_lock(dict_operation_lock);
		mutex_enter(&dict_sys->mutex);
	}

	mtr_start(&mtr);
	trx->op_info = "insert tableoptions";

	info = pars_info_create();
	pars_info_add_ull_literal(info, "tableid", table->id);
	pars_info_add_int4_literal(info, "pagecomp", (ulint)table->table_options->page_compressed);
	pars_info_add_int4_literal(info, "level", (ulint)table->table_options->page_compression_level);
	pars_info_add_int4_literal(info, "encrypt", (ulint)table->table_options->encryption);
	pars_info_add_int4_literal(info, "keyid", (ulint)table->table_options->encryption_key_id);
	pars_info_add_int4_literal(info, "is_shared", (ulint)table->table_options->is_shared);
	pars_info_add_int4_literal(info, "is_temporary", (ulint)table->table_options->is_temporary);

	err = que_eval_sql(info,
		"PROCEDURE INSERT_TABLEOPTIONS () IS\n"
		"BEGIN\n"
		"INSERT INTO SYS_TABLE_OPTIONS VALUES"
		"(:tableid, :pagecomp, :level, :encrypt, :keyid,"
		":is_shared, :is_temporary);\n"
		"END;\n",
		FALSE, trx);

	if (err != DB_SUCCESS && err != DB_DUPLICATE_KEY) {
		ib::warn()
			<< "Error:" << err << " inserting row to InnoDB data "
			<< "dictionary table SYS_TABLE_OPTIONS "
			<< "for table ID: " << table->id
			<< " name: " << table->name.m_name
			<< " page_compressed: " << table->table_options->page_compressed
			<< " compression_level: " << table->table_options->page_compression_level
			<< " encrypted: " << table->table_options->encryption
			<< " key_id: " << table->table_options->encryption_key_id
			<< " is_shared: " << table->table_options->is_shared
			<< " is_temporary: " << table->table_options->is_temporary;
	}

	table->table_options->is_stored = true;

	mtr_commit(&mtr);
	trx->op_info = "";

	if (commit) {
		trx_commit_for_mysql(trx);
		trx->dict_operation_lock_mode = 0;
		trx_free_for_background(trx);
	}

	if (!fixed) {
		mutex_exit(&dict_sys->mutex);
		rw_lock_x_unlock(dict_operation_lock);
	}

	return(err);
}

/********************************************************************//**
Update the table flags in SYS_TABLES.
@return	DB_SUCCESS if OK, dberr_t if the update failed */
UNIV_INTERN
dberr_t
dict_update_table_flags(
/*=====================*/
	dict_table_t*	    table,	/*!< in: table object */
	bool		    fixed)	/*!< in: can we fix the
					dictionary ? */
{
	dberr_t		err = DB_SUCCESS;
	trx_t*		trx;
	ulint		tmp_flags = 0;
	fil_space_t*	space = fil_space_found_by_id(table->space);

	if (space) {
		memcpy(space->table_options, table->table_options,
			sizeof(dict_tableoptions_t));
	}

	dict_tf_set(&tmp_flags,
		dict_tf_get_rec_format(table->flags),
		DICT_TF_GET_ZIP_SSIZE(table->flags),
		DICT_TF_HAS_DATA_DIR(table->flags),
		false);

	/* If dictionary flags match, no need to change them. */
	if (table->flags == tmp_flags &&
	    dict_tf_to_sys_tables_type(tmp_flags) ==
		dict_tf_to_sys_tables_type(table->flags)) {
			return (err);
	}

	trx = trx_allocate_for_background();
	trx->op_info = "update sys_tables options";
	trx->dict_operation_lock_mode = RW_X_LATCH;
	trx_start_for_ddl(trx, TRX_DICT_OP_INDEX);

	ulint type = dict_tf_to_sys_tables_type(tmp_flags);

	if (!fixed) {
		rw_lock_x_lock(dict_operation_lock);
		mutex_enter(&dict_sys->mutex);
	}

	pars_info_t*	info = pars_info_create();

	pars_info_add_ull_literal(info, "id", table->id);
	pars_info_add_int4_literal(info, "type", type);

	err = que_eval_sql(info,
			   "PROCEDURE UPDATE_TABLE_FLAGS () IS\n"
			   "BEGIN\n"
			   "UPDATE SYS_TABLES"
			   " SET TYPE = :type\n"
			   " WHERE ID = :id;\n"
			   "END;\n", FALSE, trx);

	trx_commit_for_mysql(trx);
	trx->dict_operation_lock_mode = 0;
	trx_free_for_background(trx);

	table->flags = tmp_flags;

	if (!fixed) {
		mutex_exit(&dict_sys->mutex);
		rw_lock_x_unlock(dict_operation_lock);
	}

	if (err == DB_SUCCESS) {
		/* We just updated SYS_TABLES due to the contents in
		tablespace or dictionary. Make note of that */
		ib::info()
			<< "The InnoDB data dictionary table SYS_TABLES "
			<< "for table ID: " << table->id
			<< " name: " << table->name.m_name
			<< " was updated to use "
			<< "flags: "<< type;
	} else {
		ib::warn()
			<< "Problem updating InnoDB data dictionary table "
			<< "SYS_TABLES for table ID: " << table->id
			<< " name: "<< table->name.m_name
			<< " err: " << err;
	}

	return(err);
}

/********************************************************************//**
Delete the record in SYS_TABLE_OPTIONS.
@return	DB_SUCCESS if OK, dberr_t if the update failed */
UNIV_INTERN
dberr_t
dict_delete_tableoptions(
/*=====================*/
	const dict_table_t*	table,	/*!< in: table object */
	trx_t*			trx,	/*!< in: trx */
	bool			fixed)	/*!< in: can we fix the
					dictionary ? */
{
	dberr_t		err = DB_SUCCESS;

	trx->op_info = "delete sys_table_options";
	trx_set_dict_operation(trx, TRX_DICT_OP_TABLE);

	if (!fixed) {
		row_mysql_lock_data_dictionary(trx);
	}

	if (table->table_options->is_stored) {

		pars_info_t*	info = pars_info_create();

		pars_info_add_ull_literal(info, "tableid", table->id);

		err = que_eval_sql(info,
			   "PROCEDURE DELETE_TABLE_OPTIONS () IS\n"
			   "BEGIN\n"
			   "DELETE FROM SYS_TABLE_OPTIONS"
			   " WHERE TABLE_ID = :tableid;\n"
			   "END;\n", FALSE, trx);

		if (err != DB_SUCCESS) {
			ib::warn()
				<< "Problem deleting InnoDB data dictionary table "
				<< "SYS_TABLE_OPTIONS for table ID: " << table->id
				<< " name: " << table->name.m_name
				<< " err: " << err;
		}
	}

	if (!fixed) {
		row_mysql_unlock_data_dictionary(trx);
	}

	return(err);
}
