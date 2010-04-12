//
// $Id$
//

//
// Copyright (c) 2001-2010, Andrew Aksyonoff
// Copyright (c) 2008-2010, Sphinx Technologies Inc
// All rights reserved
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License. You should have
// received a copy of the GPL license along with this program; if you
// did not, you can find it at http://www.gnu.org/
//

#include "sphinx.h"
#include "sphinxint.h"
#include "sphinxrt.h"
#include "sphinxsearch.h"
#include "sphinxutils.h"

#include <sys/stat.h>
#include <fcntl.h>

#if USE_WINDOWS
#include <io.h> // for open(), close()
#else
#include <unistd.h>
#endif

//////////////////////////////////////////////////////////////////////////

#define	COMPRESSED_WORDLIST		1
#define	COMPRESSED_DOCLIST		1
#define COMPRESSED_HITLIST		1

#if USE_64BIT
#define WORDID_MAX				U64C(0xffffffffffffffff)
#else
#define	WORDID_MAX				0xffffffffUL
#endif

//////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
#define Verify(_expr) assert(_expr)
#else
#define Verify(_expr) _expr
#endif

//////////////////////////////////////////////////////////////////////////

// !COMMIT cleanup extern ref to sphinx.cpp
extern void sphSortDocinfos ( DWORD * pBuf, int iCount, int iStride );

// !COMMIT yes i am when debugging
#ifndef NDEBUG
#define PARANOID 1
#endif

//////////////////////////////////////////////////////////////////////////

template < typename T, typename P >
static inline void ZipT ( CSphVector < BYTE, P > & dOut, T uValue )
{
	do
	{
		BYTE bOut = (BYTE)( uValue & 0x7f );
		uValue >>= 7;
		if ( uValue )
			bOut |= 0x80;
		dOut.Add ( bOut );
	} while ( uValue );
}


template < typename T >
static inline const BYTE * UnzipT ( T * pValue, const BYTE * pIn )
{
	T uValue = 0;
	BYTE bIn;
	int iOff = 0;

	do
	{
		bIn = *pIn++;
		uValue += ( T ( bIn & 0x7f ) ) << iOff;
		iOff += 7;
	} while ( bIn & 0x80 );

	*pValue = uValue;
	return pIn;
}

#define ZipDword ZipT<DWORD>
#define ZipQword ZipT<uint64_t>
#define UnzipDword UnzipT<DWORD>
#define UnzipQword UnzipT<uint64_t>

#if USE_64BIT
#define ZipDocid ZipQword
#define ZipWordid ZipQword
#define UnzipDocid UnzipQword
#define UnzipWordid UnzipQword
#else
#define ZipDocid ZipDword
#define ZipWordid ZipDword
#define UnzipDocid UnzipDword
#define UnzipWordid UnzipDword
#endif

//////////////////////////////////////////////////////////////////////////

struct CmpHit_fn
{
	inline bool IsLess ( const CSphWordHit & a, const CSphWordHit & b )
	{
		return 	( a.m_iWordID<b.m_iWordID ) ||
			( a.m_iWordID==b.m_iWordID && a.m_iDocID<b.m_iDocID ) ||
			( a.m_iWordID==b.m_iWordID && a.m_iDocID==b.m_iDocID && a.m_iWordPos<b.m_iWordPos );
	}
};


struct RtDoc_t
{
	SphDocID_t					m_uDocID;	///< my document id
	DWORD						m_uFields;	///< fields mask
	DWORD						m_uHits;	///< hit count
	DWORD						m_uHit;		///< either index into segment hits, or the only hit itself (if hit count is 1)
};


struct RtWord_t
{
	SphWordID_t					m_uWordID;	///< my keyword id
	DWORD						m_uDocs;	///< document count (for stats and/or BM25)
	DWORD						m_uHits;	///< hit count (for stats and/or BM25)
	DWORD						m_uDoc;		///< index into segment docs
};


struct RtWordCheckpoint_t
{
	SphWordID_t					m_uWordID;
	int							m_iOffset;
};

class RtDiskKlist_t : public ISphNoncopyable
{
private:
	static const int				MAX_SMALL_SIZE = 512;
	CSphVector < SphAttr_t >		m_dLargeKlist;
	CSphOrderedHash < bool, SphDocID_t, IdentityHash_fn, MAX_SMALL_SIZE, 11 >	m_hSmallKlist;
	mutable CSphRwlock				m_tRwLargelock;
	mutable CSphRwlock				m_tRwSmalllock;

	void NakedFlush();				// flush without lockers

public:
	RtDiskKlist_t() { m_tRwLargelock.Init(); m_tRwSmalllock.Init(); }
	virtual ~RtDiskKlist_t() { m_tRwLargelock.Done(); m_tRwSmalllock.Done(); }
	void Reset ();
	void Flush()
	{
		if ( m_hSmallKlist.GetLength()==0 )
			return;
		m_tRwSmalllock.WriteLock();
		m_tRwLargelock.WriteLock();
		NakedFlush();
		m_tRwLargelock.Unlock();
		m_tRwSmalllock.Unlock();
	}
	void LoadFromFile ( const char * sFilename );
	void SaveToFile ( const char * sFilename );
	inline void Delete ( SphDocID_t uDoc )
	{
		m_tRwSmalllock.WriteLock();
		if ( !m_hSmallKlist.Exists ( uDoc ) )
			m_hSmallKlist.Add ( true, uDoc );
		if ( m_hSmallKlist.GetLength()>=MAX_SMALL_SIZE )
			NakedFlush();
		m_tRwSmalllock.Unlock();
	}
	inline const SphAttr_t * GetKillList () const { return & m_dLargeKlist[0]; }
	inline int	GetKillListSize () const { return m_dLargeKlist.GetLength(); }
	inline bool KillListLock() const { return m_tRwLargelock.ReadLock(); }
	inline bool KillListUnlock() const { return m_tRwLargelock.Unlock(); }
};

void RtDiskKlist_t::Reset()
{
	m_dLargeKlist.Reset();
	m_hSmallKlist.Reset();
}

void RtDiskKlist_t::NakedFlush()
{
	if ( m_hSmallKlist.GetLength()==0 )
		return;
	m_hSmallKlist.IterateStart();
	while ( m_hSmallKlist.IterateNext() )
		m_dLargeKlist.Add ( m_hSmallKlist.IterateGetKey() );
	m_dLargeKlist.Uniq();
	m_hSmallKlist.Reset();
}

void RtDiskKlist_t::LoadFromFile ( const char * sFilename )
{
	m_tRwLargelock.WriteLock();
	m_tRwSmalllock.WriteLock();
	m_hSmallKlist.Reset();
	m_tRwSmalllock.Unlock();

	m_dLargeKlist.Reset();
	CSphString sName, sError;
	sName.SetSprintf ( "%s.kill", sFilename );
	if ( !sphIsReadable ( sName.cstr(), &sError ) )
	{
		m_tRwLargelock.Unlock();
		return;
	}

	CSphAutoreader rdKlist;
	if ( !rdKlist.Open ( sName, sError ) )
	{
		m_tRwLargelock.Unlock();
		return;
	}

	m_dLargeKlist.Resize ( rdKlist.GetDword() );
	SphDocID_t uLastDocID = 0;
	ARRAY_FOREACH ( i, m_dLargeKlist )
	{
		uLastDocID += ( SphDocID_t ) rdKlist.UnzipOffset();
		m_dLargeKlist[i] = uLastDocID;
	};
	m_tRwLargelock.Unlock();
}

void RtDiskKlist_t::SaveToFile ( const char * sFilename )
{
	m_tRwLargelock.WriteLock();
	m_tRwSmalllock.WriteLock();
	NakedFlush();
	m_tRwSmalllock.Unlock();

	CSphWriter wrKlist;
	CSphString sName, sError;
	sName.SetSprintf ( "%s.kill", sFilename );
	wrKlist.OpenFile ( sName.cstr(), sError );

	wrKlist.PutDword ( m_dLargeKlist.GetLength() );
	SphDocID_t uLastDocID = 0;
	ARRAY_FOREACH ( i, m_dLargeKlist )
	{
		wrKlist.ZipOffset ( m_dLargeKlist[i] - uLastDocID );
		uLastDocID = ( SphDocID_t ) m_dLargeKlist[i];
	};
	m_tRwLargelock.Unlock();
	wrKlist.CloseFile ();
}

struct RtSegment_t
{
protected:
	static const int			KLIST_ACCUM_THRESH	= 32;

public:
	static CSphStaticMutex		m_tSegmentSeq;
	static int					m_iSegments;	///< age tag sequence generator
	int							m_iTag;			///< segment age tag

#if COMPRESSED_WORDLIST
	CSphTightVector<BYTE>			m_dWords;
	CSphVector<RtWordCheckpoint_t>	m_dWordCheckpoints;
#else
	CSphVector<RtWord_t>		m_dWords;
#endif

#if COMPRESSED_DOCLIST
	CSphTightVector<BYTE>		m_dDocs;
#else
	CSphVector<RtDoc_t>			m_dDocs;
#endif

#if COMPRESSED_HITLIST
	CSphTightVector<BYTE>		m_dHits;
#else
	CSphVector<DWORD>			m_dHits;
#endif

	int							m_iRows;		///< number of actually allocated rows
	int							m_iAliveRows;	///< number of alive (non-killed) rows
	CSphVector<CSphRowitem>		m_dRows;		///< row data storage
	CSphVector<SphDocID_t>		m_dKlist;		///< sorted K-list
	bool						m_bTlsKlist;	///< whether to apply TLS K-list during merge (must only be used by writer during Commit())
	CSphTightVector<BYTE>		m_dStrings;		///< strings storage

	RtSegment_t ()
	{
		m_tSegmentSeq.Lock ();
		m_iTag = m_iSegments++;
		m_tSegmentSeq.Unlock ();
		m_iRows = 0;
		m_iAliveRows = 0;
		m_bTlsKlist = false;
		m_dStrings.Add ( 0 ); // dummy zero offset
	}

	int64_t GetUsedRam () const
	{
		// FIXME! gonna break on vectors over 2GB
		return
			m_dWords.GetLimit()*sizeof(m_dWords[0]) +
			m_dDocs.GetLimit()*sizeof(m_dDocs[0]) +
			m_dHits.GetLimit()*sizeof(m_dHits[0]) +
			m_dStrings.GetLimit()*sizeof(m_dStrings[0]);
	}

	int GetMergeFactor () const
	{
		return m_iRows;
	}

	bool					HasDocid ( SphDocID_t uDocid ) const;
	const CSphRowitem *		FindRow ( SphDocID_t uDocid ) const;
	const CSphRowitem *		FindAliveRow ( SphDocID_t uDocid ) const;
};

int RtSegment_t::m_iSegments = 0;
CSphStaticMutex RtSegment_t::m_tSegmentSeq;


bool RtSegment_t::HasDocid ( SphDocID_t uDocid ) const
{
	return FindRow ( uDocid )!=NULL;
}


const CSphRowitem * RtSegment_t::FindRow ( SphDocID_t uDocid ) const
{
	// binary search through the rows
	int iStride = m_dRows.GetLength() / m_iRows;
	SphDocID_t uL = DOCINFO2ID ( &m_dRows[0] );
	SphDocID_t uR = DOCINFO2ID ( &m_dRows[m_dRows.GetLength()-iStride] );

	if ( uDocid==uL )
		return &m_dRows[0];

	if ( uDocid==uR )
		return &m_dRows[m_dRows.GetLength()-iStride];

	if ( uDocid<uL || uDocid>uR )
		return NULL;

	int iL = 0;
	int iR = m_iRows-1;
	while ( iR-iL>1 )
	{
		int iM = iL + (iR-iL)/2;
		SphDocID_t uM = DOCINFO2ID ( &m_dRows[iM*iStride] );

		if ( uDocid==uM )
			return &m_dRows[iM*iStride];
		else if ( uDocid>uM )
			iL = iM;
		else
			iR = iM;
	}
	return NULL;
}


const CSphRowitem * RtSegment_t::FindAliveRow ( SphDocID_t uDocid ) const
{
	if ( m_dKlist.BinarySearch ( uDocid ) )
		return NULL;
	else
		return FindRow ( uDocid );
}

//////////////////////////////////////////////////////////////////////////

#if COMPRESSED_DOCLIST

struct RtDocWriter_t
{
	CSphTightVector<BYTE> *		m_pDocs;
	SphDocID_t					m_uLastDocID;

	explicit RtDocWriter_t ( RtSegment_t * pSeg )
		: m_pDocs ( &pSeg->m_dDocs )
		, m_uLastDocID ( 0 )
	{}

	void ZipDoc ( const RtDoc_t & tDoc )
	{
		CSphTightVector<BYTE> & dDocs = *m_pDocs;
		ZipDocid ( dDocs, tDoc.m_uDocID - m_uLastDocID );
		m_uLastDocID = tDoc.m_uDocID;
		ZipDword ( dDocs, tDoc.m_uFields );
		ZipDword ( dDocs, tDoc.m_uHits );
		if ( tDoc.m_uHits==1 )
		{
			ZipDword ( dDocs, tDoc.m_uHit & 0xffffffUL );
			ZipDword ( dDocs, tDoc.m_uHit>>24 );
		} else
			ZipDword ( dDocs, tDoc.m_uHit );
	}

	DWORD ZipDocPtr () const
	{
		return m_pDocs->GetLength();
	}

	void ZipRestart ()
	{
		m_uLastDocID = 0;
	}
};

struct RtDocReader_t
{
	const BYTE *	m_pDocs;
	int				m_iLeft;
	RtDoc_t			m_tDoc;

	explicit RtDocReader_t ( const RtSegment_t * pSeg, const RtWord_t & tWord )
	{
		m_pDocs = &pSeg->m_dDocs[0] + tWord.m_uDoc;
		m_iLeft = tWord.m_uDocs;
		m_tDoc.m_uDocID = 0;
	}

	const RtDoc_t * UnzipDoc ()
	{
		if ( !m_iLeft )
			return NULL;

		const BYTE * pIn = m_pDocs;
		SphDocID_t uDeltaID;
		pIn = UnzipDocid ( &uDeltaID, pIn );
		m_tDoc.m_uDocID += uDeltaID;
		pIn = UnzipDword ( &m_tDoc.m_uFields, pIn );
		pIn = UnzipDword ( &m_tDoc.m_uHits, pIn );
		if ( m_tDoc.m_uHits==1 )
		{
			DWORD a, b;
			pIn = UnzipDword ( &a, pIn );
			pIn = UnzipDword ( &b, pIn );
			m_tDoc.m_uHit = a + ( b<<24 );
		} else
			pIn = UnzipDword ( &m_tDoc.m_uHit, pIn );
		m_pDocs = pIn;

		m_iLeft--;
		return &m_tDoc;
	}
};

#else

struct RtDocWriter_t
{
	CSphVector<RtDoc_t> *	m_pDocs;

	explicit				RtDocWriter_t ( RtSegment_t * pSeg )	: m_pDocs ( &pSeg->m_dDocs ) {}
	void					ZipDoc ( const RtDoc_t & tDoc )			{ m_pDocs->Add ( tDoc ); }
	DWORD					ZipDocPtr () const						{ return m_pDocs->GetLength(); }
	void					ZipRestart ()							{}
};


struct RtDocReader_t
{
	const RtDoc_t *	m_pDocs;
	int				m_iPos;
	int				m_iMax;

	explicit RtDocReader_t ( const RtSegment_t * pSeg, const RtWord_t & tWord )
	{
		m_pDocs = &pSeg->m_dDocs[0];
		m_iPos = tWord.m_uDoc;
		m_iMax = tWord.m_uDoc + tWord.m_uDocs;
	}

	const RtDoc_t * UnzipDoc ()
	{
		return m_iPos<m_iMax ? m_pDocs + m_iPos++ : NULL;
	}
};

#endif // COMPRESSED_DOCLIST


#if COMPRESSED_WORDLIST

static const int WORDLIST_CHECKPOINT_SIZE = 1024;

struct RtWordWriter_t
{
	CSphTightVector<BYTE> *				m_pWords;
	CSphVector<RtWordCheckpoint_t> *	m_pCheckpoints;
	SphWordID_t							m_uLastWordID;
	SphDocID_t							m_uLastDoc;
	int									m_iWords;

	explicit RtWordWriter_t ( RtSegment_t * pSeg )
		: m_pWords ( &pSeg->m_dWords )
		, m_pCheckpoints ( &pSeg->m_dWordCheckpoints )
		, m_uLastWordID ( 0 )
		, m_uLastDoc ( 0 )
		, m_iWords ( 0 )
	{
		assert ( !m_pWords->GetLength() );
		assert ( !m_pCheckpoints->GetLength() );
	}

	void ZipWord ( const RtWord_t & tWord )
	{
		CSphTightVector<BYTE> & tWords = *m_pWords;
		if ( ++m_iWords==WORDLIST_CHECKPOINT_SIZE )
		{
			RtWordCheckpoint_t & tCheckpoint = m_pCheckpoints->Add();
			tCheckpoint.m_uWordID = tWord.m_uWordID;
			tCheckpoint.m_iOffset = tWords.GetLength();

			m_uLastWordID = 0;
			m_uLastDoc = 0;
			m_iWords = 1;
		}

		ZipWordid ( tWords, tWord.m_uWordID - m_uLastWordID );
		ZipDword ( tWords, tWord.m_uDocs );
		ZipDword ( tWords, tWord.m_uHits );
		ZipDocid ( tWords, tWord.m_uDoc - m_uLastDoc );
		m_uLastWordID = tWord.m_uWordID;
		m_uLastDoc = tWord.m_uDoc;
	}
};


struct RtWordReader_t
{
	const BYTE *	m_pCur;
	const BYTE *	m_pMax;
	RtWord_t		m_tWord;
	int				m_iWords;

	explicit RtWordReader_t ( const RtSegment_t * pSeg )
		: m_iWords ( 0 )
	{
		m_pCur = &pSeg->m_dWords[0];
		m_pMax = m_pCur + pSeg->m_dWords.GetLength();

		m_tWord.m_uWordID = 0;
		m_tWord.m_uDoc = 0;
	}

	const RtWord_t * UnzipWord ()
	{
		if ( ++m_iWords==WORDLIST_CHECKPOINT_SIZE )
		{
			m_tWord.m_uWordID = 0;
			m_tWord.m_uDoc = 0;
			m_iWords = 1;
		}
		if ( m_pCur>=m_pMax )
			return NULL;

		const BYTE * pIn = m_pCur;
		SphWordID_t uDeltaID;
		SphDocID_t uDeltaDoc;
		pIn = UnzipWordid ( &uDeltaID, pIn );
		pIn = UnzipDword ( &m_tWord.m_uDocs, pIn );
		pIn = UnzipDword ( &m_tWord.m_uHits, pIn );
		pIn = UnzipDocid ( &uDeltaDoc, pIn );
		m_pCur = pIn;

		m_tWord.m_uWordID += uDeltaID;
		m_tWord.m_uDoc += uDeltaDoc;
		return &m_tWord;
	}
};

#else // !COMPRESSED_WORDLIST

struct RtWordWriter_t
{
	CSphVector<RtWord_t> *	m_pWords;

	explicit				RtWordWriter_t ( RtSegment_t * pSeg )	: m_pWords ( &pSeg->m_dWords ) {}
	void					ZipWord ( const RtWord_t & tWord )		{ m_pWords->Add ( tWord ); }
};


struct RtWordReader_t
{
	const RtWord_t *	m_pCur;
	const RtWord_t *	m_pMax;

	explicit RtWordReader_t ( const RtSegment_t * pSeg )
	{
		m_pCur = &pSeg->m_dWords[0];
		m_pMax = m_pCur + pSeg->m_dWords.GetLength();
	}

	const RtWord_t * UnzipWord ()
	{
		return m_pCur<m_pMax ? m_pCur++ : NULL;
	}
};

#endif // COMPRESSED_WORDLIST


#if COMPRESSED_HITLIST

struct RtHitWriter_t
{
	CSphTightVector<BYTE> *		m_pHits;
	DWORD						m_uLastHit;

	explicit RtHitWriter_t ( RtSegment_t * pSeg )
		: m_pHits ( &pSeg->m_dHits )
		, m_uLastHit ( 0 )
	{}

	void ZipHit ( DWORD uValue )
	{
		ZipDword ( *m_pHits, uValue - m_uLastHit );
		m_uLastHit = uValue;
	}

	void ZipRestart ()
	{
		m_uLastHit = 0;
	}

	DWORD ZipHitPtr () const
	{
		return m_pHits->GetLength();
	}
};


struct RtHitReader_t
{
	const BYTE *	m_pCur;
	DWORD			m_iLeft;
	DWORD			m_uLast;

	RtHitReader_t ()
		: m_pCur ( NULL )
		, m_iLeft ( 0 )
		, m_uLast ( 0 )
	{}

	explicit RtHitReader_t ( const RtSegment_t * pSeg, const RtDoc_t * pDoc )
	{
		m_pCur = &pSeg->m_dHits [ pDoc->m_uHit ];
		m_iLeft = pDoc->m_uHits;
		m_uLast = 0;
	}

	DWORD UnzipHit ()
	{
		if ( !m_iLeft )
			return 0;

		DWORD uValue;
		m_pCur = UnzipDword ( &uValue, m_pCur );
		m_uLast += uValue;
		m_iLeft--;
		return m_uLast;
	}
};


struct RtHitReader2_t : public RtHitReader_t
{
	const BYTE * m_pBase;

	RtHitReader2_t ()
		: m_pBase ( NULL )
	{}

	void Seek ( SphOffset_t uOff, int iHits )
	{
		m_pCur = m_pBase + uOff;
		m_iLeft = iHits;
		m_uLast = 0;
	}
};

#else

struct RtHitWriter_t
{
	CSphVector<DWORD> *	m_pHits;

	explicit			RtHitWriter_t ( RtSegment_t * pSeg )	: m_pHits ( &pSeg->m_dHits ) {}
	void				ZipHit ( DWORD uValue )					{ m_pHits->Add ( uValue ); }
	void				ZipRestart ()							{}
	DWORD				ZipHitPtr () const						{ return m_pHits->GetLength(); }
};

struct RtHitReader_t
{
	const DWORD * m_pCur;
	const DWORD * m_pMax;

	explicit RtHitReader_t ( const RtSegment_t * pSeg, const RtDoc_t * pDoc )
	{
		m_pCur = &pSeg->m_dHits [ pDoc->m_uHit ];
		m_pMax = m_pCur + pDoc->m_uHits;
	}

	DWORD UnzipHit ()
	{
		return m_pCur<m_pMax ? *m_pCur++ : 0;
	}
};

#endif // COMPRESSED_HITLIST

//////////////////////////////////////////////////////////////////////////

/// forward ref
struct RtIndex_t;

/// indexing accumulator
struct RtAccum_t
{
	RtIndex_t *					m_pIndex;		///< my current owner in this thread
	int							m_iAccumDocs;
	CSphVector<CSphWordHit>		m_dAccum;
	CSphVector<CSphRowitem>		m_dAccumRows;
	CSphVector<SphDocID_t>		m_dAccumKlist;
	CSphTightVector<BYTE>		m_dStrings;

