//
// $Id$
//

//
// Copyright (c) 2001-2007, Andrew Aksyonoff. All rights reserved.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License. You should have
// received a copy of the GPL license along with this program; if you
// did not, you can find it at http://www.gnu.org/
//

#include "sphinx.h"

#include <time.h>
#include <math.h>

#if !USE_WINDOWS
#include <unistd.h>
#include <sys/time.h>
#endif

//////////////////////////////////////////////////////////////////////////
// TRAITS
//////////////////////////////////////////////////////////////////////////

/// match-sorting priority queue traits
class CSphMatchQueueTraits : public ISphMatchSorter, ISphNoncopyable
{
protected:
	CSphMatch *					m_pData;
	int							m_iUsed;
	int							m_iSize;

	CSphMatchComparatorState	m_tState;
	const bool					m_bUsesAttrs;

public:
	/// ctor
	CSphMatchQueueTraits ( int iSize, bool bUsesAttrs )
		: m_iUsed ( 0 )
		, m_iSize ( iSize )
		, m_bUsesAttrs ( bUsesAttrs )
	{
		assert ( iSize>0 );
		m_pData = new CSphMatch [ iSize ];
		assert ( m_pData );

		m_tState.m_iNow = (DWORD) time ( NULL );
	}

	/// dtor
	~CSphMatchQueueTraits ()
	{
		SafeDeleteArray ( m_pData );
	}

public:
	int			GetLength () const										{ return m_iUsed; }
	void		SetState ( const CSphMatchComparatorState & tState )	{ m_tState = tState; m_tState.m_iNow = (DWORD) time ( NULL ); }
	bool		UsesAttrs ()											{ return m_bUsesAttrs; }
	CSphMatch *	First ()												{ return m_pData; }
};

//////////////////////////////////////////////////////////////////////////
// PLAIN SORTING QUEUE
//////////////////////////////////////////////////////////////////////////

/// normal match-sorting priority queue
template < typename COMP > class CSphMatchQueue : public CSphMatchQueueTraits
{
public:
	/// ctor
	CSphMatchQueue ( int iSize, bool bUsesAttrs )
		: CSphMatchQueueTraits ( iSize, bUsesAttrs )
	{}

	/// add entry to the queue
	virtual bool Push ( const CSphMatch & tEntry )
	{
		m_iTotal++;

		if ( m_iUsed==m_iSize )
		{
			// if it's worse that current min, reject it, else pop off current min
			if ( COMP::IsLess ( tEntry, m_pData[0], m_tState ) )
				return true;
			else
				Pop ();
		}

		// do add
		m_pData [ m_iUsed ] = tEntry;
		int iEntry = m_iUsed++;

		// sift up if needed, so that worst (lesser) ones float to the top
		while ( iEntry )
		{
			int iParent = ( iEntry-1 ) >> 1;
			if ( !COMP::IsLess ( m_pData[iEntry], m_pData[iParent], m_tState ) )
				break;

			// entry is less than parent, should float to the top
			Swap ( m_pData[iEntry], m_pData[iParent] );
			iEntry = iParent;
		}

		return true;
	}

	/// remove root (ie. top priority) entry
	virtual void Pop ()
	{
		assert ( m_iUsed );
		if ( !(--m_iUsed) ) // empty queue? just return
			return;

		// make the last entry my new root
		m_pData[0] = m_pData[m_iUsed];

		// sift down if needed
		int iEntry = 0;
		for ( ;; )
		{
			// select child
			int iChild = (iEntry<<1) + 1;
			if ( iChild>=m_iUsed )
				break;

			// select smallest child
			if ( iChild+1<m_iUsed )
				if ( COMP::IsLess ( m_pData[iChild+1], m_pData[iChild], m_tState ) )
					iChild++;

			// if smallest child is less than entry, do float it to the top
			if ( COMP::IsLess ( m_pData[iChild], m_pData[iEntry], m_tState ) )
			{
				Swap ( m_pData[iChild], m_pData[iEntry] );
				iEntry = iChild;
				continue;
			}

			break;
		}
	}

	/// store all entries into specified location in sorted order, and remove them from queue
	void Flatten ( CSphMatch * pTo, int iTag )
	{
		assert ( m_iUsed>=0 );
		pTo += m_iUsed;
		while ( m_iUsed>0 )
		{
			--pTo;
			pTo[0] = m_pData[0]; // OPTIMIZE? reset dst + swap?
			if ( iTag>=0 )
				pTo->m_iTag = iTag;
			Pop ();
		}
		m_iTotal = 0;
	}
};

//////////////////////////////////////////////////////////////////////////
// SORTING+GROUPING QUEUE
//////////////////////////////////////////////////////////////////////////

/// simple fixed-size hash
/// doesn't keep the order
template < typename T, typename KEY, typename HASHFUNC >
class CSphFixedHash : ISphNoncopyable
{
protected:
	static const int			HASH_LIST_END	= -1;
	static const int			HASH_DELETED	= -2;

	struct HashEntry_t
	{
		KEY		m_tKey;
		T		m_tValue;
		int		m_iNext;
	};

protected:
	CSphVector<HashEntry_t>		m_dEntries;		///< key-value pairs storage pool
	CSphVector<int>				m_dHash;		///< hash into m_dEntries pool

	int							m_iFree;		///< free pairs count
	CSphVector<int>				m_dFree;		///< free pair indexes

public:
	/// ctor
	CSphFixedHash ( int iLength )
	{
		int iBuckets = 2<<sphLog2(iLength-1); // less than 50% bucket usage guaranteed
		assert ( iLength>0 );
		assert ( iLength<=iBuckets );

		m_dEntries.Resize ( iLength );
		m_dHash.Resize ( iBuckets );
		m_dFree.Resize ( iLength );

		Reset ();
	}

	/// cleanup
	void Reset ()
	{
		ARRAY_FOREACH ( i, m_dEntries )
			m_dEntries[i].m_iNext = HASH_DELETED;

		ARRAY_FOREACH ( i, m_dHash )
			m_dHash[i] = HASH_LIST_END;

		m_iFree = m_dFree.GetLength();
		ARRAY_FOREACH ( i, m_dFree )
			m_dFree[i] = i;
	}

	/// add new entry
	/// returns NULL on success
	/// returns pointer to value if already hashed
	T * Add ( const T & tValue, const KEY & tKey )
	{
		assert ( m_iFree>0 && "hash overflow" );

		// check if it's already hashed
		DWORD uHash = DWORD ( HASHFUNC::Hash ( tKey ) ) & ( m_dHash.GetLength()-1 );
		int iPrev = -1, iEntry;

		for ( iEntry=m_dHash[uHash]; iEntry>=0; iPrev=iEntry, iEntry=m_dEntries[iEntry].m_iNext )
			if ( m_dEntries[iEntry].m_tKey==tKey )
				return &m_dEntries[iEntry].m_tValue;
		assert ( iEntry!=HASH_DELETED );

		// if it's not, do add
		int iNew = m_dFree [ --m_iFree ];

		HashEntry_t & tNew = m_dEntries[iNew];
		assert ( tNew.m_iNext==HASH_DELETED );

		tNew.m_tKey = tKey;
		tNew.m_tValue = tValue;
		tNew.m_iNext = HASH_LIST_END;

		if ( iPrev>=0 )
		{
			assert ( m_dEntries[iPrev].m_iNext==HASH_LIST_END );
			m_dEntries[iPrev].m_iNext = iNew;
		} else
		{
			assert ( m_dHash[uHash]==HASH_LIST_END );
			m_dHash[uHash] = iNew;
		}
		return NULL;
	}

