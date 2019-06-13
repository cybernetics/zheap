/*-------------------------------------------------------------------------
 *
 * transam.h
 *	  postgres transaction access method support code
 *
 *
 * Portions Copyright (c) 1996-2019, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/transam.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef TRANSAM_H
#define TRANSAM_H

#include "access/xlogdefs.h"


/* ----------------
 *		Special transaction ID values
 *
 * BootstrapTransactionId is the XID for "bootstrap" operations, and
 * FrozenTransactionId is used for very old tuples.  Both should
 * always be considered valid.
 *
 * FirstNormalTransactionId is the first "normal" transaction id.
 * Note: if you need to change it, you must change pg_class.h as well.
 * ----------------
 */
#define InvalidTransactionId		((TransactionId) 0)
#define BootstrapTransactionId		((TransactionId) 1)
#define FrozenTransactionId			((TransactionId) 2)
#define FirstNormalTransactionId	((TransactionId) 3)
#define MaxTransactionId			((TransactionId) 0xFFFFFFFF)

/* ----------------
 *		transaction ID manipulation macros
 * ----------------
 */
#define TransactionIdIsValid(xid)		((xid) != InvalidTransactionId)
#define TransactionIdIsNormal(xid)		((xid) >= FirstNormalTransactionId)
#define TransactionIdEquals(id1, id2)	((id1) == (id2))
#define TransactionIdStore(xid, dest)	(*(dest) = (xid))
#define StoreInvalidTransactionId(dest) (*(dest) = InvalidTransactionId)

#define EpochFromFullTransactionId(x)	((uint32) ((x).value >> 32))
#define XidFromFullTransactionId(x)		((uint32) (x).value)
#define U64FromFullTransactionId(x)		((x).value)
#define FullTransactionIdEquals(a, b)	((a).value == (b).value)
#define FullTransactionIdPrecedes(a, b)	((a).value < (b).value)
#define FullTransactionIdFollows(a, b)	((a).value > (b).value)
#define FullTransactionIdIsValid(x)		TransactionIdIsValid(XidFromFullTransactionId(x))
#define InvalidFullTransactionId		FullTransactionIdFromEpochAndXid(0, InvalidTransactionId)

/*
 * A 64 bit value that contains an epoch and a TransactionId.  This is
 * wrapped in a struct to prevent implicit conversion to/from TransactionId.
 * Not all values represent valid normal XIDs.
 */
typedef struct FullTransactionId
{
	uint64		value;
} FullTransactionId;

static inline FullTransactionId
FullTransactionIdFromEpochAndXid(uint32 epoch, TransactionId xid)
{
	FullTransactionId result;

	result.value = ((uint64) epoch) << 32 | xid;

	return result;
}

static inline FullTransactionId
FullTransactionIdFromU64(uint64 fxid)
{
	FullTransactionId result;

	result.value = fxid;

	return result;
}

/* advance a transaction ID variable, handling wraparound correctly */
#define TransactionIdAdvance(dest)	\
	do { \
		(dest)++; \
		if ((dest) < FirstNormalTransactionId) \
			(dest) = FirstNormalTransactionId; \
	} while(0)

/* advance a FullTransactionId variable, stepping over special XIDs */
static inline void
FullTransactionIdAdvance(FullTransactionId *dest)
{
	dest->value++;
	while (XidFromFullTransactionId(*dest) < FirstNormalTransactionId)
		dest->value++;
}

/* back up a transaction ID variable, handling wraparound correctly */
#define TransactionIdRetreat(dest)	\
	do { \
		(dest)--; \
	} while ((dest) < FirstNormalTransactionId)

/* compare two XIDs already known to be normal; this is a macro for speed */
#define NormalTransactionIdPrecedes(id1, id2) \
	(AssertMacro(TransactionIdIsNormal(id1) && TransactionIdIsNormal(id2)), \
	(int32) ((id1) - (id2)) < 0)

/* compare two XIDs already known to be normal; this is a macro for speed */
#define NormalTransactionIdFollows(id1, id2) \
	(AssertMacro(TransactionIdIsNormal(id1) && TransactionIdIsNormal(id2)), \
	(int32) ((id1) - (id2)) > 0)

/* ----------
 *		Object ID (OID) zero is InvalidOid.
 *
 *		OIDs 1-9999 are reserved for manual assignment (see .dat files in
 *		src/include/catalog/).  Of these, 8000-9999 are reserved for
 *		development purposes (such as in-progress patches and forks);
 *		they should not appear in released versions.
 *
 *		OIDs 10000-11999 are reserved for assignment by genbki.pl, for use
 *		when the .dat files in src/include/catalog/ do not specify an OID
 *		for a catalog entry that requires one.
 *
 *		OIDS 12000-16383 are reserved for assignment during initdb
 *		using the OID generator.  (We start the generator at 12000.)
 *
 *		OIDs beginning at 16384 are assigned from the OID generator
 *		during normal multiuser operation.  (We force the generator up to
 *		16384 as soon as we are in normal operation.)
 *
 * The choices of 8000, 10000 and 12000 are completely arbitrary, and can be
 * moved if we run low on OIDs in any category.  Changing the macros below,
 * and updating relevant documentation (see bki.sgml and RELEASE_CHANGES),
 * should be sufficient to do this.  Moving the 16384 boundary between
 * initdb-assigned OIDs and user-defined objects would be substantially
 * more painful, however, since some user-defined OIDs will appear in
 * on-disk data; such a change would probably break pg_upgrade.
 *
 * NOTE: if the OID generator wraps around, we skip over OIDs 0-16383
 * and resume with 16384.  This minimizes the odds of OID conflict, by not
 * reassigning OIDs that might have been assigned during initdb.
 * ----------
 */
