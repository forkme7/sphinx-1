//
// $Id$
//

#ifndef _sphinx_
#define _sphinx_

#include <stdio.h>
#include <stdlib.h>
#include <mysql/mysql.h>

#define SPH_MAX_QUERY_WORDS		10
#define SPH_MAX_WORD_LEN		64

#define SPH_RLOG_MAX_BLOCKS		128
#define SPH_RLOG_BIN_SIZE		262144
#define SPH_RLOG_BLOCK_SIZE		1048576

#define SPH_CLOG_BITS_DIR		10
#define SPH_CLOG_BITS_PAGE		22
#define SPH_CLOG_DIR_PAGES		(1 << SPH_CLOG_BITS_DIR)

#define SPH_CACHE_WRITE			1048576

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define Min(a,b) ((a)<(b)?(a):(b))
#define Max(a,b) ((a)>(b)?(a):(b))

typedef unsigned int uint;
typedef unsigned char byte;

typedef unsigned int		DWORD;
typedef unsigned char		BYTE;

// ***

void sphDie(char *message, ...);
void *sphMalloc(size_t size);
void *sphRealloc(void *ptr, size_t size);
void sphFree(void *ptr);
char *sphDup(char *s);

/// time, in mcs
int		sphTimer ();

/// time, in seconds
float	sphLongTimer ();

/////////////////////////////////////////////////////////////////////////////

/// hit info
struct CSphHit
{
	DWORD	m_iGroupID;		///< documents group ID (from data source)
	DWORD	m_iDocID;		///< document ID (from data source)
	DWORD	m_iWordID;		///< word ID (from dictionary)
	DWORD	m_iWordPos;		///< word position in document
};

/// hit vector
struct CSphList_Hit
{
	int count;
	CSphHit *data;

	CSphList_Hit();
	virtual ~CSphList_Hit();

	void add ( DWORD iGroupID, DWORD iDocID, DWORD iWordID, DWORD iWordPos );
	void clear();
	void grow(int entries);

private:
	int max;
	CSphHit *pData;
};

enum ESphMorphology
{
	SPH_MORPH_NONE		= 0,
	SPH_MORPH_STEM_EN	= (1<<1),
	SPH_MORPH_STEM_RU	= (1<<2)
};


/// Generic word dictionary
struct CSphDict
{
	/// get word ID by word
	virtual DWORD		wordID ( BYTE * pWord ) = 0;
};


/// CRC32 dictionary
struct CSphDict_CRC32 : CSphDict
{
	/// ctor
	/// iMorph is a combination of ESphMorphology flags
						CSphDict_CRC32 ( DWORD iMorph );

	/// get word ID by word
	/// does requested morphology and returns CRC32
	virtual DWORD		wordID ( BYTE * pWord );

protected:
	/// morphology flags
	DWORD				m_iMorph;
};


/// source statistics
struct CSphSourceStats
{
	/// how much documents
	int		m_iTotalDocuments;

	/// how much bytes
	int		m_iTotalBytes;


	/// ctor
			CSphSourceStats () :
				m_iTotalDocuments ( 0 ),
				m_iTotalBytes ( 0 )
			{};
};


/// generic document source
struct CSphSource
{
	CSphList_Hit						hits;
	CSphDict *							dict;

public:
										CSphSource();

	/// dtor
	virtual								~CSphSource() {}

	/// set dictionary
	void								setDict ( CSphDict * dict );

	/// get stats
	virtual const CSphSourceStats *		GetStats ();

public:
	/// document getter
	/// to be implemented by descendants
	virtual int							next() = 0;

protected:
	/// my stats
	CSphSourceStats						m_iStats;
};


struct CSphSource_Document : CSphSource
{
	int lastID, numFields, callWordCallback;

	CSphSource_Document() { callWordCallback = 0; }
	virtual int next();
	virtual void wordCallback(char *word) {}
	virtual byte **nextDocument() = 0;
};

struct CSphSource_Text : CSphSource_Document
{
	CSphSource_Text() { this->numFields = 1; }
	byte **nextDocument();
	virtual byte *nextText() = 0;
};

struct CSphSource_MySQL : CSphSource_Document
{
	char *sqlQuery;