	/// remove entry from hash
	void Remove ( const KEY & tKey )
	{
		// check if it's already hashed
		DWORD uHash = DWORD ( HASHFUNC::Hash ( tKey ) ) & ( m_dHash.GetLength()-1 );
		int iPrev = -1, iEntry;

		for ( iEntry=m_dHash[uHash]; iEntry>=0; iPrev=iEntry, iEntry=m_dEntries[iEntry].m_iNext )
			if ( m_dEntries[iEntry].m_tKey==tKey )
		{
			// found, remove it
			assert ( m_dEntries[iEntry].m_iNext!=HASH_DELETED );
			if ( iPrev>=0 )
				m_dEntries[iPrev].m_iNext = m_dEntries[iEntry].m_iNext;
			else
				m_dHash[uHash] = m_dEntries[iEntry].m_iNext;

#ifndef NDEBUG
			m_dEntries[iEntry].m_iNext = HASH_DELETED;
#endif

			m_dFree [ m_iFree++ ] = iEntry;
			return;
		}
		assert ( iEntry!=HASH_DELETED );
	}

	/// get value pointer by key
	T * operator () ( const KEY & tKey ) const
	{
		DWORD uHash = DWORD ( HASHFUNC::Hash ( tKey ) ) & ( m_dHash.GetLength()-1 );
		int iEntry;

		for ( iEntry=m_dHash[uHash]; iEntry>=0; iEntry=m_dEntries[iEntry].m_iNext )
			if ( m_dEntries[iEntry].m_tKey==tKey )
				return (T*)&m_dEntries[iEntry].m_tValue;

		assert ( iEntry!=HASH_DELETED );
		return NULL;
	}
};


struct IdentityHash_fn
{
	static inline uint64_t	Hash ( uint64_t iValue )	{ return iValue; }
	static inline DWORD		Hash ( DWORD iValue )		{ return iValue; }
	static inline int		Hash ( int iValue )			{ return iValue; }
};

/////////////////////////////////////////////////////////////////////////////

typedef uint64_t	SphGroupKey_t;

/// (group,attrvalue) pair
struct SphGroupedValue_t
{
public:
	SphGroupKey_t	m_uGroup;
	DWORD			m_uValue;

public:
	SphGroupedValue_t ()
	{}

	SphGroupedValue_t ( SphGroupKey_t uGroup, DWORD uValue )
		: m_uGroup ( uGroup )
		, m_uValue ( uValue )
	{}

	inline bool operator < ( const SphGroupedValue_t & rhs ) const
	{
		if ( m_uGroup<rhs.m_uGroup ) return true;
		if ( m_uGroup>rhs.m_uGroup ) return false;
		return m_uValue<rhs.m_uValue;
	}

	inline bool operator == ( const SphGroupedValue_t & rhs ) const
	{
		return m_uGroup==rhs.m_uGroup && m_uValue==rhs.m_uValue;
	}
};


/// unique values counter
/// used for COUNT(DISTINCT xxx) GROUP BY yyy queries
class CSphUniqounter : public CSphVector<SphGroupedValue_t>
{
public:
#ifndef NDEBUG
					CSphUniqounter () : m_iCountPos ( 0 ), m_bSorted ( true ) { Reserve ( 16384 ); }
	void			Add ( const SphGroupedValue_t & tValue )	{ CSphVector<SphGroupedValue_t>::Add ( tValue ); m_bSorted = false; }
	void			Sort ()										{ CSphVector<SphGroupedValue_t>::Sort (); m_bSorted = true; }

#else
					CSphUniqounter () : m_iCountPos ( 0 ) {}
#endif

public:
	int				CountStart ( SphGroupKey_t * pOutGroup );	///< starting counting distinct values, returns count and group key (0 if empty)
	int				CountNext ( SphGroupKey_t * pOutGroup );	///< continues counting distinct values, returns count and group key (0 if done)
	void			Compact ( SphGroupKey_t * pRemoveGroups, int iRemoveGroups );

protected:
	int				m_iCountPos;

#ifndef NDEBUG
	bool			m_bSorted;
#endif
};


int CSphUniqounter::CountStart ( SphGroupKey_t * pOutGroup )
{
	m_iCountPos = 0;
	return CountNext ( pOutGroup );
}


int CSphUniqounter::CountNext ( SphGroupKey_t * pOutGroup )
{
	assert ( m_bSorted );
	if ( m_iCountPos>=m_iLength )
		return 0;

	SphGroupKey_t uGroup = m_pData[m_iCountPos].m_uGroup;
	DWORD uValue = m_pData[m_iCountPos].m_uValue;
	*pOutGroup = uGroup;

	int iCount = 1;
	while ( m_pData[m_iCountPos].m_uGroup==uGroup )
	{
		if ( m_pData[m_iCountPos].m_uValue!=uValue )
			iCount++;
		uValue = m_pData[m_iCountPos].m_uValue;
		m_iCountPos++;
	}
	return iCount;
}


void CSphUniqounter::Compact ( SphGroupKey_t * pRemoveGroups, int iRemoveGroups )
{
	assert ( m_bSorted );
	if ( !m_iLength )
		return;

	sphSort ( pRemoveGroups, iRemoveGroups );

	SphGroupedValue_t * pSrc = m_pData;
	SphGroupedValue_t * pDst = m_pData;
	SphGroupedValue_t * pEnd = m_pData + m_iLength;

	// skip remove-groups which are not in my list
	while ( iRemoveGroups && (*pRemoveGroups)<pSrc->m_uGroup )
	{
		pRemoveGroups++;
		iRemoveGroups--;
	}

	for ( ; pSrc<pEnd; pSrc++ )
	{
		// check if this entry needs to be removed
		while ( iRemoveGroups && (*pRemoveGroups)<pSrc->m_uGroup )
		{
			pRemoveGroups++;
			iRemoveGroups--;
		}
		if ( iRemoveGroups && pSrc->m_uGroup==*pRemoveGroups )
			continue;

		// check if it's a dupe
		if ( pDst>m_pData && pDst[-1]==pSrc[0] )
			continue;

		*pDst++ = *pSrc;
	}

	assert ( pDst-m_pData<=m_iLength );
	m_iLength = pDst-m_pData;
}

/////////////////////////////////////////////////////////////////////////////

SphGroupKey_t sphCalcGroupKey ( const CSphMatch & tMatch, ESphGroupBy eGroupBy, int iAttrOffset, int iAttrBits )
{
	if ( eGroupBy==SPH_GROUPBY_ATTRPAIR )
	{
		int iItem = iAttrOffset/ROWITEM_BITS;
		return *(SphGroupKey_t*)( tMatch.m_pRowitems+iItem );
	}

	CSphRowitem iAttr = tMatch.GetAttr ( iAttrOffset, iAttrBits );
	if ( eGroupBy==SPH_GROUPBY_ATTR )
		return iAttr;

	time_t tStamp = iAttr;
	struct tm * pSplit = localtime ( &tStamp ); // FIXME! use _r on UNIX

	switch ( eGroupBy )
	{
		case SPH_GROUPBY_DAY:	return (pSplit->tm_year+1900)*10000 + (1+pSplit->tm_mon)*100 + pSplit->tm_mday;
		case SPH_GROUPBY_WEEK:	return (pSplit->tm_year+1900)*1000 + (1+pSplit->tm_yday) - pSplit->tm_wday;
		case SPH_GROUPBY_MONTH:	return (pSplit->tm_year+1900)*100 + (1+pSplit->tm_mon);
		case SPH_GROUPBY_YEAR:	return (pSplit->tm_year+1900);
		default:				assert ( 0 ); return 0;
	}
}