					RtAccum_t() : m_pIndex ( NULL ), m_iAccumDocs ( 0 ) { m_dStrings.Add ( 0 ); }
	void			AddDocument ( const CSphVector<CSphWordHit> & dHits, const CSphMatch & tDoc, int iRowSize, const char ** ppStr );
	RtSegment_t *	CreateSegment ( int iRowSize );
};

/// TLS indexing accumulator (we disallow two uncommitted adds within one thread; and so need at most one)
SphThreadKey_t g_tTlsAccumKey;

static const int64_t g_iRangeMin = -1;
static const int64_t g_iRangeMax = I64C ( 0x7FFFFFFFFFFFFFFF );
struct IndexRange_t
{
	int64_t		m_iMin;
	int64_t		m_iMax;
	CSphString	m_sName;

	IndexRange_t() : m_iMin ( g_iRangeMax ), m_iMax ( g_iRangeMin ) {}
};

struct BinlogDesc_t
{
	int							m_iExt;
	CSphVector< IndexRange_t >	m_dRanges;

	BinlogDesc_t() : m_iExt ( 0 ) {}
};

struct IndexFlushPoint_t
{
	int64_t		m_iTID;
	CSphString	m_sName;

	IndexFlushPoint_t ( const char * sIndexName, int64_t iTID ) : m_iTID ( iTID ), m_sName ( sIndexName ) {}
	explicit IndexFlushPoint_t () : m_iTID ( g_iRangeMin ) {}
};

enum StoredOp_e
{
	BINLOG_DOC_ADD		= 1,
	BINLOG_DOC_DELETE	= 2,
	BINLOG_DOC_COMMIT	= 3,
	BINLOG_INDEX_ADD	= 4,
	BINLOG_UPDATE_ATTRS	= 5
};

// forward declaration
class BufferReader_t;
class RtBinlog_c;

class BinlogWriter_c : public CSphWriter
{
public:
					BinlogWriter_c ();
	virtual			~BinlogWriter_c () {}
	void			SetNotifyCallback ( RtBinlog_c * pNotifyOnFlush );
	virtual	void	Flush ();

private:
	RtBinlog_c * m_pNotifyOnFlush;
};

class RtBinlog_c : public ISphNoncopyable
{
public:
	RtBinlog_c ();
	~RtBinlog_c ();

	void	NotifyAddDocument ( const char * sIndexName, const CSphVector<CSphWordHit> & dHits, const CSphMatch & tDoc, int iRowSize
		, const char ** ppStr, const CSphSchema & tSchema );
	void	NotifyDeleteDocument ( const char * sIndexName, SphDocID_t tDocID );
	void	NotifyCommit ( const char * sIndexName, int64_t iTID );
	void	NotifyUpdateAttributes ( const char * sIndexName, const CSphAttrUpdate & tUpd );

	// here's been going binlogs removing for ALL closed indices
	void	NotifyIndexFlush ( const char * sIndexName, int64_t iTID );

	void	Configure ( const CSphConfigSection & hSearchd );
	void	Replay ( const CSphVector < ISphRtIndex * > & dRtIndices );

	void	CreateTimerThread ();
	bool	IsReplayMode () const { return m_bReplayMode; }
	void	NotifyBufferFlushed ( SphOffset_t iWritten );

private:

	static const DWORD		BINLOG_HEADER_MAGIC = 0x4c425053; /// my magic 'SPBL' header
	static const DWORD		BINLOG_VERSION = 1;
	static const DWORD		META_HEADER_MAGIC = 0x494c5053; /// my magic 'SPLI' header
	static const DWORD		META_VERSION = 1;

	int						m_iFlushPeriod;
	int64_t					m_iFlushTimeLeft;
	bool					m_bFlushOnCommit; // should we flush on every commit

	CSphMutex				m_tWriteLock; // lock on operation

	int						m_iLockFD;
	CSphString				m_sWriterError;
	BinlogWriter_c			m_tWriter;
	CSphVector<BinlogDesc_t>		m_dBinlogs; // known log descriptions
	CSphVector<IndexFlushPoint_t>	m_dFlushed; // known log flushed transactions

	CSphString				m_sLogPath;

	SphThread_t				m_tUpdateTread;
	bool					m_bReplayMode; // replay mode indicator
	bool					m_bDisabled;

	int						m_iRestartSize; // binlog size restart threshold
	SphOffset_t				m_iLastWriten;
	int64_t					m_iMetaSaveTimeStamp;

	static void				UpdateCheckFlush ( void * pBinlog );
	int 					GetWriteIndexID ( const char * sName );
	void					LoadMeta ();
	void					SaveMeta ();
	void					LockFile ( bool bLock );
	void					CheckDoRestart ();
	void					CheckRemoveFlushed();
	int						AddNewIndex ( const char * sIndexName );
	bool					NeedReplay ( const char * sIndexName, int64_t iTID ) const;
	void					FlushedCleanup ( const CSphVector < ISphRtIndex * > & dRtIndices );
	void					OpenNewLog ();
	void					Close ();
	void					ReplayBinlog ( const CSphVector < ISphRtIndex * > & dRtIndices, int iBinlog ) const;
	bool					ReplayAddDocument ( const CSphVector < ISphRtIndex * > & dRtIndices, CSphReader & tReader, int iBinlog, const CSphVector<int64_t> & dCommitedCP ) const;
	bool					ReplayDeleteDocument ( const CSphVector < ISphRtIndex * > & dRtIndices, CSphReader & tReader, int iBinlog, const CSphVector<int64_t> & dCommitedCP ) const;
	bool					ReplayCommit ( const CSphVector < ISphRtIndex * > & dRtIndices, CSphReader & tReader, int iBinlog, CSphVector<int64_t> & dCommitedCP ) const;
	bool					ReplayIndexAdd ( const CSphVector < ISphRtIndex * > & dRtIndices, CSphReader & tReader, int iBinlog ) const;
	bool					ReplayUpdateAttributes ( const CSphVector < ISphRtIndex * > & dRtIndices, CSphReader & tReader, int iBinlog ) const;
};

static RtBinlog_c * g_pBinlog = NULL;
static bool g_bRTChangesAllowed = false;


/// indexing accumulator index helper
class AccumStorage_c
{
private:
	CSphVector<RtAccum_t *> m_dBusy;
	CSphVector<RtAccum_t *> m_dFree;

public:

	AccumStorage_c () {}
	~AccumStorage_c();
	void			Reset ();
	RtAccum_t *		Aqcuire ( RtIndex_t * pIndex ); // get exists OR get free OR create new accumulator
	RtAccum_t *		Get ( const RtIndex_t * pIndex ) const; // get exists accumulator
	void			Release ( RtAccum_t * pAcc );
};


/// RAM based index
struct RtQword_t;
struct RtIndex_t : public ISphRtIndex, public ISphNoncopyable
{
private:
	static const DWORD			META_HEADER_MAGIC	= 0x54525053;	///< my magic 'SPRT' header
	static const DWORD			META_VERSION		= 2;			///< current version

private:
	const int					m_iStride;
	CSphVector<RtSegment_t*>	m_pSegments;

	CSphMutex					m_tWriterMutex;
	mutable CSphRwlock			m_tRwlock;

	int64_t						m_iRamSize;
	CSphString					m_sPath;
	CSphVector<CSphIndex*>		m_pDiskChunks;
	int							m_iLockFD;
	mutable RtDiskKlist_t		m_tKlist;

	CSphString					m_sIndexName;
	CSphSchema					m_tOutboundSchema;

public:
	explicit					RtIndex_t ( const CSphSchema & tSchema, const char * sIndexName, int64_t iRamSize, const char * sPath );
	virtual						~RtIndex_t ();

	bool						AddDocument ( int iFields, const char ** ppFields, const CSphMatch & tDoc, bool bReplace, const char ** ppStr, CSphString & sError );
	bool						AddDocument ( const CSphVector<CSphWordHit> & dHits, const CSphMatch & tDoc, const char ** ppStr, CSphString & sError );
	bool						DeleteDocument ( SphDocID_t uDoc, CSphString & sError );
	void						Commit ();
	void						RollBack ();

	bool						AddDocumentReplayable ( const CSphVector<CSphWordHit> & dHits, const CSphMatch & tDoc, const char ** ppStr, CSphString * sError=NULL );
	bool						DeleteDocumentReplayable ( SphDocID_t uDoc, CSphString * sError=NULL );
	void						CommitReplayable ();

	void						DumpToDisk ( const char * sFilename );

	virtual const char *		GetName () { return m_sIndexName.cstr(); }
	RtAccum_t *					GetAccum () const;

private:
	/// acquire thread-local indexing accumulator
	/// returns NULL if another index already uses it in an open txn
	RtAccum_t *					AcquireAccum ( CSphString * sError=NULL );

	RtSegment_t *				MergeSegments ( const RtSegment_t * pSeg1, const RtSegment_t * pSeg2 );
	const RtWord_t *			CopyWord ( RtSegment_t * pDst, RtWordWriter_t & tOutWord, const RtSegment_t * pSrc, const RtWord_t * pWord, RtWordReader_t & tInWord );
	void						MergeWord ( RtSegment_t * pDst, const RtSegment_t * pSrc1, const RtWord_t * pWord1, const RtSegment_t * pSrc2, const RtWord_t * pWord2, RtWordWriter_t & tOut );
	void						CopyDoc ( RtSegment_t * pSeg, RtDocWriter_t & tOutDoc, RtWord_t * pWord, const RtSegment_t * pSrc, const RtDoc_t * pDoc );

	void						SaveMeta ( int iDiskChunks );
	void						SaveDiskHeader ( const char * sFilename, int iCheckpoints, SphOffset_t iCheckpointsPosition, DWORD uKillListSize ) const;
	void						SaveDiskData ( const char * sFilename ) const;
	void						SaveDiskChunk ();
	CSphIndex *					LoadDiskChunk ( int iChunk );
	bool						LoadRamChunk ();
	bool						SaveRamChunk ();

public:
#if USE_WINDOWS
#pragma warning(push,1)
#pragma warning(disable:4100)
#endif
	virtual SphAttr_t *			GetKillList () const				{ return NULL; }
	virtual int					GetKillListSize () const			{ return 0; }
	virtual bool				HasDocid ( SphDocID_t ) const		{ assert ( 0 ); return false; }

	virtual int					Build ( const CSphVector<CSphSource*> & dSources, int iMemoryLimit, int iWriteBuffer ) { return 0; }
	virtual bool				Merge ( CSphIndex * pSource, CSphVector<CSphFilterSettings> & dFilters, bool bMergeKillLists ) { return false; }

	virtual bool				Prealloc ( bool bMlock, CSphString & sWarning );
	virtual void				Dealloc () {}
	virtual bool				Preread ();
	virtual void				SetBase ( const char * sNewBase ) {}
	virtual bool				Rename ( const char * sNewBase ) { return true; }
	virtual bool				Lock () { return true; }
	virtual void				Unlock () {}
	virtual bool				Mlock () { return true; }

	virtual int					UpdateAttributes ( const CSphAttrUpdate & tUpd, int iIndex, CSphString & sError );
	virtual bool				SaveAttributes () { return true; }

	virtual void				DebugDumpHeader ( FILE * fp, const char * sHeaderName ) {}
	virtual void				DebugDumpDocids ( FILE * fp ) {}
	virtual void				DebugDumpHitlist ( FILE * fp, const char * sKeyword, bool bID ) {}
	virtual int					DebugCheck ( FILE * fp ) { return 0; }
#if USE_WINDOWS
#pragma warning(pop)
#endif

public:
	virtual bool						EarlyReject ( CSphQueryContext * pCtx, CSphMatch & ) const;
	virtual const CSphSourceStats &		GetStats () const { return m_tStats; }

	virtual bool				MultiQuery ( const CSphQuery * pQuery, CSphQueryResult * pResult, int iSorters, ISphMatchSorter ** ppSorters, const CSphVector<CSphFilterSettings> * pExtraFilters, int iTag ) const;
	virtual bool				MultiQueryEx ( int iQueries, const CSphQuery * ppQueries, CSphQueryResult ** ppResults, ISphMatchSorter ** ppSorters, const CSphVector<CSphFilterSettings> * pExtraFilters, int iTag ) const;
	virtual bool				GetKeywords ( CSphVector <CSphKeywordInfo> & dKeywords, const char * szQuery, bool bGetStats, CSphString & sError ) const;

	void						CopyDocinfo ( CSphMatch & tMatch, const DWORD * pFound ) const;
	const CSphRowitem *			FindDocinfo ( const RtSegment_t * pSeg, SphDocID_t uDocID ) const;

	bool						RtQwordSetup ( RtQword_t * pQword, RtSegment_t * pSeg ) const;
	static bool					RtQwordSetupSegment ( RtQword_t * pQword, RtSegment_t * pSeg, bool bSetup );

	virtual const CSphSchema &	GetMatchSchema () const { return m_tOutboundSchema; }
	virtual const CSphSchema &	GetInternallSchema () const { return m_tSchema; }

protected:
	CSphSourceStats				m_tStats;

public:
	static AccumStorage_c		m_tAccums;
	int64_t						m_iTID;
};

AccumStorage_c RtIndex_t::m_tAccums = AccumStorage_c();


RtIndex_t::RtIndex_t ( const CSphSchema & tSchema, const char * sIndexName, int64_t iRamSize, const char * sPath )
	: ISphRtIndex ( "rtindex" )
	, m_iStride ( DOCINFO_IDSIZE + tSchema.GetRowSize() )
	, m_iRamSize ( iRamSize )
	, m_sPath ( sPath )
	, m_iLockFD ( -1 )
	, m_sIndexName ( sIndexName )
	, m_iTID ( 0 )
{
	MEMORY ( SPH_MEM_IDX_RT );

	m_tSchema = tSchema;

	// schemes strings attributes fix up
	bool bReplaceSchema = false;
	for ( int i=0; i<tSchema.GetAttrsCount() && !bReplaceSchema; i++ )
		bReplaceSchema = tSchema.GetAttr(i).m_eAttrType==SPH_ATTR_STRING && !tSchema.GetAttr(i).m_tLocator.m_bDynamic;

	m_tOutboundSchema = m_tSchema;
	if ( bReplaceSchema )
		for ( int i=m_tOutboundSchema.GetAttrsCount()-1; i>=0; i-- )
		{
			CSphColumnInfo tCol = m_tOutboundSchema.GetAttr(i);
			if ( tCol.m_eAttrType==SPH_ATTR_STRING && !tCol.m_tLocator.m_bDynamic )
			{
				tCol.m_eStage = SPH_EVAL_OVERRIDE;
				m_tOutboundSchema.RemoveAttr ( i );
				m_tOutboundSchema.AddAttr ( tCol, true );
			}
		}

#ifndef NDEBUG
	// check that index cols are static
	for ( int i=0; i<m_tSchema.GetAttrsCount(); i++ )
		assert ( !m_tSchema.GetAttr(i).m_tLocator.m_bDynamic );
#endif

	Verify ( m_tWriterMutex.Init() );
	Verify ( m_tRwlock.Init() );
}


RtIndex_t::~RtIndex_t ()
{
	SaveRamChunk ();
	SaveMeta ( m_pDiskChunks.GetLength() );

	Verify ( m_tWriterMutex.Done() );
	Verify ( m_tRwlock.Done() );

	ARRAY_FOREACH ( i, m_pSegments )
		SafeDelete ( m_pSegments[i] );

	ARRAY_FOREACH ( i, m_pDiskChunks )
		SafeDelete ( m_pDiskChunks[i] );

	if ( m_iLockFD>=0 )
		::close ( m_iLockFD );

	g_pBinlog->NotifyIndexFlush ( m_sIndexName.cstr(), m_iTID );
}

//////////////////////////////////////////////////////////////////////////
// INDEXING
//////////////////////////////////////////////////////////////////////////

class CSphSource_StringVector : public CSphSource_Document
{
public:
	explicit			CSphSource_StringVector ( int iFields, const char ** ppFields, const CSphSchema & tSchema );
	virtual				~CSphSource_StringVector () {}

	virtual bool		Connect ( CSphString & ) { return true; }
	virtual void		Disconnect () {}

	virtual bool		HasAttrsConfigured () { return false; }
	virtual bool		IterateHitsStart ( CSphString & ) { return true; }

	virtual bool		IterateMultivaluedStart ( int, CSphString & ) { return false; }
	virtual bool		IterateMultivaluedNext () { return false; }

	virtual bool		IterateFieldMVAStart ( int, CSphString & ) { return false; }
	virtual bool		IterateFieldMVANext () { return false; }

	virtual bool		IterateKillListStart ( CSphString & ) { return false; }
	virtual bool		IterateKillListNext ( SphDocID_t & ) { return false; }

	virtual BYTE **		NextDocument ( CSphString & ) { return &m_dFields[0]; }

protected:
	CSphVector<BYTE *>	m_dFields;
};


CSphSource_StringVector::CSphSource_StringVector ( int iFields, const char ** ppFields, const CSphSchema & tSchema )
	: CSphSource_Document ( "$stringvector" )
{
	m_tSchema = tSchema;

	m_dFields.Resize ( 1+iFields );
	for ( int i=0; i<iFields; i++ )
	{
		m_dFields[i] = (BYTE*) ppFields[i];
		assert ( m_dFields[i] );
	}
	m_dFields [ iFields ] = NULL;
}

bool RtIndex_t::AddDocument ( int iFields, const char ** ppFields, const CSphMatch & tDoc, bool bReplace, const char ** ppStr, CSphString & sError )
{
	assert ( g_bRTChangesAllowed );

	if ( !tDoc.m_iDocID )
		return true;

	MEMORY ( SPH_MEM_IDX_RT );

	if ( !bReplace )
	{
		m_tRwlock.ReadLock ();
		ARRAY_FOREACH ( i, m_pSegments )
			if ( FindDocinfo ( m_pSegments[i], tDoc.m_iDocID )
				&& !m_pSegments[i]->m_dKlist.BinarySearch ( tDoc.m_iDocID ) )
		{
			m_tRwlock.Unlock ();
			sError.SetSprintf ( "duplicate id '%d'", tDoc.m_iDocID );
			return false; // already exists and not deleted; INSERT fails
		}
		m_tRwlock.Unlock ();
	}

	CSphScopedPtr<ISphTokenizer> pTokenizer ( m_pTokenizer->Clone ( false ) ); // avoid race
	CSphSource_StringVector tSrc ( iFields, ppFields, m_tOutboundSchema );
	tSrc.SetTokenizer ( pTokenizer.Ptr() );
	tSrc.SetDict ( m_pDict );

	tSrc.m_tDocInfo.Clone ( tDoc, m_tOutboundSchema.GetRowSize() );
	if ( !tSrc.IterateHitsNext ( sError ) )
		return false;

	return AddDocument ( tSrc.m_dHits, tDoc, ppStr, sError );
}


void AccumCleanup ( void * pArg )
{
	RtAccum_t * pAcc = (RtAccum_t *) pArg;
	SafeDelete ( pAcc );
}


RtAccum_t * RtIndex_t::AcquireAccum ( CSphString * sError )
{
	RtAccum_t * pAcc = NULL;

	if ( g_pBinlog->IsReplayMode() )
		pAcc = m_tAccums.Aqcuire ( this );
	else
	{
		// check that no other index is holding the acc
		pAcc = (RtAccum_t*) sphThreadGet ( g_tTlsAccumKey );
		if ( pAcc && pAcc->m_pIndex!=NULL && pAcc->m_pIndex!=this )
		{
			if ( sError )
				sError->SetSprintf ( "current txn is working with another index ('%s')", pAcc->m_pIndex->m_tSchema.m_sName.cstr() );
			return NULL;
		}

		if ( !pAcc )
		{
			pAcc = new RtAccum_t ();
			sphThreadSet ( g_tTlsAccumKey, pAcc );
			sphThreadOnExit ( AccumCleanup, pAcc );
		}
	}

	assert ( pAcc->m_pIndex==NULL || pAcc->m_pIndex==this );
	pAcc->m_pIndex = this;
	return pAcc;
}

RtAccum_t * RtIndex_t::GetAccum () const
{
	if ( g_pBinlog->IsReplayMode() )
		return m_tAccums.Get ( this );
	else
		return (RtAccum_t*) sphThreadGet ( g_tTlsAccumKey );
}

bool RtIndex_t::AddDocument ( const CSphVector<CSphWordHit> & dHits, const CSphMatch & tDoc, const char ** ppStr, CSphString & sError )
{
	assert ( g_bRTChangesAllowed );
	return AddDocumentReplayable ( dHits, tDoc, ppStr, &sError );
}

bool RtIndex_t::AddDocumentReplayable ( const CSphVector<CSphWordHit> & dHits, const CSphMatch & tDoc, const char ** ppStr, CSphString * sError )
{
	RtAccum_t * pAcc = AcquireAccum ( sError );
	if ( pAcc )
	{
		pAcc->AddDocument ( dHits, tDoc, m_tOutboundSchema.GetRowSize(), ppStr );
		g_pBinlog->NotifyAddDocument ( m_sIndexName.cstr(), dHits, tDoc, m_tOutboundSchema.GetRowSize(), ppStr, m_tOutboundSchema );
	}
	return ( pAcc!=NULL );
}

void RtAccum_t::AddDocument ( const CSphVector<CSphWordHit> & dHits, const CSphMatch & tDoc, int iRowSize, const char ** ppStr )
{
	MEMORY ( SPH_MEM_IDX_RT_ACCUM );

	// schedule existing copies for deletion
	m_dAccumKlist.Add ( tDoc.m_iDocID );

	// no pain, no gain!
	if ( !dHits.GetLength() )
		return;

	// reserve some hit space on first use
	if ( !m_dAccum.GetLength() )
		m_dAccum.Reserve ( 128*1024 );

	// accumulate row data; expect fully dynamic rows
	assert ( !tDoc.m_pStatic );
	assert (!( !tDoc.m_pDynamic && iRowSize!=0 ));
	assert (!( tDoc.m_pDynamic && (int)tDoc.m_pDynamic[-1]!=iRowSize ));

	m_dAccumRows.Resize ( m_dAccumRows.GetLength() + DOCINFO_IDSIZE + iRowSize );
	CSphRowitem * pRow = &m_dAccumRows [ m_dAccumRows.GetLength() - DOCINFO_IDSIZE - iRowSize ];
	DOCINFOSETID ( pRow, tDoc.m_iDocID );

	CSphRowitem * pAttrs = DOCINFO2ATTRS(pRow);
	for ( int i=0; i<iRowSize; i++ )
		pAttrs[i] = tDoc.m_pDynamic[i];

	const CSphSchema & pSchema = m_pIndex->GetInternallSchema();
	int iAttr = 0;
	for ( int i=0; i<pSchema.GetAttrsCount(); i++ )
	{
		const CSphColumnInfo & tColumn = pSchema.GetAttr(i);
		if ( tColumn.m_eAttrType==SPH_ATTR_STRING )
		{
			const char * pStr = ppStr ? ppStr[iAttr] : NULL;
			const int iLen = pStr ? strlen ( pStr ) : 0;

			if ( iLen )
			{
				BYTE dLen[3];
				const int iLenPacked = sphPackStrlen ( dLen, iLen );
				const int iOff = m_dStrings.GetLength();
				assert ( iOff>=1 );
				m_dStrings.Resize ( iOff + iLenPacked + iLen );
				memcpy ( &m_dStrings[iOff], dLen, iLenPacked );
				memcpy ( &m_dStrings[iOff+iLenPacked], pStr, iLen );
				sphSetRowAttr ( pAttrs, tColumn.m_tLocator, iOff );
			} else
				sphSetRowAttr ( pAttrs, tColumn.m_tLocator, 0 );
		}
	}

	// accumulate hits
	ARRAY_FOREACH ( i, dHits )
		m_dAccum.Add ( dHits[i] );

	m_iAccumDocs++;
}