	CSphSource_MySQL(char *sqlQuery);
	virtual ~CSphSource_MySQL();

	virtual int connect(char *host, char *user, char *pass, char *db,
		int port, char *usock);
	virtual byte **nextDocument();

private:
	MYSQL_RES *sqlResult;
	MYSQL_ROW sqlRow;
	MYSQL sqlDriver;
};

/// search query match
struct CSphMatch
{
	DWORD		m_iGroupID;
	DWORD		m_iDocID;
	int			m_iWeight;
};

/// matches vector
struct CSphList_Match
{
	int count;
	CSphMatch *data;

	CSphList_Match();
	virtual ~CSphList_Match();

	void add ( DWORD iGroupID, DWORD iDocID, int iWeight );

private:
	int max;
	CSphMatch *pData;

	void grow();
};

// ***

struct CSphList_Int
{
	uint *data;
	int count;

	CSphList_Int();
	virtual ~CSphList_Int();

	void add(uint value);
	void clear();

private:
	uint *pData;
	int max;

	void grow();
};


/// VLN index header
struct CSphIndexHeader_VLN
{
	DWORD		m_iMinDocID;
	DWORD		m_iMinGroupID;
	DWORD		m_iGroupBits;
};


struct CSphWriter_VLN
{
	char *name;
	int pos;

	CSphWriter_VLN(char *name);
	virtual ~CSphWriter_VLN();

	int open();
	void putbytes(void *data, int size);
	void PutRawBytes ( void * pData, int iSize );
	void zipInts(CSphList_Int *data);
	void close();
	void seek(int pos);

private:
	int fd, poolUsed, poolOdd;
	byte pool[SPH_CACHE_WRITE], *pPool;

	void putNibble(int data);
	void flush();
};

struct CSphReader_VLN
{
	char *name;

	CSphReader_VLN(char *name);
	virtual ~CSphReader_VLN();

	int  open();
	void GetRawBytes ( void * pData, int iSize );
	void getbytes(void *data, int size);
	int  unzipInt();
	void unzipInts(CSphList_Int *data);
	int  decodeHits(CSphList_Int *hl);
	void close();
	void seek(int pos);

private:
	int fd, pos, filePos, bufPos, bufOdd, bufUsed, bufSize;
	byte *buf;

	int getNibble();
	void cache();
};

struct CSphQueryWord
{
	int queryPos;
	uint wordID;
	char *word;
	CSphList_Int *hits, *docs;

	CSphQueryWord()
	{
		this->word = NULL;
		this->hits = NULL;
		this->docs = NULL;
		isLink = 0;
	}

	void shareListsFrom(CSphQueryWord *qw)
	{
		this->hits = qw->hits;
		this->docs = qw->docs;
		isLink = 1;
	}

	void newLists()
	{
		this->hits = new CSphList_Int();
		this->docs = new CSphList_Int();
		isLink = 0;
	}

	~CSphQueryWord()
	{
		if (this->word) sphFree(this->word);
		if (!isLink) {
			if (this->hits) delete this->hits;
			if (this->docs) delete this->docs;
		}
	}

private:
	int isLink;
};

struct CSphQueryResult
{
	struct {
		char *word;
		int docs, hits;
	} wordStats[SPH_MAX_QUERY_WORDS];
	int numWords;
	int queryTime;

	CSphList_Match *matches;
};

struct CSphIndex
{
	virtual int build(CSphDict *dict, CSphSource *source) = 0;
	virtual CSphQueryResult *query(CSphDict *dict, char *query) = 0;
};

/// possible bin states
enum ESphBinState
{
	BIN_ERR_READ	= -2,	///< bin read error
	BIN_ERR_END		= -1,	///< bin end
	BIN_POS			= 0,	///< bin is in "expects pos delta" state
	BIN_DOC			= 1,	///< bin is in "expects doc delta" state
	BIN_WORD		= 2,	///< bin is in "expects word delta" state
	BIN_GROUP		= 3		///< bin is in "expects group delta" state
};

struct CSphBin
{
	byte *data, *pData;
	int left, done, filePos, fileLeft, filePtr, state;
	DWORD lastGroupID, lastDocID, lastWordID, lastPos; // FIXME! make it a hit

