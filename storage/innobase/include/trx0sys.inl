/*****************************************************************************

Copyright (c) 1996, 2015, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2018, 2021, MariaDB Corporation.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA

*****************************************************************************/

/**************************************************//**
@file include/trx0sys.ic
Transaction system

Created 3/26/1996 Heikki Tuuri
*******************************************************/

#include "trx0trx.h"
#include "data0type.h"
#include "srv0srv.h"
#include "mtr0log.h"

/* The typedef for rseg slot in the file copy */
typedef byte	trx_sysf_rseg_t;

/* Rollback segment specification slot offsets */
/*-------------------------------------------------------------*/
#define	TRX_SYS_RSEG_SPACE	0	/* space where the segment
					header is placed; starting with
					MySQL/InnoDB 5.1.7, this is
					UNIV_UNDEFINED if the slot is unused */
#define	TRX_SYS_RSEG_PAGE_NO	4	/*  page number where the segment
					header is placed; this is FIL_NULL
					if the slot is unused */
/*-------------------------------------------------------------*/
/* Size of a rollback segment specification slot */
#define TRX_SYS_RSEG_SLOT_SIZE	8

/*****************************************************************//**
Writes the value of max_trx_id to the file based trx system header. */
void
trx_sys_flush_max_trx_id(void);
/*==========================*/

/** Checks if a page address is the trx sys header page.
@param[in]	page_id	page id
@return true if trx sys header page */
inline bool trx_sys_hdr_page(const page_id_t page_id)
{
	return(page_id.space() == TRX_SYS_SPACE
	       && page_id.page_no() == TRX_SYS_PAGE_NO);
}

/**********************************************************************//**
Gets a pointer to the transaction system header and x-latches its page.
@return pointer to system header, page x-latched. */
UNIV_INLINE
trx_sysf_t*
trx_sysf_get(
/*=========*/
	mtr_t*	mtr)	/*!< in: mtr */
{
	buf_block_t*	block = NULL;
	trx_sysf_t*	header = NULL;

	ut_ad(mtr);

	block = buf_page_get(page_id_t(TRX_SYS_SPACE, TRX_SYS_PAGE_NO),
			     univ_page_size, RW_X_LATCH, mtr);

	if (block) {
		buf_block_dbg_add_level(block, SYNC_TRX_SYS_HEADER);

		header = TRX_SYS + buf_block_get_frame(block);
	}

	return(header);
}

/*****************************************************************//**
Gets the space of the nth rollback segment slot in the trx system
file copy.
@return space id */
UNIV_INLINE
ulint
trx_sysf_rseg_get_space(
/*====================*/
	trx_sysf_t*	sys_header,	/*!< in: trx sys header */
	ulint		i,		/*!< in: slot index == rseg id */
	mtr_t*		mtr)		/*!< in: mtr */
{
	ut_ad(sys_header);
	ut_ad(i < TRX_SYS_N_RSEGS);

	return(mtr_read_ulint(sys_header + TRX_SYS_RSEGS
			      + i * TRX_SYS_RSEG_SLOT_SIZE
			      + TRX_SYS_RSEG_SPACE, MLOG_4BYTES, mtr));
}

/*****************************************************************//**
Gets the page number of the nth rollback segment slot in the trx system
header.
@return page number, FIL_NULL if slot unused */
UNIV_INLINE
ulint
trx_sysf_rseg_get_page_no(
/*======================*/
	trx_sysf_t*	sys_header,	/*!< in: trx system header */
	ulint		i,		/*!< in: slot index == rseg id */
	mtr_t*		mtr)		/*!< in: mtr */
{
	ut_ad(sys_header);
	ut_ad(i < TRX_SYS_N_RSEGS);

	return(mtr_read_ulint(sys_header + TRX_SYS_RSEGS
			      + i * TRX_SYS_RSEG_SLOT_SIZE
			      + TRX_SYS_RSEG_PAGE_NO, MLOG_4BYTES, mtr));
}

/*****************************************************************//**
Sets the space id of the nth rollback segment slot in the trx system
file copy. */
UNIV_INLINE
void
trx_sysf_rseg_set_space(
/*====================*/
	trx_sysf_t*	sys_header,	/*!< in: trx sys file copy */
	ulint		i,		/*!< in: slot index == rseg id */
	ulint		space,		/*!< in: space id */
	mtr_t*		mtr)		/*!< in: mtr */
{
	ut_ad(sys_header);
	ut_ad(i < TRX_SYS_N_RSEGS);

	mlog_write_ulint(sys_header + TRX_SYS_RSEGS
			 + i * TRX_SYS_RSEG_SLOT_SIZE
			 + TRX_SYS_RSEG_SPACE,
			 space,
			 MLOG_4BYTES, mtr);
}