RtSegment_t * RtAccum_t::CreateSegment ( int iRowSize )
{
	if ( !m_iAccumDocs )
		return NULL;

	MEMORY ( SPH_MEM_IDX_RT_ACCUM );

	RtSegment_t * pSeg = new RtSegment_t ();

	CSphWordHit tClosingHit;
	tClosingHit.m_iWordID = WORDID_MAX;
	tClosingHit.m_iDocID = DOCID_MAX;
	tClosingHit.m_iWordPos = 1;
	m_dAccum.Add ( tClosingHit );
	m_dAccum.Sort ( CmpHit_fn() );

	RtDoc_t tDoc;
	tDoc.m_uDocID = 0;
	tDoc.m_uFields = 0;
	tDoc.m_uHits = 0;
	tDoc.m_uHit = 0;

	RtWord_t tWord;
	tWord.m_uWordID = 0;
	tWord.m_uDocs = 0;
	tWord.m_uHits = 0;
	tWord.m_uDoc = 0;

	RtDocWriter_t tOutDoc ( pSeg );
	RtWordWriter_t tOutWord ( pSeg );
	RtHitWriter_t tOutHit ( pSeg );

	DWORD uEmbeddedHit = 0;
	ARRAY_FOREACH ( i, m_dAccum )
	{
		const CSphWordHit & tHit = m_dAccum[i];

		// new keyword or doc; flush current doc
		if ( tHit.m_iWordID!=tWord.m_uWordID || tHit.m_iDocID!=tDoc.m_uDocID )
		{
			if ( tDoc.m_uDocID )
			{
				tWord.m_uDocs++;
				tWord.m_uHits += tDoc.m_uHits;

				if ( uEmbeddedHit )
				{
					assert ( tDoc.m_uHits==1 );
					tDoc.m_uHit = uEmbeddedHit;
				}

				tOutDoc.ZipDoc ( tDoc );
				tDoc.m_uFields = 0;
				tDoc.m_uHits = 0;
				tDoc.m_uHit = tOutHit.ZipHitPtr();
			}

			tDoc.m_uDocID = tHit.m_iDocID;
			tOutHit.ZipRestart ();
			uEmbeddedHit = 0;
		}

		// new keyword; flush current keyword
		if ( tHit.m_iWordID!=tWord.m_uWordID )
		{
			tOutDoc.ZipRestart ();
			if ( tWord.m_uWordID )
				tOutWord.ZipWord ( tWord );

			tWord.m_uWordID = tHit.m_iWordID;
			tWord.m_uDocs = 0;
			tWord.m_uHits = 0;
			tWord.m_uDoc = tOutDoc.ZipDocPtr();
		}

		// just a new hit
		if ( !tDoc.m_uHits )
		{
			uEmbeddedHit = tHit.m_iWordPos;
		} else
		{
			if ( uEmbeddedHit )
			{
				tOutHit.ZipHit ( uEmbeddedHit );
				uEmbeddedHit = 0;
			}

			tOutHit.ZipHit ( tHit.m_iWordPos );
		}

		tDoc.m_uFields |= 1UL << HIT2FIELD ( tHit.m_iWordPos );
		tDoc.m_uHits++;
	}

	pSeg->m_iRows = m_iAccumDocs;
	pSeg->m_iAliveRows = m_iAccumDocs;

	// copy and sort attributes
	int iStride = DOCINFO_IDSIZE + iRowSize;
	pSeg->m_dRows.SwapData ( m_dAccumRows );
	pSeg->m_dStrings.SwapData ( m_dStrings );
	sphSortDocinfos ( &pSeg->m_dRows[0], pSeg->m_dRows.GetLength()/iStride, iStride );

	// done
	return pSeg;
}


const RtWord_t * RtIndex_t::CopyWord ( RtSegment_t * pDst, RtWordWriter_t & tOutWord, const RtSegment_t * pSrc, const RtWord_t * pWord, RtWordReader_t & tInWord )
{
	RtDocReader_t tInDoc ( pSrc, *pWord );
	RtDocWriter_t tOutDoc ( pDst );

	RtWord_t tNewWord = *pWord;
	tNewWord.m_uDoc = tOutDoc.ZipDocPtr();

#ifndef NDEBUG
	RtAccum_t * pAccCheck = GetAccum();
	assert (!( pSrc->m_bTlsKlist && !pAccCheck )); // if flag is there, acc must be there
	assert ( !pAccCheck || pAccCheck->m_pIndex==this ); // *must* be holding acc during merge
#endif

	// copy docs
	for ( ;; )
	{
		const RtDoc_t * pDoc = tInDoc.UnzipDoc();
		if ( !pDoc )
			break;

		// apply klist
		bool bKill = ( pSrc->m_dKlist.BinarySearch ( pDoc->m_uDocID )!=NULL );
		if ( !bKill && pSrc->m_bTlsKlist )
		{
			RtAccum_t * pAcc = GetAccum ();
			bKill = ( pAcc->m_dAccumKlist.BinarySearch ( pDoc->m_uDocID )!=NULL );
		}
		if ( bKill )
		{
			tNewWord.m_uDocs--;
			tNewWord.m_uHits -= pDoc->m_uHits;
			continue;
		}

		// short route, single embedded hit
		if ( pDoc->m_uHits==1 )
		{
			tOutDoc.ZipDoc ( *pDoc );
			continue;
		}

		// long route, copy hits
		RtHitWriter_t tOutHit ( pDst );
		RtHitReader_t tInHit ( pSrc, pDoc );

		RtDoc_t tDoc = *pDoc;
		tDoc.m_uHit = tOutHit.ZipHitPtr();

		// OPTIMIZE? decode+memcpy?
		for ( DWORD uValue=tInHit.UnzipHit(); uValue; uValue=tInHit.UnzipHit() )
			tOutHit.ZipHit ( uValue );

		// copy doc
		tOutDoc.ZipDoc ( tDoc );
	}

	// append word to the dictionary
	if ( tNewWord.m_uDocs )
		tOutWord.ZipWord ( tNewWord );

	// move forward
	return tInWord.UnzipWord ();
}


void RtIndex_t::CopyDoc ( RtSegment_t * pSeg, RtDocWriter_t & tOutDoc, RtWord_t * pWord, const RtSegment_t * pSrc, const RtDoc_t * pDoc )
{
	pWord->m_uDocs++;
	pWord->m_uHits += pDoc->m_uHits;

	if ( pDoc->m_uHits==1 )
	{
		tOutDoc.ZipDoc ( *pDoc );
		return;
	}

	RtHitWriter_t tOutHit ( pSeg );
	RtHitReader_t tInHit ( pSrc, pDoc );

	RtDoc_t tDoc = *pDoc;
	tDoc.m_uHit = tOutHit.ZipHitPtr();
	tOutDoc.ZipDoc ( tDoc );

	// OPTIMIZE? decode+memcpy?
	for ( DWORD uValue=tInHit.UnzipHit(); uValue; uValue=tInHit.UnzipHit() )
		tOutHit.ZipHit ( uValue );
}


void RtIndex_t::MergeWord ( RtSegment_t * pSeg, const RtSegment_t * pSrc1, const RtWord_t * pWord1, const RtSegment_t * pSrc2, const RtWord_t * pWord2, RtWordWriter_t & tOut )
{
	assert ( pWord1->m_uWordID==pWord2->m_uWordID );

	RtDocWriter_t tOutDoc ( pSeg );

	RtWord_t tWord;
	tWord.m_uWordID = pWord1->m_uWordID;
	tWord.m_uDocs = 0;
	tWord.m_uHits = 0;
	tWord.m_uDoc = tOutDoc.ZipDocPtr();

	RtDocReader_t tIn1 ( pSrc1, *pWord1 );
	RtDocReader_t tIn2 ( pSrc2, *pWord2 );
	const RtDoc_t * pDoc1 = tIn1.UnzipDoc();
	const RtDoc_t * pDoc2 = tIn2.UnzipDoc();

#ifndef NDEBUG
	RtAccum_t * pAcc = GetAccum();
#endif

	while ( pDoc1 || pDoc2 )
	{
		if ( pDoc1 && pDoc2 && pDoc1->m_uDocID==pDoc2->m_uDocID )
		{
			// dupe, must (!) be killed in the first segment, might be in both
			assert ( pSrc1->m_dKlist.BinarySearch ( pDoc1->m_uDocID )
				|| ( pSrc1->m_bTlsKlist && pAcc && pAcc->m_dAccumKlist.BinarySearch ( pDoc1->m_uDocID ) ) );
			if ( !pSrc2->m_dKlist.BinarySearch ( pDoc2->m_uDocID ) )
				CopyDoc ( pSeg, tOutDoc, &tWord, pSrc2, pDoc2 );
			pDoc1 = tIn1.UnzipDoc();
			pDoc2 = tIn2.UnzipDoc();

		} else if ( pDoc1 && ( !pDoc2 || pDoc1->m_uDocID < pDoc2->m_uDocID ) )
		{
			// winner from the first segment
			if ( !pSrc1->m_dKlist.BinarySearch ( pDoc1->m_uDocID ) )
				CopyDoc ( pSeg, tOutDoc, &tWord, pSrc1, pDoc1 );
			pDoc1 = tIn1.UnzipDoc();

		} else
		{
			// winner from the second segment
			assert ( pDoc2 && ( !pDoc1 || pDoc2->m_uDocID < pDoc1->m_uDocID ) );
			if ( !pSrc2->m_dKlist.BinarySearch ( pDoc2->m_uDocID ) )
				CopyDoc ( pSeg, tOutDoc, &tWord, pSrc2, pDoc2 );
			pDoc2 = tIn2.UnzipDoc();
		}
	}

	if ( tWord.m_uDocs )
		tOut.ZipWord ( tWord );
}


#if PARANOID
static void CheckSegmentRows ( const RtSegment_t * pSeg, int iStride )
{
	const CSphVector<CSphRowitem> & dRows = pSeg->m_dRows; // shortcut
	for ( int i=iStride; i<dRows.GetLength(); i+=iStride )
		assert ( DOCINFO2ID ( &dRows[i] ) > DOCINFO2ID ( &dRows[i-iStride] ) );
}
#endif


struct RtRowIterator_t : public ISphNoncopyable
{
protected:
	const CSphRowitem * m_pRow;
	const CSphRowitem * m_pRowMax;
	const SphDocID_t * m_pKlist;
	const SphDocID_t * m_pKlistMax;
	const SphDocID_t * m_pTlsKlist;
	const SphDocID_t * m_pTlsKlistMax;
	const int m_iStride;
	const RtAccum_t * m_pAcc;

public:
	explicit RtRowIterator_t ( const RtSegment_t * pSeg, int iStride, bool bWriter, const RtAccum_t * pAcc )
		: m_pRow ( &pSeg->m_dRows[0] )
		, m_pRowMax ( &pSeg->m_dRows[0] + pSeg->m_dRows.GetLength() )
		, m_pKlist ( NULL )
		, m_pKlistMax ( NULL )
		, m_pTlsKlist ( NULL )
		, m_pTlsKlistMax ( NULL )
		, m_iStride ( iStride )
		, m_pAcc ( pAcc )
	{
		if ( pSeg->m_dKlist.GetLength() )
		{
			m_pKlist = &pSeg->m_dKlist[0];
			m_pKlistMax = m_pKlist + pSeg->m_dKlist.GetLength();
		}

		// FIXME? OPTIMIZE? must not scan tls (open txn) in readers; can implement lighter iterator
		// FIXME? OPTIMIZE? maybe we should just rely on the segment order and don't scan tls klist here
		if ( bWriter && pSeg->m_bTlsKlist )
		{
			if ( m_pAcc && m_pAcc->m_dAccumKlist.GetLength() )
			{
				m_pTlsKlist = &pAcc->m_dAccumKlist[0];
				m_pTlsKlistMax = m_pTlsKlist + pAcc->m_dAccumKlist.GetLength();
			}
		}
	}

	const CSphRowitem * GetNextAliveRow ()
	{
		// while there are rows and k-list entries
		while ( m_pRow<m_pRowMax && ( m_pKlist<m_pKlistMax || m_pTlsKlist<m_pTlsKlistMax ) )
		{
			// get next candidate id
			SphDocID_t uID = DOCINFO2ID(m_pRow);

			// check if segment k-list kills it
			while ( m_pKlist<m_pKlistMax && *m_pKlist<uID )
				m_pKlist++;

			if ( m_pKlist<m_pKlistMax && *m_pKlist==uID )
			{
				m_pKlist++;
				m_pRow += m_iStride;
				continue;
			}

			// check if txn k-list kills it
			while ( m_pTlsKlist<m_pTlsKlistMax && *m_pTlsKlist<uID )
				m_pTlsKlist++;

			if ( m_pTlsKlist<m_pTlsKlistMax && *m_pTlsKlist==uID )
			{
				m_pTlsKlist++;
				m_pRow += m_iStride;
				continue;
			}

			// oh, so nobody kills it
			break;
		}

		// oops, out of rows
		if ( m_pRow>=m_pRowMax )
			return NULL;

		// got it, and it's alive!
		m_pRow += m_iStride;
		return m_pRow-m_iStride;
	}
};

#ifdef PARANOID // sanity check in PARANOID mode
static void VerifyEmptyStrings ( const CSphTightVector<BYTE> & dStorage, const CSphSchema & tSchema, const CSphRowitem * pRow )
{
	if ( dStorage.GetLength()>1 )
		return;

	const DWORD * pAttr = DOCINFO2ATTRS(pRow);
	for ( int i=0; i<tSchema.GetAttrsCount(); i++ )
	{
		const CSphColumnInfo & tCol = tSchema.GetAttr(i);
		assert ( tCol.m_eAttrType!=SPH_ATTR_STRING
		|| ( tCol.m_eAttrType==SPH_ATTR_STRING && sphGetRowAttr ( pAttr, tCol.m_tLocator )==0 ) );
	}
}
#endif

static int CopyPackedString ( const BYTE * pSource, CSphTightVector<BYTE> & dDest )
{
	assert ( pSource );
	assert ( dDest.GetLength()>=1 );
	const BYTE * pStr = NULL;
	const int iLen = sphUnpackStr ( pSource, &pStr );
	assert ( iLen>0 );
	assert ( pStr );

	const int iOff = dDest.GetLength();
	const int iWriteLen = iLen + ( pStr - pSource ); // actual length = strings content length + packed length of string
	dDest.Resize ( iOff + iWriteLen );
	memcpy ( &dDest[iOff], pSource, iWriteLen );

	return iOff;
}

#ifndef NDEBUG
static void DoFixupStrAttr ( const BYTE * pStrBase, int iOffMax, const CSphSchema & tSchema, CSphRowitem * pRow, CSphWriter & tWriter )
#else
static void DoFixupStrAttr ( const BYTE * pStrBase, int , const CSphSchema & tSchema, CSphRowitem * pRow, CSphWriter & tWriter )
#endif
{
	// store string\mva attr for this row
	DWORD * pAttr = DOCINFO2ATTRS ( pRow );
	for ( int i=0; i<tSchema.GetAttrsCount(); i++ )
	{
		const CSphColumnInfo & tColumn = tSchema.GetAttr(i);
		const SphAttr_t tOff = sphGetRowAttr ( pAttr, tColumn.m_tLocator );
		if ( tColumn.m_eAttrType==SPH_ATTR_STRING && tOff>0 )
		{
			assert ( tWriter.GetPos()>0 );
			assert ( tWriter.GetPos()<( I64C(1)<<32 ) ); // should be 32 bit offset
			assert ( iOffMax==0 || (int)tOff<iOffMax );
			const DWORD iAttrOff = (DWORD)tWriter.GetPos();

			assert ( pStrBase );
			assert ( tWriter.GetPos()>=1 );
			const BYTE * pCodedStr = pStrBase + tOff;
			const BYTE * pStr = NULL;
			const int iLen = sphUnpackStr ( pCodedStr, &pStr );
			assert ( iLen>0 );
			assert ( pStr );
			const int iWriteLen = iLen + ( pStr - pCodedStr );
			tWriter.PutBytes ( pCodedStr, iWriteLen );

			sphSetRowAttr ( pAttr, tColumn.m_tLocator, iAttrOff );
		}
	}
}

#ifndef NDEBUG
void DoFixupStrAttr ( const BYTE * pStrBase, int iOffMax, const CSphSchema & tSchema, CSphRowitem * pRow, CSphTightVector<BYTE> & dStrings )
#else
void DoFixupStrAttr ( const BYTE * pStrBase, int , const CSphSchema & tSchema, CSphRowitem * pRow, CSphTightVector<BYTE> & dStrings )
#endif
{
	// store string\mva attr for this row
	DWORD * pAttr = DOCINFO2ATTRS(pRow);
	for ( int i=0; i<tSchema.GetAttrsCount(); i++ )
	{
		const CSphColumnInfo & tColumn = tSchema.GetAttr(i);
		const SphAttr_t tOff = sphGetRowAttr ( pAttr, tColumn.m_tLocator );
		if ( tColumn.m_eAttrType==SPH_ATTR_STRING && tOff>0 )
		{
			assert ( tOff<( I64C(1)<<32 ) ); // should be 32 bit offset
			assert ( iOffMax==0 || (int)tOff<iOffMax );

			const int iDstOff = CopyPackedString ( pStrBase + tOff, dStrings );
			sphSetRowAttr ( pAttr, tColumn.m_tLocator, iDstOff );
		}
	}
}

static void DoFixupStrAttr ( const CSphTightVector<BYTE> & dStorage, const CSphSchema & tSchema, CSphRowitem * pRow, CSphTightVector<BYTE> & dStrings )
{
#ifdef PARANOID // sanity check in PARANOID mode
	VerifyEmptyStrings ( dStorage, tSchema, pRow );
#endif

	// only dummy zero - nothing to fix
	if ( dStorage.GetLength()<=1 )
		return;

	DoFixupStrAttr ( &dStorage[0], dStorage.GetLength(), tSchema, pRow, dStrings );
}

RtSegment_t * RtIndex_t::MergeSegments ( const RtSegment_t * pSeg1, const RtSegment_t * pSeg2 )
{
	if ( pSeg1->m_iTag > pSeg2->m_iTag )
		Swap ( pSeg1, pSeg2 );

	RtSegment_t * pSeg = new RtSegment_t ();

	////////////////////
	// merge attributes
	////////////////////

	// check that all the IDs are in proper asc order
#if PARANOID
	CheckSegmentRows ( pSeg1, m_iStride );
	CheckSegmentRows ( pSeg2, m_iStride );
#endif

	// just a shortcut
	CSphVector<CSphRowitem> & dRows = pSeg->m_dRows;
	CSphTightVector<BYTE> & dStrings = pSeg->m_dStrings;

	// we might need less because of dupes, but we can not know yet
	dRows.Reserve ( pSeg1->m_dRows.GetLength() + pSeg2->m_dRows.GetLength() );
	// as each segment has dummy zero we reserve less
	assert ( pSeg1->m_dStrings.GetLength() + pSeg2->m_dStrings.GetLength()>=2 );
	dStrings.Reserve ( pSeg1->m_dStrings.GetLength() + pSeg2->m_dStrings.GetLength() - 2 );

	RtRowIterator_t tIt1 ( pSeg1, m_iStride, true, GetAccum() );
	RtRowIterator_t tIt2 ( pSeg2, m_iStride, true, GetAccum() );

	const CSphRowitem * pRow1 = tIt1.GetNextAliveRow();
	const CSphRowitem * pRow2 = tIt2.GetNextAliveRow();

	while ( pRow1 || pRow2 )
	{
		if ( !pRow2 || ( pRow1 && pRow2 && DOCINFO2ID(pRow1)<DOCINFO2ID(pRow2) ) )
		{
			assert ( pRow1 );
			for ( int i=0; i<m_iStride; i++ )
				dRows.Add ( *pRow1++ );
			CSphRowitem * pDstRow = &dRows[0] + dRows.GetLength() - m_iStride;
			DoFixupStrAttr ( pSeg1->m_dStrings, m_tSchema, pDstRow, dStrings );
			pRow1 = tIt1.GetNextAliveRow();
		} else
		{
			assert ( pRow2 );
			assert ( !pRow1 || ( DOCINFO2ID(pRow1)!=DOCINFO2ID(pRow2) ) ); // all dupes must be killed and skipped by the iterator
			for ( int i=0; i<m_iStride; i++ )
				dRows.Add ( *pRow2++ );
			CSphRowitem * pDstRow = &dRows[0] + dRows.GetLength() - m_iStride;
			DoFixupStrAttr ( pSeg2->m_dStrings, m_tSchema, pDstRow, dStrings );
			pRow2 = tIt2.GetNextAliveRow();
		}
		pSeg->m_iRows++;
		pSeg->m_iAliveRows++;
	}

	assert ( pSeg->m_iRows*m_iStride==pSeg->m_dRows.GetLength() );
#if PARANOID
	CheckSegmentRows ( pSeg, m_iStride );
#endif

	//////////////////
	// merge keywords
	//////////////////

	pSeg->m_dWords.Reserve ( pSeg1->m_dWords.GetLength() + pSeg2->m_dWords.GetLength() );
	pSeg->m_dDocs.Reserve ( pSeg1->m_dDocs.GetLength() + pSeg2->m_dDocs.GetLength() );
	pSeg->m_dHits.Reserve ( pSeg1->m_dHits.GetLength() + pSeg2->m_dHits.GetLength() );

	RtWordWriter_t tOut ( pSeg );
	RtWordReader_t tIn1 ( pSeg1 );
	RtWordReader_t tIn2 ( pSeg2 );
	const RtWord_t * pWords1 = tIn1.UnzipWord ();
	const RtWord_t * pWords2 = tIn2.UnzipWord ();

	// merge while there are common words
	for ( ;; )
	{
		while ( pWords1 && pWords2 && pWords1->m_uWordID!=pWords2->m_uWordID )
			if ( pWords1->m_uWordID < pWords2->m_uWordID )
				pWords1 = CopyWord ( pSeg, tOut, pSeg1, pWords1, tIn1 );
			else
				pWords2 = CopyWord ( pSeg, tOut, pSeg2, pWords2, tIn2 );

		if ( !pWords1 || !pWords2 )
			break;

		assert ( pWords1 && pWords2 && pWords1->m_uWordID==pWords2->m_uWordID );
		MergeWord ( pSeg, pSeg1, pWords1, pSeg2, pWords2, tOut );
		pWords1 = tIn1.UnzipWord();
		pWords2 = tIn2.UnzipWord();
	}

	// copy tails
	while ( pWords1 ) pWords1 = CopyWord ( pSeg, tOut, pSeg1, pWords1, tIn1 );
	while ( pWords2 ) pWords2 = CopyWord ( pSeg, tOut, pSeg2, pWords2, tIn2 );

	assert ( pSeg->m_dRows.GetLength() );
	assert ( pSeg->m_iRows );
	assert ( pSeg->m_iAliveRows==pSeg->m_iRows );
	return pSeg;
}