/// attribute magic
enum
{
	SPH_VATTR_ID			= -1,	///< tells match sorter to use doc id
	SPH_VATTR_RELEVANCE		= -2,	///< tells match sorter to use match weight

	OFF_POSTCALC_GROUP		= 0,	///< @group attr offset after normal and calculated attrs
	OFF_POSTCALC_COUNT		= 1,	///< @count attr offset after normal and calculated attrs
	OFF_POSTCALC_DISTINCT	= 2,	///< @distinct attr offset after normal and calculated attrs

	ADD_ITEMS_GROUP			= 2,	///< how much items to add in plain group-by mode
	ADD_ITEMS_DISTINCT		= 3		///< how much items to add in count-distinct mode
};


/// match sorter with k-buffering and group-by
#if USE_WINDOWS
#pragma warning(disable:4127)
#endif

template < typename COMPMATCH, typename COMPGROUP, bool DISTINCT >
class CSphKBufferGroupSorter : public CSphMatchQueueTraits
{
protected:
	const int		m_iRowitems;		///< normal match rowitems count (to distinguish already grouped matches)

	ESphGroupBy		m_eGroupBy;			///< group-by function
	int				m_iGroupbyOffset;	///< group-by attr bit offset
	int				m_iGroupbyCount;	///< group-by attr bit count

	CSphFixedHash < CSphMatch *, SphGroupKey_t, IdentityHash_fn >	m_hGroup2Match;

protected:
	int				m_iLimit;		///< max matches to be retrieved

	CSphUniqounter	m_tUniq;
	int				m_iDistinctOffset;
	int				m_iDistinctCount;
	bool			m_bSortByDistinct;

	CSphMatchComparatorState	m_tStateGroup;

	static const int			GROUPBY_FACTOR = 4;	///< allocate this times more storage when doing group-by (k, as in k-buffer)

public:
	/// ctor
	CSphKBufferGroupSorter ( const CSphQuery * pQuery ) // FIXME! make k configurable
		: CSphMatchQueueTraits ( pQuery->m_iMaxMatches*GROUPBY_FACTOR, true )
		, m_iRowitems		( pQuery->m_iPresortRowitems )
		, m_eGroupBy		( pQuery->m_eGroupFunc )
		, m_iGroupbyOffset	( pQuery->m_iGroupbyOffset )
		, m_iGroupbyCount	( pQuery->m_iGroupbyCount )
		, m_hGroup2Match	( pQuery->m_iMaxMatches*GROUPBY_FACTOR )
		, m_iLimit			( pQuery->m_iMaxMatches )
		, m_iDistinctOffset	( pQuery->m_iDistinctOffset )
		, m_iDistinctCount	( pQuery->m_iDistinctCount )
		, m_bSortByDistinct	( false )
	{
		assert ( GROUPBY_FACTOR>1 );
		assert ( DISTINCT==false || m_iDistinctOffset>=0 );
	}

	/// add entry to the queue
	virtual bool Push ( const CSphMatch & tEntry )
	{

		const int ADD_ITEMS_TOTAL = DISTINCT ? ADD_ITEMS_DISTINCT : ADD_ITEMS_GROUP;
		assert ( tEntry.m_iRowitems==m_iRowitems || tEntry.m_iRowitems==m_iRowitems+ADD_ITEMS_TOTAL );

		bool bGrouped = ( tEntry.m_iRowitems!=m_iRowitems );
		SphGroupKey_t uGroupKey = sphCalcGroupKey ( tEntry, m_eGroupBy, m_iGroupbyOffset, m_iGroupbyCount );

		// if this group is already hashed, we only need to update the corresponding match
		CSphMatch ** ppMatch = m_hGroup2Match ( uGroupKey );
		if ( ppMatch )
		{
			CSphMatch * pMatch = (*ppMatch);
			assert ( pMatch );
			assert ( m_eGroupBy==SPH_GROUPBY_ATTRPAIR || pMatch->m_pRowitems [ m_iRowitems+OFF_POSTCALC_GROUP ]==uGroupKey );

			if ( bGrouped )
			{
				// it's already grouped match
				// sum grouped matches count
				assert ( pMatch->m_iRowitems==tEntry.m_iRowitems );
				pMatch->m_pRowitems [ m_iRowitems+OFF_POSTCALC_COUNT ] += tEntry.m_pRowitems [ m_iRowitems+OFF_POSTCALC_COUNT ];
				if ( DISTINCT )
					pMatch->m_pRowitems [ m_iRowitems+OFF_POSTCALC_DISTINCT ] += tEntry.m_pRowitems [ m_iRowitems+OFF_POSTCALC_DISTINCT ];

			} else
			{
				// it's a simple match
				// increase grouped matches count
				assert ( pMatch->m_iRowitems==tEntry.m_iRowitems+ADD_ITEMS_TOTAL );
				pMatch->m_pRowitems [ m_iRowitems+OFF_POSTCALC_COUNT ]++;
			}

			// if new entry is more relevant, update from it
			if ( COMPMATCH::IsLess ( *pMatch, tEntry, m_tState ) )
			{
				pMatch->m_iDocID = tEntry.m_iDocID;
				pMatch->m_iWeight = tEntry.m_iWeight;
				pMatch->m_iTag = tEntry.m_iTag;

				for ( int i=0; i<pMatch->m_iRowitems-ADD_ITEMS_TOTAL; i++ )
					pMatch->m_pRowitems[i] = tEntry.m_pRowitems[i];
			}
		}

		// submit actual distinct value in all cases
		if ( DISTINCT && !bGrouped )
			m_tUniq.Add ( SphGroupedValue_t ( uGroupKey, tEntry.GetAttr ( m_iDistinctOffset, m_iDistinctCount ) ) );

		// it's a dupe anyway, so we shouldn't update total matches count
		if ( ppMatch )
			return false;

		// if we're full, let's cut off some worst groups
		if ( m_iUsed==m_iSize )
			CutWorst ();

		// do add
		assert ( m_iUsed<m_iSize );
		CSphMatch & tNew = m_pData [ m_iUsed++ ];

		int iNewSize = tEntry.m_iRowitems + ( bGrouped ? 0 : ADD_ITEMS_TOTAL );
		assert ( tNew.m_iRowitems==0 || tNew.m_iRowitems==iNewSize );

		tNew.m_iDocID = tEntry.m_iDocID;
		tNew.m_iWeight = tEntry.m_iWeight;
		tNew.m_iTag = tEntry.m_iTag;
		if ( !tNew.m_iRowitems )
		{
			tNew.m_iRowitems = iNewSize;
			tNew.m_pRowitems = new CSphRowitem [ iNewSize ];
		}
		memcpy ( tNew.m_pRowitems, tEntry.m_pRowitems, tEntry.m_iRowitems*sizeof(CSphRowitem) );
		if ( !bGrouped )
		{
			tNew.m_pRowitems [ m_iRowitems+OFF_POSTCALC_GROUP ] = (DWORD)uGroupKey; // intentionally truncate
			tNew.m_pRowitems [ m_iRowitems+OFF_POSTCALC_COUNT ] = 1;
			if ( DISTINCT )
				tNew.m_pRowitems [ m_iRowitems+OFF_POSTCALC_DISTINCT ] = 0;
		}

		m_hGroup2Match.Add ( &tNew, uGroupKey );
		m_iTotal++;
		return true;
	}

