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
#include "sphinxexcerpt.h"
#include <ctype.h>

/////////////////////////////////////////////////////////////////////////////
// THE EXCERPTS GENERATOR
/////////////////////////////////////////////////////////////////////////////

class ExcerptGen_c
{
public:
							ExcerptGen_c ();
							~ExcerptGen_c () {}

	char *					BuildExcerpt ( const ExcerptQuery_t & q, CSphDict * pDict, ISphTokenizer * pTokenizer );

public:
	enum Token_e
	{
		TOK_NONE = 0,		///< unspecified type, also used as the end marker
		TOK_WORD,			///< just a word
		TOK_SPACE,			///< whitespace chars seq
		TOK_NONWORD,		///< non-word, non-space chars seq
		TOK_BREAK			///< non-word chars seq which delimit a phrase part or boundary
	};

	struct Token_t
	{
		Token_e				m_eType;		///< token type
		int					m_iStart;		///< token start (index in codepoints array)
		int					m_iLength;		///< token length (in codepoints)
		int					m_iWeight;		///< token weight
		DWORD				m_uWords;		///< matching query words mask
		SphWordID_t			m_iWordID;		///< token word ID from dictionary
	};

	struct Passage_t
	{
		int					m_iStart;		///< start token index
		int					m_iTokens;		///< token count
		int					m_iCodes;		///< codepoints count
		DWORD				m_uWords;		///< matching query words mask
		int					m_iWordsWeight;	///< passage weight factor
		int					m_iWordCount;	///< passage weight factor
		int					m_iMaxLCS;		///< passage weight factor
		int					m_iMinGap;		///< passage weight factor

		void Reset ()
		{
			m_iStart = 0;
			m_iTokens = 0;
			m_iCodes = 0;
			m_uWords = 0;
			m_iWordsWeight = 0;
			m_iWordCount = 0;
			m_iMaxLCS = 0;
			m_iMinGap = 0;
		}

		inline int GetWeight () const
		{
			return m_iWordCount + m_iWordsWeight*m_iMaxLCS + m_iMinGap;
		}
	};

protected:
	CSphVector<int>			m_dCodes;		///< original source text codepoints
	CSphVector<Token_t>		m_dTokens;		///< source text tokens
	CSphVector<Token_t>		m_dWords;		///< query words tokens

	CSphDict *				m_pDict;
	BYTE					m_sAccum [ 3*SPH_MAX_WORD_LEN+3 ];
	BYTE *					m_pAccum;
	int						m_iAccum;

	Token_t					m_tTok;			///< currently decoded token

	CSphVector<BYTE>		m_dResult;		///< result holder
	int						m_iResultLen;	///< result codepoints count

	CSphLowercaser			m_tLC;

	CSphVector<Passage_t>	m_dPassages;	///< extracted passages

	bool					m_bUtf8;
	bool					m_bExactPhrase;

protected:
	void					DecodeText ( const char * sText, CSphVector<Token_t> & dBuf );
	void					SubmitCodepoint ( CSphVector<Token_t> & dBuf, int iCode );
	void					AccumulateCodepoint ( int iCode );

	bool					TokensMatch ( const Token_t & a, const Token_t & b);
	int						TokenLen ( int iPos, int bRemoveSpaces );

	void					CalcPassageWeight ( const CSphVector<int> & dPassage, Passage_t & tPass, int iMaxWords, int iWordCountCoeff );
	bool					ExtractPassages ( const ExcerptQuery_t & q );
	bool					ExtractPhrases ( const ExcerptQuery_t & q );

	void					HighlightAll ( const ExcerptQuery_t & q );
	void					HighlightStart ( const ExcerptQuery_t & q );
	bool					HighlightBestPassages ( const ExcerptQuery_t & q );

	void					ResultEmit ( int iCode );
	void					ResultEmit ( const char * sLine );
	void					ResultEmit ( const Token_t & sTok );
};

/////////////////////////////////////////////////////////////////////////////

inline bool operator < ( const ExcerptGen_c::Token_t & a, const ExcerptGen_c::Token_t & b )
{
	if ( a.m_iLength==b.m_iLength )
		return a.m_iStart > b.m_iStart;
	return a.m_iLength < b.m_iLength;
}


inline bool operator < ( const ExcerptGen_c::Passage_t & a, const ExcerptGen_c::Passage_t & b )
{
	if ( a.GetWeight()==b.GetWeight() )
		return a.m_iCodes > b.m_iCodes;
	return a.GetWeight() > b.GetWeight();
}