struct CmpSegments_fn
{
	inline bool IsLess ( const RtSegment_t * a, const RtSegment_t * b )
	{
		return a->GetMergeFactor() > b->GetMergeFactor();
	}
};

void RtIndex_t::Commit ()
{
	assert ( g_bRTChangesAllowed );
	CommitReplayable();
}

void RtIndex_t::CommitReplayable ()
{
	MEMORY ( SPH_MEM_IDX_RT );

	RtAccum_t * pAcc = AcquireAccum();
	if ( !pAcc )
		return;

	// phase 0, build a new segment
	// accum and segment are thread local; so no locking needed yet
	// might be NULL if we're only killing rows this txn
	RtSegment_t * pNewSeg = pAcc->CreateSegment ( m_tOutboundSchema.GetRowSize() );
	assert ( !pNewSeg || pNewSeg->m_iRows>0 );
	assert ( !pNewSeg || pNewSeg->m_iAliveRows>0 );
	assert ( !pNewSeg || pNewSeg->m_bTlsKlist==false );

	// clean up parts we no longer need
	pAcc->m_dAccum.Resize ( 0 );
	pAcc->m_dAccumRows.Resize ( 0 );
	pAcc->m_dStrings.Resize ( 1 ); // handle dummy zero offset

	// sort accum klist, too
	pAcc->m_dAccumKlist.Sort ();

	// phase 1, lock out other writers (but not readers yet)
	// concurrent readers are ok during merges, as existing segments won't be modified yet
	// however, concurrent writers are not
	Verify ( m_tWriterMutex.Lock() );

	// let merger know that existing segments are subject to additional, TLS K-list filter
	// safe despite the readers, flag must only be used by writer
	if ( pAcc->m_dAccumKlist.GetLength() )
		ARRAY_FOREACH ( i, m_pSegments )
	{
		// OPTIMIZE? only need to set the flag if TLS K-list *actually* affects segment
		assert ( m_pSegments[i]->m_bTlsKlist==false );
		m_pSegments[i]->m_bTlsKlist = true;
	}

	// prepare new segments vector
	// create more new segments by merging as needed
	// do not (!) kill processed old segments just yet, as readers might still need them
	CSphVector<RtSegment_t*> dSegments;
	CSphVector<RtSegment_t*> dToKill;

	dSegments = m_pSegments;
	if ( pNewSeg )
		dSegments.Add ( pNewSeg );

	int64_t iRamFreed = 0;

	// enforce RAM usage limit
	int64_t iRamLeft = m_iRamSize;
	ARRAY_FOREACH ( i, dSegments )
		iRamLeft = Max ( 0, iRamLeft - dSegments[i]->GetUsedRam() );

	// skip merging if no rows were added or no memory left
	bool bDump = ( iRamLeft==0 );
	const int MAX_SEGMENTS = 32;
	const int MAX_PROGRESSION_SEGMENT = 8;
	while ( pNewSeg && iRamLeft>0 )
	{
		dSegments.Sort ( CmpSegments_fn() );

		// unconditionally merge if there's too much segments now
		// conditionally merge if smallest segment has grown too large
		// otherwise, we're done
		const int iLen = dSegments.GetLength();
		if ( iLen < ( MAX_SEGMENTS - MAX_PROGRESSION_SEGMENT ) )
			break;
		assert ( iLen>=2 );
		// exit if progression is kept AND lesser MAX_SEGMENTS limit
		if ( dSegments[iLen-2]->GetMergeFactor() > dSegments[iLen-1]->GetMergeFactor()*2 && iLen < MAX_SEGMENTS )
			break;

		// check whether we have enough RAM
#define LOC_ESTIMATE1(_seg,_vec) \
	(int)( ( (int64_t)_seg->_vec.GetLength() ) * _seg->m_iAliveRows / _seg->m_iRows )

#define LOC_ESTIMATE(_vec) \
	( LOC_ESTIMATE1 ( dSegments[iLen-1], _vec ) + LOC_ESTIMATE1 ( dSegments[iLen-2], _vec ) )

		int64_t iEstimate =
			CSphTightVectorPolicy<BYTE>::Relimit ( 0, LOC_ESTIMATE ( m_dWords ) ) +
			CSphTightVectorPolicy<BYTE>::Relimit ( 0, LOC_ESTIMATE ( m_dDocs ) ) +
			CSphTightVectorPolicy<BYTE>::Relimit ( 0, LOC_ESTIMATE ( m_dHits ) ) +
			CSphTightVectorPolicy<BYTE>::Relimit ( 0, LOC_ESTIMATE ( m_dStrings ) );

#undef LOC_ESTIMATE
#undef LOC_ESTIMATE1

		if ( iEstimate>iRamLeft )
		{
			// dump case: can't merge any more AND segments count limit's reached
			bDump = ( ( iRamLeft + iRamFreed )<=iEstimate ) && ( iLen>=MAX_SEGMENTS );
			break;
		}

		// do it
		RtSegment_t * pA = dSegments.Pop();
		RtSegment_t * pB = dSegments.Pop();
		dSegments.Add ( MergeSegments ( pA, pB ) );
		dToKill.Add ( pA );
		dToKill.Add ( pB );

		iRamFreed += pA->GetUsedRam() + pB->GetUsedRam();

		int64_t iMerged = dSegments.Last()->GetUsedRam();
		iRamLeft -= Min ( iRamLeft, iMerged );
	}

	// phase 2, obtain exclusive writer lock
	// we now have to update K-lists in (some of) the survived segments
	// and also swap in new segment list
	m_tRwlock.WriteLock ();

	// adjust for an incoming accumulator K-list
	int iTotalKilled = 0;
	if ( pAcc->m_dAccumKlist.GetLength() )
	{
		pAcc->m_dAccumKlist.Uniq(); // FIXME? can we guarantee this is redundant?

		// update totals
		// work the original (!) segments, and before (!) updating their K-lists
		ARRAY_FOREACH ( i, pAcc->m_dAccumKlist )
		{
			SphDocID_t uDocid = pAcc->m_dAccumKlist[i];
			bool bKilled = false;

			// check RAM chunk
			for ( int j=0; j<m_pSegments.GetLength() && !bKilled; j++ )
				if ( m_pSegments[j]->HasDocid ( uDocid ) && !m_pSegments[j]->m_dKlist.BinarySearch ( uDocid ) )
					bKilled = true;

			// check disk chunks
			if ( !bKilled )
				for ( int j=m_pDiskChunks.GetLength()-1; j>=0; j-- )
					if ( m_pDiskChunks[j]->HasDocid ( uDocid ) )
			{
				// we just found the most recent chunk with our suspect docid
				// let's check whether it's already killed by subsequent chunks, or gets killed now
				bKilled = true;
				SphAttr_t uRef = uDocid;
				for ( int k=j+1; k<m_pDiskChunks.GetLength(); k++ )
					if ( sphBinarySearch ( m_pDiskChunks[k]->GetKillList(), m_pDiskChunks[k]->GetKillList() + m_pDiskChunks[k]->GetKillListSize(), uRef ) )
				{
					bKilled = false;
					break;
				}
				break;
			}

			if ( bKilled )
				iTotalKilled++;
		}

		// update K-lists on survivors
		ARRAY_FOREACH ( iSeg, dSegments )
		{
			RtSegment_t * pSeg = dSegments[iSeg];
			if ( !pSeg->m_bTlsKlist )
				continue; // should be fresh enough

			// this segment was not created by this txn
			// so we need to merge additional K-list from current txn into it
			ARRAY_FOREACH ( j, pAcc->m_dAccumKlist )
			{
				SphDocID_t uDocid = pAcc->m_dAccumKlist[j];
				if ( pSeg->HasDocid ( uDocid ) )
				{
					pSeg->m_dKlist.Add ( uDocid );
					pSeg->m_iAliveRows--;
				}
			}

			// we did not check for existence in K-list, only in segment
			// so need to use Uniq(), not just Sort()
			pSeg->m_dKlist.Uniq ();

			// mark as good
			pSeg->m_bTlsKlist = false;
		}

		// update disk K-list
		ARRAY_FOREACH ( i, pAcc->m_dAccumKlist )
			m_tKlist.Delete ( pAcc->m_dAccumKlist[i] );
	}

	// go live!
	Swap ( m_pSegments, dSegments );

	// we can kill retired segments now
	ARRAY_FOREACH ( i, dToKill )
		SafeDelete ( dToKill[i] );

	// update stats
	m_tStats.m_iTotalDocuments += pAcc->m_iAccumDocs - iTotalKilled;

	// finish cleaning up and release accumulator
	pAcc->m_pIndex = NULL;
	pAcc->m_iAccumDocs = 0;
	if ( g_pBinlog->IsReplayMode() )
	{
		pAcc->m_dAccumKlist.Resize ( 0 );
		pAcc->m_dAccum.Resize ( 0 );
		pAcc->m_dAccumRows.Resize ( 0 );
		pAcc->m_dStrings.Resize ( 1 ); // handle dummy zero offset
		m_tAccums.Release ( pAcc );
	} else
		pAcc->m_dAccumKlist.Reset();

	m_iTID++;

	// phase 3, enable readers again
	// we might need to dump data to disk now
	// but during the dump, readers can still use RAM chunk data
	Verify ( m_tRwlock.Unlock() );

	g_pBinlog->NotifyCommit ( m_sIndexName.cstr(), m_iTID );

	if ( bDump )
	{
		SaveDiskChunk();
		g_pBinlog->NotifyIndexFlush ( m_sIndexName.cstr(), m_iTID );
	}

	// all done, enable other writers
	Verify ( m_tWriterMutex.Unlock() );
}

void RtIndex_t::RollBack ()
{
	assert ( g_bRTChangesAllowed );

	RtAccum_t * pAcc = AcquireAccum();
	if ( !pAcc )
		return;

	// clean up parts we no longer need
	pAcc->m_dAccum.Resize ( 0 );
	pAcc->m_dAccumRows.Resize ( 0 );

	// finish cleaning up and release accumulator
	pAcc->m_pIndex = NULL;
	pAcc->m_iAccumDocs = 0;
	pAcc->m_dAccumKlist.Reset();
}

bool RtIndex_t::DeleteDocument ( SphDocID_t uDoc, CSphString & sError )
{
	assert ( g_bRTChangesAllowed );
	return DeleteDocumentReplayable ( uDoc, &sError );
}

bool RtIndex_t::DeleteDocumentReplayable ( SphDocID_t uDoc, CSphString * sError )
{
	MEMORY ( SPH_MEM_IDX_RT_ACCUM );

	RtAccum_t * pAcc = AcquireAccum ( sError );
	if ( pAcc )
	{
		pAcc->m_dAccumKlist.Add ( uDoc );
		g_pBinlog->NotifyDeleteDocument ( m_sIndexName.cstr(), uDoc );
	}
	return ( pAcc!=NULL );
}

//////////////////////////////////////////////////////////////////////////
// LOAD/SAVE
//////////////////////////////////////////////////////////////////////////

struct Checkpoint_t
{
	uint64_t m_uWord;
	uint64_t m_uOffset;
};


void RtIndex_t::DumpToDisk ( const char * sFilename )
{
	MEMORY ( SPH_MEM_IDX_RT );

	Verify ( m_tWriterMutex.Lock() );
	Verify ( m_tRwlock.WriteLock() );
	SaveDiskData ( sFilename );
	Verify ( m_tRwlock.Unlock() );
	Verify ( m_tWriterMutex.Unlock() );
}


void RtIndex_t::SaveDiskData ( const char * sFilename ) const
{
	CSphString sName, sError;

	CSphWriter wrHits, wrDocs, wrDict, wrRows;
	sName.SetSprintf ( "%s.spp", sFilename ); wrHits.OpenFile ( sName.cstr(), sError );
	sName.SetSprintf ( "%s.spd", sFilename ); wrDocs.OpenFile ( sName.cstr(), sError );
	sName.SetSprintf ( "%s.spi", sFilename ); wrDict.OpenFile ( sName.cstr(), sError );
	sName.SetSprintf ( "%s.spa", sFilename ); wrRows.OpenFile ( sName.cstr(), sError );

	BYTE bDummy = 1;
	wrDict.PutBytes ( &bDummy, 1 );
	wrDocs.PutBytes ( &bDummy, 1 );
	wrHits.PutBytes ( &bDummy, 1 );

	// we don't have enough RAM to create new merged segments
	// and have to do N-way merge kinda in-place
	CSphVector<RtWordReader_t*> pWordReaders;
	CSphVector<RtDocReader_t*> pDocReaders;
	CSphVector<RtSegment_t*> pSegments;
	CSphVector<const RtWord_t*> pWords;
	CSphVector<const RtDoc_t*> pDocs;

	pWordReaders.Reserve ( m_pSegments.GetLength() );
	pDocReaders.Reserve ( m_pSegments.GetLength() );
	pSegments.Reserve ( m_pSegments.GetLength() );
	pWords.Reserve ( m_pSegments.GetLength() );
	pDocs.Reserve ( m_pSegments.GetLength() );

	// OPTIMIZE? somehow avoid new on iterators maybe?
	ARRAY_FOREACH ( i, m_pSegments )
		pWordReaders.Add ( new RtWordReader_t ( m_pSegments[i] ) );

	ARRAY_FOREACH ( i, pWordReaders )
		pWords.Add ( pWordReaders[i]->UnzipWord() );

	// loop keywords
	static const int WORDLIST_CHECKPOINT = 1024;
	CSphVector<Checkpoint_t> dCheckpoints;
	int iWords = 0;

	SphWordID_t uLastWord = 0;
	SphOffset_t uLastDocpos = 0;

	for ( ;; )
	{
		// find keyword with min id
		const RtWord_t * pWord = NULL;
		ARRAY_FOREACH ( i, pWords ) // OPTIMIZE? PQ or at least nulls removal here?!
			if ( pWords[i] )
				if ( !pWord || pWords[i]->m_uWordID < pWord->m_uWordID )
					pWord = pWords[i];
		if ( !pWord )
			break;

		// loop all segments that have this keyword
		assert ( pSegments.GetLength()==0 );
		assert ( pDocReaders.GetLength()==0 );
		assert ( pDocs.GetLength()==0 );

		ARRAY_FOREACH ( i, pWords )
			if ( pWords[i] && pWords[i]->m_uWordID==pWord->m_uWordID )
		{
			pSegments.Add ( m_pSegments[i] );
			pDocReaders.Add ( new RtDocReader_t ( m_pSegments[i], *pWords[i] ) );

			const RtDoc_t * pDoc = pDocReaders.Last()->UnzipDoc();
			while ( pDoc && m_pSegments[i]->m_dKlist.BinarySearch ( pDoc->m_uDocID ) )
				pDoc = pDocReaders.Last()->UnzipDoc();

			pDocs.Add ( pDoc );
		}

		// loop documents
		SphOffset_t uDocpos = wrDocs.GetPos();
		SphDocID_t uLastDoc = 0;
		SphOffset_t uLastHitpos = 0;
		int iDocs = 0;
		int iHits = 0;
		for ( ;; )
		{
			// find alive doc with min id
			int iMinReader = -1;
			ARRAY_FOREACH ( i, pDocs ) // OPTIMIZE?
			{
				if ( !pDocs[i] )
					continue;

				assert ( !pSegments[i]->m_dKlist.BinarySearch ( pDocs[i]->m_uDocID ) );
				if ( iMinReader<0 || pDocs[i]->m_uDocID < pDocs[iMinReader]->m_uDocID )
					iMinReader = i;
			}
			if ( iMinReader<0 )
				break;

			// write doclist entry
			const RtDoc_t * pDoc = pDocs[iMinReader]; // shortcut
			iDocs++;
			iHits += pDoc->m_uHits;

			wrDocs.ZipOffset ( pDoc->m_uDocID - uLastDoc );
			wrDocs.ZipOffset ( wrHits.GetPos() - uLastHitpos );
			wrDocs.ZipInt ( pDoc->m_uFields );
			wrDocs.ZipInt ( pDoc->m_uHits );
			uLastDoc = pDoc->m_uDocID;
			uLastHitpos = wrHits.GetPos();

			// loop hits from most current segment
			if ( pDoc->m_uHits>1 )
			{
				DWORD uLastHit = 0;
				RtHitReader_t tInHit ( pSegments[iMinReader], pDoc );
				for ( DWORD uValue=tInHit.UnzipHit(); uValue; uValue=tInHit.UnzipHit() )
				{
					wrHits.ZipInt ( uValue - uLastHit );
					uLastHit = uValue;
				}
			} else
			{
				wrHits.ZipInt ( pDoc->m_uHit );
			}
			wrHits.ZipInt ( 0 );

			// fast forward readers
			SphDocID_t uMinID = pDocs[iMinReader]->m_uDocID;
			ARRAY_FOREACH ( i, pDocs )
				while ( pDocs[i] && ( pDocs[i]->m_uDocID<=uMinID || pSegments[i]->m_dKlist.BinarySearch ( pDocs[i]->m_uDocID ) ) )
					pDocs[i] = pDocReaders[i]->UnzipDoc();
		}

		// write dict entry if necessary
		if ( wrDocs.GetPos()!=uDocpos )
		{
			wrDocs.ZipInt ( 0 );

			if ( !iWords )
			{
				Checkpoint_t & tChk = dCheckpoints.Add ();
				tChk.m_uWord = pWord->m_uWordID;
				tChk.m_uOffset = wrDict.GetPos();
			}

			wrDict.ZipOffset ( pWord->m_uWordID - uLastWord );
			wrDict.ZipOffset ( uDocpos - uLastDocpos );
			wrDict.ZipInt ( iDocs );
			wrDict.ZipInt ( iHits );
			uLastWord = pWord->m_uWordID;
			uLastDocpos = uDocpos;

			if ( ++iWords==WORDLIST_CHECKPOINT )
			{
				wrDict.ZipInt ( 0 );
				wrDict.ZipOffset ( wrDocs.GetPos() - uLastDocpos ); // store last hitlist length
				uLastDocpos = 0;
				uLastWord = 0;
				iWords = 0;
			}
		}

		// move words forward
		SphWordID_t uMinID = pWord->m_uWordID; // because pWord contents will move forward too!
		ARRAY_FOREACH ( i, pWords )
			if ( pWords[i] && pWords[i]->m_uWordID==uMinID )
				pWords[i] = pWordReaders[i]->UnzipWord();

		// cleanup
		ARRAY_FOREACH ( i, pDocReaders )
			SafeDelete ( pDocReaders[i] );
		pSegments.Resize ( 0 );
		pDocReaders.Resize ( 0 );
		pDocs.Resize ( 0 );
	}

	// write checkpoints
	wrDict.ZipInt ( 0 ); // indicate checkpoint
	wrDict.ZipOffset ( wrDocs.GetPos() - uLastDocpos ); // store last doclist length

	SphOffset_t iCheckpointsPosition = wrDict.GetPos();
	if ( dCheckpoints.GetLength() )
		wrDict.PutBytes ( &dCheckpoints[0], dCheckpoints.GetLength()*sizeof(Checkpoint_t) );

	// write attributes
	CSphVector<RtRowIterator_t*> pRowIterators ( m_pSegments.GetLength() );
	ARRAY_FOREACH ( i, m_pSegments )
		pRowIterators[i] = new RtRowIterator_t ( m_pSegments[i], m_iStride, false, GetAccum() );

	CSphVector<const CSphRowitem*> pRows ( m_pSegments.GetLength() );
	ARRAY_FOREACH ( i, pRowIterators )
		pRows[i] = pRowIterators[i]->GetNextAliveRow();

	sName.SetSprintf ( "%s.sps", sFilename );
	CSphWriter tStrWriter;
	tStrWriter.OpenFile ( sName.cstr(), sError );
	tStrWriter.PutByte ( 0 ); // dummy byte, to reserve magic zero offset

	CSphRowitem * pFixedRow = new CSphRowitem[m_iStride];

	for ( ;; )
	{
		// find min row
		int iMinRow = -1;
		ARRAY_FOREACH ( i, pRows )
			if ( pRows[i] )
				if ( iMinRow<0 || DOCINFO2ID ( pRows[i] ) < DOCINFO2ID ( pRows[iMinRow] ) )
					iMinRow = i;
		if ( iMinRow<0 )
			break;

#ifndef NDEBUG
		// verify that it's unique
		int iDupes = 0;
		ARRAY_FOREACH ( i, pRows )
			if ( pRows[i] )
				if ( DOCINFO2ID ( pRows[i] )==DOCINFO2ID ( pRows[iMinRow] ) )
					iDupes++;
		assert ( iDupes==1 );
#endif

		const CSphRowitem * pRow = pRows[iMinRow];

		// strings storage for stored row
		assert ( iMinRow<m_pSegments.GetLength() );
		const RtSegment_t * pSegment = m_pSegments[iMinRow];

#ifdef PARANOID // sanity check in PARANOID mode
		VerifyEmptyStrings ( pSegment->m_dStrings, m_tSchema, pRow );
#endif

		const int iMaxOff = pSegment->m_dStrings.GetLength();
		if ( iMaxOff>1 ) // should be more then dummy zero elements
		{
			// copy row content as we'll fix up its attrs ( string offset for now )
			memcpy ( pFixedRow, pRow, m_iStride*sizeof(CSphRowitem) );
			pRow = pFixedRow;

			DoFixupStrAttr ( pSegment->m_dStrings.Begin(), iMaxOff, m_tSchema, pFixedRow, tStrWriter );
		}

		// emit it
		wrRows.PutBytes ( pRow, m_iStride*sizeof(CSphRowitem) );

		// fast forward
		pRows[iMinRow] = pRowIterators[iMinRow]->GetNextAliveRow();
	}

	SafeDeleteArray ( pFixedRow );

	tStrWriter.CloseFile ();

	// write dummy string attributes, mva and kill-list files
	CSphWriter wrDummy;

	// dump killlist
	sName.SetSprintf ( "%s.spk", sFilename );
	wrDummy.OpenFile ( sName.cstr(), sError );
	m_tKlist.Flush();
	m_tKlist.KillListLock();
	DWORD uKlistSize = m_tKlist.GetKillListSize();
	if ( uKlistSize )
		wrDummy.PutBytes ( m_tKlist.GetKillList(), uKlistSize*sizeof ( SphAttr_t ) );
	m_tKlist.Reset();
	m_tKlist.KillListUnlock();
	wrDummy.CloseFile ();

	sName.SetSprintf ( "%s.spm", sFilename ); wrDummy.OpenFile ( sName.cstr(), sError ); wrDummy.CloseFile ();

	// header
	SaveDiskHeader ( sFilename, dCheckpoints.GetLength(), iCheckpointsPosition, uKlistSize );

	// cleanup
	ARRAY_FOREACH ( i, pWordReaders )
		SafeDelete ( pWordReaders[i] );
	ARRAY_FOREACH ( i, pDocReaders )
		SafeDelete ( pDocReaders[i] );
	ARRAY_FOREACH ( i, pRowIterators )
		SafeDelete ( pRowIterators[i] );

	// done
	wrHits.CloseFile ();
	wrDocs.CloseFile ();
	wrDict.CloseFile ();
	wrRows.CloseFile ();
}