	/// store all entries into specified location in sorted order, and remove them from queue
	void Flatten ( CSphMatch * pTo, int iTag )
	{
		CountDistinct ();
		SortGroups ();

		int iLen = GetLength ();
		for ( int i=0; i<iLen; i++, pTo++ )
		{
			pTo[0] = m_pData[i];
			if ( iTag>=0 )
				pTo->m_iTag = iTag;
		}

		m_iUsed = 0;
		m_iTotal = 0;
	}

	/// get entries count
	int GetLength () const
	{
		return Min ( m_iUsed, m_iLimit );
	}

	/// set group comparator state
	void SetGroupState ( const CSphMatchComparatorState & tState )
	{
		m_tStateGroup = tState;

		if ( DISTINCT && m_iDistinctOffset>=0 )
			for ( int i=0; i<CSphMatchComparatorState::MAX_ATTRS; i++ )
				if ( m_tStateGroup.m_iBitOffset[i]==m_iDistinctOffset )
			{
				m_bSortByDistinct = true;
				break;
			}
	}

protected:
	/// count distinct values if necessary
	void CountDistinct ()
	{
		if ( DISTINCT )
		{
			m_tUniq.Sort ();
			SphGroupKey_t uGroup;
			for ( int iCount = m_tUniq.CountStart(&uGroup); iCount; iCount = m_tUniq.CountNext(&uGroup) )
			{
				CSphMatch ** ppMatch = m_hGroup2Match(uGroup);
				if ( ppMatch )
					(*ppMatch)->m_pRowitems[m_iRowitems+OFF_POSTCALC_DISTINCT] = iCount;
			}
		}
	}

	/// cut worst N groups off the buffer tail
	void CutWorst ()
	{
		// sort groups
		if ( m_bSortByDistinct )
			CountDistinct ();
		SortGroups ();

		// cut groups
		int iCut = m_iLimit * (int)(GROUPBY_FACTOR/2);
		m_iUsed -= iCut;

		// cleanup unused distinct stuff
		if ( DISTINCT )
		{
			// build kill-list
			CSphVector<SphGroupKey_t> dRemove;
			dRemove.Resize ( iCut );

			if ( m_eGroupBy==SPH_GROUPBY_ATTRPAIR )
			{
				for ( int i=0; i<iCut; i++ )
					dRemove[i] = sphCalcGroupKey ( m_pData[m_iUsed+i], m_eGroupBy, m_iGroupbyOffset, m_iGroupbyCount );
			} else
			{
				for ( int i=0; i<iCut; i++ )
					dRemove[i] = m_pData[m_iUsed+i].m_pRowitems[m_iRowitems+OFF_POSTCALC_GROUP];
			}

			// sort and compact
			if ( !m_bSortByDistinct )
				m_tUniq.Sort ();
			m_tUniq.Compact ( &dRemove[0], iCut );
		}

		// rehash
		m_hGroup2Match.Reset ();
		if ( m_eGroupBy==SPH_GROUPBY_ATTRPAIR )
		{
			for ( int i=0; i<m_iUsed; i++ )
			{
				SphGroupKey_t uKey = sphCalcGroupKey ( m_pData[i], m_eGroupBy, m_iGroupbyOffset, m_iGroupbyCount );
				m_hGroup2Match.Add ( &m_pData[i], uKey );
			}
		} else
		{
			for ( int i=0; i<m_iUsed; i++ )
				m_hGroup2Match.Add ( m_pData+i, m_pData[i].m_pRowitems[ m_iRowitems+OFF_POSTCALC_GROUP ] );
		}
	}

	/// sort groups buffer
	void SortGroups ()
	{
		CSphMatch * pData = m_pData;
		int iCount = m_iUsed;

		int st0[32], st1[32], a, b, k, i, j;
		CSphMatch x;

		k = 1;
		st0[0] = 0;
		st1[0] = iCount-1;
		while ( k )
		{
			k--;
			i = a = st0[k];
			j = b = st1[k];
			x = pData [ (a+b)/2 ]; // FIXME! add better median at least
			while ( a<b )
			{
				while ( i<=j )
				{

					while ( COMPGROUP::IsLess ( x, pData[i], m_tStateGroup ) ) i++;
					while ( COMPGROUP::IsLess ( pData[j], x, m_tStateGroup ) ) j--;
					if (i <= j) { Swap ( pData[i], pData[j] ); i++; j--; }
				}

				if ( j-a>=b-i )
				{
					if ( a<j ) { st0[k] = a; st1[k] = j; k++; }
					a = i;
				} else
				{
					if ( i<b ) { st0[k] = i; st1[k] = b; k++; }
					b = j;
				}
			}
		}
	}
};

#if USE_WINDOWS
#pragma warning(default:4127)
#endif

//////////////////////////////////////////////////////////////////////////
// PLAIN SORTING FUNCTORS 
//////////////////////////////////////////////////////////////////////////

/// match sorter
template < bool BITS >
struct MatchRelevanceLt_fn
{
	static inline bool IsLess ( const CSphMatch & a, const CSphMatch & b, const CSphMatchComparatorState & )
	{
		if ( a.m_iWeight!=b.m_iWeight )
			return a.m_iWeight < b.m_iWeight;

		return a.m_iDocID > b.m_iDocID;
	};
};


/// match sorter
template < bool BITS >
struct MatchAttrLt_fn
{
	static inline bool IsLess ( const CSphMatch & a, const CSphMatch & b, const CSphMatchComparatorState & t )
	{
		CSphRowitem aa = t.GetAttr<BITS>(a,0);
		CSphRowitem bb = t.GetAttr<BITS>(b,0);
		if ( aa!=bb )
			return aa<bb;

		if ( a.m_iWeight!=b.m_iWeight )
			return a.m_iWeight < b.m_iWeight;

		return a.m_iDocID > b.m_iDocID;
	};
};


/// match sorter
template < bool BITS >
struct MatchAttrGt_fn
{
	static inline bool IsLess ( const CSphMatch & a, const CSphMatch & b, const CSphMatchComparatorState & t )
	{
		CSphRowitem aa = t.GetAttr<BITS>(a,0);
		CSphRowitem bb = t.GetAttr<BITS>(b,0);
		if ( aa!=bb )
			return aa>bb;

		if ( a.m_iWeight!=b.m_iWeight )
			return a.m_iWeight < b.m_iWeight;

		return a.m_iDocID > b.m_iDocID;
	};
};


/// match sorter
template < bool BITS >
struct MatchTimeSegments_fn
{
	static inline bool IsLess ( const CSphMatch & a, const CSphMatch & b, const CSphMatchComparatorState & t )
	{
		CSphRowitem aa = t.GetAttr<BITS>(a,0);
		CSphRowitem bb = t.GetAttr<BITS>(b,0);
		int iA = GetSegment ( aa, t.m_iNow );
		int iB = GetSegment ( bb, t.m_iNow );
		if ( iA!=iB )
			return iA > iB;

		if ( a.m_iWeight!=b.m_iWeight )
			return a.m_iWeight < b.m_iWeight;

		if ( aa!=bb )
			return aa<bb;

		return a.m_iDocID > b.m_iDocID;
	};

protected:
	static inline int GetSegment ( DWORD iStamp, DWORD iNow )
	{
		if ( iStamp>=iNow-3600 ) return 0; // last hour
		if ( iStamp>=iNow-24*3600 ) return 1; // last day
		if ( iStamp>=iNow-7*24*3600 ) return 2; // last week
		if ( iStamp>=iNow-30*24*3600 ) return 3; // last month
		if ( iStamp>=iNow-90*24*3600 ) return 4; // last 3 months
		return 5; // everything else
	}
};