ExcerptGen_c::ExcerptGen_c ()
{
	m_tTok.m_eType = TOK_NONE;
	m_tTok.m_iStart = -1;
	m_tTok.m_iLength = -1;

	m_pDict = NULL;
	m_pAccum = m_sAccum;
	m_iAccum = 0;

	m_bUtf8 = true;
	m_bExactPhrase = false;
}


char * ExcerptGen_c::BuildExcerpt ( const ExcerptQuery_t & q, CSphDict * pDict, ISphTokenizer * pTokenizer )
{
	m_pDict = pDict;
	m_bUtf8 = pTokenizer->IsUtf8 ();
	m_tLC.SetRemap ( pTokenizer->GetLowercaser() );

	// decode everything
	m_dCodes.Reserve ( 8192 );
	m_dTokens.Reserve ( 1024 );
	DecodeText ( q.m_sSource.cstr(), m_dTokens );
	int iSourceCodes = m_dCodes.GetLength ();

	DecodeText ( q.m_sWords.cstr(), m_dWords );

	// remove non-words
	ARRAY_FOREACH ( i, m_dWords )
		if ( m_dWords[i].m_eType!=TOK_WORD || m_dWords[i].m_iWordID==0 )
			m_dWords.Remove ( i-- );

	m_bExactPhrase = q.m_bExactPhrase && ( m_dWords.GetLength()>1 );

	// truncate the array
	if ( m_dWords.GetLength()>SPH_MAX_QUERY_WORDS )
		m_dWords.Resize ( SPH_MAX_QUERY_WORDS );

	// assign word weights
	ARRAY_FOREACH ( i, m_dWords )
		m_dWords[i].m_iWeight = m_dWords[i].m_iLength; // FIXME! should obtain freqs from dict

	// reset result
	m_dResult.Reserve ( 16384 );
	m_dResult.Resize ( 0 );
	m_iResultLen = 0;

	// calc matching word masks
	ARRAY_FOREACH ( iTok, m_dTokens )
	{
		m_dTokens[iTok].m_uWords = 0;
		ARRAY_FOREACH ( iWord, m_dWords )
			if ( TokensMatch ( m_dTokens[iTok], m_dWords[iWord] ) )
				m_dTokens[iTok].m_uWords |= (1UL<<iWord);
	}

	// do highlighting
	if ( q.m_iLimit<=0 || q.m_iLimit>iSourceCodes )
	{
		HighlightAll ( q );

	} else
	{
		if ( !( ExtractPassages ( q ) && HighlightBestPassages ( q ) ) )
			HighlightStart ( q );
	}

	// cleanup
	m_dCodes.Reset ();
	m_dTokens.Reset ();
	m_dWords.Reset ();

	// alloc, fill and return the result
	m_dResult.Add ( 0 );
	char * pRes = new char [ m_dResult.GetLength() ];
	memcpy ( pRes, &m_dResult[0], m_dResult.GetLength() );
	m_dResult.Reset ();

	return pRes;
}


void ExcerptGen_c::HighlightAll ( const ExcerptQuery_t & q )
{
	bool bOpen = false;
	const int iMaxTok = m_dTokens.GetLength()-1; // skip last one, it's TOK_NONE

	if ( m_bExactPhrase )
	{
		// exact phrase
		for ( int iCur=0; iCur<iMaxTok; )
		{
			// skip non-opening words
			while ( iCur<iMaxTok && !( m_dTokens[iCur].m_uWords & 1 ))
				ResultEmit ( m_dTokens[iCur++] );

			// check if we have enough words left
			if ( iCur+m_dWords.GetLength()-1>=iMaxTok )
			{
				// not enough, just copy the tail
				while ( iCur<iMaxTok )
					ResultEmit ( m_dTokens[iCur++] );
				break;
			}

			// lookahead
			assert ( iCur>=0 && iCur<iMaxTok );
			assert ( m_dTokens[iCur].m_uWords & 1 );

			int iLookahead = 1; // current lookahead position
			int iMatched = 1; // phrase words matched so far
			while ( iCur+iLookahead<iMaxTok && iMatched<m_dWords.GetLength() )
			{
				const Token_t & tTok = m_dTokens[iCur+iLookahead];
				if ( !tTok.m_uWords )
				{
					iLookahead++;
					continue;
				}

				if (!( tTok.m_uWords & (1<<iMatched) ))
					break;

				iLookahead++;
				iMatched++;
			}

			// emit looked-ahead tokens
			if ( iMatched==m_dWords.GetLength() )
				ResultEmit ( q.m_sBeforeMatch.cstr() );

			while ( iLookahead-- )
				ResultEmit ( m_dTokens[iCur++] );

			if ( iMatched==m_dWords.GetLength() )
				ResultEmit ( q.m_sAfterMatch.cstr() );
		}

	} else
	{
		// bag of words
		for ( int iTok=0; iTok<iMaxTok; iTok++ )
		{
			if ( ( m_dTokens[iTok].m_uWords!=0 ) ^ bOpen )
			{
				ResultEmit ( bOpen ? q.m_sAfterMatch.cstr() : q.m_sBeforeMatch.cstr() );
				bOpen = !bOpen;
			}
			ResultEmit ( m_dTokens[iTok] );
		}
		if ( bOpen )
			ResultEmit ( q.m_sAfterMatch.cstr() );
	}
}