static void WriteFileInfo ( CSphWriter & tWriter, const CSphSavedFile & tInfo )
{
	tWriter.PutOffset ( tInfo.m_uSize );
	tWriter.PutOffset ( tInfo.m_uCTime );
	tWriter.PutOffset ( tInfo.m_uMTime );
	tWriter.PutDword ( tInfo.m_uCRC32 );
}


static void WriteSchemaColumn ( CSphWriter & tWriter, const CSphColumnInfo & tColumn )
{
	int iLen = strlen ( tColumn.m_sName.cstr() );
	tWriter.PutDword ( iLen );
	tWriter.PutBytes ( tColumn.m_sName.cstr(), iLen );

	DWORD eAttrType = tColumn.m_eAttrType;
	if ( eAttrType==SPH_ATTR_WORDCOUNT )
		eAttrType = SPH_ATTR_INTEGER;
	tWriter.PutDword ( eAttrType );

	tWriter.PutDword ( tColumn.m_tLocator.CalcRowitem() ); // for backwards compatibility
	tWriter.PutDword ( tColumn.m_tLocator.m_iBitOffset );
	tWriter.PutDword ( tColumn.m_tLocator.m_iBitCount );

	tWriter.PutByte ( tColumn.m_bPayload );
}


void RtIndex_t::SaveDiskHeader ( const char * sFilename, int iCheckpoints, SphOffset_t iCheckpointsPosition, DWORD uKillListSize ) const
{
	static const DWORD INDEX_MAGIC_HEADER	= 0x58485053;	///< my magic 'SPHX' header
	static const DWORD INDEX_FORMAT_VERSION	= 19;			///< my format version

	CSphWriter tWriter;
	CSphString sName, sError;
	sName.SetSprintf ( "%s.sph", sFilename );
	tWriter.OpenFile ( sName.cstr(), sError );

	// format
	tWriter.PutDword ( INDEX_MAGIC_HEADER );
	tWriter.PutDword ( INDEX_FORMAT_VERSION );

	tWriter.PutDword ( 0 ); // use-64bit
	tWriter.PutDword ( SPH_DOCINFO_EXTERN );

	// schema
	tWriter.PutDword ( m_tSchema.m_dFields.GetLength() );
	ARRAY_FOREACH ( i, m_tSchema.m_dFields )
		WriteSchemaColumn ( tWriter, m_tSchema.m_dFields[i] );

	tWriter.PutDword ( m_tSchema.GetAttrsCount() );
	for ( int i=0; i<m_tSchema.GetAttrsCount(); i++ )
		WriteSchemaColumn ( tWriter, m_tSchema.GetAttr(i) );

	tWriter.PutOffset ( 0 ); // min docid

	// wordlist checkpoints
	tWriter.PutOffset ( iCheckpointsPosition );
	tWriter.PutDword ( iCheckpoints );

	// stats
	tWriter.PutDword ( m_tStats.m_iTotalDocuments );
	tWriter.PutOffset ( m_tStats.m_iTotalBytes );

	// index settings
	tWriter.PutDword ( m_tSettings.m_iMinPrefixLen );
	tWriter.PutDword ( m_tSettings.m_iMinInfixLen );
	tWriter.PutByte ( m_tSettings.m_bHtmlStrip ? 1 : 0 );
	tWriter.PutString ( m_tSettings.m_sHtmlIndexAttrs.cstr () );
	tWriter.PutString ( m_tSettings.m_sHtmlRemoveElements.cstr () );
	tWriter.PutByte ( m_tSettings.m_bIndexExactWords ? 1 : 0 );
	tWriter.PutDword ( m_tSettings.m_eHitless );
	tWriter.PutDword ( SPH_HIT_FORMAT_PLAIN );

	// tokenizer
	assert ( m_pTokenizer );
	const CSphTokenizerSettings & tSettings = m_pTokenizer->GetSettings ();
	tWriter.PutByte ( tSettings.m_iType );
	tWriter.PutString ( tSettings.m_sCaseFolding.cstr () );
	tWriter.PutDword ( tSettings.m_iMinWordLen );
	tWriter.PutString ( tSettings.m_sSynonymsFile.cstr () );
	WriteFileInfo ( tWriter, m_pTokenizer->GetSynFileInfo () );
	tWriter.PutString ( tSettings.m_sBoundary.cstr () );
	tWriter.PutString ( tSettings.m_sIgnoreChars.cstr () );
	tWriter.PutDword ( tSettings.m_iNgramLen );
	tWriter.PutString ( tSettings.m_sNgramChars.cstr () );
	tWriter.PutString ( tSettings.m_sBlendChars.cstr () );

	// dictionary
	assert ( m_pDict );

	const CSphDictSettings & tDict = m_pDict->GetSettings ();
	tWriter.PutString ( tDict.m_sMorphology.cstr () );
	tWriter.PutString ( tDict.m_sStopwords.cstr () );

	const CSphVector <CSphSavedFile> & dSWFileInfos = m_pDict->GetStopwordsFileInfos ();
	tWriter.PutDword ( dSWFileInfos.GetLength () );
	ARRAY_FOREACH ( i, dSWFileInfos )
	{
		tWriter.PutString ( dSWFileInfos[i].m_sFilename.cstr () );
		WriteFileInfo ( tWriter, dSWFileInfos[i] );
	}

	const CSphSavedFile & tWFFileInfo = m_pDict->GetWordformsFileInfo ();
	tWriter.PutString ( tDict.m_sWordforms.cstr () );
	WriteFileInfo ( tWriter, tWFFileInfo );
	tWriter.PutDword ( tDict.m_iMinStemmingLen );

	// kill-list size
	tWriter.PutDword ( uKillListSize );

	// done
	tWriter.CloseFile ();
}


#if USE_WINDOWS
#undef rename
int rename ( const char * sOld, const char * sNew )
{
	if ( MoveFileEx ( sOld, sNew, MOVEFILE_REPLACE_EXISTING ) )
		return 0;
	errno = GetLastError();
	return -1;
}
#endif


void RtIndex_t::SaveMeta ( int iDiskChunks )
{
	// sanity check
	if ( m_iLockFD<0 )
		return;

	// write new meta
	CSphString sMeta, sMetaNew;
	sMeta.SetSprintf ( "%s.meta", m_sPath.cstr() );
	sMetaNew.SetSprintf ( "%s.meta.new", m_sPath.cstr() );

	CSphString sError;
	CSphWriter wrMeta;
	if ( !wrMeta.OpenFile ( sMetaNew, sError ) )
		sphDie ( "failed to serialize meta: %s", sError.cstr() ); // !COMMIT handle this gracefully
	wrMeta.PutDword ( META_HEADER_MAGIC );
	wrMeta.PutDword ( META_VERSION );
	wrMeta.PutDword ( iDiskChunks );
	wrMeta.PutDword ( m_tStats.m_iTotalDocuments );
	wrMeta.PutOffset ( m_tStats.m_iTotalBytes ); // FIXME? need PutQword ideally
	wrMeta.PutOffset ( m_iTID );
	wrMeta.CloseFile();

	// rename
	if ( ::rename ( sMetaNew.cstr(), sMeta.cstr() ) )
		sphDie ( "failed to rename meta (src=%s, dst=%s, errno=%d, error=%s)",
			sMetaNew.cstr(), sMeta.cstr(), errno, strerror(errno) ); // !COMMIT handle this gracefully
}


void RtIndex_t::SaveDiskChunk ()
{
	if ( !m_pSegments.GetLength() )
		return;

	MEMORY ( SPH_MEM_IDX_RT );

	// dump it
	CSphString sNewChunk;
	sNewChunk.SetSprintf ( "%s.%d", m_sPath.cstr(), m_pDiskChunks.GetLength() );
	SaveDiskData ( sNewChunk.cstr() );

	// bring new disk chunk online
	CSphIndex * pDiskChunk = LoadDiskChunk ( m_pDiskChunks.GetLength() );
	assert ( pDiskChunk );

	// save updated meta
	SaveMeta ( m_pDiskChunks.GetLength()+1 );

	// FIXME! add binlog cleanup here once we have binlogs

	// get exclusive lock again, gotta reset RAM chunk now
	Verify ( m_tRwlock.WriteLock() );
	ARRAY_FOREACH ( i, m_pSegments )
		SafeDelete ( m_pSegments[i] );
	m_pSegments.Reset();
	m_pDiskChunks.Add ( pDiskChunk );
	Verify ( m_tRwlock.Unlock() );
}


CSphIndex * RtIndex_t::LoadDiskChunk ( int iChunk )
{
	MEMORY ( SPH_MEM_IDX_DISK );

	CSphString sChunk, sError;
	sChunk.SetSprintf ( "%s.%d", m_sPath.cstr(), iChunk );

	CSphIndex * pDiskChunk = sphCreateIndexPhrase ( sChunk.cstr() );
	if ( pDiskChunk )
	{
		if ( !pDiskChunk->Prealloc ( false, sError ) || !pDiskChunk->Preread() )
			SafeDelete ( pDiskChunk );
	}
	if ( !pDiskChunk )
		sphDie ( "failed to load disk chunk '%s'", sChunk.cstr() ); // !COMMIT handle this gracefully

	return pDiskChunk;
}


bool RtIndex_t::Prealloc ( bool, CSphString & )
{
	MEMORY ( SPH_MEM_IDX_RT );

	// locking uber alles
	// in RT backed case, we just must be multi-threaded
	// so we simply lock here, and ignore Lock/Unlock hassle caused by forks
	assert ( m_iLockFD<0 );

	CSphString sLock;
	sLock.SetSprintf ( "%s.lock", m_sPath.cstr() );
	m_iLockFD = ::open ( sLock.cstr(), SPH_O_NEW, 0644 );
	if ( m_iLockFD<0 )
	{
		m_sLastError.SetSprintf ( "failed to open %s: %s", sLock.cstr(), strerror(errno) );
		return false;
	}
	if ( !sphLockEx ( m_iLockFD, false ) )
	{
		m_sLastError.SetSprintf ( "failed to lock %s: %s", sLock.cstr(), strerror(errno) );
		::close ( m_iLockFD );
		return false;
	}

	// check if we have a meta file (kinda-header)
	CSphString sMeta;
	sMeta.SetSprintf ( "%s.meta", m_sPath.cstr() );

	// no readable meta? no disk part yet
	if ( !sphIsReadable ( sMeta.cstr() ) )
		return true;

	// opened and locked, lets read
	CSphAutoreader rdMeta;
	if ( !rdMeta.Open ( sMeta, m_sLastError ) )
		return false;

	if ( rdMeta.GetDword()!=META_HEADER_MAGIC )
	{
		m_sLastError.SetSprintf ( "invalid meta file %s", sMeta.cstr() );
		return false;
	}
	DWORD uVersion = rdMeta.GetDword();
	if ( uVersion==0 || uVersion>META_VERSION )
	{
		m_sLastError.SetSprintf ( "%s is v.%d, binary is v.%d", sMeta.cstr(), uVersion, META_VERSION );
		return false;
	}
	const int iDiskChunks = rdMeta.GetDword();
	m_tStats.m_iTotalDocuments = rdMeta.GetDword();
	m_tStats.m_iTotalBytes = rdMeta.GetOffset();
	if ( uVersion>=2 )
		m_iTID = rdMeta.GetOffset();

	// load disk chunks, if any
	for ( int iChunk=0; iChunk<iDiskChunks; iChunk++ )
	{
		m_pDiskChunks.Add ( LoadDiskChunk ( iChunk ) );

		// tricky bit
		// outgoing match schema on disk chunk should be identical to our internal (!) schema
		if ( !m_tSchema.CompareTo ( m_pDiskChunks.Last()->GetMatchSchema(), m_sLastError ) )
			return false;
	}

	// load ram chunk
	return LoadRamChunk();
}


bool RtIndex_t::Preread ()
{
	// !COMMIT move disk chunks prereading here
	return true;
}


template < typename T, typename P >
static void SaveVector ( CSphWriter & tWriter, const CSphVector < T, P > & tVector )
{
	tWriter.PutDword ( tVector.GetLength() );
	if ( tVector.GetLength() )
		tWriter.PutBytes ( &tVector[0], tVector.GetLength()*sizeof(T) );
}


template < typename T, typename P >
static void LoadVector ( CSphAutoreader & tReader, CSphVector < T, P > & tVector )
{
	tVector.Resize ( tReader.GetDword() ); // FIXME? sanitize?
	if ( tVector.GetLength() )
		tReader.GetBytes ( &tVector[0], tVector.GetLength()*sizeof(T) );
}


bool RtIndex_t::SaveRamChunk ()
{
	MEMORY ( SPH_MEM_IDX_RT );

	CSphString sChunk, sNewChunk;
	sChunk.SetSprintf ( "%s.ram", m_sPath.cstr() );
	sNewChunk.SetSprintf ( "%s.ram.new", m_sPath.cstr() );
	m_tKlist.SaveToFile ( m_sPath.cstr() );

	CSphWriter wrChunk;
	if ( !wrChunk.OpenFile ( sNewChunk, m_sLastError ) )
		return false;

	wrChunk.PutDword ( USE_64BIT );
	wrChunk.PutDword ( RtSegment_t::m_iSegments );
	wrChunk.PutDword ( m_pSegments.GetLength() );

	// no locks here, because it's only intended to be called from dtor
	ARRAY_FOREACH ( iSeg, m_pSegments )
	{
		const RtSegment_t * pSeg = m_pSegments[iSeg];
		wrChunk.PutDword ( pSeg->m_iTag );
		SaveVector ( wrChunk, pSeg->m_dWords );
#if COMPRESSED_WORDLIST
		wrChunk.PutDword ( pSeg->m_dWordCheckpoints.GetLength() );
		ARRAY_FOREACH ( i, pSeg->m_dWordCheckpoints )
		{
			wrChunk.PutOffset ( pSeg->m_dWordCheckpoints[i].m_iOffset );
			wrChunk.PutOffset ( pSeg->m_dWordCheckpoints[i].m_uWordID );
		}
#endif
		SaveVector ( wrChunk, pSeg->m_dDocs );
		SaveVector ( wrChunk, pSeg->m_dHits );
		wrChunk.PutDword ( pSeg->m_iRows );
		wrChunk.PutDword ( pSeg->m_iAliveRows );
		SaveVector ( wrChunk, pSeg->m_dRows );
		SaveVector ( wrChunk, pSeg->m_dKlist );
		SaveVector ( wrChunk, pSeg->m_dStrings );
	}

	wrChunk.CloseFile();
	if ( wrChunk.IsError() )
		return false;

	// rename
	if ( ::rename ( sNewChunk.cstr(), sChunk.cstr() ) )
		sphDie ( "failed to rename ram chunk (src=%s, dst=%s, errno=%d, error=%s)",
			sNewChunk.cstr(), sChunk.cstr(), errno, strerror(errno) ); // !COMMIT handle this gracefully

	return true;
}


bool RtIndex_t::LoadRamChunk ()
{
	MEMORY ( SPH_MEM_IDX_RT );

	CSphString sChunk;
	sChunk.SetSprintf ( "%s.ram", m_sPath.cstr() );

	if ( !sphIsReadable ( sChunk.cstr(), &m_sLastError ) )
		return true;

	m_tKlist.LoadFromFile ( m_sPath.cstr() );

	CSphAutoreader rdChunk;
	if ( !rdChunk.Open ( sChunk, m_sLastError ) )
		return false;

	bool bId64 = ( rdChunk.GetDword()!=0 );
	if ( bId64!=USE_64BIT )
	{
		m_sLastError.SetSprintf ( "ram chunk dumped by %s binary; this binary is %s",
			bId64 ? "id64" : "id32",
			USE_64BIT ? "id64" : "id32" );
		return false;
	}

	int iSegmentSeq = rdChunk.GetDword();
	m_pSegments.Resize ( rdChunk.GetDword() ); // FIXME? sanitize

	ARRAY_FOREACH ( iSeg, m_pSegments )
	{
		RtSegment_t * pSeg = new RtSegment_t ();
		m_pSegments[iSeg] = pSeg;

		pSeg->m_iTag = rdChunk.GetDword ();
		LoadVector ( rdChunk, pSeg->m_dWords );
#if COMPRESSED_WORDLIST
		pSeg->m_dWordCheckpoints.Resize ( rdChunk.GetDword() );
		ARRAY_FOREACH ( i, pSeg->m_dWordCheckpoints )
		{
			pSeg->m_dWordCheckpoints[i].m_iOffset = (int)rdChunk.GetOffset();
			pSeg->m_dWordCheckpoints[i].m_uWordID = (SphWordID_t)rdChunk.GetOffset();
		}
#endif
		LoadVector ( rdChunk, pSeg->m_dDocs );
		LoadVector ( rdChunk, pSeg->m_dHits );
		pSeg->m_iRows = rdChunk.GetDword();
		pSeg->m_iAliveRows = rdChunk.GetDword();
		LoadVector ( rdChunk, pSeg->m_dRows );
		LoadVector ( rdChunk, pSeg->m_dKlist );
		LoadVector ( rdChunk, pSeg->m_dStrings );
	}

	RtSegment_t::m_iSegments = iSegmentSeq;
	return !rdChunk.GetErrorFlag();
}

//////////////////////////////////////////////////////////////////////////
// SEARCHING
//////////////////////////////////////////////////////////////////////////

struct RtQword_t : public ISphQword
{
	friend struct RtIndex_t;
	friend struct RtQwordSetup_t;

protected:
	RtDocReader_t *		m_pDocReader;
	CSphMatch			m_tMatch;

	DWORD				m_uNextHit;
	RtHitReader2_t		m_tHitReader;

	RtSegment_t *		m_pSeg;

public:
	RtQword_t ()
		: m_pDocReader ( NULL )
		, m_uNextHit ( 0 )
		, m_pSeg ( NULL )
	{
		m_tMatch.Reset ( 0 );
	}

	virtual ~RtQword_t ()
	{
		SafeDelete ( m_pDocReader );
	}

	virtual const CSphMatch & GetNextDoc ( DWORD * )
	{
		for ( ;; )
		{
			const RtDoc_t * pDoc = m_pDocReader->UnzipDoc();
			if ( !pDoc )
			{
				m_tMatch.m_iDocID = 0;
				return m_tMatch;
			}

			if ( m_pSeg->m_dKlist.BinarySearch ( pDoc->m_uDocID ) )
				continue;

			m_tMatch.m_iDocID = pDoc->m_uDocID;
			m_uFields = pDoc->m_uFields;
			m_uMatchHits = pDoc->m_uHits;
			m_iHitlistPos = (uint64_t(pDoc->m_uHits)<<32) + pDoc->m_uHit;
			return m_tMatch;
		}
	}

	virtual void SeekHitlist ( SphOffset_t uOff )
	{
		int iHits = (int)(uOff>>32);
		if ( iHits==1 )
		{
			m_uNextHit = DWORD(uOff);
		} else
		{
			m_uNextHit = 0;
			m_tHitReader.Seek ( DWORD(uOff), iHits );
		}
	}

	virtual DWORD GetNextHit ()
	{
		if ( m_uNextHit==0 )
		{
			return m_tHitReader.UnzipHit();

		} else if ( m_uNextHit==0xffffffffUL )
		{
			return 0;

		} else
		{
			DWORD uRes = m_uNextHit;
			m_uNextHit = 0xffffffffUL;
			return uRes;
		}
	}
};


struct RtQwordSetup_t : ISphQwordSetup
{
	RtSegment_t *		m_pSeg;

	virtual ISphQword *	QwordSpawn ( const XQKeyword_t & ) const;
	virtual bool		QwordSetup ( ISphQword * pQword ) const;
};


ISphQword * RtQwordSetup_t::QwordSpawn ( const XQKeyword_t & ) const
{
	return new RtQword_t ();
}


bool RtQwordSetup_t::QwordSetup ( ISphQword * pQword ) const
{
	RtQword_t * pMyWord = dynamic_cast<RtQword_t*> ( pQword );
	if ( !pMyWord )
		return false;

	const RtIndex_t * pIndex = dynamic_cast< const RtIndex_t * > ( m_pIndex );
	if ( !pIndex )
		return false;

	return pIndex->RtQwordSetup ( pMyWord, m_pSeg );
}


bool RtIndex_t::EarlyReject ( CSphQueryContext * pCtx, CSphMatch & tMatch ) const
{
	// early calc might be needed even when we do not have a filter
	if ( pCtx->m_bEarlyLookup )
		CopyDocinfo ( tMatch, FindDocinfo ( (RtSegment_t*)pCtx->m_pIndexData, tMatch.m_iDocID ) );

	pCtx->EarlyCalc ( tMatch );
	return pCtx->m_pFilter ? !pCtx->m_pFilter->Eval ( tMatch ) : false;
}


void RtIndex_t::CopyDocinfo ( CSphMatch & tMatch, const DWORD * pFound ) const
{
	if ( !pFound )
		return;

	// setup static pointer
	assert ( DOCINFO2ID(pFound)==tMatch.m_iDocID );
	tMatch.m_pStatic = DOCINFO2ATTRS(pFound);

	// FIXME? implement overrides
}


const CSphRowitem * RtIndex_t::FindDocinfo ( const RtSegment_t * pSeg, SphDocID_t uDocID ) const
{
	// FIXME! move to CSphIndex, and implement hashing
	if ( pSeg->m_dRows.GetLength()==0 )
		return NULL;

	int iStride = m_iStride;
	int iStart = 0;
	int iEnd = pSeg->m_iRows-1;
	assert ( iStride==( DOCINFO_IDSIZE + m_tSchema.GetRowSize() ) );

	const CSphRowitem * pStorage = &pSeg->m_dRows[0];
	const CSphRowitem * pFound = NULL;

	if ( uDocID==DOCINFO2ID ( &pStorage [ iStart*iStride ] ) )
	{
		pFound = &pStorage [ iStart*iStride ];

	} else if ( uDocID==DOCINFO2ID ( &pStorage [ iEnd*iStride ] ) )
	{
		pFound = &pStorage [ iEnd*iStride ];

	} else
	{
		while ( iEnd-iStart>1 )
		{
			// check if nothing found
			if (
				uDocID < DOCINFO2ID ( &pStorage [ iStart*iStride ] ) ||
				uDocID > DOCINFO2ID ( &pStorage [ iEnd*iStride ] ) )
				break;
			assert ( uDocID > DOCINFO2ID ( &pStorage [ iStart*iStride ] ) );
			assert ( uDocID < DOCINFO2ID ( &pStorage [ iEnd*iStride ] ) );

			int iMid = iStart + (iEnd-iStart)/2;
			if ( uDocID==DOCINFO2ID ( &pStorage [ iMid*iStride ] ) )
			{
				pFound = &pStorage [ iMid*iStride ];
				break;
			}
			if ( uDocID<DOCINFO2ID ( &pStorage [ iMid*iStride ] ) )
				iEnd = iMid;
			else
				iStart = iMid;
		}
	}

	return pFound;
}