/////////////////////////////////////////////////////////////////////////////

#define SPH_TEST_PAIR(_aa,_bb,_idx ) \
	if ( (_aa)!=(_bb) ) \
		return ( (t.m_uAttrDesc>>(_idx))&1 ) ^ ( (_aa)>(_bb) );


#define SPH_TEST_KEYPART(_idx) \
	switch ( t.m_iAttr[_idx] ) \
	{ \
		case SPH_VATTR_ID:			SPH_TEST_PAIR ( a.m_iDocID, b.m_iDocID, _idx ); break; \
		case SPH_VATTR_RELEVANCE:	SPH_TEST_PAIR ( a.m_iWeight, b.m_iWeight, _idx ); break; \
		default: \
		{ \
			register CSphRowitem aa = t.GetAttr<BITS> ( a, _idx ); \
			register CSphRowitem bb = t.GetAttr<BITS> ( b, _idx ); \
			SPH_TEST_PAIR ( aa, bb, _idx ); \
			break; \
		} \
	}


template < bool BITS >
struct MatchGeneric2_fn
{
	static inline bool IsLess ( const CSphMatch & a, const CSphMatch & b, const CSphMatchComparatorState & t )
	{
			SPH_TEST_KEYPART(0);
		SPH_TEST_KEYPART(1);
		return false;
	};
};


template < bool BITS >
struct MatchGeneric3_fn
{
	static inline bool IsLess ( const CSphMatch & a, const CSphMatch & b, const CSphMatchComparatorState & t )
	{
		SPH_TEST_KEYPART(0);
		SPH_TEST_KEYPART(1);
		SPH_TEST_KEYPART(2);
		return false;
	};
};


template < bool BITS >
struct MatchGeneric4_fn
{
	static inline bool IsLess ( const CSphMatch & a, const CSphMatch & b, const CSphMatchComparatorState & t )
	{
		SPH_TEST_KEYPART(0);
		SPH_TEST_KEYPART(1);
		SPH_TEST_KEYPART(2);
		SPH_TEST_KEYPART(3);
		return false;
	};
};


template < bool BITS >
struct MatchGeneric5_fn
{
	static inline bool IsLess ( const CSphMatch & a, const CSphMatch & b, const CSphMatchComparatorState & t )
	{
		SPH_TEST_KEYPART(0);
		SPH_TEST_KEYPART(1);
		SPH_TEST_KEYPART(2);
		SPH_TEST_KEYPART(3);
		SPH_TEST_KEYPART(4);
		return false;
	};
};

//////////////////////////////////////////////////////////////////////////

template < bool BITS >
struct MatchCustom_fn
{
	// setup sorting state
	static bool SetupAttr ( const CSphSchema & tSchema, CSphMatchComparatorState & tState, CSphString & sError, int iIdx, const char * sAttr )
	{
		if ( iIdx>=CSphMatchComparatorState::MAX_ATTRS )
		{
			sError.SetSprintf ( "custom sort: too many attributes declared" );
			return false;
		}

		int iAttr = tSchema.GetAttrIndex(sAttr);
		if ( iAttr<0 )
		{
			sError.SetSprintf ( "custom sort: attr '%s' not found in schema", sAttr );
			return false;
		}

		const CSphColumnInfo & tAttr = tSchema.GetAttr(iAttr);
		tState.m_iAttr[iIdx] = iAttr;
		tState.m_iBitOffset[iIdx] = tAttr.m_iBitOffset;
		tState.m_iBitCount[iIdx] = tAttr.m_iBitCount;
		tState.m_iRowitem[iIdx] = tAttr.m_iRowitem;
		return true;
	}

	// setup sorting state
	static bool Setup ( const CSphSchema & tSchema, CSphMatchComparatorState & tState, CSphString & sError )
	{
		float fTmp;
		int iAttr = 0;

#define MATCH_FUNCTION				fTmp
#define MATCH_WEIGHT				1.0f
#define MATCH_NOW					1.0f
#define MATCH_ATTR(_idx)			1.0f
#define MATCH_DECLARE_ATTR(_name)	if ( !SetupAttr ( tSchema, tState, sError, iAttr++, _name ) ) return false;
#include "sphinxcustomsort.inl"
;

		return true;
	}

	// calc function and compare matches
	// OPTIMIZE? could calc once per match on submit
	static inline bool IsLess ( const CSphMatch & a, const CSphMatch & b, const CSphMatchComparatorState & t )
	{
#undef MATCH_DECLARE_ATTR
#undef MATCH_WEIGHT
#undef MATCH_NOW
#undef MATCH_ATTR
#define MATCH_DECLARE_ATTR(_name)	;
#define MATCH_WEIGHT				float(MATCH_VAR.m_iWeight)
#define MATCH_NOW					float(t.m_iNow)
#define MATCH_ATTR(_idx)			float(t.GetAttr<BITS>(MATCH_VAR,_idx))

		float aa, bb;

#undef MATCH_FUNCTION
#undef MATCH_VAR
#define MATCH_FUNCTION aa
#define MATCH_VAR a
#include "sphinxcustomsort.inl"
;

#undef MATCH_FUNCTION
#undef MATCH_VAR
#define MATCH_FUNCTION bb
#define MATCH_VAR b
#include "sphinxcustomsort.inl"
;

		return aa<bb;
	}
};

//////////////////////////////////////////////////////////////////////////
// SORT CLAUSE PARSER
//////////////////////////////////////////////////////////////////////////

static const int MAX_SORT_FIELDS = 5; // MUST be in sync with CSphMatchComparatorState::m_iAttr


enum ESphSortFunc
{
	FUNC_REL_DESC,
	FUNC_ATTR_DESC,
	FUNC_ATTR_ASC,
	FUNC_TIMESEGS,
	FUNC_GENERIC2,
	FUNC_GENERIC3,
	FUNC_GENERIC4,
	FUNC_GENERIC5,
	FUNC_CUSTOM
};


enum ESortClauseParseResult
{
	SORT_CLAUSE_OK,
	SORT_CLAUSE_ERROR,
	SORT_CLAUSE_RANDOM
};


enum
{
	FIXUP_GEODIST	= -1000,
	FIXUP_COUNT		= -1001,
	FIXUP_GROUP		= -1002,
	FIXUP_DISTINCT	= -1003
};