/*****************************************************************//**
Sets the page number of the nth rollback segment slot in the trx system
header. */
UNIV_INLINE
void
trx_sysf_rseg_set_page_no(
/*======================*/
	trx_sysf_t*	sys_header,	/*!< in: trx sys header */
	ulint		i,		/*!< in: slot index == rseg id */
	ulint		page_no,	/*!< in: page number, FIL_NULL if the
					slot is reset to unused */
	mtr_t*		mtr)		/*!< in: mtr */
{
	ut_ad(sys_header);
	ut_ad(i < TRX_SYS_N_RSEGS);

	mlog_write_ulint(sys_header + TRX_SYS_RSEGS
			 + i * TRX_SYS_RSEG_SLOT_SIZE
			 + TRX_SYS_RSEG_PAGE_NO,
			 page_no,
			 MLOG_4BYTES, mtr);
}

/*****************************************************************//**
Writes a trx id to an index page. In case that the id size changes in
some future version, this function should be used instead of
mach_write_... */
UNIV_INLINE
void
trx_write_trx_id(
/*=============*/
	byte*		ptr,	/*!< in: pointer to memory where written */
	trx_id_t	id)	/*!< in: id */
{
#if DATA_TRX_ID_LEN != 6
# error "DATA_TRX_ID_LEN != 6"
#endif
	mach_write_to_6(ptr, id);
}

/*****************************************************************//**
Reads a trx id from an index page. In case that the id size changes in
some future version, this function should be used instead of
mach_read_...
@return id */
UNIV_INLINE
trx_id_t
trx_read_trx_id(
/*============*/
	const byte*	ptr)	/*!< in: pointer to memory from where to read */
{
#if DATA_TRX_ID_LEN != 6
# error "DATA_TRX_ID_LEN != 6"
#endif
	return(mach_read_from_6(ptr));
}

/****************************************************************//**
Looks for the trx handle with the given id in rw_trx_list.
The caller must be holding trx_sys->mutex.
@return the trx handle or NULL if not found;
the pointer must not be dereferenced unless lock_sys->mutex was
acquired before calling this function and is still being held */
UNIV_INLINE
trx_t*
trx_get_rw_trx_by_id(
/*=================*/
	trx_id_t	trx_id)	/*!< in: trx id to search for */
{
	ut_ad(trx_id > 0);
	ut_ad(trx_sys_mutex_own());

	if (trx_sys->rw_trx_set.empty()) {
		return(NULL);
	}

	TrxIdSet::iterator	it;

	it = trx_sys->rw_trx_set.find(TrxTrack(trx_id));

	return(it == trx_sys->rw_trx_set.end() ? NULL : it->m_trx);
}

/****************************************************************//**
Returns the minimum trx id in trx list. This is the smallest id for which
the trx can possibly be active. (But, you must look at the trx->state
to find out if the minimum trx id transaction itself is active, or already
committed.). The caller must be holding the trx_sys_t::mutex in shared mode.
@return the minimum trx id, or trx_sys->max_trx_id if the trx list is empty */
UNIV_INLINE
trx_id_t
trx_rw_min_trx_id_low(void)
/*=======================*/
{
	trx_id_t	id;

	ut_ad(trx_sys_mutex_own());

	const trx_t*	trx = UT_LIST_GET_LAST(trx_sys->rw_trx_list);

	if (trx == NULL) {
		id = trx_sys->max_trx_id;
	} else {
		ut_ad(!trx->read_only);
		ut_ad(trx->in_rw_trx_list);
		ut_ad(!trx->is_autocommit_non_locking());
		id = trx->id;
	}

	return(id);
}

#if defined UNIV_DEBUG || defined UNIV_BLOB_LIGHT_DEBUG
/***********************************************************//**
Assert that a transaction has been recovered.
@return TRUE */
UNIV_INLINE
ibool
trx_assert_recovered(
/*=================*/
	trx_id_t	trx_id)		/*!< in: transaction identifier */
{
	const trx_t*	trx;

	trx_sys_mutex_enter();

	trx = trx_get_rw_trx_by_id(trx_id);
	ut_a(trx->is_recovered);

	trx_sys_mutex_exit();

	return(TRUE);
}
#endif /* UNIV_DEBUG || UNIV_BLOB_LIGHT_DEBUG */

/****************************************************************//**
Returns the minimum trx id in rw trx list. This is the smallest id for which
the rw trx can possibly be active. (But, you must look at the trx->state
to find out if the minimum trx id transaction itself is active, or already
committed.)
@return the minimum trx id, or trx_sys->max_trx_id if rw trx list is empty */
UNIV_INLINE
trx_id_t
trx_rw_min_trx_id(void)
/*===================*/
{
	trx_sys_mutex_enter();

	trx_id_t	id = trx_rw_min_trx_id_low();

	trx_sys_mutex_exit();

	return(id);
}