// WARNING, setup is pretty tricky
// for RT queries, we setup qwords several times
// first pass (with NULL segment arg) should sum all stats over all segments
// others passes (with non-NULL segments) should setup specific segment (including local stats)
bool RtIndex_t::RtQwordSetupSegment ( RtQword_t * pQword, RtSegment_t * pCurSeg, bool bSetup )
{
	if ( !pCurSeg )
		return false;

	SphWordID_t uWordID = pQword->m_iWordID;
	RtWordReader_t tReader ( pCurSeg );

#if COMPRESSED_WORDLIST

	// position reader to the right checkpoint
	const CSphVector<RtWordCheckpoint_t> & dCheckpoints = pCurSeg->m_dWordCheckpoints;
	if ( dCheckpoints.GetLength() )
	{
		if ( dCheckpoints[0].m_uWordID > uWordID )
		{
			tReader.m_pMax = tReader.m_pCur + dCheckpoints[0].m_iOffset;

		} else if ( dCheckpoints.Last().m_uWordID<=uWordID )
		{
			tReader.m_pCur += dCheckpoints.Last().m_iOffset;

		} else
		{
			int L = 0;
			int R = dCheckpoints.GetLength()-1;
			while ( L+1<R )
			{
				int M = L + (R-L)/2;
				if ( uWordID < dCheckpoints[M].m_uWordID )
					R = M;
				else if ( uWordID > dCheckpoints[M].m_uWordID )
					L = M;
				else
				{
					L = M;
					break;
				}
			}

			assert ( dCheckpoints[L].m_uWordID<=uWordID );
			if ( L < dCheckpoints.GetLength()-1 )
			{
				assert ( dCheckpoints[L+1].m_uWordID > uWordID );
				tReader.m_pMax = tReader.m_pCur + dCheckpoints[L+1].m_iOffset;
			}
			tReader.m_pCur += dCheckpoints[L].m_iOffset;
		}
	}

	// find the word between checkpoints
	const RtWord_t * pWord = NULL;
	while ( ( pWord = tReader.UnzipWord() )!=NULL )
	{
		if ( pWord->m_uWordID==uWordID )
		{
			pQword->m_iDocs += pWord->m_uDocs;
			pQword->m_iHits += pWord->m_uHits;
			if ( bSetup )
			{
				SafeDelete ( pQword->m_pDocReader );
				pQword->m_pDocReader = new RtDocReader_t ( pCurSeg, *pWord );
				pQword->m_tHitReader.m_pBase = NULL;
				if ( pCurSeg->m_dHits.GetLength() )
					pQword->m_tHitReader.m_pBase = &pCurSeg->m_dHits[0];
				pQword->m_pSeg = pCurSeg;
			}
			return true;

		} else if ( pWord->m_uWordID > uWordID )
			return false;
	}
	return false;

#else // !COMPRESSED_WORDLIST

	const RtWord_t * pWord = pCurSeg->m_dWords.BinarySearch ( bind ( &RtWord_t::m_uWordID ), uWordID );
	if ( pWord )
	{
		pQword->m_iDocs += pWord->m_uDocs;
		pQword->m_iHits += pWord->m_uHits;
		if ( bSetup )
		{
			pQword->m_pDocReader = new RtDocReader_t ( pCurSeg, *pWord );
			pQword->m_tHitReader.m_pBase = NULL;
			if ( pCurSeg->m_dHits.GetLength() )
				pQword->m_tHitReader.m_pBase = &pCurSeg->m_dHits[0];
			pQword->m_pSeg = pCurSeg;
		}
	}
	return pWord!=0;
#endif
}

bool RtIndex_t::RtQwordSetup ( RtQword_t * pQword, RtSegment_t * pSeg ) const
{
	// segment-specific setup pass
	if ( pSeg )
		return RtQwordSetupSegment ( pQword, pSeg, true );

	// stat-only pass
	// loop all segments, gather stats, do not setup anything
	assert ( !pSeg );
	pQword->m_iDocs = 0;
	pQword->m_iHits = 0;

	// we care about the results anyway though
	// because if all (!) segments miss this word, we must notify the caller, right?
	bool bRes = true;
	ARRAY_FOREACH ( i, m_pSegments )
		bRes &= RtQwordSetupSegment ( pQword, m_pSegments[i], false );

	// sanity check
	assert ( !( bRes==true && pQword->m_iDocs==0 ) );
	return bRes;
}

static void AddKillListFilter ( CSphVector<CSphFilterSettings> * pExtra, const SphAttr_t * pKillList, int nEntries )
{
	assert ( nEntries && pKillList && pExtra );
	CSphFilterSettings & tFilter = pExtra->Add();
	tFilter.m_bExclude = true;
	tFilter.m_eType = SPH_FILTER_VALUES;
	tFilter.m_uMinValue = pKillList[0];
	tFilter.m_uMaxValue = pKillList[nEntries-1];
	tFilter.m_sAttrName = "@id";
	tFilter.SetExternalValues ( pKillList, nEntries );
}

// FIXME! missing MVA, index_exact_words support
// FIXME? missing enable_star, legacy match modes support
// FIXME? any chance to factor out common backend agnostic code?
// FIXME? do we need to support pExtraFilters?
#ifndef NDEBUG
bool RtIndex_t::MultiQuery ( const CSphQuery * pQuery, CSphQueryResult * pResult, int iSorters, ISphMatchSorter ** ppSorters, const CSphVector<CSphFilterSettings> *, int iTag ) const
#else
bool RtIndex_t::MultiQuery ( const CSphQuery * pQuery, CSphQueryResult * pResult, int iSorters, ISphMatchSorter ** ppSorters, const CSphVector<CSphFilterSettings> *, int ) const
#endif
{
	// FIXME! too early (how low can you go?)
	m_tRwlock.ReadLock ();

	assert ( pQuery );
	assert ( pResult );
	assert ( ppSorters );
	assert ( iTag==0 );

	// empty index, empty result
	if ( !m_pSegments.GetLength() && !m_pDiskChunks.GetLength() )
	{
		pResult->m_iQueryTime = 0;
		m_tRwlock.Unlock ();
		return true;
	}

	MEMORY ( SPH_MEM_IDX_RT_MULTY_QUERY );

	// start counting
	pResult->m_iQueryTime = 0;
	int64_t tmQueryStart = sphMicroTimer();

	// force ext2 mode for them
	// FIXME! eliminate this const breakage
	const_cast<CSphQuery*> ( pQuery )->m_eMode = SPH_MATCH_EXTENDED2;

	////////////////////
	// search RAM chunk
	////////////////////

	if ( m_pSegments.GetLength() )
	{
		// setup calculations and result schema
		CSphQueryContext tCtx;
		if ( !tCtx.SetupCalc ( pResult, ppSorters[0]->GetSchema(), m_tOutboundSchema, NULL ) )
		{
			m_tRwlock.Unlock ();
			return false;
		}

		// setup search terms
		RtQwordSetup_t tTermSetup;
		tTermSetup.m_pDict = m_pDict;
		tTermSetup.m_pIndex = this;
		tTermSetup.m_eDocinfo = m_tSettings.m_eDocinfo;
		tTermSetup.m_iDynamicRowitems = pResult->m_tSchema.GetDynamicSize();
		if ( pQuery->m_uMaxQueryMsec>0 )
			tTermSetup.m_iMaxTimer = sphMicroTimer() + pQuery->m_uMaxQueryMsec*1000; // max_query_time
		tTermSetup.m_pWarning = &pResult->m_sWarning;
		tTermSetup.m_pSeg = NULL;
		tTermSetup.m_pCtx = &tCtx;

		// bind weights
		tCtx.BindWeights ( pQuery, m_tOutboundSchema );

		// parse query
		XQQuery_t tParsed;
		if ( !sphParseExtendedQuery ( tParsed, pQuery->m_sQuery.cstr(), GetTokenizer(), &m_tOutboundSchema, m_pDict ) )
		{
			pResult->m_sError = tParsed.m_sParseError;
			m_tRwlock.Unlock ();
			return false;
		}

		// setup query
		// must happen before index-level reject, in order to build proper keyword stats
		CSphScopedPtr<ISphRanker> pRanker ( sphCreateRanker ( tParsed.m_pRoot, pQuery->m_eRanker, pResult, tTermSetup ) );
		if ( !pRanker.Ptr() )
		{
			m_tRwlock.Unlock ();
			return false;
		}

		// setup filters
		// FIXME! setup filters MVA pool
		bool bFullscan = ( pQuery->m_eMode==SPH_MATCH_FULLSCAN || pQuery->m_sQuery.IsEmpty() );
		if ( !tCtx.CreateFilters ( bFullscan, &pQuery->m_dFilters, pResult->m_tSchema, NULL, pResult->m_sError ) )
		{
			m_tRwlock.Unlock ();
			return false;
		}

		// FIXME! OPTIMIZE! check if we can early reject the whole index

		// setup lookup
		// do pre-filter lookup as needed
		// do pre-sort lookup in all cases
		// post-sort lookup is complicated (because of many segments)
		// pre-sort lookup is cheap now anyway, and almost always anyway
		// (except maybe by stupid relevance-sorting-only benchmarks!!)
		tCtx.m_bEarlyLookup = ( pQuery->m_dFilters.GetLength() || tCtx.m_dEarlyCalc.GetLength() );
		tCtx.m_bLateLookup = true;

		// FIXME! setup sorters vs. MVA
		for ( int i=0; i<iSorters; i++ )
			(ppSorters[i])->SetMVAPool ( NULL );

		// FIXME! setup overrides

		// do searching
		bool bRandomize = ppSorters[0]->m_bRandomize;
		int iCutoff = pQuery->m_iCutoff;
		if ( iCutoff<=0 )
			iCutoff = -1;

		if ( bFullscan )
		{
			// full scan
			// FIXME? OPTIMIZE? add shortcuts here too?
			CSphMatch tMatch;
			tMatch.Reset ( pResult->m_tSchema.GetDynamicSize() );
			tMatch.m_iWeight = 1;

			int iCutoff = pQuery->m_iCutoff;
			if ( iCutoff<=0 )
				iCutoff = -1;

			ARRAY_FOREACH ( iSeg, m_pSegments )
			{
				RtRowIterator_t tIt ( m_pSegments[iSeg], m_iStride, false, GetAccum() );
				for ( ;; )
				{
					const CSphRowitem * pRow = tIt.GetNextAliveRow();
					if ( !pRow )
						break;

					tMatch.m_iDocID = DOCINFO2ID(pRow);
					tMatch.m_pStatic = DOCINFO2ATTRS(pRow); // FIXME! overrides

					tCtx.EarlyCalc ( tMatch );
					if ( tCtx.m_pFilter && !tCtx.m_pFilter->Eval ( tMatch ) )
						continue;

					tCtx.LateCalc ( tMatch );

					// storing segment in matches tag for finding strings attrs offset later, biased against default zero
					tMatch.m_iTag = iSeg+1;

					bool bNewMatch = false;
					for ( int iSorter=0; iSorter<iSorters; iSorter++ )
						bNewMatch |= ppSorters[iSorter]->Push ( tMatch );

					// handle cutoff
					if ( bNewMatch )
						if ( --iCutoff==0 )
							break;
				}

				if ( iCutoff==0 )
					break;
			}

		} else
		{
			// query matching
			ARRAY_FOREACH ( iSeg, m_pSegments )
			{
				tTermSetup.m_pSeg = m_pSegments[iSeg];
				pRanker->Reset ( tTermSetup );

				// for lookups to work
				tCtx.m_pIndexData = m_pSegments[iSeg];

				CSphMatch * pMatch = pRanker->GetMatchesBuffer();
				for ( ;; )
				{
					int iMatches = pRanker->GetMatches ( tCtx.m_iWeights, tCtx.m_dWeights );
					if ( iMatches<=0 )
						break;

					for ( int i=0; i<iMatches; i++ )
					{
						if ( tCtx.m_bLateLookup )
							CopyDocinfo ( pMatch[i], FindDocinfo ( m_pSegments[iSeg], pMatch[i].m_iDocID ) );
						tCtx.LateCalc ( pMatch[i] );

						if ( bRandomize )
							pMatch[i].m_iWeight = ( sphRand() & 0xffff );

						if ( tCtx.m_pWeightFilter && !tCtx.m_pWeightFilter->Eval ( pMatch[i] ) )
							continue;

						// storing segment in matches tag for finding strings attrs offset later, biased against default zero
						pMatch[i].m_iTag = iSeg+1;

						bool bNewMatch = false;
						for ( int iSorter=0; iSorter<iSorters; iSorter++ )
							bNewMatch |= ppSorters[iSorter]->Push ( pMatch[i] );

						if ( bNewMatch )
							if ( --iCutoff==0 )
								break;
					}

					if ( iCutoff==0 )
					{
						iSeg = m_pSegments.GetLength();
						break;
					}
				}
			}
		}
	}

	// FIXME! slow disk searches could lock out concurrent writes for too long
	// FIXME! each result will point to its own MVA and string pools
	// !COMMIT need to setup disk K-list here

	//////////////////////
	// search disk chunks
	//////////////////////

	bool m_bKlistLocked = false;
	CSphVector<CSphFilterSettings> dExtra;
	// first, collect all the killlists into a vector
	for ( int iChunk = m_pDiskChunks.GetLength()-1; iChunk>=0; iChunk-- )
	{
		const int iOldLength = dExtra.GetLength();
		if ( iChunk==m_pDiskChunks.GetLength()-1 )
		{
			// For the topmost chunk we add the killlist from the ram-index
			m_tKlist.Flush();
			m_tKlist.KillListLock();
			if ( m_tKlist.GetKillListSize() )
			{
				// we don't lock in vain...
				m_bKlistLocked = true;
				AddKillListFilter ( &dExtra, m_tKlist.GetKillList(), m_tKlist.GetKillListSize() );
			} else
				m_tKlist.KillListUnlock();
		} else
		{
			const CSphIndex * pDiskChunk = m_pDiskChunks[iChunk+1];
			if ( pDiskChunk->GetKillListSize () )
				AddKillListFilter ( &dExtra, pDiskChunk->GetKillList(), pDiskChunk->GetKillListSize() );
		}

		if ( dExtra.GetLength()==iOldLength )
			dExtra.Add();
	}

	assert ( dExtra.GetLength()==m_pDiskChunks.GetLength() );
	CSphVector<const BYTE *> dDiskStrings ( m_pDiskChunks.GetLength() );
	ARRAY_FOREACH ( iChunk, m_pDiskChunks )
	{
		CSphQueryResult tChunkResult;
		// storing index in matches tag for finding strings attrs offset later, biased against default zero and segments
		const int iTag = m_pSegments.GetLength()+iChunk+1;
		if ( !m_pDiskChunks[iChunk]->MultiQuery ( pQuery, &tChunkResult, iSorters, ppSorters, &dExtra, iTag ) )
		{
			// FIXME? maybe handle this more gracefully (convert to a warning)?
			pResult->m_sError = tChunkResult.m_sError;
			m_tRwlock.Unlock ();
			if ( m_bKlistLocked )
				m_tKlist.KillListUnlock();
			return false;
		}

		dDiskStrings[iChunk] = tChunkResult.m_pStrings;
		dExtra.Pop();
	}

	if ( m_bKlistLocked )
		m_tKlist.KillListUnlock();

	//////////////////////
	// fixing string offset and data in resulting matches
	//////////////////////

	CSphVector<CSphAttrLocator> dStringGetLoc;
	CSphVector<CSphAttrLocator> dStringSetLoc;
	for ( int i=0; i<pResult->m_tSchema.GetAttrsCount(); i++ )
	{
		const CSphColumnInfo & tSetInfo = pResult->m_tSchema.GetAttr(i);
		if ( tSetInfo.m_eAttrType==SPH_ATTR_STRING )
		{
			const int iInLocator = m_tSchema.GetAttrIndex ( tSetInfo.m_sName.cstr() );
			assert ( iInLocator>=0 );

			dStringGetLoc.Add ( m_tSchema.GetAttr ( iInLocator ).m_tLocator );
			dStringSetLoc.Add ( tSetInfo.m_tLocator );
		}

		assert ( tSetInfo.m_eAttrType!=SPH_ATTR_STRING ||
			( tSetInfo.m_eAttrType==SPH_ATTR_STRING && tSetInfo.m_tLocator.m_bDynamic ) );
	}
	if ( dStringGetLoc.GetLength() )
	{
		CSphTightVector<BYTE> & dStorage = pResult->m_dStrStorage; // for shortness
		if ( !dStorage.GetLength() ) // handle dummy offset
			dStorage.Add ( 0 );

		for ( int iSorter=0; iSorter<iSorters; iSorter++ )
		{
			ISphMatchSorter * pSorter = ppSorters[iSorter];

			const int iMatchesCount = pSorter->GetLength();
			CSphMatch * pMatches = pSorter->First();

			for ( int i=0; i<iMatchesCount; i++ )
			{
				CSphMatch & tMatch = pMatches[i];

				const int iSegCount = m_pSegments.GetLength();
				assert ( tMatch.m_iTag>=1 && tMatch.m_iTag<iSegCount+dDiskStrings.GetLength()+1 );
				assert ( tMatch.m_pDynamic );

				const int iStrSrc = tMatch.m_iTag-1;
				const BYTE * pBase = iStrSrc < iSegCount ? &m_pSegments[iStrSrc]->m_dStrings[0] : dDiskStrings[ iStrSrc-iSegCount ];
				int iRange = 0;
				if ( iStrSrc < iSegCount )
					iRange = m_pSegments[iStrSrc]->m_dStrings.GetLength();

				ARRAY_FOREACH ( i, dStringGetLoc )
				{
					const SphAttr_t tOff = tMatch.GetAttr ( dStringGetLoc[i] );
					if ( tOff>0 ) // have to fix up only exists attribute
					{
						assert ( tOff<( I64C(1)<<32 ) ); // should be 32 bit offset
						assert ( iRange==0 || (int)tOff<iRange );

						const int iDstOff = CopyPackedString ( pBase + tOff, dStorage );
						tMatch.SetAttr ( dStringSetLoc[i], iDstOff );
					} else
						tMatch.SetAttr ( dStringSetLoc[i], 0 );
				}
			}
		}
	}

	// FIXME! mva pools ptrs
	pResult->m_pMva = NULL;
	pResult->m_pStrings = pResult->m_dStrStorage.Begin();

	// query timer
	pResult->m_iQueryTime = int ( ( sphMicroTimer()-tmQueryStart )/1000 );
	m_tRwlock.Unlock ();
	return true;
}

bool RtIndex_t::MultiQueryEx ( int iQueries, const CSphQuery * ppQueries, CSphQueryResult ** ppResults, ISphMatchSorter ** ppSorters, const CSphVector<CSphFilterSettings> * pExtraFilters, int iTag ) const
{
	// FIXME! OPTIMIZE! implement common subtree cache here
	bool bResult = false;
	for ( int i=0; i<iQueries; i++ )
		if ( MultiQuery ( &ppQueries[i], ppResults[i], 1, &ppSorters[i], pExtraFilters, iTag ) )
			bResult = true;
		else
			ppResults[i]->m_iMultiplier = -1;

	return bResult;
}

bool RtIndex_t::GetKeywords ( CSphVector<CSphKeywordInfo> & dKeywords, const char * sQuery, bool bGetStats, CSphString & sError ) const
{
	m_tRwlock.ReadLock(); // this is actually needed only if they want stats

	RtQword_t tQword;
	CSphString sBuffer ( sQuery );

	CSphScopedPtr<ISphTokenizer> pTokenizer ( m_pTokenizer->Clone ( false ) ); // avoid race
	pTokenizer->SetBuffer ( (BYTE *)sBuffer.cstr(), sBuffer.Length() );

	while ( BYTE * pToken = pTokenizer->GetToken() )
	{
		const char * sToken = (const char *)pToken;
		CSphString sWord ( sToken );
		SphWordID_t iWord = m_pDict->GetWordID ( pToken );
		if ( iWord )
		{
			CSphKeywordInfo & tInfo = dKeywords.Add();
			tInfo.m_sTokenized = sWord;
			tInfo.m_sNormalized = sToken;
			tInfo.m_iDocs = 0;
			tInfo.m_iHits = 0;

			if ( !bGetStats )
				continue;

			tQword.m_iWordID = iWord;
			tQword.m_iDocs = 0;
			tQword.m_iHits = 0;
			ARRAY_FOREACH ( iSeg, m_pSegments )
				RtQwordSetupSegment ( &tQword, m_pSegments[iSeg], false );

			tInfo.m_iDocs = tQword.m_iDocs;
			tInfo.m_iHits = tQword.m_iHits;
		}
	}

	// get stats from disk chunks too
	if ( bGetStats )
		ARRAY_FOREACH ( iChunk, m_pDiskChunks )
	{
		CSphVector<CSphKeywordInfo> dKeywords2;
		if ( !m_pDiskChunks[iChunk]->GetKeywords ( dKeywords2, sQuery, bGetStats, sError ) )
		{
			m_tRwlock.Unlock();
			return false;
		}

		if ( dKeywords.GetLength()!=dKeywords2.GetLength() )
		{
			sError.SetSprintf ( "INTERNAL ERROR: keyword count mismatch (ram=%d, disk[%d]=%d)",
				dKeywords.GetLength(), iChunk, dKeywords2.GetLength() );
			m_tRwlock.Unlock ();
			break;
		}

		ARRAY_FOREACH ( i, dKeywords )
		{
			if ( dKeywords[i].m_sTokenized!=dKeywords2[i].m_sTokenized )
			{
				sError.SetSprintf ( "INTERNAL ERROR: tokenized keyword mismatch (n=%d, ram=%s, disk[%d]=%s)",
					i, dKeywords[i].m_sTokenized.cstr(), iChunk, dKeywords2[i].m_sTokenized.cstr() );
				m_tRwlock.Unlock ();
				break;
			}

			if ( dKeywords[i].m_sNormalized!=dKeywords2[i].m_sNormalized )
			{
				sError.SetSprintf ( "INTERNAL ERROR: normalized keyword mismatch (n=%d, ram=%s, disk[%d]=%s)",
					i, dKeywords[i].m_sTokenized.cstr(), iChunk, dKeywords2[i].m_sTokenized.cstr() );
				m_tRwlock.Unlock ();
				break;
			}

			dKeywords[i].m_iDocs += dKeywords2[i].m_iDocs;
			dKeywords[i].m_iHits += dKeywords2[i].m_iHits;
		}
	}

	m_tRwlock.Unlock();
	return true;
}