static ESortClauseParseResult sphParseSortClause ( const char * sClause, const CSphQuery * pQuery,
	const CSphSchema & tSchema, bool bGroupClause,
	ESphSortFunc & eFunc, CSphMatchComparatorState & tState, CSphString & sError )
{
	// mini parser
	ISphTokenizer * pTokenizer = sphCreateSBCSTokenizer ();
	if ( !pTokenizer )
	{
		sError.SetSprintf ( "INTERNAL ERROR: failed to create tokenizer when parsing sort clause" );
		return SORT_CLAUSE_ERROR;
	}

	CSphString sTmp;
	pTokenizer->SetCaseFolding ( "0..9, A..Z->a..z, _, a..z, @", sTmp );

	CSphString sSortClause ( sClause );
	pTokenizer->SetBuffer ( (BYTE*)sSortClause.cstr(), strlen(sSortClause.cstr()), true );

	bool bField = false; // whether i'm expecting field name or sort order
	int iField = 0;

	for ( const char * pTok=(char*)pTokenizer->GetToken(); pTok; pTok=(char*)pTokenizer->GetToken() )
	{
		bField = !bField;

		// special case, sort by random
		if ( iField==0 && bField && strcmp ( pTok, "@random" )==0 )
			return SORT_CLAUSE_RANDOM;

		// special case, sort by custom
		if ( iField==0 && bField && strcmp ( pTok, "@custom" )==0 )
		{
			eFunc = FUNC_CUSTOM;
			return MatchCustom_fn<false>::Setup ( tSchema, tState, sError ) ? SORT_CLAUSE_OK : SORT_CLAUSE_ERROR;
		}

		// handle sort order
		if ( !bField )
		{
			// check
			if ( strcmp ( pTok, "desc" ) && strcmp ( pTok, "asc" ) )
			{
				sError.SetSprintf ( "invalid sorting order '%s'", pTok );
				return SORT_CLAUSE_ERROR;
			}

			// set
			if ( !strcmp ( pTok, "desc" ) )
				tState.m_uAttrDesc |= ( 1<<iField );

			iField++;
			continue;
		}

		// handle field name
		if ( iField==MAX_SORT_FIELDS )
		{
			sError.SetSprintf ( "too much sort-by fields; maximum count is %d", MAX_SORT_FIELDS );
			return SORT_CLAUSE_ERROR;
		}

		if ( !strcasecmp ( pTok, "@relevance" )
			|| !strcasecmp ( pTok, "@rank" )
			|| !strcasecmp ( pTok, "@weight" ) )
		{
			tState.m_iAttr[iField] = SPH_VATTR_RELEVANCE;

		} else if ( !strcasecmp ( pTok, "@id" ) )
		{
			tState.m_iAttr[iField] = SPH_VATTR_ID;

		} else if ( !strcasecmp ( pTok, "@geodist" ) )
		{
			tState.m_iAttr[iField] = FIXUP_GEODIST;

		} else if ( !strcasecmp ( pTok, "@count" ) && bGroupClause )
		{
			if ( pQuery->m_iGroupbyOffset<0 )
			{
				sError.SetSprintf ( "no group-by attribute; can not sort by @count" );
				return SORT_CLAUSE_ERROR;
			}
			tState.m_iAttr[iField] = FIXUP_COUNT;

		} else if ( !strcasecmp ( pTok, "@group" ) && bGroupClause )
		{
			if ( pQuery->m_iGroupbyOffset<0 )
			{
				sError.SetSprintf ( "no group-by attribute; can not sort by @group" );
				return SORT_CLAUSE_ERROR;
			}
			tState.m_iAttr[iField] = FIXUP_GROUP;

		} else if ( !strcasecmp ( pTok, "@distinct" ) && bGroupClause )
		{
			if ( pQuery->m_iDistinctOffset<0 )
			{
				sError.SetSprintf ( "no count-distinct attribute; can not sort by @distinct" );
				return SORT_CLAUSE_ERROR;
			}
			tState.m_iAttr[iField] = FIXUP_DISTINCT;

		} else
		{
			int iAttr = tSchema.GetAttrIndex ( pTok );
			if ( iAttr<0 )
			{
				sError.SetSprintf ( "sort-by attribute '%s' not found", pTok );
				return SORT_CLAUSE_ERROR;
			}
			tState.m_iAttr[iField] = iAttr;
			tState.m_iRowitem[iField] = tSchema.GetAttr(iAttr).m_iRowitem;
			tState.m_iBitOffset[iField] = tSchema.GetAttr(iAttr).m_iBitOffset;
			tState.m_iBitCount[iField] = tSchema.GetAttr(iAttr).m_iBitCount;
		}
	}

	if ( iField==0 )
	{
		sError.SetSprintf ( "no sort order defined" );
		return SORT_CLAUSE_ERROR;
	}

	if ( iField==1 )
		tState.m_iAttr[iField++] = SPH_VATTR_ID; // add "id ASC"

	switch ( iField )
	{
		case 2:		eFunc = FUNC_GENERIC2; break;
		case 3:		eFunc = FUNC_GENERIC3; break;
		case 4:		eFunc = FUNC_GENERIC4; break;
		case 5:		eFunc = FUNC_GENERIC5; break;
		default:	sError.SetSprintf ( "INTERNAL ERROR: %d fields in sphParseSortClause()", iField ); return SORT_CLAUSE_ERROR;
	}
	return SORT_CLAUSE_OK;
}

//////////////////////////////////////////////////////////////////////////
// SORTING+GROUPING INSTANTIATION
//////////////////////////////////////////////////////////////////////////

template < typename COMPMATCH, typename COMPGROUP, typename ARG >
static ISphMatchSorter * sphCreateSorter3rd ( bool bDistinct, ARG arg )
{
	if ( bDistinct==true )
		return new CSphKBufferGroupSorter<COMPMATCH,COMPGROUP,true> ( arg );
	else
		return new CSphKBufferGroupSorter<COMPMATCH,COMPGROUP,false> ( arg );
}


template < typename COMPMATCH, typename ARG >
static ISphMatchSorter * sphCreateSorter2nd ( ESphSortFunc eGroupFunc, bool bGroupBits, bool bDistinct, ARG arg )
{
	if ( bGroupBits )
	{
		switch ( eGroupFunc )
		{
			case FUNC_GENERIC2:		return sphCreateSorter3rd<COMPMATCH,MatchGeneric2_fn<true> >	( bDistinct, arg ); break;
			case FUNC_GENERIC3:		return sphCreateSorter3rd<COMPMATCH,MatchGeneric3_fn<true> >	( bDistinct, arg ); break;
			case FUNC_GENERIC4:		return sphCreateSorter3rd<COMPMATCH,MatchGeneric4_fn<true> >	( bDistinct, arg ); break;
			case FUNC_GENERIC5:		return sphCreateSorter3rd<COMPMATCH,MatchGeneric5_fn<true> >	( bDistinct, arg ); break;
			default:				return NULL;
		}
	} else
	{
		switch ( eGroupFunc )
		{
			case FUNC_GENERIC2:		return sphCreateSorter3rd<COMPMATCH,MatchGeneric2_fn<false> >	( bDistinct, arg ); break;
			case FUNC_GENERIC3:		return sphCreateSorter3rd<COMPMATCH,MatchGeneric3_fn<false> >	( bDistinct, arg ); break;
			case FUNC_GENERIC4:		return sphCreateSorter3rd<COMPMATCH,MatchGeneric4_fn<false> >	( bDistinct, arg ); break;
			case FUNC_GENERIC5:		return sphCreateSorter3rd<COMPMATCH,MatchGeneric5_fn<false> >	( bDistinct, arg ); break;
			default:				return NULL;
		}
	}
}