/** Look up a rw transaction with the given id.
@param[in]	trx_id		transaction identifier
@param[out]	corrupt		flag that will be set if trx_id is corrupted
@return transaction; its state should be rechecked after acquiring trx_t::mutex
@retval NULL if there is no transaction identified by trx_id. */
inline trx_t* trx_rw_is_active_low(trx_id_t trx_id, bool* corrupt)
{
	ut_ad(trx_sys_mutex_own());

	if (trx_id < trx_rw_min_trx_id_low()) {
	} else if (trx_id >= trx_sys->max_trx_id) {

		/* There must be corruption: we let the caller handle the
		diagnostic prints in this case. */

		if (corrupt != NULL) {
			*corrupt = true;
		}
	} else if (trx_t* trx = trx_get_rw_trx_by_id(trx_id)) {
		return trx;
	}

	return NULL;
}

/** Look up a rw transaction with the given id.
@param[in]	trx_id		transaction identifier
@param[out]	corrupt		flag that will be set if trx_id is corrupted
@param[in]	ref_count	whether to increment trx->n_ref
@return transaction; its state should be rechecked after acquiring trx_t::mutex
@retval NULL if there is no active transaction identified by trx_id. */
inline trx_t* trx_rw_is_active(trx_id_t trx_id, bool* corrupt, bool ref_count)
{
	ut_ad(trx_id);

	trx_sys_mutex_enter();

	trx_t* trx = trx_rw_is_active_low(trx_id, corrupt);

	if (trx && ref_count) {
		TrxMutex* trx_mutex = &trx->mutex;
		mutex_enter(trx_mutex);
		ut_ad(!trx_state_eq(trx, TRX_STATE_NOT_STARTED));
		ut_ad(trx->id == trx_id);
		if (trx_state_eq(trx, TRX_STATE_COMMITTED_IN_MEMORY)) {
			/* We have an early state check here to avoid
			committer starvation in a wait loop for
			transaction references, when there's a stream of
			trx_rw_is_active() calls from other threads.
			The trx->state may change to COMMITTED after
			trx_mutex is released, and it will have to be
			rechecked by the caller after reacquiring the mutex. */
			trx = NULL;
		} else {
			/* The reference could be safely incremented after
			releasing one of trx_mutex or trx_sys->mutex.
			Holding trx->mutex here may prevent a few false
			references that could have a negative performance
			impact on trx_commit_in_memory(). */
			trx->reference();
		}
		mutex_exit(trx_mutex);
	}

	trx_sys_mutex_exit();

	return(trx);
}

/*****************************************************************//**
Allocates a new transaction id.
@return new, allocated trx id */
UNIV_INLINE
trx_id_t
trx_sys_get_new_trx_id()
/*====================*/
{
	/* wsrep_fake_trx_id  violates this assert */
	ut_ad(trx_sys_mutex_own());

	/* VERY important: after the database is started, max_trx_id value is
	divisible by TRX_SYS_TRX_ID_WRITE_MARGIN, and the following if
	will evaluate to TRUE when this function is first time called,
	and the value for trx id will be written to disk-based header!
	Thus trx id values will not overlap when the database is
	repeatedly started! */

	if (!(trx_sys->max_trx_id % TRX_SYS_TRX_ID_WRITE_MARGIN)) {

		trx_sys_flush_max_trx_id();
	}

	return(trx_sys->max_trx_id++);
}

/*****************************************************************//**
Determines the maximum transaction id.
@return maximum currently allocated trx id; will be stale after the
next call to trx_sys_get_new_trx_id() */
UNIV_INLINE
trx_id_t
trx_sys_get_max_trx_id(void)
/*========================*/
{
	ut_ad(!trx_sys_mutex_own());

#if UNIV_WORD_SIZE < DATA_TRX_ID_LEN
	/* Avoid torn reads. */

	trx_sys_mutex_enter();

	trx_id_t	max_trx_id = trx_sys->max_trx_id;

	trx_sys_mutex_exit();

	return(max_trx_id);
#else
	/* Perform a dirty read. Callers should be prepared for stale
	values, and we know that the value fits in a machine word, so
	that it will be read and written atomically. */
	return(trx_sys->max_trx_id);
#endif /* UNIV_WORD_SIZE < DATA_TRX_ID_LEN */
}

/*****************************************************************//**
Get the number of transaction in the system, independent of their state.
@return count of transactions in trx_sys_t::rw_trx_list */
UNIV_INLINE
ulint
trx_sys_get_n_rw_trx(void)
/*======================*/
{
	ulint	n_trx;

	trx_sys_mutex_enter();

	n_trx = UT_LIST_GET_LEN(trx_sys->rw_trx_list);

	trx_sys_mutex_exit();

	return(n_trx);
}

/**
Add the transaction to the RW transaction set
@param trx		transaction instance to add */
UNIV_INLINE
void
trx_sys_rw_trx_add(trx_t* trx)
{
	ut_ad(trx->id != 0);

	trx_sys->rw_trx_set.insert(TrxTrack(trx->id, trx));
	ut_d(trx->in_rw_trx_list = true);
}
