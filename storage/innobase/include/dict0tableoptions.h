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
@file include/dict0tableoptions.h
Definitions for the system table SYS_TABLE_OPTIONS

Created 22/01/2006 Jan Lindström
*******************************************************/

#ifndef dict0tableoptions_h
#define dict0tableoptions_h

#include "univ.i"
#include "fil0crypt.h"

/** Data structure to hold contents of SYS_TABLE_OPTIONS */
struct dict_tableoptions_t{
	/* Table id, primary key */
	table_id_t		table_id;
	/* true if table is page compressed */
	bool			page_compressed;
	/* Used compression level if set */
	ulonglong		page_compression_level;
	/* fil0crypt.h: FIL_SPACE_ENCRYPTION_DEFAULT, _ON, or _OFF */
	fil_encryption_t	encryption;
	/* Used encryption key identifier if set */
	ulonglong		encryption_key_id;
	/* Tablespace can use atomic writes. Note that this field
	is not persistent. */
	bool			atomic_writes;
	/* Has atomic writes tried on this tablespace ? */
	bool			atomic_writes_checked;
	/* Tablespace can use punch holes (trim). Note that this field
	is not persistent. */
	bool			punch_hole;
	/* Has punch hole operation checked for this tablespace ? */
	bool			punch_hole_checked;
	/* Shared tablespace */
	bool			is_shared;
	/* Temporal tablespace */
	bool			is_temporary;
	/* Tableoptions are stored. Note that this field
	is not persistent. */
	bool			is_stored;
	/* Does table options need to be stored */
	bool			need_stored;

#ifdef UNIV_DEBUG
	uint32_t		magic_n;
#define DICT_TABLEOPTIONS_MAGIC_N 34278719
#endif
};

/* The columns in SYS_TABLE_OPTIONS */
enum dict_col_sys_tableoptions_enum {
	DICT_COL__SYS_TABLEOPTIONS__TABLE_ID		= 0,
	DICT_COL__SYS_TABLEOPTIONS__PAGE_COMPRESSED	= 1,
	DICT_COL__SYS_TABLEOPTIONS__PAGE_COMPRESSION_LEVEL		= 2,
	DICT_COL__SYS_TABLEOPTIONS__ENCRYPTED		= 3,
	DICT_COL__SYS_TABLEOPTIONS__ENCRYPTION_KEY_ID	= 4,
	DICT_COL__SYS_TABLEOPTIONS__IS_SHARED		= 5,
	DICT_COL__SYS_TABLEOPTIONS__IS_TEMPORARY	= 6,
	DICT_NUM_COLS__SYS_TABLEOPTIONS			= 7
};

/* The field numbers in the SYS_TABLE_OPTIONS clustered index */
enum dict_fld_sys_tableoptions_enum {
	DICT_FLD__SYS_TABLEOPTIONS__TABLE_ID		= 0,
	DICT_FLD__SYS_TABLEOPTIONS__DB_TRX_ID		= 1,
	DICT_FLD__SYS_TABLEOPTIONS__DB_ROLL_PTR		= 2,
	DICT_FLD__SYS_TABLEOPTIONS__PAGE_COMPRESSED	= 3,
	DICT_FLD__SYS_TABLEOPTIONS__PAGE_COMPRESSION_LEVEL		= 4,
	DICT_FLD__SYS_TABLEOPTIONS__ENCRYPTED		= 5,
	DICT_FLD__SYS_TABLEOPTIONS__ENCRYPTION_KEY_ID	= 6,
	DICT_FLD__SYS_TABLEOPTIONS__IS_SHARED		= 7,
	DICT_FLD__SYS_TABLEOPTIONS__IS_TEMPORARY	= 8,
	DICT_NUM_FIELDS__SYS_TABLEOPTIONS		= 9
};

/** Fields on INFORMATION_SCHEMA.SYS_TABLE_OPTIONS table.
Note that not all of these are stored persistently to
SYS_TABLE_OPTIONS */
#define SYS_TABLE_OPTIONS_TABLE_ID			0
#define SYS_TABLE_OPTIONS_TABLE_NAME			1
#define SYS_TABLE_OPTIONS_PAGE_COMPRESSED		2
#define SYS_TABLE_OPTIONS_PAGE_COMPRESSION_LEVEL	3
#define SYS_TABLE_OPTIONS_ENCRYPTED			4
#define SYS_TABLE_OPTIONS_ENCRYPTION_KEY_ID		5
#define SYS_TABLE_OPTIONS_IS_SHARED			6
#define SYS_TABLE_OPTIONS_IS_TEMPORARY			7
#define SYS_TABLE_OPTIONS_ATOMIC_WRITES			8
#define SYS_TABLE_OPTIONS_PUNCH_HOLE			9

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
	MY_ATTRIBUTE((warn_unused_result));

/********************************************************************//**
Load the table options from SYS_TABLE_OPTIONS based on table_id
@return true if found, false if not found */
UNIV_INTERN
bool
dict_load_table_options(
/*===================*/
	dict_table_t*	table,
	mem_heap_t*	heap)
	MY_ATTRIBUTE((warn_unused_result));

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
	MY_ATTRIBUTE((warn_unused_result));

/********************************************************************//**
Update the table flags in SYS_TABLES.
@return	DB_SUCCESS if OK, dberr_t if the update failed */
UNIV_INTERN
dberr_t
dict_update_table_flags(
/*=====================*/
	dict_table_t*	table,	/*!< in: table object */
	bool		fixed)	/*!< in: can we fix the
				dictionary ? */
	MY_ATTRIBUTE((warn_unused_result));

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
	MY_ATTRIBUTE((warn_unused_result));

#endif /* dict0tableoptions_h */