	CSphBin()
	{
		data = (byte*)sphMalloc(SPH_RLOG_BIN_SIZE);
		pData = data; 
		left = done = filePos = fileLeft = 0;
		lastDocID = lastWordID = lastPos = 0;
		state = BIN_POS;
	}

	~CSphBin()
	{
		sphFree(data);
	}
};

struct CSphIndex_VLN : CSphIndex
{
	CSphIndex_VLN(char *filename);
	virtual ~CSphIndex_VLN();

	virtual int build(CSphDict *dict, CSphSource *source);
	virtual CSphQueryResult *query(CSphDict *dict, char *query);

private:
	char *filename;
	int fdRaw, filePos;
	CSphWriter_VLN *fdIndex, *fdData;
	CSphBin *bins[SPH_RLOG_MAX_BLOCKS];

	CSphList_Int *vChunk, *vChunkHeader, *vIndexPage;
	int  cidxPagesDir[SPH_CLOG_DIR_PAGES];
	int  cidxDirUsed;
	int  cidxPageUsed;
	int  cidxIndexPos;
	int  cidxDataPos;
	int  lastDocDelta;
	uint lastDocID;
	int  lastDocHits;

	CSphIndexHeader_VLN m_tHeader;

	int  open(char *ext, int mode);

	int  binsInit(int blocks);
	void binsDone(int blocks);
	uint binsReadVLB(int b);
	int  binsReadByte(int b);
	int  binsRead(int b, CSphHit *e);

	int  cidxCreate();
	int  cidxWriteRawVLB(int fd, CSphHit *hit, int count);
	void cidxFlushHitList();
	void cidxFlushChunk();
	void cidxFlushIndexPage();
	void cidxHit(CSphHit *hit);
	void cidxDone();
};

// ***

struct CSphHash
{
	int count;
	char **keys, **values;

	CSphHash();
	~CSphHash();

	void add(char *key, char *value);
	char *get(char *key);

private:
	int max;
};

struct CSphConfig
{
	CSphConfig();
	~CSphConfig();

	int open ( const char *file );
	CSphHash * loadSection ( const char *section );

private:
	FILE *fp;
};

/////////////////////////////////////////////////////////////////////////////

class CSphSource_XMLPipe : public CSphSource
{
public:
	/// ctor
					CSphSource_XMLPipe ();

	/// dtor
					~CSphSource_XMLPipe ();

	/// initializer
	bool			Init ( const char * sCommand );

	/// hit chunk getter
	virtual int		next ();

private:
	enum Tag_e
	{
		TAG_DOCUMENT = 0,
		TAG_ID,
		TAG_GROUP,
		TAG_TITLE,
		TAG_BODY
	};

private:
	/// are we scanning body or expecting document?
	bool			m_bBody;

	/// what's our current tag
	Tag_e			m_eTag;

	/// tag name
	const char *	m_pTag;

	/// tag name length
	int				m_iTagLength;

	/// incoming stream
	FILE *			m_pPipe;

	/// buffer
	BYTE			m_sBuffer [ 4096 ];

	/// current buffer pos
	BYTE *			m_pBuffer;

	/// buffered end pos
	BYTE *			m_pBufferEnd;

private:
	/// current document ID
	int				m_iDocID;

	/// current group ID
	int				m_iGroupID;

	/// current word position
	int				m_iWordPos;

private:
	/// set current tag
	void			SetTag ( const char * sTag );

	/// read in some more data
	/// moves everything from current ptr (m_pBuffer) to the beginng
	/// reads in as much data as possible to the end
	/// returns false on EOF
	bool			UpdateBuffer ();

	/// skips whitespace
	/// does buffer updates
	/// returns false on EOF
	bool			SkipWhitespace ();

	/// check if what's at current pos is either opening/closing current tag (m_pTag)
	/// return false on failure
	bool			CheckTag ( bool bOpen );

	/// skips whitespace and opening/closing current tag (m_pTag)
	/// returns false on failure
	bool			SkipTag ( bool bOpen, bool bWarnOnEOF=true );

	/// scan for tag with integer value
	bool			ScanInt ( const char * sTag, int * pRes );

	/// scan for tag with string value
	bool			ScanStr ( const char * sTag, char * pRes, int iMaxLength );
};

#endif // _sphinx_

//
// $Id$
//