void ExcerptGen_c::HighlightStart ( const ExcerptQuery_t & q )
{
	// no matches found. just show the starting tokens
	int i = 0;
	while ( m_iResultLen+m_dTokens[i].m_iLength < q.m_iLimit )
	{
		ResultEmit ( m_dTokens[i++] );
		if ( i>=m_dTokens.GetLength() )
			break;
	}
	ResultEmit ( q.m_sChunkSeparator.cstr() );
}


void ExcerptGen_c::DecodeText ( const char * sText, CSphVector<Token_t> & dBuf )
{
	BYTE * pCur = (BYTE*) sText;

	// SBCS decoder
	if ( !m_bUtf8 )
	{
		while ( *pCur )
			SubmitCodepoint ( dBuf, *pCur++ );
		SubmitCodepoint ( dBuf, 0 );
		return;
	}

	// UTF-8 decoder
	while ( *pCur )
	{
		BYTE v = *pCur++;

		if ( v<128 )
		{
			SubmitCodepoint ( dBuf, v );
			continue;
		}

		// get number of bytes
		int iBytes = 0;
		while ( v & 0x80 )
		{
			iBytes++;
			v <<= 1;
		}

		// check for valid number of bytes
		if ( iBytes<2 || iBytes>4 )
			continue;

		int iCode = ( v>>iBytes );
		iBytes--;
		do
		{
			if ( !(*pCur) )
			{
				SubmitCodepoint ( dBuf, 0 );
				return; // EOF
			}
			if ( ((*pCur) & 0xC0)!=0x80 )
				break; // broken code

			iCode = ( iCode<<6 ) + ( (*pCur) & 0x3F );
			iBytes--;
			pCur++;
		} while ( iBytes );

		// return code point if there were no errors
		// ignore and continue scanning otherwise
		if ( !iBytes )
			SubmitCodepoint ( dBuf, iCode );
	}
	SubmitCodepoint ( dBuf, 0 );
}


/// check if this codepoint is a phrase (part) boundary
bool myisbreak ( int c )
{
	return ( c=='.' || c==';' || c=='?' || c=='!' || c=='\r' || c=='\n' );
}


void ExcerptGen_c::AccumulateCodepoint ( int iCode )
{
	if ( m_tTok.m_eType!=TOK_WORD || m_iAccum>SPH_MAX_WORD_LEN )
		return;

	if ( !m_bUtf8 )
	{
		// SBCS encoder
		assert ( iCode>=0 && iCode<=255 );
		*m_pAccum++ = (BYTE)iCode;

	} else
	{
		// UTF-8 encoder
		if ( iCode<0x80 )
		{
			*m_pAccum++ = (BYTE)( iCode & 0x7F );

		} else if ( iCode<0x800 )
		{
			*m_pAccum++ = (BYTE)( ( (iCode>>6) & 0x1F ) | 0xC0 );
			*m_pAccum++ = (BYTE)( ( iCode & 0x3F ) | 0x80 );

		} else
		{
			*m_pAccum++ = (BYTE)( ( (iCode>>12) & 0x0F ) | 0xC0 );
			*m_pAccum++ = (BYTE)( ( (iCode>>6) & 0x3F ) | 0x80 );
			*m_pAccum++ = (BYTE)( ( iCode & 0x3F ) | 0x80 );
		}
	}

	assert ( m_pAccum>=m_sAccum && m_pAccum<m_sAccum+sizeof(m_sAccum) );
	m_iAccum++;
}