// FIXME! might be inconsistent in case disk chunk update fails
int RtIndex_t::UpdateAttributes ( const CSphAttrUpdate & tUpd, int iIndex, CSphString & sError )
{
	// check if we have to
	assert ( tUpd.m_dDocids.GetLength()==tUpd.m_dRowOffset.GetLength() );
	if ( !tUpd.m_dDocids.GetLength() )
		return 0;

	// remap update schema to index schema
	CSphVector<CSphAttrLocator> dLocators;
	ARRAY_FOREACH ( i, tUpd.m_dAttrs )
	{
		int iIndex = m_tSchema.GetAttrIndex ( tUpd.m_dAttrs[i].m_sName.cstr() );
		if ( iIndex<0 )
		{
			sError.SetSprintf ( "attribute '%s' not found", tUpd.m_dAttrs[i].m_sName.cstr() );
			return -1;
		}

		// forbid updates on non-int columns
		const CSphColumnInfo & tCol = m_tSchema.GetAttr(iIndex);
		if (!( tCol.m_eAttrType==SPH_ATTR_BOOL || tCol.m_eAttrType==SPH_ATTR_INTEGER || tCol.m_eAttrType==SPH_ATTR_TIMESTAMP ))
		{
			sError.SetSprintf ( "attribute '%s' can not be updated (must be boolean, integer, or timestamp)", tUpd.m_dAttrs[i].m_sName.cstr() );
			return -1;
		}

		dLocators.Add ( tCol.m_tLocator );
	}
	assert ( dLocators.GetLength()==tUpd.m_dAttrs.GetLength() );

	// get that lock
	m_tRwlock.WriteLock();

	// check if we are empty
	if ( !m_pSegments.GetLength() && !m_pDiskChunks.GetLength() )
	{
		m_tRwlock.Unlock();
		return true;
	}

	// do the update
	int iUpdated = 0;
	DWORD uUpdateMask = 0;

	int iFirst = ( iIndex<0 ) ? 0 : iIndex;
	int iLast = ( iIndex<0 ) ? tUpd.m_dDocids.GetLength() : iIndex+1;
	for ( int iUpd=iFirst; iUpd<iLast; iUpd++ )
	{
		// search segments first
		bool bUpdated = false;
		ARRAY_FOREACH ( iSeg, m_pSegments )
		{
			CSphRowitem * pRow = const_cast<CSphRowitem*> ( m_pSegments[iSeg]->FindAliveRow ( tUpd.m_dDocids[iUpd] ) );
			if ( !pRow )
				continue;

			assert ( DOCINFO2ID(pRow)==tUpd.m_dDocids[iUpd] );
			pRow = DOCINFO2ATTRS(pRow);

			int iPos = tUpd.m_dRowOffset[iUpd];
			ARRAY_FOREACH ( iCol, tUpd.m_dAttrs )
			{
				// plain update
				SphAttr_t uValue = tUpd.m_dPool[iPos];
				sphSetRowAttr ( pRow, dLocators[iCol], uValue );

				iPos++;
				uUpdateMask |= ATTRS_UPDATED;
			}

			bUpdated = true;
			iUpdated++;
		}
		if ( bUpdated )
			continue;

		// check disk K-list now
		// FIXME! optimize away flush
		m_tKlist.Flush();
		m_tKlist.KillListLock();
		const SphAttr_t uRef = tUpd.m_dDocids[iUpd];
		bUpdated = ( sphBinarySearch ( m_tKlist.GetKillList(), m_tKlist.GetKillList() + m_tKlist.GetKillListSize(), uRef )!=NULL );
		m_tKlist.KillListUnlock();
		if ( bUpdated )
			continue;

		// finally, try disk chunks
		for ( int iChunk = m_pDiskChunks.GetLength()-1; iChunk>=0; iChunk-- )
		{
			// run just this update
			// FIXME! might be inefficient in case of big batches (redundant allocs in disk update)
			int iRes = m_pDiskChunks[iChunk]->UpdateAttributes ( tUpd, iUpd, sError );

			// early-return errors
			if ( iRes<0 )
			{
				m_tRwlock.Unlock();
				return -1;
			}

			// update stats
			iUpdated += iRes;

			// we only need to update the most fresh chunk
			if ( iRes>0 )
				break;
		}
	}

	// binlog the update!
	assert ( iIndex<0 );
	g_pBinlog->NotifyUpdateAttributes ( m_sIndexName.cstr(), tUpd );

	// all done
	m_uAttrsStatus |= uUpdateMask;
	m_tRwlock.Unlock ();
	return iUpdated;
}

//////////////////////////////////////////////////////////////////////////
// BINLOG
//////////////////////////////////////////////////////////////////////////

template <typename T>
static int GetIndexByName ( const CSphVector<T> & dArray, const char * sName )
{
	ARRAY_FOREACH ( i, dArray )
		if ( dArray[i].m_sName==sName )
			return i;

	return -1;
}

static RtIndex_t * GetIndexByName ( const CSphVector< ISphRtIndex * > & dIndices, const char * sName )
{
	ARRAY_FOREACH ( i, dIndices )
		if ( strcmp ( dIndices[i]->GetName(), sName )==0 )
			return (RtIndex_t *)dIndices[i];

	return NULL;
}

static CSphString MakeBinlogName ( const char * sPath, int iExt )
{
	CSphString sName;
	sName.SetSprintf ( "%s/binlog.%03d", sPath, iExt );
	return sName;
}

BinlogWriter_c::BinlogWriter_c ()
	: m_pNotifyOnFlush ( NULL )
{
}

void BinlogWriter_c::SetNotifyCallback ( RtBinlog_c * pNotifyOnFlush )
{
	m_pNotifyOnFlush = pNotifyOnFlush;
}

void BinlogWriter_c::Flush ()
{
	if ( m_iPoolUsed<=0 )
		return;

	CSphWriter::Flush();

	int iSyncRes = 0;

#if !USE_WINDOWS
	iSyncRes = fsync ( m_iFD );
#endif

	m_bError = iSyncRes!=0;

	if ( iSyncRes!=0 && m_pError )
		m_pError->SetSprintf ( "failed to sync %s: %s" , m_sName.cstr(), strerror(errno) );

	if ( m_pNotifyOnFlush )
		m_pNotifyOnFlush->NotifyBufferFlushed ( m_iWritten );
}

//////////////////////////////////////////////////////////////////////////

#define MIN_BINLOG_SIZE 262144

RtBinlog_c::RtBinlog_c ()
	: m_iFlushPeriod ( 0 )
	, m_iFlushTimeLeft ( 0 )
	, m_bFlushOnCommit ( true )
	, m_iLockFD ( -1 )
	, m_bReplayMode ( false )
	, m_bDisabled ( true )
	, m_iRestartSize ( 0 )
	, m_iLastWriten ( 0 )
	, m_iMetaSaveTimeStamp ( 0 )
{
	MEMORY ( SPH_MEM_BINLOG );

	Verify ( m_tWriteLock.Init() );

	m_tWriter.SetBufferSize ( MIN_BINLOG_SIZE );
	m_tWriter.SetNotifyCallback ( this );
}

RtBinlog_c::~RtBinlog_c ()
{
	if ( !m_bDisabled )
	{
		if ( m_iFlushPeriod > 0 )
		{
			m_iFlushPeriod = 0;
			sphThreadJoin ( &m_tUpdateTread );
		}
		Close ();
		LockFile ( false );
	}

	Verify ( m_tWriteLock.Done() );
}

void RtBinlog_c::NotifyAddDocument ( const char * sIndexName, const CSphVector<CSphWordHit> & dHits, const CSphMatch & tDoc, int iRowSize
		, const char ** ppStr, const CSphSchema & tSchema )
{
	assert ( !tDoc.m_pStatic );
	assert (!( !tDoc.m_pDynamic && iRowSize!=0 ));
	assert (!( tDoc.m_pDynamic && (int)tDoc.m_pDynamic[-1]!=iRowSize ));

	if ( m_bReplayMode || m_bDisabled )
		return;

	MEMORY ( SPH_MEM_BINLOG );

	// item: Operation(BYTE) + index order(BYTE) + doc_id(SphDocID) + iRowSize(VLE) + hit.count(VLE) + strings.count + strings
	// + CSphMatch.m_pDynamic[...](DWORD[]) + + dHits[...] ()

	Verify ( m_tWriteLock.Lock() );

	const int uIndex = GetWriteIndexID ( sIndexName );

	m_tWriter.PutByte ( BINLOG_DOC_ADD );
	m_tWriter.PutByte ( uIndex );

	m_tWriter.PutDocid ( tDoc.m_iDocID );

	m_tWriter.PutDword ( iRowSize );
	m_tWriter.PutDword ( dHits.GetLength() );

	// strings putting here then attr info
	int iStringCount = 0;
	for ( int i=0; i<tSchema.GetAttrsCount(); i++ )
	{
		const CSphColumnInfo & tCol = tSchema.GetAttr(i);
		iStringCount += tCol.m_eAttrType==SPH_ATTR_STRING && tCol.m_tLocator.m_bDynamic;
	}
	m_tWriter.PutDword ( ppStr ? iStringCount : 0 );
	for ( int i=0; i<iStringCount && ppStr; i++ )
		m_tWriter.PutString ( ppStr[i] );

	m_tWriter.PutBytes ( tDoc.m_pDynamic, iRowSize*sizeof(CSphRowitem) );

	ARRAY_FOREACH ( i, dHits )
	{
		const CSphWordHit & tHit = dHits[i];
		m_tWriter.PutDocid ( tHit.m_iDocID );
		m_tWriter.PutDocid ( tHit.m_iWordID );
		m_tWriter.PutDword ( tHit.m_iWordPos );
	}

	CheckDoRestart();

	Verify ( m_tWriteLock.Unlock() );
}

void RtBinlog_c::NotifyDeleteDocument ( const char * sIndexName, SphDocID_t tDocID )
{
	if ( m_bReplayMode || m_bDisabled )
		return;

	MEMORY ( SPH_MEM_BINLOG );

	// item: Operation(BYTE) + index order(BYTE) + doc_id(SphDocID)

	Verify ( m_tWriteLock.Lock() );

	const int uIndex = GetWriteIndexID ( sIndexName );

	m_tWriter.PutByte ( BINLOG_DOC_DELETE );
	m_tWriter.PutByte ( uIndex );
	m_tWriter.PutDocid ( tDocID );

	CheckDoRestart();

	Verify ( m_tWriteLock.Unlock() );
}

void RtBinlog_c::NotifyCommit ( const char * sIndexName, int64_t iTID )
{
	if ( m_bReplayMode || m_bDisabled )
		return;

	MEMORY ( SPH_MEM_BINLOG );

	Verify ( m_tWriteLock.Lock() );

	const int uIndexesIndex = GetWriteIndexID ( sIndexName );

	// item: Operation(BYTE) + index order(BYTE)

	m_tWriter.PutByte ( BINLOG_DOC_COMMIT );
	m_tWriter.PutByte ( uIndexesIndex );
	m_tWriter.PutOffset ( iTID );

	// update indices min max
	assert ( m_dBinlogs.GetLength() );
	assert ( uIndexesIndex>=0 && uIndexesIndex < m_dBinlogs.Last().m_dRanges.GetLength() );
	IndexRange_t & tRange = m_dBinlogs.Last().m_dRanges[uIndexesIndex];
	tRange.m_iMin = Min ( tRange.m_iMin, iTID );
	tRange.m_iMax = Max ( tRange.m_iMax, iTID );

	if ( m_bFlushOnCommit )
		m_tWriter.Flush();

	CheckDoRestart();

	Verify ( m_tWriteLock.Unlock() );
}

void RtBinlog_c::NotifyUpdateAttributes ( const char * sIndexName, const CSphAttrUpdate & tUpd )
{
	if ( m_bReplayMode || m_bDisabled )
		return;

	MEMORY ( SPH_MEM_BINLOG );

	Verify ( m_tWriteLock.Lock() );

	const int uIndexesIndex = GetWriteIndexID ( sIndexName );
	m_tWriter.PutByte ( BINLOG_UPDATE_ATTRS );
	m_tWriter.PutByte ( uIndexesIndex );

	m_tWriter.PutDword ( tUpd.m_dAttrs.GetLength() );
	ARRAY_FOREACH ( i, tUpd.m_dAttrs )
	{
		m_tWriter.PutString ( tUpd.m_dAttrs[i].m_sName.cstr() );
		m_tWriter.PutDword ( tUpd.m_dAttrs[i].m_eAttrType );
	}

	m_tWriter.PutDword ( tUpd.m_dPool.GetLength() );
	ARRAY_FOREACH ( i, tUpd.m_dPool )
		m_tWriter.PutDword ( tUpd.m_dPool[i] );

	m_tWriter.PutDword ( tUpd.m_dDocids.GetLength() );
	ARRAY_FOREACH ( i, tUpd.m_dDocids )
		m_tWriter.PutOffset ( tUpd.m_dDocids[i] );

	m_tWriter.PutDword ( tUpd.m_dRowOffset.GetLength() );
	ARRAY_FOREACH ( i, tUpd.m_dRowOffset )
		m_tWriter.PutDword ( tUpd.m_dRowOffset[i] );

	if ( m_bFlushOnCommit )
		m_tWriter.Flush();

	CheckDoRestart();

	Verify ( m_tWriteLock.Unlock() );
}

// here's been going binlogs with ALL closed indices removing
void RtBinlog_c::NotifyIndexFlush ( const char * sIndexName, int64_t iTID )
{
	if ( m_bDisabled )
		return;

	MEMORY ( SPH_MEM_BINLOG );

	Verify ( m_tWriteLock.Lock() );
	const int iFlushed = GetIndexByName<IndexFlushPoint_t> ( m_dFlushed, sIndexName );
	if ( iFlushed!=-1 )
		m_dFlushed[iFlushed].m_iTID = iTID;
	else
		m_dFlushed.Add ( IndexFlushPoint_t ( sIndexName, iTID ) );

	SaveMeta();
	Verify ( m_tWriteLock.Unlock() );
}

void RtBinlog_c::Configure ( const CSphConfigSection & hSearchd )
{
	MEMORY ( SPH_MEM_BINLOG );

	if ( hSearchd ( "binlog_flush" ) )
	{
		m_bFlushOnCommit = ( strcmp ( hSearchd.GetStr ( "binlog_flush", "none" ), "commit" )==0 );
		m_iFlushPeriod = hSearchd.GetInt ( "binlog_flush", 0 );
	}

	if ( hSearchd ( "binlog_buffer_size" ) )
	{
		const DWORD uSize = hSearchd.GetSize ( "binlog_buffer_size", MIN_BINLOG_SIZE );
		const DWORD uSizeCliped = Max ( MIN_BINLOG_SIZE, uSize );

		if ( uSize<uSizeCliped )
			sphCallWarningCallback ( "binlog_buffer_size less than min %d KB, fixed up", uSizeCliped / 1024 );

		m_tWriter.SetBufferSize ( uSizeCliped );
	}
	if ( hSearchd ( "binlog_path" ) )
	{
		m_sLogPath = hSearchd.GetStr ( "binlog_path", "." );
		m_bDisabled = false;
	}
	if ( hSearchd ( "binlog_restart_limit" ) )
		m_iRestartSize = hSearchd.GetSize ( "binlog_restart_limit", m_iRestartSize );

	if ( !m_bDisabled )
	{
		LockFile ( true );
		LoadMeta();
	}
}

void RtBinlog_c::Replay ( const CSphVector < ISphRtIndex * > & dRtIndices )
{
	if ( m_bDisabled )
		return;

	// remove flush info unused anymore
	FlushedCleanup ( dRtIndices );

	// we still want scrap throe binlog in check mode
	if ( dRtIndices.GetLength()==0 )
		return;

	m_bReplayMode = true;

	ARRAY_FOREACH ( i, m_dBinlogs )
		ReplayBinlog ( dRtIndices, i );

	// final cleanup
	RtIndex_t::m_tAccums.Reset();

	m_bReplayMode = false;

	OpenNewLog();
}

void RtBinlog_c::CreateTimerThread ()
{
	if ( m_iFlushPeriod > 0 && !m_bDisabled )
	{
		m_iFlushTimeLeft = sphMicroTimer() + m_iFlushPeriod * 1000 * 1000;
		sphThreadCreate ( &m_tUpdateTread, RtBinlog_c::UpdateCheckFlush, this );
	}
}

void RtBinlog_c::UpdateCheckFlush ( void * pBinlog )
{
	assert ( pBinlog );
	RtBinlog_c * pLog = (RtBinlog_c *)pBinlog;
	assert ( !pLog->m_bDisabled );

	while ( pLog->m_iFlushPeriod>0 )
	{
		if ( pLog->m_iFlushPeriod>0 && pLog->m_iFlushTimeLeft < sphMicroTimer() )
		{
			MEMORY ( SPH_MEM_BINLOG );

			const int64_t iMetaSave = g_pBinlog->m_iMetaSaveTimeStamp;
			Verify ( pLog->m_tWriteLock.Lock() );
			pLog->m_tWriter.Flush();

			// at not flushed case we still have to save meta
			if ( iMetaSave==g_pBinlog->m_iMetaSaveTimeStamp )
				g_pBinlog->SaveMeta();

			pLog->m_iFlushTimeLeft = sphMicroTimer() + pLog->m_iFlushPeriod * 1000 * 1000;
			Verify ( pLog->m_tWriteLock.Unlock() );
		}

		sphSleepMsec ( 50 ); // sleep 50 msec before next iter as we could be in shutdown state
	}
}

void RtBinlog_c::NotifyBufferFlushed ( SphOffset_t iWritten )
{
	assert ( !m_bReplayMode );
	assert ( !m_bDisabled );

	m_iLastWriten = iWritten;
	SaveMeta();
}

int RtBinlog_c::GetWriteIndexID ( const char * sName )
{
	MEMORY ( SPH_MEM_BINLOG );

	assert ( m_dBinlogs.GetLength() && m_dBinlogs.Last().m_dRanges.GetLength()<0xff );

	const int iFoundIndex = GetIndexByName<IndexRange_t> ( m_dBinlogs.Last().m_dRanges, sName );
	if ( iFoundIndex>=0 )
		return iFoundIndex;

	const int iIndex = AddNewIndex ( sName );

	// item: Operation(BYTE) + index order(BYTE) + char count(VLE) + string(BYTE[])
	m_tWriter.PutByte ( BINLOG_INDEX_ADD );
	m_tWriter.PutByte ( iIndex );
	m_tWriter.PutString ( sName );

	return iIndex;
}

void RtBinlog_c::LoadMeta ()
{
	MEMORY ( SPH_MEM_BINLOG );

	CSphString sMeta;
	sMeta.SetSprintf ( "%s/binlog.meta", m_sLogPath.cstr() );
	if ( !sphIsReadable ( sMeta.cstr() ) )
		return;

	CSphString sError;

	// opened and locked, lets read
	CSphAutoreader rdMeta;
	if ( !rdMeta.Open ( sMeta, sError ) )
		sphDie ( "%s error: '%s'", sMeta.cstr(), sError.cstr() );

	if ( rdMeta.GetDword()!=META_HEADER_MAGIC )
		sphDie ( "invalid meta file '%s'", sMeta.cstr() );

	DWORD uVersion = rdMeta.GetDword();
	if ( uVersion==0 || uVersion>META_VERSION )
		sphDie ( "'%s' is v.%d, binary is v.%d", sMeta.cstr(), uVersion, META_VERSION );

	// reading still not flushed indices
	const int iFlushesLeft = rdMeta.GetDword();
	if ( iFlushesLeft )
		m_dFlushed.Resize ( iFlushesLeft );
	ARRAY_FOREACH ( i, m_dFlushed )
	{
		m_dFlushed[i].m_sName = rdMeta.GetString();
		m_dFlushed[i].m_iTID = rdMeta.GetOffset();
	}

	// reading binlogs desc
	const int iBinlogs = rdMeta.GetDword();
	if ( iBinlogs )
		m_dBinlogs.Resize ( iBinlogs );
	ARRAY_FOREACH ( i, m_dBinlogs )
	{
		BinlogDesc_t & tDesc = m_dBinlogs[i];
		tDesc.m_iExt = rdMeta.GetByte();
		const int iDescs = rdMeta.GetByte();
		if ( iDescs )
			tDesc.m_dRanges.Resize ( iDescs );
		ARRAY_FOREACH ( j, tDesc.m_dRanges )
		{
			tDesc.m_dRanges[j].m_sName = rdMeta.GetString();
			tDesc.m_dRanges[j].m_iMin = rdMeta.GetOffset();
			tDesc.m_dRanges[j].m_iMax = rdMeta.GetOffset();
		}
	}

	CheckRemoveFlushed();
}

void RtBinlog_c::SaveMeta ()
{
	MEMORY ( SPH_MEM_BINLOG );

	CSphString sMeta, sMetaOld;
	sMeta.SetSprintf ( "%s/binlog.meta.new", m_sLogPath.cstr() );
	sMetaOld.SetSprintf ( "%s/binlog.meta", m_sLogPath.cstr() );

	CSphString sError;

	// opened and locked, lets write
	CSphWriter wrMeta;
	if ( !wrMeta.OpenFile ( sMeta, sError ) )
		sphDie ( "failed to open '%s': '%s'", sMeta.cstr(), sError.cstr() );

	wrMeta.PutDword ( META_HEADER_MAGIC );
	wrMeta.PutDword ( META_VERSION );

	// writing still not flushed indices
	wrMeta.PutDword ( m_dFlushed.GetLength() );
	ARRAY_FOREACH ( i, m_dFlushed )
	{
		wrMeta.PutString ( m_dFlushed[i].m_sName.cstr() );
		wrMeta.PutOffset ( m_dFlushed[i].m_iTID );
	}

	// reading binlogs desc
	wrMeta.PutDword ( m_dBinlogs.GetLength() );
	ARRAY_FOREACH ( i, m_dBinlogs )
	{
		const BinlogDesc_t & tDesc = m_dBinlogs[i];
		wrMeta.PutByte ( tDesc.m_iExt );
		wrMeta.PutByte ( tDesc.m_dRanges.GetLength() );
		ARRAY_FOREACH ( j, tDesc.m_dRanges )
		{
			wrMeta.PutString ( tDesc.m_dRanges[j].m_sName.cstr() );
			wrMeta.PutOffset ( tDesc.m_dRanges[j].m_iMin );
			wrMeta.PutOffset ( tDesc.m_dRanges[j].m_iMax );
		}
	}

	wrMeta.CloseFile();

	if ( ::rename ( sMeta.cstr(), sMetaOld.cstr() ) )
		sphDie ( "failed to rename meta (src=%s, dst=%s, errno=%d, error=%s)",
			sMeta.cstr(), sMetaOld.cstr(), errno, strerror(errno) ); // !COMMIT handle this gracefully

	m_iMetaSaveTimeStamp = sphMicroTimer();
}

void RtBinlog_c::LockFile ( bool bLock )
{
	CSphString sName;
	sName.SetSprintf ( "%s/binlog.lock", m_sLogPath.cstr() );

	if ( bLock )
	{
		assert ( m_iLockFD==-1 );
		const int iLockFD = ::open ( sName.cstr(), SPH_O_NEW, 0644 );

		if ( iLockFD<0 )
			sphDie ( "failed to open '%s': %u '%s'", sName.cstr(), errno, strerror(errno) );

		if ( !sphLockEx ( iLockFD, false ) )
			sphDie ( "failed to lock '%s': %u '%s'", sName.cstr(), errno, strerror(errno) );

		m_iLockFD = iLockFD;
	} else
	{
		SafeClose ( m_iLockFD );
		::unlink ( sName.cstr()	);
	}
}

void RtBinlog_c::OpenNewLog ()
{
	MEMORY ( SPH_MEM_BINLOG );

	BinlogDesc_t & tDesc = m_dBinlogs.Add();
	tDesc.m_iExt = m_dBinlogs.GetLength()>1 ? m_dBinlogs[ m_dBinlogs.GetLength()-2 ].m_iExt+1 : 1;

	CSphString sFullLogName = MakeBinlogName ( m_sLogPath.cstr(), tDesc.m_iExt );

	const bool bOpened = m_tWriter.OpenFile ( sFullLogName.cstr(), m_sWriterError );

	if ( !bOpened )
		sphDie ( "failed to open '%s': %u '%s'", sFullLogName.cstr(), errno, strerror(errno) );

	m_iLastWriten = 0;

	// store last known log number
	SaveMeta();

	// add binlog's header \ version info
	m_tWriter.PutDword ( BINLOG_HEADER_MAGIC );
	m_tWriter.PutDword ( BINLOG_VERSION );
}

