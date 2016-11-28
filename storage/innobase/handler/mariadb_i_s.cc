/*****************************************************************************

Copyright (c) 2016, MariaDB Corporation. All Rights Reserved.

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
@file handler/mariadb_i_s.cc
MariaDB extended information schema tables

Created 18/11/2016 Jan Lindström
*******************************************************/

#include "ha_prototypes.h"
#include <mysql_version.h>
#include <field.h>
#include "univ.i"

#include <sql_acl.h>
#include <sql_show.h>
#include <sql_time.h>

#include "i_s.h"
#include "btr0pcur.h"
#include "btr0types.h"
#include "dict0dict.h"
#include "dict0load.h"
#include "buf0buddy.h"
#include "buf0buf.h"
#include "ibuf0ibuf.h"
#include "dict0mem.h"
#include "dict0types.h"
#include "srv0start.h"
#include "trx0i_s.h"
#include "trx0trx.h"
#include "srv0mon.h"
#include "fut0fut.h"
#include "pars0pars.h"
#include "fts0types.h"
#include "fts0opt.h"
#include "fts0priv.h"
#include "btr0btr.h"
#include "page0zip.h"
#include "sync0arr.h"
#include "fil0fil.h"
#include "fil0crypt.h"
#include "fsp0sysspace.h"
#include "ut0new.h"
#include "dict0crea.h"
#include <dict0tableoptions.h>

#define PLUGIN_AUTHOR "MariaDB Corporation."

/** Implemented on sync0arr.cc */
/*******************************************************************//**
Function to populate INFORMATION_SCHEMA.INNODB_SYS_SEMAPHORE_WAITS table.
Loop through each item on sync array, and extract the column
information and fill the INFORMATION_SCHEMA.INNODB_SYS_SEMAPHORE_WAITS table.
@return 0 on success */
UNIV_INTERN
int
sync_arr_fill_sys_semphore_waits_table(
/*===================================*/
	THD*		thd,	/*!< in: thread */
	TABLE_LIST*	tables,	/*!< in/out: tables to fill */
	Item*		);	/*!< in: condition (not used) */

extern struct st_mysql_information_schema	i_s_info;

/**  INNODB_MUTEXES  *********************************************/
/* Fields of the dynamic table INFORMATION_SCHEMA.INNODB_MUTEXES */
static ST_FIELD_INFO	innodb_mutexes_fields_info[] =
{
#define MUTEXES_NAME			0
	{STRUCT_FLD(field_name,		"NAME"),
	 STRUCT_FLD(field_length,	OS_FILE_MAX_PATH),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_STRING),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	0),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},
#define MUTEXES_CREATE_FILE		1
	{STRUCT_FLD(field_name,		"CREATE_FILE"),
	 STRUCT_FLD(field_length,	OS_FILE_MAX_PATH),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_STRING),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	0),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},