template < typename ARG >
static ISphMatchSorter * sphCreateSorter1st ( ESphSortFunc eMatchFunc, bool bMatchBits, ESphSortFunc eGroupFunc, bool bGroupBits, bool bDistinct, ARG arg )
{
	if ( bMatchBits )
	{
		switch ( eMatchFunc )
		{
			case FUNC_REL_DESC:		return sphCreateSorter2nd<MatchRelevanceLt_fn<true> >	( eGroupFunc, bGroupBits, bDistinct, arg ); break;
			case FUNC_ATTR_DESC:	return sphCreateSorter2nd<MatchAttrLt_fn<true> >		( eGroupFunc, bGroupBits, bDistinct, arg ); break;
			case FUNC_ATTR_ASC:		return sphCreateSorter2nd<MatchAttrGt_fn<true> >		( eGroupFunc, bGroupBits, bDistinct, arg ); break;
			case FUNC_TIMESEGS:		return sphCreateSorter2nd<MatchTimeSegments_fn<true> >	( eGroupFunc, bGroupBits, bDistinct, arg ); break;
			case FUNC_GENERIC2:		return sphCreateSorter2nd<MatchGeneric2_fn<true> >		( eGroupFunc, bGroupBits, bDistinct, arg ); break;
			case FUNC_GENERIC3:		return sphCreateSorter2nd<MatchGeneric3_fn<true> >		( eGroupFunc, bGroupBits, bDistinct, arg ); break;
			case FUNC_GENERIC4:		return sphCreateSorter2nd<MatchGeneric4_fn<true> >		( eGroupFunc, bGroupBits, bDistinct, arg ); break;
			case FUNC_GENERIC5:		return sphCreateSorter2nd<MatchGeneric5_fn<true> >		( eGroupFunc, bGroupBits, bDistinct, arg ); break;
			default:				return NULL;
		}
	} else
	{
		switch ( eMatchFunc )
		{
			case FUNC_REL_DESC:		return sphCreateSorter2nd<MatchRelevanceLt_fn<false> >	( eGroupFunc, bGroupBits, bDistinct, arg ); break;
			case FUNC_ATTR_DESC:	return sphCreateSorter2nd<MatchAttrLt_fn<false> >		( eGroupFunc, bGroupBits, bDistinct, arg ); break;
			case FUNC_ATTR_ASC:		return sphCreateSorter2nd<MatchAttrGt_fn<false> >		( eGroupFunc, bGroupBits, bDistinct, arg ); break;
			case FUNC_TIMESEGS:		return sphCreateSorter2nd<MatchTimeSegments_fn<false> >	( eGroupFunc, bGroupBits, bDistinct, arg ); break;
			case FUNC_GENERIC2:		return sphCreateSorter2nd<MatchGeneric2_fn<false> >		( eGroupFunc, bGroupBits, bDistinct, arg ); break;
			case FUNC_GENERIC3:		return sphCreateSorter2nd<MatchGeneric3_fn<false> >		( eGroupFunc, bGroupBits, bDistinct, arg ); break;
			case FUNC_GENERIC4:		return sphCreateSorter2nd<MatchGeneric4_fn<false> >		( eGroupFunc, bGroupBits, bDistinct, arg ); break;
			case FUNC_GENERIC5:		return sphCreateSorter2nd<MatchGeneric5_fn<false> >		( eGroupFunc, bGroupBits, bDistinct, arg ); break;
			default:				return NULL;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS (FACTORY AND FLATTENING)
//////////////////////////////////////////////////////////////////////////

static bool UpdateQueryForSchema ( CSphQuery * pQuery, const CSphSchema & tSchema, CSphString & sError )
{
	pQuery->m_iGroupbyOffset = -1;
	pQuery->m_iDistinctOffset = -1;

	if ( pQuery->m_sGroupBy.IsEmpty() )
		return true;

	// setup gropuby attr
	int iGroupBy = tSchema.GetAttrIndex ( pQuery->m_sGroupBy.cstr() );
	if ( iGroupBy<0 )
	{
		sError.SetSprintf ( "group-by attribute '%s' not found", pQuery->m_sGroupBy.cstr() );
		return false;
	}
	if ( pQuery->m_eGroupFunc==SPH_GROUPBY_ATTRPAIR && iGroupBy+1>=tSchema.GetAttrsCount() )
	{
		sError.SetSprintf ( "group-by attribute '%s' must not be last in ATTRPAIR grouping mode", pQuery->m_sGroupBy.cstr() );
		return false;
	}
	pQuery->m_iGroupbyOffset = tSchema.GetAttr(iGroupBy).m_iBitOffset;
	pQuery->m_iGroupbyCount = tSchema.GetAttr(iGroupBy).m_iBitCount;

	// setup distinct attr
	if ( !pQuery->m_sGroupDistinct.IsEmpty() )
	{
		int iDistinct = tSchema.GetAttrIndex ( pQuery->m_sGroupDistinct.cstr() );
		if ( iDistinct<0 )
		{
			sError.SetSprintf ( "group-count-distinct attribute '%s' not found", pQuery->m_sGroupDistinct.cstr() );
			return false;
		}

		pQuery->m_iDistinctOffset = tSchema.GetAttr(iDistinct).m_iBitOffset;
		pQuery->m_iDistinctCount = tSchema.GetAttr(iDistinct).m_iBitCount;
	}

	return true;
}


ISphMatchSorter * sphCreateQueue ( CSphQuery * pQuery, const CSphSchema & tSchema, CSphString & sError )
{
	// lookup proper attribute index to group by, if any
	if ( !UpdateQueryForSchema ( pQuery, tSchema, sError ) )
		return NULL;
	assert ( pQuery->m_sGroupBy.IsEmpty() || pQuery->m_iGroupbyOffset>=0 );

	// prepare
	ISphMatchSorter * pTop = NULL;
	CSphMatchComparatorState tStateMatch, tStateGroup;

	sError = "";

	////////////////////////////////////
	// choose and setup sorting functor
	////////////////////////////////////

	ESphSortFunc eMatchFunc = FUNC_REL_DESC;
	ESphSortFunc eGroupFunc = FUNC_REL_DESC;
	bool bUsesAttrs = false;
	bool bRandomize = false;
	pQuery->m_bCalcGeodist = false;

	// matches sorting function
	if ( pQuery->m_eSort==SPH_SORT_EXTENDED )
	{
		ESortClauseParseResult eRes = sphParseSortClause ( pQuery->m_sSortBy.cstr(), pQuery, tSchema,
			false, eMatchFunc, tStateMatch, sError );

		if ( eRes==SORT_CLAUSE_ERROR )
			return false;

		if ( eRes==SORT_CLAUSE_RANDOM )
			bRandomize = true;

		for ( int i=0; i<CSphMatchComparatorState::MAX_ATTRS; i++ )
		{
			if ( tStateMatch.m_iAttr[i]>=0 )
				bUsesAttrs = true;
			if ( tStateMatch.m_iAttr[i]==FIXUP_GEODIST )
			{
				bUsesAttrs = true;
				pQuery->m_bCalcGeodist = true;
			}
		}

	} else
	{
		// check sort-by attribute
		if ( pQuery->m_eSort!=SPH_SORT_RELEVANCE )
		{
			tStateMatch.m_iAttr[0] = tSchema.GetAttrIndex ( pQuery->m_sSortBy.cstr() );
			if ( tStateMatch.m_iAttr[0]<0 )
			{
				sError.SetSprintf ( "sort-by attribute '%s' not found", pQuery->m_sSortBy.cstr() );
				return false;
			}

			const CSphColumnInfo & tAttr = tSchema.GetAttr ( tStateMatch.m_iAttr[0] );
			tStateMatch.m_iRowitem[0] = tAttr.m_iRowitem;
			tStateMatch.m_iBitOffset[0] = tAttr.m_iBitOffset;
			tStateMatch.m_iBitCount[0] = tAttr.m_iBitCount;
		}
	
		// find out what function to use and whether it needs attributes
		bUsesAttrs = true;
		switch ( pQuery->m_eSort )
		{
			case SPH_SORT_ATTR_DESC:		eMatchFunc = FUNC_ATTR_DESC; break;
			case SPH_SORT_ATTR_ASC:			eMatchFunc = FUNC_ATTR_ASC; break;
			case SPH_SORT_TIME_SEGMENTS:	eMatchFunc = FUNC_TIMESEGS; break;
			case SPH_SORT_RELEVANCE:		eMatchFunc = FUNC_REL_DESC; bUsesAttrs = false; break;
			default:
				sError.SetSprintf ( "unknown sorting mode %d", pQuery->m_eSort );
				return false;
		}
	}

	// groups sorting function
	if ( pQuery->m_iGroupbyOffset>=0 )
	{
		ESortClauseParseResult eRes = sphParseSortClause ( pQuery->m_sGroupSortBy.cstr(), pQuery, tSchema,
			true, eGroupFunc, tStateGroup, sError );

		if ( eRes==SORT_CLAUSE_ERROR || eRes==SORT_CLAUSE_RANDOM )
		{
			if ( eRes==SORT_CLAUSE_RANDOM )
				sError.SetSprintf ( "groups can not be sorted by @random" );
			return false;
		}
	}

	// update for calculated stuff
	ARRAY_FOREACH ( i, pQuery->m_dFilters )
		if ( pQuery->m_dFilters[i].m_sAttrName=="@geodist" )
	{
		pQuery->m_bCalcGeodist = true;
		break;
	}

	int iToCalc = ( pQuery->m_bCalcGeodist ? 1 : 0 );
	pQuery->m_iPresortRowitems = tSchema.GetRealRowSize() + iToCalc;

	for ( int iState=0; iState<2; iState++ )
	{
		CSphMatchComparatorState & tState = iState ? tStateMatch : tStateGroup;
		for ( int i=0; i<CSphMatchComparatorState::MAX_ATTRS; i++ )
		{
			int iOffset = -1;
			switch ( tState.m_iAttr[i] )
			{
				case FIXUP_GEODIST:		iOffset = 0; break;
				case FIXUP_COUNT:		iOffset = iToCalc+OFF_POSTCALC_COUNT; break;
				case FIXUP_GROUP:		iOffset = iToCalc+OFF_POSTCALC_GROUP; break;
				case FIXUP_DISTINCT:	iOffset = iToCalc+OFF_POSTCALC_DISTINCT; break;
			}
			if ( iOffset>=0 )
			{
				tState.m_iAttr[i] = tSchema.GetRealAttrsCount() + iOffset;
				tState.m_iRowitem[i] = tSchema.GetRealRowSize() + iOffset;
				tState.m_iBitOffset[i] = tState.m_iRowitem[i]*ROWITEM_BITS;
				tState.m_iBitCount[i] = ROWITEM_BITS;
			}
		}
	}

	///////////////////
	// spawn the queue
	///////////////////

	bool bMatchBits = tStateMatch.UsesBitfields ();
	bool bGroupBits = tStateGroup.UsesBitfields ();

	if ( pQuery->m_iGroupbyOffset<0 )
	{
		if ( bMatchBits )
		{
			switch ( eMatchFunc )
			{
				case FUNC_REL_DESC:	pTop = new CSphMatchQueue<MatchRelevanceLt_fn<true> >	( pQuery->m_iMaxMatches, bUsesAttrs ); break;
				case FUNC_ATTR_DESC:pTop = new CSphMatchQueue<MatchAttrLt_fn<true> >		( pQuery->m_iMaxMatches, bUsesAttrs ); break;
				case FUNC_ATTR_ASC:	pTop = new CSphMatchQueue<MatchAttrGt_fn<true> >		( pQuery->m_iMaxMatches, bUsesAttrs ); break;
				case FUNC_TIMESEGS:	pTop = new CSphMatchQueue<MatchTimeSegments_fn<true> >	( pQuery->m_iMaxMatches, bUsesAttrs ); break;
				case FUNC_GENERIC2:	pTop = new CSphMatchQueue<MatchGeneric2_fn<true> >		( pQuery->m_iMaxMatches, bUsesAttrs ); break;
				case FUNC_GENERIC3:	pTop = new CSphMatchQueue<MatchGeneric3_fn<true> >		( pQuery->m_iMaxMatches, bUsesAttrs ); break;
				case FUNC_GENERIC4:	pTop = new CSphMatchQueue<MatchGeneric4_fn<true> >		( pQuery->m_iMaxMatches, bUsesAttrs ); break;
				case FUNC_GENERIC5:	pTop = new CSphMatchQueue<MatchGeneric5_fn<true> >		( pQuery->m_iMaxMatches, bUsesAttrs ); break;
				case FUNC_CUSTOM:	pTop = new CSphMatchQueue<MatchCustom_fn<true> >		( pQuery->m_iMaxMatches, bUsesAttrs ); break;
				default:			pTop = NULL;
			}
		} else
		{
			switch ( eMatchFunc )
			{
				case FUNC_REL_DESC:	pTop = new CSphMatchQueue<MatchRelevanceLt_fn<false> >	( pQuery->m_iMaxMatches, bUsesAttrs ); break;
				case FUNC_ATTR_DESC:pTop = new CSphMatchQueue<MatchAttrLt_fn<false> >		( pQuery->m_iMaxMatches, bUsesAttrs ); break;
				case FUNC_ATTR_ASC:	pTop = new CSphMatchQueue<MatchAttrGt_fn<false> >		( pQuery->m_iMaxMatches, bUsesAttrs ); break;
				case FUNC_TIMESEGS:	pTop = new CSphMatchQueue<MatchTimeSegments_fn<false> >	( pQuery->m_iMaxMatches, bUsesAttrs ); break;
				case FUNC_GENERIC2:	pTop = new CSphMatchQueue<MatchGeneric2_fn<false> >		( pQuery->m_iMaxMatches, bUsesAttrs ); break;
				case FUNC_GENERIC3:	pTop = new CSphMatchQueue<MatchGeneric3_fn<false> >		( pQuery->m_iMaxMatches, bUsesAttrs ); break;
				case FUNC_GENERIC4:	pTop = new CSphMatchQueue<MatchGeneric4_fn<false> >		( pQuery->m_iMaxMatches, bUsesAttrs ); break;
				case FUNC_GENERIC5:	pTop = new CSphMatchQueue<MatchGeneric5_fn<false> >		( pQuery->m_iMaxMatches, bUsesAttrs ); break;
				case FUNC_CUSTOM:	pTop = new CSphMatchQueue<MatchCustom_fn<false> >		( pQuery->m_iMaxMatches, bUsesAttrs ); break;
				default:			pTop = NULL;
			}
		}

	} else
	{
		pTop = sphCreateSorter1st ( eMatchFunc, bMatchBits, eGroupFunc, bGroupBits, pQuery->m_iDistinctOffset>=0, pQuery );
	}

	if ( !pTop )
	{
		sError.SetSprintf ( "internal error: unhandled sorting mode (match-sort=%d, group=%d, group-sort=%d)",
			pQuery->m_iGroupbyOffset>=0, eMatchFunc, eGroupFunc );
		return false;
	}

	assert ( pTop );
	pTop->SetState ( tStateMatch );
	pTop->SetGroupState ( tStateGroup );
	pTop->m_bRandomize = bRandomize;
	return pTop;
}


void sphFlattenQueue ( ISphMatchSorter * pQueue, CSphQueryResult * pResult, int iTag )
{
	if ( pQueue && pQueue->GetLength() )
	{
		int iOffset = pResult->m_dMatches.GetLength ();
		pResult->m_dMatches.Resize ( iOffset + pQueue->GetLength() );
		pQueue->Flatten ( &pResult->m_dMatches[iOffset], iTag );
	}
}

//
// $Id$
//