void RtBinlog_c::CheckDoRestart ()
{
	if ( m_iRestartSize>0 && m_iLastWriten>=m_iRestartSize )
	{
		MEMORY ( SPH_MEM_BINLOG );

		Close ();
		OpenNewLog();
	}
}

void RtBinlog_c::CheckRemoveFlushed()
{
	if ( !m_dBinlogs.GetLength() )
		return;

	MEMORY ( SPH_MEM_BINLOG );

	// removing binlogs with ALL flushed indices
	ARRAY_FOREACH ( i, m_dBinlogs )
	{
		const BinlogDesc_t & tDesc = m_dBinlogs[i];
		bool bIsBinlogUsed = false;
		ARRAY_FOREACH_COND ( j, tDesc.m_dRanges, !bIsBinlogUsed )
		{
			const int iCheckedIndex = GetIndexByName ( m_dFlushed, tDesc.m_dRanges[j].m_sName.cstr() );
			bIsBinlogUsed |= iCheckedIndex==-1 ||
				( m_dFlushed[iCheckedIndex].m_iTID<tDesc.m_dRanges[j].m_iMax && tDesc.m_dRanges[j].m_iMax!=g_iRangeMin );
		}
		if ( !bIsBinlogUsed )
		{
			const CSphString sBinlogName = MakeBinlogName ( m_sLogPath.cstr(), tDesc.m_iExt );
			CSphString sError;
			if ( sphIsReadable ( sBinlogName.cstr(), &sError ) )
				::unlink ( sBinlogName.cstr() );
			else
				sphCallWarningCallback ( "binlog: can't remove file '%s' : '%s'", sBinlogName.cstr(), sError.cstr() );
			m_dBinlogs.Remove ( i-- );
		}
	}
}

int RtBinlog_c::AddNewIndex ( const char * sIndexName )
{
	assert ( m_dBinlogs.GetLength() );
	BinlogDesc_t & tDesc = m_dBinlogs.Last();
	const int iIndex = tDesc.m_dRanges.GetLength();
	IndexRange_t & tRange = tDesc.m_dRanges.Add();
	tRange = IndexRange_t();
	tRange.m_sName = sIndexName;

	return iIndex;
}

bool RtBinlog_c::NeedReplay ( const char * sIndexName, int64_t iTID ) const
{
	const int iFlushed = GetIndexByName<IndexFlushPoint_t> ( m_dFlushed, sIndexName );
	if ( iFlushed==-1 )
		return true;

	return m_dFlushed[iFlushed].m_iTID<iTID;
}

void RtBinlog_c::FlushedCleanup ( const CSphVector < ISphRtIndex * > & dRtIndices )
{
	ARRAY_FOREACH ( i, m_dFlushed )
		if ( GetIndexByName ( dRtIndices, m_dFlushed[i].m_sName.cstr() )==NULL )
			m_dFlushed.RemoveFast ( i-- );
}

void RtBinlog_c::Close ()
{
	m_tWriter.CloseFile();

	CheckRemoveFlushed();
	SaveMeta();
}

void RtBinlog_c::ReplayBinlog ( const CSphVector < ISphRtIndex * > & dRtIndices, int iBinlog ) const
{
	assert ( iBinlog>=0 && iBinlog<m_dBinlogs.GetLength() );
	CSphString sError;
	CSphString sBinlogName ( MakeBinlogName ( m_sLogPath.cstr(), m_dBinlogs[iBinlog].m_iExt ) );

	if ( !sphIsReadable ( sBinlogName.cstr(), &sError ) )
	{
		sphCallWarningCallback ( "%s error: '%s'", sBinlogName.cstr(), sError.cstr() );
		return;
	}

	// opened and locked, lets read
	CSphAutoreader tReader;
	if ( !tReader.Open ( sBinlogName, sError ) )
		sphDie ( "%s error: '%s'", sBinlogName.cstr(), sError.cstr() );

	const SphOffset_t iFileSize = tReader.GetFilesize();

	if ( tReader.GetDword()!=BINLOG_HEADER_MAGIC || tReader.GetErrorFlag() )
		sphDie ( "binlog: invalid file='%s'", sBinlogName.cstr() );

	DWORD uVersion = tReader.GetDword();
	if ( ( uVersion==0 || uVersion>BINLOG_VERSION ) || tReader.GetErrorFlag() )
		sphDie ( "binlog: '%s' is v.%d, binary is v.%d", sBinlogName.cstr(), uVersion, BINLOG_VERSION );

	// init committed transactions control points by min values from meta
	const BinlogDesc_t & tDesc = m_dBinlogs[iBinlog];
	CSphVector<int64_t> dCommitedCP ( tDesc.m_dRanges.GetLength() );
	ARRAY_FOREACH ( i, tDesc.m_dRanges )
		dCommitedCP[i] = tDesc.m_dRanges[i].m_iMin;

	// replay stat
	struct Stat { int m_iPassed; int m_iTotal; Stat() : m_iPassed ( 0 ), m_iTotal ( 0 ) {} };
	Stat dStat[4];

	while ( iFileSize-tReader.GetPos()>=(int)sizeof(BYTE) && !tReader.GetErrorFlag() )
	{
		const int uOp = tReader.GetByte();
		if ( tReader.GetErrorFlag() )
			break;

		dStat[0].m_iTotal++;

		switch ( uOp )
		{
		case BINLOG_DOC_ADD:
			dStat[1].m_iTotal++;
			if ( ReplayAddDocument ( dRtIndices, tReader, iBinlog, dCommitedCP ) )
				dStat[1].m_iPassed++;
			break;
		case BINLOG_DOC_DELETE:
			dStat[2].m_iTotal++;
			if ( ReplayDeleteDocument ( dRtIndices, tReader, iBinlog, dCommitedCP ) )
				dStat[2].m_iPassed++;
			break;
		case BINLOG_DOC_COMMIT:
			dStat[3].m_iTotal++;
			if ( ReplayCommit ( dRtIndices, tReader, iBinlog, dCommitedCP ) )
				dStat[3].m_iPassed++;
			break;
		case BINLOG_INDEX_ADD:
			ReplayIndexAdd ( dRtIndices, tReader, iBinlog );
			break;
		case BINLOG_UPDATE_ATTRS:
			ReplayUpdateAttributes ( dRtIndices, tReader, iBinlog );
			break;
		default:
			sphDie	( "binlog: unknown operation (operation=%d, file='%s', pos=%d)", uOp, sBinlogName.cstr(), tReader.GetPos() );
		}

		dStat[0].m_iPassed++;
	}

	if ( tReader.GetErrorFlag() )
		sphCallWarningCallback ( "binlog: there is an error (file='%s', pos=%d, message='%s')",
			sBinlogName.cstr(), tReader.GetPos(), sError.cstr() );

	sphCallWarningCallback ( "%s: total (%d/%d), committed (%d/%d), added (%d/%d), deleted (%d/%d)"
		, sBinlogName.cstr()
		, dStat[0].m_iPassed, dStat[0].m_iTotal
		, dStat[3].m_iPassed, dStat[3].m_iTotal
		, dStat[1].m_iPassed, dStat[1].m_iTotal
		, dStat[2].m_iPassed, dStat[2].m_iTotal );
}

bool RtBinlog_c::ReplayAddDocument ( const CSphVector < ISphRtIndex * > & dRtIndices, CSphReader & tReader, int iBinlog, const CSphVector<int64_t> & dCommitedCP ) const
{
	// item: Operation(BYTE) + index order(BYTE) + doc_id(SphDocID) + iRowSize(VLE) + hit.count(VLE) + strings.count + strings + CSphMatch.m_pDynamic[...](DWORD[]) +
	// + dHits[...] ()

	assert ( m_dBinlogs.GetLength() );
	assert ( iBinlog>=0 && iBinlog<m_dBinlogs.GetLength() );

	const BinlogDesc_t & tDesc = m_dBinlogs[iBinlog];

	// check index order
	const int iReplayedIndex = tReader.GetByte();
	if ( tReader.GetErrorFlag() )
		return false;
	if ( iReplayedIndex<0 || iReplayedIndex>=tDesc.m_dRanges.GetLength() )
		sphDie ( "binlog: unexpected added index (loaded=%d, pos=%d)",
			iReplayedIndex, tReader.GetPos() );

	// check that index exists amongst searchd indices
	RtIndex_t * pIndex = GetIndexByName ( dRtIndices, tDesc.m_dRanges[iReplayedIndex].m_sName.cstr() );
	if ( !pIndex )
		sphCallWarningCallback ( "binlog: added index doesn't exist (loaded=%d, name=%s, pos=%d)",
			iReplayedIndex, tDesc.m_dRanges[ iReplayedIndex ].m_sName.cstr(), tReader.GetPos() );

	const SphDocID_t tDocID = tReader.GetDocid();
	if ( tReader.GetErrorFlag() )
		return false;

	const int iRowSize = tReader.GetDword();
	if ( tReader.GetErrorFlag() )
		return false;

	if ( pIndex && iRowSize!=pIndex->GetMatchSchema().GetRowSize() )
		sphCallWarningCallback ( "binlog: added attributes row mismatch (loaded=%s, expected=%d, got=%d, pos=%d)",
			pIndex->GetName(), pIndex->GetMatchSchema().GetRowSize(), iRowSize, tReader.GetPos() );

	const int iHitCount = tReader.GetDword();
	if ( tReader.GetErrorFlag() )
		return false;

	// actual strings reading
	const int iStringsCount = tReader.GetDword();
	if ( tReader.GetErrorFlag() )
		return false;
	CSphVector<CSphString> dStrings ( iStringsCount );
	for ( int i=0; i<iStringsCount && !tReader.GetErrorFlag(); i++ )
		dStrings[i] = tReader.GetString();

	if ( tReader.GetErrorFlag() )
		return false;

	CSphMatch tDoc;
	tDoc.Reset ( iRowSize );
	tDoc.m_iDocID = tDocID;
	for ( int i = 0; i<iRowSize && !tReader.GetErrorFlag(); i++ )
		tDoc.m_pDynamic[i] = tReader.GetDword();

	if ( tReader.GetErrorFlag() )
		return false;

	CSphVector<CSphWordHit> dHits;
	if ( iHitCount )
		dHits.Resize ( iHitCount );
	for ( int i = 0; i<iHitCount && !tReader.GetErrorFlag(); i++ )
	{
		CSphWordHit & tHit = dHits[i];
		tHit.m_iDocID = tReader.GetDocid();
		tHit.m_iWordID = tReader.GetDocid();
		tHit.m_iWordPos = tReader.GetDword();
	}

	if ( tReader.GetErrorFlag() )
		return false;

	if ( pIndex && iRowSize==pIndex->GetMatchSchema().GetRowSize()
		&& NeedReplay ( tDesc.m_dRanges[iReplayedIndex].m_sName.cstr(), dCommitedCP[iReplayedIndex] ) )
	{
		CSphVector< const char * > dStrPtrs ( dStrings.GetLength() );
		ARRAY_FOREACH ( i, dStrings )
			dStrPtrs[i] = dStrings[i].cstr();

		pIndex->AddDocumentReplayable ( dHits, tDoc, dStrPtrs.Begin() );
		return true;
	} else
		return false;
}

bool RtBinlog_c::ReplayDeleteDocument ( const CSphVector < ISphRtIndex * > & dRtIndices, CSphReader & tReader, int iBinlog, const CSphVector<int64_t> & dCommitedCP ) const
{
	// item: Operation(BYTE) + index order(BYTE) + doc_id(SphDocID)

	assert ( m_dBinlogs.GetLength() );
	assert ( iBinlog>=0 && iBinlog<m_dBinlogs.GetLength() );

	const BinlogDesc_t & tDesc = m_dBinlogs[iBinlog];

	// check index order
	const int iReplayedIndex = tReader.GetByte();
	if ( tReader.GetErrorFlag() )
		return false;
	if ( iReplayedIndex<0 || iReplayedIndex>=tDesc.m_dRanges.GetLength() )
		sphDie ( "binlog: unexpected deleted index (loaded=%d, pos=%d)",
			iReplayedIndex, tReader.GetPos() );

	// check that index exists amongst searchd indices
	RtIndex_t * pIndex = GetIndexByName ( dRtIndices, tDesc.m_dRanges[ iReplayedIndex ].m_sName.cstr() );
	if ( !pIndex )
		sphCallWarningCallback ( "binlog: deleted index doesn't exist (loaded=%d, name=%s, pos=%d)",
			iReplayedIndex, tDesc.m_dRanges[ iReplayedIndex ].m_sName.cstr(), tReader.GetPos() );

	const SphDocID_t tDocID = tReader.GetDocid();

	if ( tReader.GetErrorFlag() )
		return false;

	if ( pIndex && NeedReplay ( tDesc.m_dRanges[iReplayedIndex].m_sName.cstr(), dCommitedCP[iReplayedIndex] ) )
	{
		pIndex->DeleteDocumentReplayable ( tDocID );
		return true;
	} else
		return false;
}

bool RtBinlog_c::ReplayCommit ( const CSphVector < ISphRtIndex * > & dRtIndices, CSphReader & tReader, int iBinlog, CSphVector<int64_t> & dCommitedCP ) const
{
	// item: Operation(BYTE) + index order(BYTE)

	assert ( m_dBinlogs.GetLength() );
	assert ( iBinlog>=0 && iBinlog<m_dBinlogs.GetLength() );

	const BinlogDesc_t & tDesc = m_dBinlogs[iBinlog];

	// check index order
	const int iReplayedIndex = tReader.GetByte();
	if ( tReader.GetErrorFlag() )
		return false;
	if ( iReplayedIndex<0 || iReplayedIndex>=tDesc.m_dRanges.GetLength() )
		sphDie ( "binlog: unexpected commited index (loaded=%d, pos=%d)",
			iReplayedIndex, tReader.GetPos() );

	// check that index exists amongst searchd indices
	RtIndex_t * pIndex = GetIndexByName ( dRtIndices, tDesc.m_dRanges [ iReplayedIndex ].m_sName.cstr() );
	if ( !pIndex )
		sphCallWarningCallback ( "binlog: commited index doesn't exist (loaded=%d, name=%s, pos=%d)",
			iReplayedIndex, tDesc.m_dRanges [ iReplayedIndex ].m_sName.cstr(), tReader.GetPos() );

	const int64_t iTID = tReader.GetOffset();
	if ( tReader.GetErrorFlag() )
		return false;

	if ( dCommitedCP[iReplayedIndex]!=g_iRangeMin && dCommitedCP[iReplayedIndex]>iTID )
		sphDie ( "binlog: transaction id descending (loaded=%d, prev=%d, next=%d, pos=%d)",
			iReplayedIndex, (DWORD)dCommitedCP[iReplayedIndex], (DWORD)iTID, tReader.GetPos() );

	dCommitedCP[iReplayedIndex] = iTID;

	if ( pIndex && NeedReplay ( tDesc.m_dRanges[iReplayedIndex].m_sName.cstr(), iTID ) )
	{
		pIndex->CommitReplayable();

		if ( pIndex->m_iTID!=iTID )
		{
			sphCallWarningCallback ( "binlog: commited transaction id mismatch (expected=%d, got=%d)",
				(DWORD)iTID, (DWORD) pIndex->m_iTID);

			pIndex->m_iTID = iTID;
		}

		return true;
	} else
		return false;
}

bool RtBinlog_c::ReplayIndexAdd ( const CSphVector < ISphRtIndex * > & dRtIndices, CSphReader & tReader, int iBinlog ) const
{
	// item: Operation(BYTE) + index order(BYTE) + char count(VLE) + string(BYTE[])

	assert ( m_dBinlogs.GetLength() );
	assert ( iBinlog>=0 && iBinlog<m_dBinlogs.GetLength() );

	const BinlogDesc_t & tDesc = m_dBinlogs[iBinlog];

	// check index order
	const int iReplayedIndex = tReader.GetByte();
	if ( tReader.GetErrorFlag() )
		return false;
	if ( iReplayedIndex<0 || iReplayedIndex>=tDesc.m_dRanges.GetLength() )
		sphDie ( "binlog: unexpected added index (loaded=%d, pos=%d)",
			iReplayedIndex, tReader.GetPos() );

	// check index name
	const CSphString sIndex = tReader.GetString();
	if ( tReader.GetErrorFlag() )
		return false;

	if ( sIndex!=tDesc.m_dRanges[iReplayedIndex].m_sName )
		sphDie ( "binlog: unexpected added index (loaded=%d, expected=%s, got=%s, pos=%d)",
			iReplayedIndex, tDesc.m_dRanges[iReplayedIndex].m_sName.cstr(), sIndex.cstr(), tReader.GetPos() );

	// check that index exists amongst searchd indices
	const RtIndex_t * pIndex = GetIndexByName ( dRtIndices, sIndex.cstr() );
	if ( !pIndex )
		sphCallWarningCallback ( "binlog: index to add doesn't exist (loaded=%d, name=%s, pos=%d)",
			iReplayedIndex, sIndex.cstr(), tReader.GetPos() );

	return true;
}

bool RtBinlog_c::ReplayUpdateAttributes ( const CSphVector < ISphRtIndex * > & dRtIndices, CSphReader & tReader, int iBinlog ) const
{
	const BinlogDesc_t & tDesc = m_dBinlogs[iBinlog];

	// check index order
	const int iReplayedIndex = tReader.GetByte();
	if ( tReader.GetErrorFlag() )
		return false;
	if ( iReplayedIndex<0 || iReplayedIndex>=tDesc.m_dRanges.GetLength() )
		sphDie ( "binlog: unexpected commited index (loaded=%d, pos=%d)", iReplayedIndex, tReader.GetPos() );

	// check that index exists amongst searchd indices
	RtIndex_t * pIndex = GetIndexByName ( dRtIndices, tDesc.m_dRanges [ iReplayedIndex ].m_sName.cstr() );
	if ( !pIndex )
		sphCallWarningCallback ( "binlog: commited index doesn't exist (loaded=%d, name=%s, pos=%d)",
			iReplayedIndex, tDesc.m_dRanges [ iReplayedIndex ].m_sName.cstr(), tReader.GetPos() );

	// unserialize log entry
	CSphAttrUpdate tUpd;

	tUpd.m_dAttrs.Resize ( tReader.GetDword() ); // FIXME! add size sanity check
	ARRAY_FOREACH ( i, tUpd.m_dAttrs )
	{
		tUpd.m_dAttrs[i].m_sName = tReader.GetString();
		tUpd.m_dAttrs[i].m_eAttrType = tReader.GetDword();
	}

	tUpd.m_dPool.Resize ( tReader.GetDword() );
	ARRAY_FOREACH ( i, tUpd.m_dPool )
		tUpd.m_dPool[i] = tReader.GetDword();

	tUpd.m_dDocids.Resize ( tReader.GetDword() );
	ARRAY_FOREACH ( i, tUpd.m_dDocids )
		tUpd.m_dDocids[i] = (SphDocID_t) tReader.GetOffset();

	tUpd.m_dRowOffset.Resize ( tReader.GetDword() );
	ARRAY_FOREACH ( i, tUpd.m_dRowOffset )
		tUpd.m_dRowOffset[i] = tReader.GetDword();

	// shoot
	CSphString sError;
	pIndex->UpdateAttributes ( tUpd, -1, sError ); // FIXME! check for errors

	return true;
}

AccumStorage_c::~AccumStorage_c ()
{
	Reset();
}

void AccumStorage_c::Reset ()
{
	if ( m_dFree.GetLength() )
		sphCallWarningCallback ( "max used accumulators=%d", m_dFree.GetLength() );

	if ( m_dBusy.GetLength() )
		sphCallWarningCallback ( "there are using accumulators=%d", m_dBusy.GetLength() );

	ARRAY_FOREACH ( i, m_dBusy )
		SafeDelete ( m_dBusy[i] );

	ARRAY_FOREACH ( i, m_dFree )
	{
		assert ( !m_dFree[i]->m_pIndex );
		SafeDelete ( m_dFree[i] );
	}

	m_dBusy.Reset();
	m_dFree.Reset();
}

RtAccum_t * AccumStorage_c::Aqcuire ( RtIndex_t * pIndex )
{
	MEMORY ( SPH_MEM_BINLOG );

	assert ( pIndex );

	RtAccum_t * pAcc = Get ( pIndex );
	if ( pAcc )
		return pAcc;

	if ( m_dFree.GetLength() )
		pAcc = m_dFree.Pop();
	else
		pAcc = new RtAccum_t();

	pAcc->m_pIndex = pIndex;
	m_dBusy.Add ( pAcc );
	return pAcc;
}

RtAccum_t * AccumStorage_c::Get ( const RtIndex_t * pIndex ) const
{
	assert ( pIndex );

	ARRAY_FOREACH ( i, m_dBusy )
		if ( m_dBusy[i]->m_pIndex==pIndex )
			return m_dBusy[i];

	return NULL;
}

void AccumStorage_c::Release ( RtAccum_t * pAcc )
{
	assert ( !pAcc->m_pIndex );

	Verify ( m_dBusy.RemoveValue ( pAcc ) );
	assert ( !m_dFree.Contains ( pAcc ) );
	m_dFree.Add ( pAcc );
}

//////////////////////////////////////////////////////////////////////////

ISphRtIndex * sphGetCurrentIndexRT()
{
	RtAccum_t * pAcc = (RtAccum_t*) sphThreadGet ( g_tTlsAccumKey );
	if ( pAcc )
		return pAcc->m_pIndex;
	return NULL;
}

ISphRtIndex * sphCreateIndexRT ( const CSphSchema & tSchema, const char * sIndexName, DWORD uRamSize, const char * sPath )
{
	MEMORY ( SPH_MEM_IDX_RT );
	return new RtIndex_t ( tSchema, sIndexName, uRamSize, sPath );
}

void sphRTInit ( const CSphConfigSection & hSearchd )
{
	MEMORY ( SPH_MEM_BINLOG );

	g_bRTChangesAllowed = false;
	Verify ( RtSegment_t::m_tSegmentSeq.Init() );
	Verify ( sphThreadKeyCreate ( &g_tTlsAccumKey ) );
	g_pBinlog = new RtBinlog_c();
	if ( !g_pBinlog )
		sphDie ( "binlog: failed to create binlog" );
	g_pBinlog->Configure ( hSearchd );
}

void sphRTDone ()
{
	sphThreadKeyDelete ( g_tTlsAccumKey );
	Verify ( RtSegment_t::m_tSegmentSeq.Done() );
	// its valid for "searchd --stop" case
	SafeDelete ( g_pBinlog );
}

void sphReplayBinlog ( const CSphVector < ISphRtIndex * > & dRtIndices )
{
	MEMORY ( SPH_MEM_BINLOG );

#ifndef _NDEBUG
	ARRAY_FOREACH ( i, dRtIndices )
		assert ( dynamic_cast< RtIndex_t * > ( dRtIndices[i] ) );
#endif

	g_pBinlog->Replay ( dRtIndices );
	g_pBinlog->CreateTimerThread();
	g_bRTChangesAllowed = true;
}

//
// $Id$
//