void ExcerptGen_c::SubmitCodepoint ( CSphVector<Token_t> & dBuf, int iCode )
{
	// find out its type
	Token_e eType = TOK_NONE;
	int iLC = m_tLC.ToLower ( iCode );
	if ( iCode )
	{
		if ( iCode<256 && isspace(iCode) )
			eType = TOK_SPACE;

		else if ( iLC & 0x10000000UL ) // FIXME! FLAG_CODEPOINT_BOUNDARY
			eType = TOK_BREAK;

		else
			eType = iLC ? TOK_WORD : TOK_NONWORD;
	}

	// add the codepoint
	int iPos = m_dCodes.GetLength ();
	m_dCodes.Add ( iCode );

	// do tokenizing
	if ( m_tTok.m_eType==eType )
	{
		// type did not change, continue accumulating
		AccumulateCodepoint ( iLC );
		m_tTok.m_iLength++;

	} else
	{
		// type changed, do flush last one
		if ( m_tTok.m_eType!=TOK_NONE )
		{
			m_tTok.m_iWordID = 0;
			if ( m_tTok.m_eType==TOK_WORD )
			{
				*m_pAccum++ = '\0';
				m_tTok.m_iWordID = m_pDict->GetWordID ( m_sAccum );
			}
			dBuf.Add ( m_tTok );
		}

		m_tTok.m_eType = eType;
		m_tTok.m_iStart = iPos;
		m_tTok.m_iLength = 1;
		
		m_pAccum = m_sAccum;
		m_iAccum = 0;
		AccumulateCodepoint ( iLC );

		// emit terminating token
		if ( eType==TOK_NONE )
		{
			m_tTok.m_iWordID = 0;
			dBuf.Add ( m_tTok );
		}
	}
}


bool ExcerptGen_c::TokensMatch ( const Token_t & a, const Token_t & b )
{
	return a.m_iWordID==b.m_iWordID;
}


int ExcerptGen_c::TokenLen ( int iPos, int bRemoveSpaces )
{
	return ( m_dTokens[iPos].m_eType == TOK_SPACE && bRemoveSpaces )
		? 1
		: m_dTokens[iPos].m_iLength;
}


void ExcerptGen_c::ResultEmit ( int iCode )
{
	if ( !m_bUtf8 )
	{
		m_dResult.Add ( BYTE(iCode) );
	} else
	{
		if ( iCode<=0x7f )
		{
			m_dResult.Add ( BYTE(iCode) );

		} else if ( iCode<=0x7ff )
		{
			m_dResult.Add ( 0xc0 | BYTE( iCode>>6) );
			m_dResult.Add ( 0x80 | BYTE( iCode&0x3f ) );

		} else if ( iCode<=0xffff )
		{
			m_dResult.Add ( 0xe0 | BYTE( iCode>>12) );
			m_dResult.Add ( 0x80 | BYTE( (iCode>>6)&0x3f ) );
			m_dResult.Add ( 0x80 | BYTE( iCode&0x3f ) );

		} else
		{
			m_dResult.Add ( 0xf0 | BYTE( iCode>>18) );
			m_dResult.Add ( 0x80 | BYTE( (iCode>>12)&0x3f ) );
			m_dResult.Add ( 0x80 | BYTE( (iCode>>6)&0x3f ) );
			m_dResult.Add ( 0x80 | BYTE( iCode&0x3f ) );
		}
	}
	m_iResultLen++;
}


void ExcerptGen_c::ResultEmit ( const char * sLine )
{
	while ( *sLine )
	{
		assert ( (*(BYTE*)sLine)<128 );
		ResultEmit ( (int)*sLine++ );
	}
}


void ExcerptGen_c::ResultEmit ( const Token_t & sTok )
{
	for ( int i=0; i<sTok.m_iLength; i++ )
		ResultEmit ( m_dCodes [ i+sTok.m_iStart ] );
}

/////////////////////////////////////////////////////////////////////////////