#define FirstGenbkiObjectId		10000
#define FirstBootstrapObjectId	12000
#define FirstNormalObjectId		16384

/*
 * VariableCache is a data structure in shared memory that is used to track
 * OID and XID assignment state.  For largely historical reasons, there is
 * just one struct with different fields that are protected by different
 * LWLocks.
 *
 * Note: xidWrapLimit and oldestXidDB are not "active" values, but are
 * used just to generate useful messages when xidWarnLimit or xidStopLimit
 * are exceeded.
 */
typedef struct VariableCacheData
{
	/*
	 * These fields are protected by OidGenLock.
	 */
	Oid			nextOid;		/* next OID to assign */
	uint32		oidCount;		/* OIDs available before must do XLOG work */

	/*
	 * These fields are protected by XidGenLock.
	 */
	FullTransactionId nextFullXid;	/* next full XID to assign */

	TransactionId oldestXid;	/* cluster-wide minimum datfrozenxid */
	TransactionId xidVacLimit;	/* start forcing autovacuums here */
	TransactionId xidWarnLimit; /* start complaining here */
	TransactionId xidStopLimit; /* refuse to advance nextFullXid beyond here */
	TransactionId xidWrapLimit; /* where the world ends */
	Oid			oldestXidDB;	/* database with minimum datfrozenxid */

	/*
	 * These fields are protected by CommitTsLock
	 */
	TransactionId oldestCommitTsXid;
	TransactionId newestCommitTsXid;

	/*
	 * These fields are protected by ProcArrayLock.
	 */
	TransactionId latestCompletedXid;	/* newest XID that has committed or
										 * aborted */

	/*
	 * These fields are protected by CLogTruncationLock
	 */
	TransactionId oldestClogXid;	/* oldest it's safe to look up in clog */

} VariableCacheData;

typedef VariableCacheData *VariableCache;


/* ----------------
 *		extern declarations
 * ----------------
 */

/* in transam/xact.c */
extern bool TransactionStartedDuringRecovery(void);

/* in transam/varsup.c */
extern PGDLLIMPORT VariableCache ShmemVariableCache;

/*
 * prototypes for functions in transam/transam.c
 */
extern bool TransactionIdDidCommit(TransactionId transactionId);
extern bool TransactionIdDidAbort(TransactionId transactionId);
extern bool TransactionIdIsKnownCompleted(TransactionId transactionId);
extern void TransactionIdCommitTree(TransactionId xid, int nxids, TransactionId *xids);
extern void TransactionIdAsyncCommitTree(TransactionId xid, int nxids, TransactionId *xids, XLogRecPtr lsn);
extern void TransactionIdAbortTree(TransactionId xid, int nxids, TransactionId *xids);
extern bool TransactionIdPrecedes(TransactionId id1, TransactionId id2);
extern bool TransactionIdPrecedesOrEquals(TransactionId id1, TransactionId id2);
extern bool TransactionIdFollows(TransactionId id1, TransactionId id2);
extern bool TransactionIdFollowsOrEquals(TransactionId id1, TransactionId id2);
extern TransactionId TransactionIdLatest(TransactionId mainxid,
										 int nxids, const TransactionId *xids);
extern XLogRecPtr TransactionIdGetCommitLSN(TransactionId xid);

/* in transam/varsup.c */
extern FullTransactionId GetNewTransactionId(bool isSubXact);
extern void AdvanceNextFullTransactionIdPastXid(TransactionId xid);
extern FullTransactionId ReadNextFullTransactionId(void);
extern void SetTransactionIdLimit(TransactionId oldest_datfrozenxid,
								  Oid oldest_datoid);
extern void AdvanceOldestClogXid(TransactionId oldest_datfrozenxid);
extern bool ForceTransactionIdLimitUpdate(void);
extern Oid	GetNewObjectId(void);
extern uint32 GetEpochForXid(TransactionId xid);

/*
 * Some frontend programs include this header.  For compilers that emit static
 * inline functions even when they're unused, that leads to unsatisfied
 * external references; hence hide them with #ifndef FRONTEND.
 */
#ifndef FRONTEND

/*
 * For callers that just need the XID part of the next transaction ID.
 */
static inline TransactionId
ReadNewTransactionId(void)
{
	return XidFromFullTransactionId(ReadNextFullTransactionId());
}

#endif							/* FRONTEND */

#endif							/* TRANSAM_H */