#define MUTEXES_CREATE_LINE		2
	{STRUCT_FLD(field_name,		"CREATE_LINE"),
	 STRUCT_FLD(field_length,	MY_INT32_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},
#define MUTEXES_OS_WAITS		3
	{STRUCT_FLD(field_name,		"OS_WAITS"),
	 STRUCT_FLD(field_length,	MY_INT64_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONGLONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	END_OF_ST_FIELD_INFO
};

/*******************************************************************//**
Function to populate INFORMATION_SCHEMA.INNODB_MUTEXES table.
Loop through each record in mutex and rw_lock lists, and extract the column
information and fill the INFORMATION_SCHEMA.INNODB_MUTEXES table.
@return 0 on success */
static
int
i_s_innodb_mutexes_fill_table(
/*==========================*/
	THD*		thd,	/*!< in: thread */
	TABLE_LIST*	tables,	/*!< in/out: tables to fill */
	Item*		)	/*!< in: condition (not used) */
{
	ib_mutex_t*	mutex;
	rw_lock_t*	lock;
	ulint		block_mutex_oswait_count = 0;
	ulint		block_lock_oswait_count = 0;
	ib_mutex_t*	block_mutex = NULL;
	rw_lock_t*	block_lock = NULL;
	Field**		fields = tables->table->field;

	DBUG_ENTER("i_s_innodb_mutexes_fill_table");
	RETURN_IF_INNODB_NOT_STARTED(tables->schema_table_name);

	/* deny access to user without PROCESS_ACL privilege */
	if (check_global_access(thd, PROCESS_ACL)) {
		DBUG_RETURN(0);
	}

	// mutex_enter(&mutex_list_mutex);

#ifdef JAN_TODO_FIXME
	for (mutex = UT_LIST_GET_FIRST(os_mutex_list); mutex != NULL;
	     mutex = UT_LIST_GET_NEXT(list, mutex)) {
		if (mutex->count_os_wait == 0) {
			continue;
		}

		if (buf_pool_is_block_mutex(mutex)) {
			block_mutex = mutex;
			block_mutex_oswait_count += mutex->count_os_wait;
			continue;
		}

		OK(field_store_string(fields[MUTEXES_NAME], mutex->cmutex_name));
		OK(field_store_string(fields[MUTEXES_CREATE_FILE], innobase_basename(mutex->cfile_name)));
		OK(field_store_ulint(fields[MUTEXES_CREATE_LINE], mutex->cline));
		OK(field_store_ulint(fields[MUTEXES_OS_WAITS], (longlong)mutex->count_os_wait));
		OK(schema_table_store_record(thd, tables->table));
	}

	if (block_mutex) {
		char buf1[IO_SIZE];

		my_snprintf(buf1, sizeof buf1, "combined %s",
			    innobase_basename(block_mutex->cfile_name));

		OK(field_store_string(fields[MUTEXES_NAME], block_mutex->cmutex_name));
		OK(field_store_string(fields[MUTEXES_CREATE_FILE], buf1));
		OK(field_store_ulint(fields[MUTEXES_CREATE_LINE], block_mutex->cline));
		OK(field_store_ulint(fields[MUTEXES_OS_WAITS], (longlong)block_mutex_oswait_count));
		OK(schema_table_store_record(thd, tables->table));
	}

	mutex_exit(&mutex_list_mutex);
#endif /* JAN_TODO_FIXME */

	mutex_enter(&rw_lock_list_mutex);

	for (lock = UT_LIST_GET_FIRST(rw_lock_list); lock != NULL;
	     lock = UT_LIST_GET_NEXT(list, lock)) {
		if (lock->count_os_wait == 0) {
			continue;
		}

		if (buf_pool_is_block_lock(lock)) {
			block_lock = lock;
			block_lock_oswait_count += lock->count_os_wait;
			continue;
		}

		//OK(field_store_string(fields[MUTEXES_NAME], lock->lock_name));
		OK(field_store_string(fields[MUTEXES_CREATE_FILE], innobase_basename(lock->cfile_name)));
		OK(field_store_ulint(fields[MUTEXES_CREATE_LINE], lock->cline));
		OK(field_store_ulint(fields[MUTEXES_OS_WAITS], (longlong)lock->count_os_wait));
		OK(schema_table_store_record(thd, tables->table));
	}

	if (block_lock) {
		char buf1[IO_SIZE];

		my_snprintf(buf1, sizeof buf1, "combined %s",
			    innobase_basename(block_lock->cfile_name));

		//OK(field_store_string(fields[MUTEXES_NAME], block_lock->lock_name));
		OK(field_store_string(fields[MUTEXES_CREATE_FILE], buf1));
		OK(field_store_ulint(fields[MUTEXES_CREATE_LINE], block_lock->cline));
		OK(field_store_ulint(fields[MUTEXES_OS_WAITS], (longlong)block_lock_oswait_count));
		OK(schema_table_store_record(thd, tables->table));
	}

	mutex_exit(&rw_lock_list_mutex);

	DBUG_RETURN(0);
}

/*******************************************************************//**
Bind the dynamic table INFORMATION_SCHEMA.INNODB_MUTEXES
@return 0 on success */
static
int
innodb_mutexes_init(
/*================*/
	void*	p)	/*!< in/out: table schema object */
{
	ST_SCHEMA_TABLE*	schema;

	DBUG_ENTER("innodb_mutexes_init");

	schema = (ST_SCHEMA_TABLE*) p;

	schema->fields_info = innodb_mutexes_fields_info;
	schema->fill_table = i_s_innodb_mutexes_fill_table;

	DBUG_RETURN(0);
}

UNIV_INTERN struct st_maria_plugin	i_s_innodb_mutexes =
{
	/* the plugin type (a MYSQL_XXX_PLUGIN value) */
	/* int */
	STRUCT_FLD(type, MYSQL_INFORMATION_SCHEMA_PLUGIN),

	/* pointer to type-specific plugin descriptor */
	/* void* */
	STRUCT_FLD(info, &i_s_info),

	/* plugin name */
	/* const char* */
	STRUCT_FLD(name, "INNODB_MUTEXES"),

	/* plugin author (for SHOW PLUGINS) */
	/* const char* */
	STRUCT_FLD(author, plugin_author),

	/* general descriptive text (for SHOW PLUGINS) */
	/* const char* */
	STRUCT_FLD(descr, "InnoDB SYS_DATAFILES"),

	/* the plugin license (PLUGIN_LICENSE_XXX) */
	/* int */
	STRUCT_FLD(license, PLUGIN_LICENSE_GPL),

	/* the function to invoke when plugin is loaded */
	/* int (*)(void*); */
	STRUCT_FLD(init, innodb_mutexes_init),

	/* the function to invoke when plugin is unloaded */
	/* int (*)(void*); */
	STRUCT_FLD(deinit, i_s_common_deinit),

	/* plugin version (for SHOW PLUGINS) */
	/* unsigned int */
	STRUCT_FLD(version, INNODB_VERSION_SHORT),

	/* struct st_mysql_show_var* */
	STRUCT_FLD(status_vars, NULL),

	/* struct st_mysql_sys_var** */
	STRUCT_FLD(system_vars, NULL),

        /* Maria extension */
	STRUCT_FLD(version_info, INNODB_VERSION_STR),
        STRUCT_FLD(maturity, MariaDB_PLUGIN_MATURITY_STABLE),
};

/**  SYS_SEMAPHORE_WAITS  ************************************************/
/* Fields of the dynamic table INFORMATION_SCHEMA.INNODB_SYS_SEMAPHORE_WAITS */
static ST_FIELD_INFO	innodb_sys_semaphore_waits_fields_info[] =
{
	// SYS_SEMAPHORE_WAITS_THREAD_ID	0
	{STRUCT_FLD(field_name,		"THREAD_ID"),
	 STRUCT_FLD(field_length,	MY_INT64_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONGLONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_OBJECT_NAME	1
	{STRUCT_FLD(field_name,		"OBJECT_NAME"),
	 STRUCT_FLD(field_length,	OS_FILE_MAX_PATH),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_STRING),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_MAYBE_NULL),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_FILE	2
	{STRUCT_FLD(field_name,		"FILE"),
	 STRUCT_FLD(field_length,	OS_FILE_MAX_PATH),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_STRING),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_MAYBE_NULL),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_LINE	3
	{STRUCT_FLD(field_name,		"LINE"),
	 STRUCT_FLD(field_length,	MY_INT32_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_WAIT_TIME	4
	{STRUCT_FLD(field_name,		"WAIT_TIME"),
	 STRUCT_FLD(field_length,	MY_INT64_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONGLONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_WAIT_OBJECT	5
	{STRUCT_FLD(field_name,		"WAIT_OBJECT"),
	 STRUCT_FLD(field_length,	MY_INT64_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONGLONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_WAIT_TYPE	6
	{STRUCT_FLD(field_name,		"WAIT_TYPE"),
	 STRUCT_FLD(field_length,	16),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_STRING),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_MAYBE_NULL),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_HOLDER_THREAD_ID	7
	{STRUCT_FLD(field_name,		"HOLDER_THREAD_ID"),
	 STRUCT_FLD(field_length,	MY_INT64_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONGLONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_HOLDER_FILE 8
	{STRUCT_FLD(field_name,		"HOLDER_FILE"),
	 STRUCT_FLD(field_length,	OS_FILE_MAX_PATH),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_STRING),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_MAYBE_NULL),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_HOLDER_LINE 9
	{STRUCT_FLD(field_name,		"HOLDER_LINE"),
	 STRUCT_FLD(field_length,	MY_INT32_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_CREATED_FILE 10
	{STRUCT_FLD(field_name,		"CREATED_FILE"),
	 STRUCT_FLD(field_length,	OS_FILE_MAX_PATH),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_STRING),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_MAYBE_NULL),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_CREATED_LINE 11
	{STRUCT_FLD(field_name,		"CREATED_LINE"),
	 STRUCT_FLD(field_length,	MY_INT32_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_WRITER_THREAD 12
	{STRUCT_FLD(field_name,		"WRITER_THREAD"),
	 STRUCT_FLD(field_length,	MY_INT64_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONGLONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_RESERVATION_MODE 13
	{STRUCT_FLD(field_name,		"RESERVATION_MODE"),
	 STRUCT_FLD(field_length,	16),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_STRING),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_MAYBE_NULL),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_READERS	14
	{STRUCT_FLD(field_name,		"READERS"),
	 STRUCT_FLD(field_length,	MY_INT32_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_WAITERS_FLAG 15
	{STRUCT_FLD(field_name,		"WAITERS_FLAG"),
	 STRUCT_FLD(field_length,	MY_INT64_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONGLONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_LOCK_WORD	16
	{STRUCT_FLD(field_name,		"LOCK_WORD"),
	 STRUCT_FLD(field_length,	MY_INT64_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONGLONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_LAST_READER_FILE 17
	{STRUCT_FLD(field_name,		"LAST_READER_FILE"),
	 STRUCT_FLD(field_length,	OS_FILE_MAX_PATH),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_STRING),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_MAYBE_NULL),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_LAST_READER_LINE 18
	{STRUCT_FLD(field_name,		"LAST_READER_LINE"),
	 STRUCT_FLD(field_length,	MY_INT32_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_LAST_WRITER_FILE 19
	{STRUCT_FLD(field_name,		"LAST_WRITER_FILE"),
	 STRUCT_FLD(field_length,	OS_FILE_MAX_PATH),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_STRING),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_MAYBE_NULL),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_LAST_WRITER_LINE 20
	{STRUCT_FLD(field_name,		"LAST_WRITER_LINE"),
	 STRUCT_FLD(field_length,	MY_INT32_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_SEMAPHORE_WAITS_OS_WAIT_COUNT 21
	{STRUCT_FLD(field_name,		"OS_WAIT_COUNT"),
	 STRUCT_FLD(field_length,	MY_INT32_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	END_OF_ST_FIELD_INFO
};

/*******************************************************************//**
Bind the dynamic table INFORMATION_SCHEMA.INNODB_SYS_SEMAPHORE_WAITS
@return 0 on success */
static
int
innodb_sys_semaphore_waits_init(
/*============================*/
	void*	p)	/*!< in/out: table schema object */
{
	ST_SCHEMA_TABLE*	schema;

	DBUG_ENTER("innodb_sys_semaphore_waits_init");

	schema = (ST_SCHEMA_TABLE*) p;

	schema->fields_info = innodb_sys_semaphore_waits_fields_info;
	schema->fill_table = sync_arr_fill_sys_semphore_waits_table;

	DBUG_RETURN(0);
}

UNIV_INTERN struct st_maria_plugin	i_s_innodb_sys_semaphore_waits =
{
	/* the plugin type (a MYSQL_XXX_PLUGIN value) */
	/* int */
	STRUCT_FLD(type, MYSQL_INFORMATION_SCHEMA_PLUGIN),

	/* pointer to type-specific plugin descriptor */
	/* void* */
	STRUCT_FLD(info, &i_s_info),

	/* plugin name */
	/* const char* */
	STRUCT_FLD(name, "INNODB_SYS_SEMAPHORE_WAITS"),

	/* plugin author (for SHOW PLUGINS) */
	/* const char* */
	STRUCT_FLD(author, maria_plugin_author),

	/* general descriptive text (for SHOW PLUGINS) */
	/* const char* */
	STRUCT_FLD(descr, "InnoDB SYS_SEMAPHORE_WAITS"),

	/* the plugin license (PLUGIN_LICENSE_XXX) */
	/* int */
	STRUCT_FLD(license, PLUGIN_LICENSE_GPL),

	/* the function to invoke when plugin is loaded */
	/* int (*)(void*); */
	STRUCT_FLD(init, innodb_sys_semaphore_waits_init),

	/* the function to invoke when plugin is unloaded */
	/* int (*)(void*); */
	STRUCT_FLD(deinit, i_s_common_deinit),

	/* plugin version (for SHOW PLUGINS) */
	/* unsigned int */
	STRUCT_FLD(version, INNODB_VERSION_SHORT),

	/* struct st_mysql_show_var* */
	STRUCT_FLD(status_vars, NULL),

	/* struct st_mysql_sys_var** */
	STRUCT_FLD(system_vars, NULL),

        /* Maria extension */
	STRUCT_FLD(version_info, INNODB_VERSION_STR),
        STRUCT_FLD(maturity, MariaDB_PLUGIN_MATURITY_STABLE),
};

/**  TABLESPACES_SCRUBBING    ********************************************/
/* Fields of the table INFORMATION_SCHEMA.INNODB_TABLESPACES_SCRUBBING */
static ST_FIELD_INFO	innodb_tablespaces_scrubbing_fields_info[] =
{
#define TABLESPACES_SCRUBBING_SPACE	0
	{STRUCT_FLD(field_name,		"SPACE"),
	 STRUCT_FLD(field_length,	MY_INT64_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONGLONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

#define TABLESPACES_SCRUBBING_NAME		1
	{STRUCT_FLD(field_name,		"NAME"),
	 STRUCT_FLD(field_length,	MAX_FULL_NAME_LEN + 1),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_STRING),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_MAYBE_NULL),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

#define TABLESPACES_SCRUBBING_COMPRESSED	2
	{STRUCT_FLD(field_name,		"COMPRESSED"),
	 STRUCT_FLD(field_length,	MY_INT32_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

#define TABLESPACES_SCRUBBING_LAST_SCRUB_COMPLETED	3
	{STRUCT_FLD(field_name,		"LAST_SCRUB_COMPLETED"),
	 STRUCT_FLD(field_length,	0),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_DATETIME),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_MAYBE_NULL),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

#define TABLESPACES_SCRUBBING_CURRENT_SCRUB_STARTED	4
	{STRUCT_FLD(field_name,		"CURRENT_SCRUB_STARTED"),
	 STRUCT_FLD(field_length,	0),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_DATETIME),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_MAYBE_NULL),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

#define TABLESPACES_SCRUBBING_CURRENT_SCRUB_ACTIVE_THREADS	5
	{STRUCT_FLD(field_name,		"CURRENT_SCRUB_ACTIVE_THREADS"),
	 STRUCT_FLD(field_length,	MY_INT32_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED | MY_I_S_MAYBE_NULL),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

#define TABLESPACES_SCRUBBING_CURRENT_SCRUB_PAGE_NUMBER	6
	{STRUCT_FLD(field_name,		"CURRENT_SCRUB_PAGE_NUMBER"),
	 STRUCT_FLD(field_length,	MY_INT64_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONGLONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

#define TABLESPACES_SCRUBBING_CURRENT_SCRUB_MAX_PAGE_NUMBER	7
	{STRUCT_FLD(field_name,		"CURRENT_SCRUB_MAX_PAGE_NUMBER"),
	 STRUCT_FLD(field_length,	MY_INT64_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONGLONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	END_OF_ST_FIELD_INFO
};

/**********************************************************************//**
Function to fill INFORMATION_SCHEMA.INNODB_TABLESPACES_SCRUBBING
with information collected by scanning SYS_TABLESPACES_SCRUBBING
table 
@return 0 on success */
static
int
i_s_dict_fill_tablespaces_scrubbing(
/*==========================*/
	THD*		thd,		/*!< in: thread */
	ulint		space,		/*!< in: space ID */
	const char*	name,		/*!< in: tablespace name */
	TABLE*		table_to_fill)	/*!< in/out: fill this table */
{
	Field**	fields;
        struct fil_space_scrub_status_t status;

	DBUG_ENTER("i_s_dict_fill_tablespaces_scrubbing");

	fields = table_to_fill->field;

	fil_space_get_scrub_status(space, &status);
	OK(fields[TABLESPACES_SCRUBBING_SPACE]->store(space));

	OK(field_store_string(fields[TABLESPACES_SCRUBBING_NAME],
			      name));

	OK(fields[TABLESPACES_SCRUBBING_COMPRESSED]->store(
		   status.compressed ? 1 : 0));

	if (status.last_scrub_completed == 0) {
		fields[TABLESPACES_SCRUBBING_LAST_SCRUB_COMPLETED]->set_null();
	} else {
		fields[TABLESPACES_SCRUBBING_LAST_SCRUB_COMPLETED]
			->set_notnull();
		OK(field_store_time_t(
			   fields[TABLESPACES_SCRUBBING_LAST_SCRUB_COMPLETED],
			   status.last_scrub_completed));
	}

	int field_numbers[] = {
		TABLESPACES_SCRUBBING_CURRENT_SCRUB_STARTED,
		TABLESPACES_SCRUBBING_CURRENT_SCRUB_ACTIVE_THREADS,
		TABLESPACES_SCRUBBING_CURRENT_SCRUB_PAGE_NUMBER,
		TABLESPACES_SCRUBBING_CURRENT_SCRUB_MAX_PAGE_NUMBER };
	if (status.scrubbing) {
		for (uint i = 0; i < array_elements(field_numbers); i++) {
			fields[field_numbers[i]]->set_notnull();
		}

		OK(field_store_time_t(
			   fields[TABLESPACES_SCRUBBING_CURRENT_SCRUB_STARTED],
			   status.current_scrub_started));
		OK(fields[TABLESPACES_SCRUBBING_CURRENT_SCRUB_ACTIVE_THREADS]
		   ->store(status.current_scrub_active_threads));
		OK(fields[TABLESPACES_SCRUBBING_CURRENT_SCRUB_PAGE_NUMBER]
		   ->store(status.current_scrub_page_number));
		OK(fields[TABLESPACES_SCRUBBING_CURRENT_SCRUB_MAX_PAGE_NUMBER]
		   ->store(status.current_scrub_max_page_number));
	} else {
		for (uint i = 0; i < array_elements(field_numbers); i++) {
			fields[field_numbers[i]]->set_null();
		}
	}
	OK(schema_table_store_record(thd, table_to_fill));

	DBUG_RETURN(0);
}

/*******************************************************************//**
Function to populate INFORMATION_SCHEMA.INNODB_TABLESPACES_SCRUBBING table.
Loop through each record in TABLESPACES_SCRUBBING, and extract the column
information and fill the INFORMATION_SCHEMA.INNODB_TABLESPACES_SCRUBBING table.
@return 0 on success */
static
int
i_s_tablespaces_scrubbing_fill_table(
/*=================================*/
	THD*		thd,	/*!< in: thread */
	TABLE_LIST*	tables,	/*!< in/out: tables to fill */
	Item*		)	/*!< in: condition (not used) */
{
	btr_pcur_t	pcur;
	const rec_t*	rec;
	mem_heap_t*	heap;
	mtr_t		mtr;
	bool		found_space_0 = false;

	DBUG_ENTER("i_s_tablespaces_scrubbing_fill_table");
	RETURN_IF_INNODB_NOT_STARTED(tables->schema_table_name);

	/* deny access to user without SUPER_ACL privilege */
	if (check_global_access(thd, SUPER_ACL)) {
		DBUG_RETURN(0);
	}

	heap = mem_heap_create(1000);
	mutex_enter(&dict_sys->mutex);
	mtr_start(&mtr);

	rec = dict_startscan_system(&pcur, &mtr, SYS_TABLESPACES);

	while (rec) {
		const char*	err_msg;
		ulint		space;
		const char*	name;
		ulint		flags;

		/* Extract necessary information from a SYS_TABLESPACES row */
		err_msg = dict_process_sys_tablespaces(
			heap, rec, &space, &name, &flags);

		mtr_commit(&mtr);
		mutex_exit(&dict_sys->mutex);

		if (space == 0) {
			found_space_0 = true;
		}

		if (!err_msg) {
			i_s_dict_fill_tablespaces_scrubbing(
				thd, space, name, tables->table);
		} else {
			push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
					    ER_CANT_FIND_SYSTEM_REC, "%s",
					    err_msg);
		}

		mem_heap_empty(heap);

		/* Get the next record */
		mutex_enter(&dict_sys->mutex);
		mtr_start(&mtr);
		rec = dict_getnext_system(&pcur, &mtr);
	}

	mtr_commit(&mtr);
	mutex_exit(&dict_sys->mutex);
	mem_heap_free(heap);

	if (found_space_0 == false) {
		/* space 0 does for what ever unknown reason not show up
		* in iteration above, add it manually */
		ulint		space = 0;
		const char*	name = NULL;
		i_s_dict_fill_tablespaces_scrubbing(
			thd, space, name, tables->table);
	}

	DBUG_RETURN(0);
}

/*******************************************************************//**
Bind the dynamic table INFORMATION_SCHEMA.INNODB_TABLESPACES_SCRUBBING
@return 0 on success */
static
int
innodb_tablespaces_scrubbing_init(
/*========================*/
	void*	p)	/*!< in/out: table schema object */
{
	ST_SCHEMA_TABLE*	schema;

	DBUG_ENTER("innodb_tablespaces_scrubbing_init");

	schema = (ST_SCHEMA_TABLE*) p;

	schema->fields_info = innodb_tablespaces_scrubbing_fields_info;
	schema->fill_table = i_s_tablespaces_scrubbing_fill_table;

	DBUG_RETURN(0);
}

UNIV_INTERN struct st_maria_plugin	i_s_innodb_tablespaces_scrubbing =
{
	/* the plugin type (a MYSQL_XXX_PLUGIN value) */
	/* int */
	STRUCT_FLD(type, MYSQL_INFORMATION_SCHEMA_PLUGIN),

	/* pointer to type-specific plugin descriptor */
	/* void* */
	STRUCT_FLD(info, &i_s_info),

	/* plugin name */
	/* const char* */
	STRUCT_FLD(name, "INNODB_TABLESPACES_SCRUBBING"),

	/* plugin author (for SHOW PLUGINS) */
	/* const char* */
	STRUCT_FLD(author, "Google Inc"),

	/* general descriptive text (for SHOW PLUGINS) */
	/* const char* */
	STRUCT_FLD(descr, "InnoDB TABLESPACES_SCRUBBING"),

	/* the plugin license (PLUGIN_LICENSE_XXX) */
	/* int */
	STRUCT_FLD(license, PLUGIN_LICENSE_BSD),

	/* the function to invoke when plugin is loaded */
	/* int (*)(void*); */
	STRUCT_FLD(init, innodb_tablespaces_scrubbing_init),

	/* the function to invoke when plugin is unloaded */
	/* int (*)(void*); */
	STRUCT_FLD(deinit, i_s_common_deinit),

	/* plugin version (for SHOW PLUGINS) */
	/* unsigned int */
	STRUCT_FLD(version, INNODB_VERSION_SHORT),

	/* struct st_mysql_show_var* */
	STRUCT_FLD(status_vars, NULL),

	/* struct st_mysql_sys_var** */
	STRUCT_FLD(system_vars, NULL),

	/* Maria extension */
	STRUCT_FLD(version_info, INNODB_VERSION_STR),
	STRUCT_FLD(maturity, MariaDB_PLUGIN_MATURITY_STABLE)
};

/**  TABLESPACES_ENCRYPTION    ********************************************/
/* Fields of the table INFORMATION_SCHEMA.INNODB_TABLESPACES_ENCRYPTION */
static ST_FIELD_INFO	innodb_tablespaces_encryption_fields_info[] =
{
#define TABLESPACES_ENCRYPTION_SPACE	0
	{STRUCT_FLD(field_name,		"SPACE"),
	 STRUCT_FLD(field_length,	MY_INT32_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

#define TABLESPACES_ENCRYPTION_NAME		1
	{STRUCT_FLD(field_name,		"NAME"),
	 STRUCT_FLD(field_length,	MAX_FULL_NAME_LEN + 1),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_STRING),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_MAYBE_NULL),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

#define TABLESPACES_ENCRYPTION_ENCRYPTION_SCHEME	2
	{STRUCT_FLD(field_name,		"ENCRYPTION_SCHEME"),
	 STRUCT_FLD(field_length,	MY_INT32_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

#define TABLESPACES_ENCRYPTION_KEYSERVER_REQUESTS	3
	{STRUCT_FLD(field_name,		"KEYSERVER_REQUESTS"),
	 STRUCT_FLD(field_length,	MY_INT32_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

#define TABLESPACES_ENCRYPTION_MIN_KEY_VERSION	4
	{STRUCT_FLD(field_name,		"MIN_KEY_VERSION"),
	 STRUCT_FLD(field_length,	MY_INT32_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

#define TABLESPACES_ENCRYPTION_CURRENT_KEY_VERSION	5
	{STRUCT_FLD(field_name,		"CURRENT_KEY_VERSION"),
	 STRUCT_FLD(field_length,	MY_INT32_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

#define TABLESPACES_ENCRYPTION_KEY_ROTATION_PAGE_NUMBER	6
	{STRUCT_FLD(field_name,		"KEY_ROTATION_PAGE_NUMBER"),
	 STRUCT_FLD(field_length,	MY_INT64_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONGLONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED | MY_I_S_MAYBE_NULL),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

#define TABLESPACES_ENCRYPTION_KEY_ROTATION_MAX_PAGE_NUMBER 7
	{STRUCT_FLD(field_name,		"KEY_ROTATION_MAX_PAGE_NUMBER"),
	 STRUCT_FLD(field_length,	MY_INT64_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONGLONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED | MY_I_S_MAYBE_NULL),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

#define TABLESPACES_ENCRYPTION_CURRENT_KEY_ID	8
	{STRUCT_FLD(field_name,		"CURRENT_KEY_ID"),
	 STRUCT_FLD(field_length,	MY_INT32_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	END_OF_ST_FIELD_INFO
};

/**********************************************************************//**
Function to fill INFORMATION_SCHEMA.INNODB_TABLESPACES_ENCRYPTION
with information collected by scanning SYS_TABLESPACES table and then use
fil_space()
@return 0 on success */
static
int
i_s_dict_fill_tablespaces_encryption(
/*==========================*/
	THD*		thd,		/*!< in: thread */
	ulint		space,		/*!< in: space ID */
	const char*	name,		/*!< in: tablespace name */
	TABLE*		table_to_fill)	/*!< in/out: fill this table */
{
	Field**	fields;
	struct fil_space_crypt_status_t status;

	DBUG_ENTER("i_s_dict_fill_tablespaces_encryption");

	fields = table_to_fill->field;

	fil_space_crypt_get_status(space, &status);
	OK(fields[TABLESPACES_ENCRYPTION_SPACE]->store(space));

	OK(field_store_string(fields[TABLESPACES_ENCRYPTION_NAME],
			      name));

	OK(fields[TABLESPACES_ENCRYPTION_ENCRYPTION_SCHEME]->store(
		   status.scheme));
	OK(fields[TABLESPACES_ENCRYPTION_KEYSERVER_REQUESTS]->store(
		   status.keyserver_requests));
	OK(fields[TABLESPACES_ENCRYPTION_MIN_KEY_VERSION]->store(
		   status.min_key_version));
	OK(fields[TABLESPACES_ENCRYPTION_CURRENT_KEY_VERSION]->store(
		   status.current_key_version));
	OK(fields[TABLESPACES_ENCRYPTION_CURRENT_KEY_ID]->store(
		   status.key_id));
	if (status.rotating) {
		fields[TABLESPACES_ENCRYPTION_KEY_ROTATION_PAGE_NUMBER]->set_notnull();
		OK(fields[TABLESPACES_ENCRYPTION_KEY_ROTATION_PAGE_NUMBER]->store(
			   status.rotate_next_page_number));
		fields[TABLESPACES_ENCRYPTION_KEY_ROTATION_MAX_PAGE_NUMBER]->set_notnull();
		OK(fields[TABLESPACES_ENCRYPTION_KEY_ROTATION_MAX_PAGE_NUMBER]->store(
			   status.rotate_max_page_number));
	} else {
		fields[TABLESPACES_ENCRYPTION_KEY_ROTATION_PAGE_NUMBER]
			->set_null();
		fields[TABLESPACES_ENCRYPTION_KEY_ROTATION_MAX_PAGE_NUMBER]
			->set_null();
	}
	OK(schema_table_store_record(thd, table_to_fill));

	DBUG_RETURN(0);
}
/*******************************************************************//**
Function to populate INFORMATION_SCHEMA.INNODB_TABLESPACES_ENCRYPTION table.
Loop through each record in TABLESPACES_ENCRYPTION, and extract the column
information and fill the INFORMATION_SCHEMA.INNODB_TABLESPACES_ENCRYPTION table.
@return 0 on success */
static
int
i_s_tablespaces_encryption_fill_table(
/*===========================*/
	THD*		thd,	/*!< in: thread */
	TABLE_LIST*	tables,	/*!< in/out: tables to fill */
	Item*		)	/*!< in: condition (not used) */
{
	btr_pcur_t	pcur;
	const rec_t*	rec;
	mem_heap_t*	heap;
	mtr_t		mtr;
	bool		found_space_0 = false;

	DBUG_ENTER("i_s_tablespaces_encryption_fill_table");
	RETURN_IF_INNODB_NOT_STARTED(tables->schema_table_name);

	/* deny access to user without PROCESS_ACL privilege */
	if (check_global_access(thd, SUPER_ACL)) {
		DBUG_RETURN(0);
	}

	heap = mem_heap_create(1000);
	mutex_enter(&dict_sys->mutex);
	mtr_start(&mtr);

	rec = dict_startscan_system(&pcur, &mtr, SYS_TABLESPACES);

	while (rec) {
		const char*	err_msg;
		ulint		space;
		const char*	name;
		ulint		flags;

		/* Extract necessary information from a SYS_TABLESPACES row */
		err_msg = dict_process_sys_tablespaces(
			heap, rec, &space, &name, &flags);

		mtr_commit(&mtr);
		mutex_exit(&dict_sys->mutex);

		if (space == 0) {
			found_space_0 = true;
		}

		if (!err_msg) {
			i_s_dict_fill_tablespaces_encryption(
				thd, space, name, tables->table);
		} else {
			push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
					    ER_CANT_FIND_SYSTEM_REC, "%s",
					    err_msg);
		}

		mem_heap_empty(heap);

		/* Get the next record */
		mutex_enter(&dict_sys->mutex);
		mtr_start(&mtr);
		rec = dict_getnext_system(&pcur, &mtr);
	}

	mtr_commit(&mtr);
	mutex_exit(&dict_sys->mutex);
	mem_heap_free(heap);

	if (found_space_0 == false) {
		/* space 0 does for what ever unknown reason not show up
		* in iteration above, add it manually */
		ulint		space = 0;
		const char*	name = NULL;
		i_s_dict_fill_tablespaces_encryption(
			thd, space, name, tables->table);
	}

	DBUG_RETURN(0);
}
/*******************************************************************//**
Bind the dynamic table INFORMATION_SCHEMA.INNODB_TABLESPACES_ENCRYPTION
@return 0 on success */
static
int
innodb_tablespaces_encryption_init(
/*========================*/
	void*	p)	/*!< in/out: table schema object */
{
	ST_SCHEMA_TABLE*	schema;

	DBUG_ENTER("innodb_tablespaces_encryption_init");

	schema = (ST_SCHEMA_TABLE*) p;

	schema->fields_info = innodb_tablespaces_encryption_fields_info;
	schema->fill_table = i_s_tablespaces_encryption_fill_table;

	DBUG_RETURN(0);
}

UNIV_INTERN struct st_maria_plugin	i_s_innodb_tablespaces_encryption =
{
	/* the plugin type (a MYSQL_XXX_PLUGIN value) */
	/* int */
	STRUCT_FLD(type, MYSQL_INFORMATION_SCHEMA_PLUGIN),

	/* pointer to type-specific plugin descriptor */
	/* void* */
	STRUCT_FLD(info, &i_s_info),

	/* plugin name */
	/* const char* */
	STRUCT_FLD(name, "INNODB_TABLESPACES_ENCRYPTION"),

	/* plugin author (for SHOW PLUGINS) */
	/* const char* */
	STRUCT_FLD(author, "Google Inc"),

	/* general descriptive text (for SHOW PLUGINS) */
	/* const char* */
	STRUCT_FLD(descr, "InnoDB TABLESPACES_ENCRYPTION"),

	/* the plugin license (PLUGIN_LICENSE_XXX) */
	/* int */
	STRUCT_FLD(license, PLUGIN_LICENSE_BSD),

	/* the function to invoke when plugin is loaded */
	/* int (*)(void*); */
	STRUCT_FLD(init, innodb_tablespaces_encryption_init),

	/* the function to invoke when plugin is unloaded */
	/* int (*)(void*); */
	STRUCT_FLD(deinit, i_s_common_deinit),

	/* plugin version (for SHOW PLUGINS) */
	/* unsigned int */
	STRUCT_FLD(version, INNODB_VERSION_SHORT),

	/* struct st_mysql_show_var* */
	STRUCT_FLD(status_vars, NULL),

	/* struct st_mysql_sys_var** */
	STRUCT_FLD(system_vars, NULL),

	/* Maria extension */
	STRUCT_FLD(version_info, INNODB_VERSION_STR),
	STRUCT_FLD(maturity, MariaDB_PLUGIN_MATURITY_STABLE)
};

/**  SYS_TABLE_OPTIONS  ************************************************/
/* Fields of the dynamic table INFORMATION_SCHEMA.INNODB_SYS_TABLE_OPTIONS */
static ST_FIELD_INFO	innodb_sys_tableoptions_fields_info[] =
{
	// SYS_TABLE_OPTIONS_TABLE_ID	0
	{STRUCT_FLD(field_name,		"TABLE_ID"),
	 STRUCT_FLD(field_length,	MY_INT64_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONGLONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_TABLE_OPTIONS_TABLE_NAME	1
	{STRUCT_FLD(field_name,		"TABLE_NAME"),
	 STRUCT_FLD(field_length,	192),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_STRING),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	0),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_TABLE_OPTIONS_PAGE_COMPRESSED 2
	{STRUCT_FLD(field_name,		"PAGE_COMPRESSED"),
	 STRUCT_FLD(field_length,	9),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_STRING),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	0),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_TABLE_OPTIONS_PAGE_COMPRESSION_LEVEL 3
	{STRUCT_FLD(field_name,		"PAGE_COMPRESSION_LEVEL"),
	 STRUCT_FLD(field_length,	MY_INT32_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_TABLE_OPTIONS_ENCRYPTED 4
	{STRUCT_FLD(field_name,		"ENCRYPTED"),
	 STRUCT_FLD(field_length,	9),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_STRING),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	0),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_TABLE_OPTIONS_ENCRYPTION_KEY_ID 5
	{STRUCT_FLD(field_name,		"ENCRYPTION_KEY_ID"),
	 STRUCT_FLD(field_length,	MY_INT32_NUM_DECIMAL_DIGITS),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_LONG),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	MY_I_S_UNSIGNED),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_TABLE_OPTIONS_IS_SHARED 6
	{STRUCT_FLD(field_name,		"IS_SHARED"),
	 STRUCT_FLD(field_length,	9),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_STRING),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	0),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_TABLE_OPTIONS_IS_TEMPORARY 7
	{STRUCT_FLD(field_name,		"IS_TEMPORARY"),
	 STRUCT_FLD(field_length,	9),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_STRING),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	0),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_TABLE_OPTIONS_ATOMIC_WRITES 8
	{STRUCT_FLD(field_name,		"ATOMIC_WRITES"),
	 STRUCT_FLD(field_length,	9),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_STRING),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	0),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	// SYS_TABLE_OPTIONS_PUNCH_HOLE 9
	{STRUCT_FLD(field_name,		"PUNCH_HOLE"),
	 STRUCT_FLD(field_length,	9),
	 STRUCT_FLD(field_type,		MYSQL_TYPE_STRING),
	 STRUCT_FLD(value,		0),
	 STRUCT_FLD(field_flags,	0),
	 STRUCT_FLD(old_name,		""),
	 STRUCT_FLD(open_method,	SKIP_OPEN_TABLE)},

	END_OF_ST_FIELD_INFO
};

/*******************************************************************//**
Function to go through each record in SYS_TABLE_OPTIONS table, and fill the
information_schema.innodb_sys_table_options table with related table information
@return 0 on success */
static
int
i_s_dict_fill_sys_table_options(
/*============================*/
	THD*		thd,	/*!< in: thread */
	TABLE_LIST*	tables,	/*!< in/out: tables to fill */
	Item*		)	/*!< in: condition (not used) */
{
	btr_pcur_t	pcur;
	const rec_t*	rec;
	mem_heap_t*	heap;
	mtr_t		mtr;

	DBUG_ENTER("i_s_sys_table_options_fill_table");
	RETURN_IF_INNODB_NOT_STARTED(tables->schema_table_name);

	/* deny access to user without PROCESS_ACL privilege */
	if (check_global_access(thd, PROCESS_ACL, true)) {
		DBUG_RETURN(0);
	}

	heap = mem_heap_create(1000);
	mutex_enter(&(dict_sys->mutex));
	mtr_start(&mtr);

	rec = dict_startscan_system(&pcur, &mtr, SYS_TABLE_OPTIONS);

	while (rec) {
		const char*	err_msg;
		dict_tableoptions_t options;

		/* Create and populate a dict_tableoptions_t structure with
		information from SYS_TABLE_OPTIONS row */
		memset(&options, 0, sizeof(dict_tableoptions_t));

		err_msg = dict_process_sys_tableoptions(
			heap, rec, &options);

		mtr_commit(&mtr);
		mutex_exit(&dict_sys->mutex);

		if (!err_msg) {
			Field**		fields = tables->table->field;

			OK(fields[SYS_TABLE_OPTIONS_TABLE_ID]->store((longlong) options.table_id));

			mutex_enter(&dict_sys->mutex);
			dict_table_t* table = dict_load_table_on_id(options.table_id, DICT_ERR_IGNORE_ALL);
			mutex_exit(&dict_sys->mutex);
			
			if (table) {
				OK(field_store_string(fields[SYS_TABLE_OPTIONS_TABLE_NAME],
						table->name.m_name));
			} else {
				OK(field_store_string(fields[SYS_TABLE_OPTIONS_TABLE_NAME],
						"NULL"));
			}

			OK(fields[SYS_TABLE_OPTIONS_PAGE_COMPRESSION_LEVEL]->store((ulint)options.page_compression_level));
			OK(fields[SYS_TABLE_OPTIONS_ENCRYPTION_KEY_ID]->store((ulint)options.encryption_key_id));

			if (options.page_compressed) {
				OK(field_store_string(fields[SYS_TABLE_OPTIONS_PAGE_COMPRESSED], "YES"));
			} else {
				OK(field_store_string(fields[SYS_TABLE_OPTIONS_PAGE_COMPRESSED], "NO"));
			}

			if (options.encryption == FIL_SPACE_ENCRYPTION_DEFAULT) {
				OK(field_store_string(fields[SYS_TABLE_OPTIONS_ENCRYPTED], "DEFAULT"));
			} else if (options.encryption == FIL_SPACE_ENCRYPTION_ON) {
				OK(field_store_string(fields[SYS_TABLE_OPTIONS_ENCRYPTED], "ON"));
			} else {
				OK(field_store_string(fields[SYS_TABLE_OPTIONS_ENCRYPTED], "OFF"));
			}

			/* Now we need tablespace */
			fil_space_t* space = NULL;
			dict_tableoptions_t* to = NULL;

			if (table) {
				space = fil_space_found_by_id(table->space);
			}

			if (space && space->table_options) {
				to = (dict_tableoptions_t*)space->table_options;
			}

			if (to && to->atomic_writes) {
				OK(field_store_string(fields[SYS_TABLE_OPTIONS_ATOMIC_WRITES], "YES"));
			} else {
				OK(field_store_string(fields[SYS_TABLE_OPTIONS_ATOMIC_WRITES], "OFF"));
			}

			if (to && to->punch_hole) {
				OK(field_store_string(fields[SYS_TABLE_OPTIONS_PUNCH_HOLE], "YES"));
			} else {
				OK(field_store_string(fields[SYS_TABLE_OPTIONS_PUNCH_HOLE], "OFF"));
			}

			if (to && to->is_shared) {
				OK(field_store_string(fields[SYS_TABLE_OPTIONS_IS_SHARED], "YES"));
			} else {
				OK(field_store_string(fields[SYS_TABLE_OPTIONS_IS_SHARED], "OFF"));
			}

			if (to && to->is_temporary) {
				OK(field_store_string(fields[SYS_TABLE_OPTIONS_IS_TEMPORARY], "YES"));
			} else {
				OK(field_store_string(fields[SYS_TABLE_OPTIONS_IS_TEMPORARY], "OFF"));
			}

			OK(schema_table_store_record(thd, tables->table));

		} else {
			push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
					    ER_CANT_FIND_SYSTEM_REC, "%s",
					    err_msg);
		}

		mem_heap_empty(heap);

		/* Get the next record */
		mutex_enter(&dict_sys->mutex);
		mtr_start(&mtr);
		rec = dict_getnext_system(&pcur, &mtr);
	}

	mtr_commit(&mtr);
	mutex_exit(&dict_sys->mutex);
	mem_heap_free(heap);

	DBUG_RETURN(0);
}
/*******************************************************************//**
Bind the dynamic table INFORMATION_SCHEMA.INNODB_SYS_TABLE_OPTIONS
@return 0 on success */
static
int
innodb_sys_table_options_init(
/*============================*/
	void*	p)	/*!< in/out: table schema object */
{
	ST_SCHEMA_TABLE*	schema;

	DBUG_ENTER("innodb_sys_table_options_init");

	schema = (ST_SCHEMA_TABLE*) p;

	schema->fields_info = innodb_sys_tableoptions_fields_info;
	schema->fill_table = i_s_dict_fill_sys_table_options;

	DBUG_RETURN(0);
}

UNIV_INTERN struct st_maria_plugin	i_s_innodb_sys_table_options =
{
	/* the plugin type (a MYSQL_XXX_PLUGIN value) */
	/* int */
	STRUCT_FLD(type, MYSQL_INFORMATION_SCHEMA_PLUGIN),

	/* pointer to type-specific plugin descriptor */
	/* void* */
	STRUCT_FLD(info, &i_s_info),

	/* plugin name */
	/* const char* */
	STRUCT_FLD(name, "INNODB_SYS_TABLE_OPTIONS"),

	/* plugin author (for SHOW PLUGINS) */
	/* const char* */
	STRUCT_FLD(author, maria_plugin_author),

	/* general descriptive text (for SHOW PLUGINS) */
	/* const char* */
	STRUCT_FLD(descr, "InnoDB SYS_TABLE_OPTIONS"),

	/* the plugin license (PLUGIN_LICENSE_XXX) */
	/* int */
	STRUCT_FLD(license, PLUGIN_LICENSE_GPL),

	/* the function to invoke when plugin is loaded */
	/* int (*)(void*); */
	STRUCT_FLD(init, innodb_sys_table_options_init),

	/* the function to invoke when plugin is unloaded */
	/* int (*)(void*); */
	STRUCT_FLD(deinit, i_s_common_deinit),

	/* plugin version (for SHOW PLUGINS) */
	/* unsigned int */
	STRUCT_FLD(version, INNODB_VERSION_SHORT),

	/* struct st_mysql_show_var* */
	STRUCT_FLD(status_vars, NULL),

	/* struct st_mysql_sys_var** */
	STRUCT_FLD(system_vars, NULL),

        /* Maria extension */
	STRUCT_FLD(version_info, INNODB_VERSION_STR),
        STRUCT_FLD(maturity, MariaDB_PLUGIN_MATURITY_BETA),
};