void ExcerptGen_c::CalcPassageWeight ( const CSphVector<int> & dPassage, Passage_t & tPass, int iMaxWords, int iWordCountCoeff )
{
	DWORD uLast = 0;
	int iLCS = 1;
	tPass.m_iMaxLCS = 1;

	// calc everything
	tPass.m_uWords = 0;
	tPass.m_iMinGap = iMaxWords-1;

	ARRAY_FOREACH ( i, dPassage )
	{
		Token_t & tTok = m_dTokens[dPassage[i]];
		assert ( tTok.m_eType==TOK_WORD );

		// update mask
		tPass.m_uWords |= tTok.m_uWords;

		// update LCS
		uLast = tTok.m_uWords & ( uLast<<1 );
		if ( uLast )
		{
			iLCS++;
			tPass.m_iMaxLCS = Max ( iLCS, tPass.m_iMaxLCS );
		} else
		{
			iLCS = 1;
			uLast = tTok.m_uWords;
		}

		// update min gap
		if ( tTok.m_uWords )
		{
			tPass.m_iMinGap = Min ( tPass.m_iMinGap, i );
			tPass.m_iMinGap = Min ( tPass.m_iMinGap, dPassage.GetLength()-1-i );
		}
	}
	assert ( tPass.m_iMinGap>=0 );

	// calc final weight
	tPass.m_iWordsWeight = 0;
	tPass.m_iWordCount = 0;

	DWORD uWords = tPass.m_uWords;
	for ( int iWord=0; uWords; uWords>>=1, iWord++ )
		if ( uWords & 1 )
	{
		tPass.m_iWordsWeight += m_dWords[iWord].m_iWeight;
		tPass.m_iWordCount++;
	}

	tPass.m_iMaxLCS *= iMaxWords;
	tPass.m_iWordCount *= iWordCountCoeff;
}


bool ExcerptGen_c::ExtractPassages ( const ExcerptQuery_t & q )
{
	m_dPassages.Reserve ( 256 );
	m_dPassages.Resize ( 0 );

	if ( q.m_bUseBoundaries )
		return ExtractPhrases ( q );

	// my current passage
	CSphVector<int> dPass;
	Passage_t tPass;
	tPass.Reset ();

	int iMaxWords = 2*q.m_iAround+1;
	int iLCSThresh = m_bExactPhrase ? m_dWords.GetLength()*iMaxWords : 0;

	ARRAY_FOREACH ( iTok, m_dTokens )
	{
		// skip starting whitespace
		if ( tPass.m_iTokens==0 && m_dTokens[iTok].m_eType!=TOK_WORD )
		{
			tPass.m_iStart++;
			continue;
		}

		// got token, update passage
		tPass.m_iTokens++;
		tPass.m_iCodes += m_dTokens[iTok].m_iLength;

		if ( m_dTokens[iTok].m_eType!=TOK_WORD )
			continue;

		// got word
		dPass.Add ( iTok );
		tPass.m_uWords |= m_dTokens[iTok].m_uWords;

		// check if we're done
		if ( dPass.GetLength()>=iMaxWords )
			break;
	}

	if ( tPass.m_uWords )
	{
		CalcPassageWeight ( dPass, tPass, iMaxWords, 0 );
		if ( tPass.m_iMaxLCS>=iLCSThresh )
			m_dPassages.Add ( tPass );
	}

	// my lovely update-and-submit loop
	for ( ;; )
	{
		int iAdd = tPass.m_iStart + tPass.m_iTokens;
		if ( iAdd>=m_dTokens.GetLength() )
			break;

		// remove opening word
		assert ( m_dTokens[tPass.m_iStart].m_eType==TOK_WORD );
		tPass.m_iTokens--;
		tPass.m_iCodes -= m_dTokens [ tPass.m_iStart ].m_iLength;
		tPass.m_iStart++;
		dPass.Remove ( 0 ); // FIXME! OPTIMIZE! make this a cyclic buffer

		// remove opening non-words
		while ( m_dTokens[tPass.m_iStart].m_eType!=TOK_WORD )
		{
			tPass.m_iTokens--;
			tPass.m_iCodes -= m_dTokens [ tPass.m_iStart ].m_iLength;
			tPass.m_iStart++;
		}

		// add trailing non-words
		assert ( tPass.m_iStart+tPass.m_iTokens==iAdd );
		while ( iAdd<m_dTokens.GetLength() && m_dTokens[iAdd].m_eType!=TOK_WORD )
		{
			tPass.m_iTokens++;
			tPass.m_iCodes += m_dTokens[iAdd].m_iLength;
			iAdd++;
		}
		if ( iAdd>=m_dTokens.GetLength() )
			break;

		// add trailing word
		assert ( tPass.m_iStart+tPass.m_iTokens==iAdd );
		assert ( m_dTokens[iAdd].m_eType==TOK_WORD );
		tPass.m_iTokens++;
		tPass.m_iCodes += m_dTokens[iAdd].m_iLength;
		dPass.Add ( iAdd );

		// re-weight current passage, and check if it matches
		CalcPassageWeight ( dPass, tPass, iMaxWords, 0 );
		if ( !tPass.m_uWords || tPass.m_iMaxLCS<iLCSThresh )
			continue;

		// if it's the very first one, do add
		if ( !m_dPassages.GetLength() )
		{
			m_dPassages.Add ( tPass );
			continue;
		}

		// check if it's new or better
		Passage_t & tLast = m_dPassages.Last();
		if ( tLast.m_uWords!=tPass.m_uWords
			|| tLast.m_iStart+tLast.m_iTokens-1 < tPass.m_iStart )
		{
			// new
			m_dPassages.Add ( tPass );
		} else
		{
			// better
			if ( tLast.GetWeight()<tPass.GetWeight() )
				tLast = tPass;
		}
	}

	return m_dPassages.GetLength()!=0;
}


bool ExcerptGen_c::ExtractPhrases ( const ExcerptQuery_t & )
{
	int iStart = 0;
	DWORD uWords = 0;

	ARRAY_FOREACH ( iTok, m_dTokens )
	{
		// phrase boundary found, go flush
		if ( m_dTokens[iTok].m_eType==TOK_BREAK || m_dTokens[iTok].m_eType==TOK_NONE )
		{
			// where's my ending token
			int iEnd = iTok;
			if ( m_dTokens[iTok].m_eType==TOK_NONE )
				iEnd--;

			// emit non-empty phrases with matching words as passages
			if ( iStart<iEnd && uWords!=0 )
			{
				Passage_t tPass;
				tPass.Reset ();

				tPass.m_iStart = iStart;
				tPass.m_iTokens = iEnd-iStart+1;

				CSphVector<int> dPass;
				for ( int i=iStart; i<=iEnd; i++ )
				{
					tPass.m_iCodes += m_dTokens[i].m_iLength;
					if ( m_dTokens[i].m_eType==TOK_WORD )
						dPass.Add ( i );
				}

				CalcPassageWeight ( dPass, tPass, 100, 10000 );
				m_dPassages.Add ( tPass );
			}

			// skip until first word or EOF
			while ( m_dTokens[iTok].m_eType!=TOK_WORD && m_dTokens[iTok].m_eType!=TOK_NONE )
				iTok++;
			if ( m_dTokens[iTok].m_eType==TOK_NONE )
				break;

			assert ( m_dTokens[iTok].m_eType==TOK_WORD );
			iStart = iTok;
			uWords = 0;
		}

		// just an incoming token
		if ( m_dTokens[iTok].m_eType==TOK_WORD )
			uWords |= m_dTokens[iTok].m_uWords;
	}

	return m_dPassages.GetLength()!=0;
}


struct PassageOrder_fn
{
	inline bool operator () ( const ExcerptGen_c::Passage_t & a, const ExcerptGen_c::Passage_t & b ) const
	{
		return a.m_iStart < b.m_iStart;
	}
};


bool ExcerptGen_c::HighlightBestPassages ( const ExcerptQuery_t & q )
{
	///////////////////////////
	// select the ones to show
	///////////////////////////

	CSphVector<Passage_t> dShow;
	int iLeft = q.m_iLimit;

	while ( ( q.m_bUseBoundaries || iLeft>0 ) && m_dPassages.GetLength() )
	{
		// FIXME! use heap instead of sorting again every time?
		m_dPassages.Sort ();
		Passage_t & tPass = m_dPassages[0];

		if ( tPass.m_iCodes<=iLeft || q.m_bUseBoundaries )
		{
			// add it to the show
			dShow.Add ( tPass );
			iLeft -= tPass.m_iCodes;

			// sometimes be need only one best one
			if ( q.m_bSinglePassage )
				break;

			// we now show some of the query words,
			// so displaying other passages containing those is less significant,
			// so let's update all the other weights (and word masks, to avoid updating twice)
			for ( int i=1; i<m_dPassages.GetLength(); i++ )
				if ( m_dPassages[i].m_uWords & tPass.m_uWords )
			{
				DWORD uWords = tPass.m_uWords;
				for ( int iWord=0; uWords; iWord++, uWords>>=1 )
					if ( ( uWords & 1 ) && ( m_dPassages[i].m_uWords & ( 1UL<<iWord ) ) )
						m_dPassages[i].m_iWordsWeight -= m_dWords[iWord].m_iWeight;

				m_dPassages[i].m_uWords &= ~tPass.m_uWords;
				assert ( m_dPassages[i].m_iWordsWeight>=0 );
			}
		}

		m_dPassages.RemoveFast ( 0 );
	}

	if ( !dShow.GetLength() )
		return false;

	///////////
	// do show
	///////////

	// sort the passaged in the document order
	dShow.Sort ( PassageOrder_fn() );

	// estimate length, and grow it up to the limit
	int iLast = -1;
	int iLength = 0;
	ARRAY_FOREACH ( i, dShow )
	{
		int iEnd = dShow[i].m_iStart + dShow[i].m_iTokens - 1;
		for ( int iTok = dShow[i].m_iStart; iTok<=iEnd; iTok++ )
			if ( iTok>iLast )
				iLength += m_dTokens[iTok].m_iLength;
		iLast = iEnd;
	}
	if ( iLength<q.m_iLimit && !q.m_bUseBoundaries )
	{
		// word id is no longer needed; we'll use it to store index into dShow
		ARRAY_FOREACH ( i, m_dTokens )
			m_dTokens[i].m_iWordID = 0;

		ARRAY_FOREACH ( i, dShow )
			for ( int iTok = dShow[i].m_iStart; iTok < dShow[i].m_iStart+dShow[i].m_iTokens; iTok++ )
				if ( m_dTokens[iTok].m_iWordID==0 )
					m_dTokens[iTok].m_iWordID = i;

		int iLeft = q.m_iLimit - iLength;
		int iLastLeft = 0;
		while ( iLeft>0 && iLeft!=iLastLeft )
		{
			iLastLeft = iLeft;
			for ( int iShow=0; iShow<dShow.GetLength() && iLeft>0; iShow++ )
			{
				Passage_t & tPass = dShow [ iShow ];

				// the first one
				int iTok = tPass.m_iStart - 1;
				if ( iTok>=0
					&& m_dTokens[iTok].m_iWordID==0
					&& iLeft>=m_dTokens[iTok].m_iLength )
				{
					iLeft -= m_dTokens [ iTok ].m_iLength;
					m_dTokens [ iTok ].m_iWordID = iShow;
					tPass.m_iStart--;
					tPass.m_iTokens++;
				}

				// the last one
				iTok = tPass.m_iStart + tPass.m_iTokens;
				if ( iTok<m_dTokens.GetLength()
					&& m_dTokens[iTok].m_iWordID==0
					&& iLeft>=m_dTokens[iTok].m_iLength )
				{
					iLeft -= m_dTokens [ iTok ].m_iLength;
					m_dTokens [ iTok ].m_iWordID = iShow;
					tPass.m_iTokens++;
				}
			}
		}
	}

	// show everything
	iLast = -1;
	ARRAY_FOREACH ( i, dShow )
	{
		int iTok = dShow[i].m_iStart;
		int iEnd = iTok + dShow[i].m_iTokens - 1;

		if ( iTok>1+iLast )
			ResultEmit ( q.m_sChunkSeparator.cstr() );

		while ( iTok<=iEnd )
		{
			if ( iTok>iLast )
			{
				// FIXME! glue
				if ( m_dTokens[iTok].m_uWords )
				{
					ResultEmit ( q.m_sBeforeMatch.cstr() );
					ResultEmit ( m_dTokens[iTok] );
					ResultEmit ( q.m_sAfterMatch.cstr() );
				} else
				{
					ResultEmit ( m_dTokens[iTok] );
				}
			}
			iTok++;
		}

		iLast = iEnd;
	}
	if ( iLast!=m_dTokens.GetLength() )
		ResultEmit ( q.m_sChunkSeparator.cstr() );

	return true;
}

/////////////////////////////////////////////////////////////////////////////

char * sphBuildExcerpt ( const ExcerptQuery_t & q, CSphDict * pDict, ISphTokenizer * pTokenizer )
{
	ExcerptGen_c tGen;
	return tGen.BuildExcerpt ( q, pDict, pTokenizer );
}

//
// $Id$
//
