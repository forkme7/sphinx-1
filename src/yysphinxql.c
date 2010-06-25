/* A Bison parser, made by GNU Bison 1.875.  */

/* Skeleton parser for Yacc-like parsing with Bison,
   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* As a special exception, when this file is copied by Bison into a
   Bison output file, you may use that output file without restriction.
   This special exception was added by the Free Software Foundation
   in version 1.24 of Bison.  */

/* Written by Richard Stallman by simplifying the original so called
   ``semantic'' parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 1

/* Using locations.  */
#define YYLSP_NEEDED 0



/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     TOK_IDENT = 258,
     TOK_CONST_INT = 259,
     TOK_CONST_FLOAT = 260,
     TOK_QUOTED_STRING = 261,
     TOK_AS = 262,
     TOK_ASC = 263,
     TOK_AVG = 264,
     TOK_BETWEEN = 265,
     TOK_BY = 266,
     TOK_COUNT = 267,
     TOK_DESC = 268,
     TOK_DELETE = 269,
     TOK_DISTINCT = 270,
     TOK_FROM = 271,
     TOK_GROUP = 272,
     TOK_LIMIT = 273,
     TOK_IN = 274,
     TOK_INSERT = 275,
     TOK_INTO = 276,
     TOK_ID = 277,
     TOK_MATCH = 278,
     TOK_MAX = 279,
     TOK_META = 280,
     TOK_MIN = 281,
     TOK_OPTION = 282,
     TOK_ORDER = 283,
     TOK_REPLACE = 284,
     TOK_SELECT = 285,
     TOK_SHOW = 286,
     TOK_STATUS = 287,
     TOK_SUM = 288,
     TOK_VALUES = 289,
     TOK_WARNINGS = 290,
     TOK_WEIGHT = 291,
     TOK_WITHIN = 292,
     TOK_WHERE = 293,
     TOK_SET = 294,
     TOK_COMMIT = 295,
     TOK_ROLLBACK = 296,
     TOK_START = 297,
     TOK_TRANSACTION = 298,
     TOK_BEGIN = 299,
     TOK_TRUE = 300,
     TOK_FALSE = 301,
     TOK_OR = 302,
     TOK_AND = 303,
     TOK_NE = 304,
     TOK_GTE = 305,
     TOK_LTE = 306,
     TOK_NOT = 307,
     TOK_NEG = 308
   };
#endif
#define TOK_IDENT 258
#define TOK_CONST_INT 259
#define TOK_CONST_FLOAT 260
#define TOK_QUOTED_STRING 261
#define TOK_AS 262
#define TOK_ASC 263
#define TOK_AVG 264
#define TOK_BETWEEN 265
#define TOK_BY 266
#define TOK_COUNT 267
#define TOK_DESC 268
#define TOK_DELETE 269
#define TOK_DISTINCT 270
#define TOK_FROM 271
#define TOK_GROUP 272
#define TOK_LIMIT 273
#define TOK_IN 274
#define TOK_INSERT 275
#define TOK_INTO 276
#define TOK_ID 277
#define TOK_MATCH 278
#define TOK_MAX 279
#define TOK_META 280
#define TOK_MIN 281
#define TOK_OPTION 282
#define TOK_ORDER 283
#define TOK_REPLACE 284
#define TOK_SELECT 285
#define TOK_SHOW 286
#define TOK_STATUS 287
#define TOK_SUM 288
#define TOK_VALUES 289
#define TOK_WARNINGS 290
#define TOK_WEIGHT 291
#define TOK_WITHIN 292
#define TOK_WHERE 293
#define TOK_SET 294
#define TOK_COMMIT 295
#define TOK_ROLLBACK 296
#define TOK_START 297
#define TOK_TRANSACTION 298
#define TOK_BEGIN 299
#define TOK_TRUE 300
#define TOK_FALSE 301
#define TOK_OR 302
#define TOK_AND 303
#define TOK_NE 304
#define TOK_GTE 305
#define TOK_LTE 306
#define TOK_NOT 307
#define TOK_NEG 308




/* Copy the first part of user declarations.  */


#if USE_WINDOWS
#pragma warning(push,1)
#pragma warning(disable:4702) // unreachable code
#endif


// some helpers
#include <float.h> // for FLT_MAX

static void AddFloatRangeFilter ( SqlParser_t * pParser, const CSphString & sAttr, float fMin, float fMax )
{
	CSphFilterSettings tFilter;
	tFilter.m_sAttrName = sAttr;
	tFilter.m_eType = SPH_FILTER_FLOATRANGE;
	tFilter.m_fMinValue = fMin;
	tFilter.m_fMaxValue = fMax;
	pParser->m_pQuery->m_dFilters.Add ( tFilter );
}

static void AddUintRangeFilter ( SqlParser_t * pParser, const CSphString & sAttr, DWORD uMin, DWORD uMax )
{
	CSphFilterSettings tFilter;
	tFilter.m_sAttrName = sAttr;
	tFilter.m_eType = SPH_FILTER_RANGE;
	tFilter.m_uMinValue = uMin;
	tFilter.m_uMaxValue = uMax;
	pParser->m_pQuery->m_dFilters.Add ( tFilter );
}


/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 1
#endif

#if ! defined (YYSTYPE) && ! defined (YYSTYPE_IS_DECLARED)
typedef int YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 214 of yacc.c.  */


#if ! defined (yyoverflow) || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# if YYSTACK_USE_ALLOCA
#  define YYSTACK_ALLOC alloca
# else
#  ifndef YYSTACK_USE_ALLOCA
#   if defined (alloca) || defined (_ALLOCA_H)
#    define YYSTACK_ALLOC alloca
#   else
#    ifdef __GNUC__
#     define YYSTACK_ALLOC __builtin_alloca
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning. */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
# else
#  if defined (__STDC__) || defined (__cplusplus)
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   define YYSIZE_T size_t
#  endif
#  define YYSTACK_ALLOC malloc
#  define YYSTACK_FREE free
# endif
#endif /* ! defined (yyoverflow) || YYERROR_VERBOSE */


#if (! defined (yyoverflow) \
     && (! defined (__cplusplus) \
	 || (YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  short yyss;
  YYSTYPE yyvs;
  };

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (short) + sizeof (YYSTYPE))				\
      + YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  register YYSIZE_T yyi;		\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (0)
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack)					\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack, Stack, yysize);				\
	Stack = &yyptr->Stack;						\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (0)

#endif

#if defined (__STDC__) || defined (__cplusplus)
   typedef signed char yysigned_char;
#else
   typedef short yysigned_char;
#endif

/* YYFINAL -- State number of the termination state. */
#define YYFINAL  44
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   454

/* YYNTOKENS -- Number of terminals. */
#define YYNTOKENS  66
/* YYNNTS -- Number of nonterminals. */
#define YYNNTS  45
/* YYNRULES -- Number of rules. */
#define YYNRULES  133
/* YYNRULES -- Number of states. */
#define YYNSTATES  267

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   308

#define YYTRANSLATE(YYX) 						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const unsigned char yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    50,     2,
      64,    65,    59,    57,    63,    58,     2,    60,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      53,    51,    54,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,    49,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    52,    55,    56,    61,    62
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const unsigned short yyprhs[] =
{
       0,     0,     3,     5,     7,     9,    11,    13,    15,    26,
      28,    32,    34,    38,    45,    52,    59,    66,    68,    74,
      76,    80,    81,    83,    86,    88,    92,    97,   101,   105,
     109,   115,   122,   128,   132,   136,   140,   144,   148,   152,
     156,   160,   166,   170,   174,   176,   179,   181,   184,   186,
     190,   191,   193,   197,   198,   200,   206,   207,   209,   213,
     215,   219,   221,   224,   227,   228,   230,   233,   238,   239,
     241,   244,   246,   250,   254,   258,   260,   262,   264,   267,
     270,   274,   278,   282,   286,   290,   294,   298,   302,   306,
     310,   314,   318,   322,   326,   330,   332,   337,   342,   346,
     353,   360,   362,   366,   369,   371,   373,   375,   380,   382,
     384,   386,   388,   390,   392,   394,   397,   399,   401,   408,
     409,   413,   415,   417,   421,   425,   427,   431,   435,   437,
     441,   443,   445,   447
};

/* YYRHS -- A `-1'-separated list of the rules' RHS. */
static const yysigned_char yyrhs[] =
{
      67,     0,    -1,    68,    -1,    96,    -1,   103,    -1,   110,
      -1,    98,    -1,   100,    -1,    30,    69,    16,    71,    72,
      79,    81,    83,    87,    89,    -1,    70,    -1,    69,    63,
      70,    -1,     3,    -1,    93,     7,     3,    -1,     9,    64,
      93,    65,     7,     3,    -1,    24,    64,    93,    65,     7,
       3,    -1,    26,    64,    93,    65,     7,     3,    -1,    33,
      64,    93,    65,     7,     3,    -1,    59,    -1,    12,    64,
      15,     3,    65,    -1,     3,    -1,    71,    63,     3,    -1,
      -1,    73,    -1,    38,    74,    -1,    75,    -1,    74,    48,
      75,    -1,    23,    64,     6,    65,    -1,    22,    51,    76,
      -1,     3,    51,    76,    -1,     3,    52,    76,    -1,     3,
      19,    64,    78,    65,    -1,     3,    61,    19,    64,    78,
      65,    -1,     3,    10,    76,    48,    76,    -1,     3,    54,
      76,    -1,     3,    53,    76,    -1,     3,    55,    76,    -1,
       3,    56,    76,    -1,     3,    51,    77,    -1,     3,    52,
      77,    -1,     3,    54,    77,    -1,     3,    53,    77,    -1,
       3,    10,    77,    48,    77,    -1,     3,    55,    77,    -1,
       3,    56,    77,    -1,     4,    -1,    58,     4,    -1,     5,
      -1,    58,     5,    -1,    76,    -1,    78,    63,    76,    -1,
      -1,    80,    -1,    17,    11,     3,    -1,    -1,    82,    -1,
      37,    17,    28,    11,    85,    -1,    -1,    84,    -1,    28,
      11,    85,    -1,    86,    -1,    85,    63,    86,    -1,     3,
      -1,     3,     8,    -1,     3,    13,    -1,    -1,    88,    -1,
      18,     4,    -1,    18,     4,    63,     4,    -1,    -1,    90,
      -1,    27,    91,    -1,    92,    -1,    91,    63,    92,    -1,
       3,    51,     3,    -1,     3,    51,     4,    -1,     3,    -1,
       4,    -1,     5,    -1,    58,    93,    -1,    61,    93,    -1,
      93,    57,    93,    -1,    93,    58,    93,    -1,    93,    59,
      93,    -1,    93,    60,    93,    -1,    93,    53,    93,    -1,
      93,    54,    93,    -1,    93,    50,    93,    -1,    93,    49,
      93,    -1,    93,    56,    93,    -1,    93,    55,    93,    -1,
      93,    51,    93,    -1,    93,    52,    93,    -1,    93,    48,
      93,    -1,    93,    47,    93,    -1,    64,    93,    65,    -1,
      94,    -1,     3,    64,    95,    65,    -1,    19,    64,    95,
      65,    -1,     3,    64,    65,    -1,    26,    64,    93,    63,
      93,    65,    -1,    24,    64,    93,    63,    93,    65,    -1,
      93,    -1,    95,    63,    93,    -1,    31,    97,    -1,    35,
      -1,    32,    -1,    25,    -1,    39,     3,    51,    99,    -1,
      45,    -1,    46,    -1,    76,    -1,    40,    -1,    41,    -1,
     101,    -1,    44,    -1,    42,    43,    -1,    20,    -1,    29,
      -1,   102,    21,     3,   104,    34,   106,    -1,    -1,    64,
     105,    65,    -1,     3,    -1,    22,    -1,   105,    63,     3,
      -1,   105,    63,    22,    -1,   107,    -1,   106,    63,   107,
      -1,    64,   108,    65,    -1,   109,    -1,   108,    63,   109,
      -1,    76,    -1,    77,    -1,     6,    -1,    14,    16,     3,
      38,    22,    51,    76,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned short yyrline[] =
{
       0,   102,   102,   103,   104,   105,   106,   107,   113,   128,
     129,   133,   134,   135,   136,   137,   138,   139,   140,   155,
     156,   159,   161,   165,   169,   170,   174,   187,   195,   203,
     212,   220,   229,   233,   237,   241,   245,   249,   250,   251,
     252,   257,   261,   265,   272,   273,   277,   278,   282,   283,
     286,   288,   292,   299,   301,   305,   311,   313,   317,   324,
     325,   329,   330,   331,   334,   336,   340,   345,   352,   354,
     358,   362,   363,   367,   368,   374,   375,   376,   377,   378,
     379,   380,   381,   382,   383,   384,   385,   386,   387,   388,
     389,   390,   391,   392,   393,   394,   398,   399,   400,   401,
     402,   406,   407,   413,   417,   418,   419,   425,   434,   435,
     436,   450,   451,   452,   456,   457,   463,   464,   468,   475,
     477,   481,   482,   483,   484,   488,   489,   493,   497,   498,
     502,   503,   504,   510
};
#endif

#if YYDEBUG || YYERROR_VERBOSE
/* YYTNME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals. */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "TOK_IDENT", "TOK_CONST_INT", 
  "TOK_CONST_FLOAT", "TOK_QUOTED_STRING", "TOK_AS", "TOK_ASC", "TOK_AVG", 
  "TOK_BETWEEN", "TOK_BY", "TOK_COUNT", "TOK_DESC", "TOK_DELETE", 
  "TOK_DISTINCT", "TOK_FROM", "TOK_GROUP", "TOK_LIMIT", "TOK_IN", 
  "TOK_INSERT", "TOK_INTO", "TOK_ID", "TOK_MATCH", "TOK_MAX", "TOK_META", 
  "TOK_MIN", "TOK_OPTION", "TOK_ORDER", "TOK_REPLACE", "TOK_SELECT", 
  "TOK_SHOW", "TOK_STATUS", "TOK_SUM", "TOK_VALUES", "TOK_WARNINGS", 
  "TOK_WEIGHT", "TOK_WITHIN", "TOK_WHERE", "TOK_SET", "TOK_COMMIT", 
  "TOK_ROLLBACK", "TOK_START", "TOK_TRANSACTION", "TOK_BEGIN", "TOK_TRUE", 
  "TOK_FALSE", "TOK_OR", "TOK_AND", "'|'", "'&'", "'='", "TOK_NE", "'<'", 
  "'>'", "TOK_GTE", "TOK_LTE", "'+'", "'-'", "'*'", "'/'", "TOK_NOT", 
  "TOK_NEG", "','", "'('", "')'", "$accept", "statement", "select_from", 
  "select_items_list", "select_item", "ident_list", "opt_where_clause", 
  "where_clause", "where_expr", "where_item", "const_int", "const_float", 
  "const_list", "opt_group_clause", "group_clause", 
  "opt_group_order_clause", "group_order_clause", "opt_order_clause", 
  "order_clause", "order_items_list", "order_item", "opt_limit_clause", 
  "limit_clause", "opt_option_clause", "option_clause", "option_list", 
  "option_item", "expr", "function", "arglist", "show_clause", 
  "show_variable", "set_clause", "boolean_value", "transact_op", 
  "start_transaction", "insert_or_replace_tok", "insert_into", 
  "opt_insert_cols_list", "schema_list", "opt_insert_cols_set", 
  "opt_insert_cols", "insert_vals_list", "insert_val", "delete_from", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const unsigned short yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303,   124,
      38,    61,   304,    60,    62,   305,   306,    43,    45,    42,
      47,   307,   308,    44,    40,    41
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const unsigned char yyr1[] =
{
       0,    66,    67,    67,    67,    67,    67,    67,    68,    69,
      69,    70,    70,    70,    70,    70,    70,    70,    70,    71,
      71,    72,    72,    73,    74,    74,    75,    75,    75,    75,
      75,    75,    75,    75,    75,    75,    75,    75,    75,    75,
      75,    75,    75,    75,    76,    76,    77,    77,    78,    78,
      79,    79,    80,    81,    81,    82,    83,    83,    84,    85,
      85,    86,    86,    86,    87,    87,    88,    88,    89,    89,
      90,    91,    91,    92,    92,    93,    93,    93,    93,    93,
      93,    93,    93,    93,    93,    93,    93,    93,    93,    93,
      93,    93,    93,    93,    93,    93,    94,    94,    94,    94,
      94,    95,    95,    96,    97,    97,    97,    98,    99,    99,
      99,   100,   100,   100,   101,   101,   102,   102,   103,   104,
     104,   105,   105,   105,   105,   106,   106,   107,   108,   108,
     109,   109,   109,   110
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     1,     1,     1,     1,     1,     1,    10,     1,
       3,     1,     3,     6,     6,     6,     6,     1,     5,     1,
       3,     0,     1,     2,     1,     3,     4,     3,     3,     3,
       5,     6,     5,     3,     3,     3,     3,     3,     3,     3,
       3,     5,     3,     3,     1,     2,     1,     2,     1,     3,
       0,     1,     3,     0,     1,     5,     0,     1,     3,     1,
       3,     1,     2,     2,     0,     1,     2,     4,     0,     1,
       2,     1,     3,     3,     3,     1,     1,     1,     2,     2,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     1,     4,     4,     3,     6,
       6,     1,     3,     2,     1,     1,     1,     4,     1,     1,
       1,     1,     1,     1,     1,     2,     1,     1,     6,     0,
       3,     1,     1,     3,     3,     1,     3,     3,     1,     3,
       1,     1,     1,     7
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const unsigned char yydefact[] =
{
       0,     0,   116,   117,     0,     0,     0,   111,   112,     0,
     114,     0,     2,     3,     6,     7,   113,     0,     4,     5,
       0,    75,    76,    77,     0,     0,     0,     0,     0,     0,
       0,    17,     0,     0,     0,     9,     0,    95,   106,   105,
     104,   103,     0,   115,     1,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    75,     0,     0,    78,    79,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   119,     0,
      98,   101,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    94,    19,    21,    10,    12,    93,    92,    87,    86,
      90,    91,    84,    85,    89,    88,    80,    81,    82,    83,
      44,   108,   109,     0,   110,   107,     0,     0,     0,     0,
      96,     0,     0,    97,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    50,    22,    45,   121,   122,     0,     0,
       0,   102,     0,    18,     0,     0,     0,     0,     0,     0,
       0,     0,    23,    24,    20,     0,    53,    51,     0,   120,
       0,   118,   125,   133,    13,   100,    14,    99,    15,    16,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    56,    54,   123,   124,    46,   132,
       0,   130,   131,     0,   128,     0,     0,     0,     0,    28,
      37,    29,    38,    34,    40,    33,    39,    35,    42,    36,
      43,     0,    27,     0,    25,    52,     0,     0,    64,    57,
      47,     0,   127,   126,     0,     0,    48,     0,     0,    26,
       0,     0,     0,    68,    65,   129,    32,     0,    41,     0,
      30,     0,     0,    61,    58,    59,    66,     0,     8,    69,
      49,    31,    55,    62,    63,     0,     0,     0,    70,    71,
      60,    67,     0,     0,    73,    74,    72
};

/* YYDEFGOTO[NTERM-NUM]. */
static const short yydefgoto[] =
{
      -1,    11,    12,    34,    35,    93,   133,   134,   152,   153,
     191,   192,   227,   156,   157,   184,   185,   218,   219,   244,
     245,   233,   234,   248,   249,   258,   259,    36,    37,    82,
      13,    41,    14,   115,    15,    16,    17,    18,   117,   138,
     161,   162,   193,   194,    19
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -90
static const short yypact[] =
{
     172,   -13,   -90,   -90,   100,    95,    25,   -90,   -90,   -12,
     -90,    36,   -90,   -90,   -90,   -90,   -90,    48,   -90,   -90,
      77,    27,   -90,   -90,    14,    43,    64,    67,    81,    82,
      18,   -90,    18,    18,    52,   -90,   118,   -90,   -90,   -90,
     -90,   -90,    38,   -90,   -90,   114,   111,     1,    18,   135,
      18,    18,    18,    18,    88,    90,    92,   -90,   -90,   208,
     150,   100,   152,    18,    18,    18,    18,    18,    18,    18,
      18,    18,    18,    18,    18,    18,    18,    15,    93,   138,
     -90,   337,    53,   227,   160,    69,   170,   189,   246,    18,
      18,   -90,   -90,   -37,   -90,   -90,   350,   362,   373,   383,
     140,   140,   -19,   -19,   -19,   -19,   -51,   -51,   -90,   -90,
     -90,   -90,   -90,   175,   -90,   -90,    89,   153,   137,    18,
     -90,   182,   126,   -90,    18,   197,    18,   198,   199,   303,
     320,    91,   204,   191,   -90,   -90,   -90,   -90,    72,   145,
       9,   337,   207,   -90,   265,   212,   284,   228,   229,   129,
     183,   186,   203,   -90,   -90,   242,   232,   -90,   107,   -90,
      12,   209,   -90,   -90,   -90,   -90,   -90,   -90,   -90,   -90,
       6,   206,     6,     6,     6,     6,     6,     6,   252,     9,
     282,    91,   286,   273,   263,   -90,   -90,   -90,   -90,   -90,
      70,   -90,   -90,    75,   -90,   145,   259,   260,     9,   -90,
     -90,   -90,   -90,   -90,   -90,   -90,   -90,   -90,   -90,   -90,
     -90,   245,   -90,   261,   -90,   -90,   299,   317,   292,   -90,
     -90,    12,   -90,   -90,     9,    19,   -90,    78,     9,   -90,
     318,   342,   343,   319,   -90,   -90,   -90,   359,   -90,     9,
     -90,    79,   342,    -6,   285,   -90,   302,   378,   -90,   -90,
     -90,   -90,   285,   -90,   -90,   342,   440,   331,   382,   -90,
     -90,   -90,   119,   378,   -90,   -90,   -90
};

/* YYPGOTO[NTERM-NUM].  */
static const short yypgoto[] =
{
     -90,   -90,   -90,   -90,   385,   -90,   -90,   -90,   -90,   266,
     -77,   -89,   220,   -90,   -90,   -90,   -90,   -90,   -90,   210,
     194,   -90,   -90,   -90,   -90,   -90,   187,   -18,   -90,   401,
     -90,   -90,   -90,   -90,   -90,   -90,   -90,   -90,   -90,   -90,
     -90,   258,   -90,   233,   -90
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -12
static const short yytable[] =
{
     114,   131,   253,    20,    54,    22,    23,   254,    75,    76,
     110,   188,    57,   110,    58,    59,   110,   188,   189,   110,
      26,    54,    22,    23,   188,    55,   132,    56,    42,    81,
      83,    43,    81,    86,    87,    88,    44,    26,    73,    74,
      75,    76,    55,   -11,    56,    96,    97,    98,    99,   100,
     101,   102,   103,   104,   105,   106,   107,   108,   109,    30,
     111,   112,    32,   163,   190,    33,    80,   113,    60,    45,
     190,   129,   130,   113,   135,   220,    30,   237,    48,    32,
      46,   197,    33,   200,   202,   204,   206,   208,   210,    77,
     -11,    47,   136,   196,   149,   199,   201,   203,   205,   207,
     209,   141,   212,    21,    22,    23,   144,    49,   146,    24,
     186,   137,    25,   150,   151,    61,   119,    78,   120,    26,
      38,   226,   264,   265,    27,    62,    28,    39,    50,   187,
      40,    51,   119,    29,   123,   158,   238,   159,   221,   170,
     222,   239,   239,   240,   251,    52,    53,   236,   171,    79,
      84,   226,    47,    92,    89,    95,    90,   116,    30,    31,
     118,    32,   250,   122,    33,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,   135,
     172,   173,   174,   175,   176,   177,     1,   139,   140,   142,
     178,   143,     2,    69,    70,    71,    72,    73,    74,    75,
      76,     3,     4,     5,   145,   147,   148,   154,   155,   160,
     164,     6,     7,     8,     9,   166,    10,    63,    64,    65,
      66,    67,    68,    69,    70,    71,    72,    73,    74,    75,
      76,   168,   169,   124,   179,   125,    63,    64,    65,    66,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
     180,   181,   126,   182,   127,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,   183,
     198,   211,   195,    91,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,   213,   215,
     216,   217,   121,    63,    64,    65,    66,    67,    68,    69,
      70,    71,    72,    73,    74,    75,    76,   224,   225,   228,
     232,   128,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,   229,   230,   231,   242,
     165,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,   243,   247,   246,   255,   167,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,   220,   256,   124,    63,    64,    65,
      66,    67,    68,    69,    70,    71,    72,    73,    74,    75,
      76,   257,   262,   126,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    64,    65,
      66,    67,    68,    69,    70,    71,    72,    73,    74,    75,
      76,    65,    66,    67,    68,    69,    70,    71,    72,    73,
      74,    75,    76,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,   261,   263,    94,   214,   241,   260,
     266,    85,   252,   223,   235
};

static const unsigned short yycheck[] =
{
      77,    38,     8,    16,     3,     4,     5,    13,    59,    60,
       4,     5,    30,     4,    32,    33,     4,     5,     6,     4,
      19,     3,     4,     5,     5,    24,    63,    26,     3,    47,
      48,    43,    50,    51,    52,    53,     0,    19,    57,    58,
      59,    60,    24,    16,    26,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    58,
      45,    46,    61,   140,    58,    64,    65,    58,    16,    21,
      58,    89,    90,    58,     4,     5,    58,    58,    64,    61,
       3,   170,    64,   172,   173,   174,   175,   176,   177,    51,
      63,    64,     3,   170,     3,   172,   173,   174,   175,   176,
     177,   119,   179,     3,     4,     5,   124,    64,   126,     9,
       3,    22,    12,    22,    23,    63,    63,     3,    65,    19,
      25,   198,     3,     4,    24,     7,    26,    32,    64,    22,
      35,    64,    63,    33,    65,    63,   225,    65,    63,    10,
      65,    63,    63,    65,    65,    64,    64,   224,    19,    38,
      15,   228,    64,     3,    64,     3,    64,    64,    58,    59,
      22,    61,   239,     3,    64,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,     4,
      51,    52,    53,    54,    55,    56,    14,    34,    51,     7,
      61,    65,    20,    53,    54,    55,    56,    57,    58,    59,
      60,    29,    30,    31,     7,     7,     7,     3,    17,    64,
       3,    39,    40,    41,    42,     3,    44,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,     3,     3,    63,    51,    65,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      64,    48,    63,    11,    65,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    37,
      64,    19,    63,    65,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,     6,     3,
      17,    28,    65,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    48,    48,    64,
      18,    65,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    65,    28,    11,    11,
      65,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,     3,    27,     4,    63,    65,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,     5,    63,    63,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,     3,    51,    63,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,     4,    63,    61,   181,   228,   255,
     263,    50,   242,   195,   221
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned char yystos[] =
{
       0,    14,    20,    29,    30,    31,    39,    40,    41,    42,
      44,    67,    68,    96,    98,   100,   101,   102,   103,   110,
      16,     3,     4,     5,     9,    12,    19,    24,    26,    33,
      58,    59,    61,    64,    69,    70,    93,    94,    25,    32,
      35,    97,     3,    43,     0,    21,     3,    64,    64,    64,
      64,    64,    64,    64,     3,    24,    26,    93,    93,    93,
      16,    63,     7,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    51,     3,    38,
      65,    93,    95,    93,    15,    95,    93,    93,    93,    64,
      64,    65,     3,    71,    70,     3,    93,    93,    93,    93,
      93,    93,    93,    93,    93,    93,    93,    93,    93,    93,
       4,    45,    46,    58,    76,    99,    64,   104,    22,    63,
      65,    65,     3,    65,    63,    65,    63,    65,    65,    93,
      93,    38,    63,    72,    73,     4,     3,    22,   105,    34,
      51,    93,     7,    65,    93,     7,    93,     7,     7,     3,
      22,    23,    74,    75,     3,    17,    79,    80,    63,    65,
      64,   106,   107,    76,     3,    65,     3,    65,     3,     3,
      10,    19,    51,    52,    53,    54,    55,    56,    61,    51,
      64,    48,    11,    37,    81,    82,     3,    22,     5,     6,
      58,    76,    77,   108,   109,    63,    76,    77,    64,    76,
      77,    76,    77,    76,    77,    76,    77,    76,    77,    76,
      77,    19,    76,     6,    75,     3,    17,    28,    83,    84,
       5,    63,    65,   107,    48,    48,    76,    78,    64,    65,
      28,    11,    18,    87,    88,   109,    76,    58,    77,    63,
      65,    78,    11,     3,    85,    86,     4,    27,    89,    90,
      76,    65,    85,     8,    13,    63,    63,     3,    91,    92,
      86,     4,    51,    63,     3,     4,    92
};

#if ! defined (YYSIZE_T) && defined (__SIZE_TYPE__)
# define YYSIZE_T __SIZE_TYPE__
#endif
#if ! defined (YYSIZE_T) && defined (size_t)
# define YYSIZE_T size_t
#endif
#if ! defined (YYSIZE_T)
# if defined (__STDC__) || defined (__cplusplus)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# endif
#endif
#if ! defined (YYSIZE_T)
# define YYSIZE_T unsigned int
#endif

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		(-2)
#define YYEOF		0

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		goto yyerrlab1

/* Like YYERROR except do call yyerror.  This remains here temporarily
   to ease the transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */

#define YYFAIL		goto yyerrlab

#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)					\
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    {								\
      yychar = (Token);						\
      yylval = (Value);						\
      yytoken = YYTRANSLATE (yychar);				\
      YYPOPSTACK;						\
      goto yybackup;						\
    }								\
  else								\
    { 								\
      yyerror (pParser, "syntax error: cannot back up");\
      YYERROR;							\
    }								\
while (0)

#define YYTERROR	1
#define YYERRCODE	256

/* YYLLOC_DEFAULT -- Compute the default location (before the actions
   are run).  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)         \
  Current.first_line   = Rhs[1].first_line;      \
  Current.first_column = Rhs[1].first_column;    \
  Current.last_line    = Rhs[N].last_line;       \
  Current.last_column  = Rhs[N].last_column;
#endif

/* YYLEX -- calling `yylex' with the right arguments.  */

#ifdef YYLEX_PARAM
# define YYLEX yylex (&yylval, YYLEX_PARAM)
#else
# define YYLEX yylex (&yylval, pParser)
#endif

/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)			\
do {						\
  if (yydebug)					\
    YYFPRINTF Args;				\
} while (0)

# define YYDSYMPRINT(Args)			\
do {						\
  if (yydebug)					\
    yysymprint Args;				\
} while (0)

# define YYDSYMPRINTF(Title, Token, Value, Location)		\
do {								\
  if (yydebug)							\
    {								\
      YYFPRINTF (stderr, "%s ", Title);				\
      yysymprint (stderr, 					\
                  Token, Value);	\
      YYFPRINTF (stderr, "\n");					\
    }								\
} while (0)

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (cinluded).                                                   |
`------------------------------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yy_stack_print (short *bottom, short *top)
#else
static void
yy_stack_print (bottom, top)
    short *bottom;
    short *top;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (/* Nothing. */; bottom <= top; ++bottom)
    YYFPRINTF (stderr, " %d", *bottom);
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yy_reduce_print (int yyrule)
#else
static void
yy_reduce_print (yyrule)
    int yyrule;
#endif
{
  int yyi;
  unsigned int yylineno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %u), ",
             yyrule - 1, yylineno);
  /* Print the symbols being reduced, and their result.  */
  for (yyi = yyprhs[yyrule]; 0 <= yyrhs[yyi]; yyi++)
    YYFPRINTF (stderr, "%s ", yytname [yyrhs[yyi]]);
  YYFPRINTF (stderr, "-> %s\n", yytname [yyr1[yyrule]]);
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (Rule);		\
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YYDSYMPRINT(Args)
# define YYDSYMPRINTF(Title, Token, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef	YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   SIZE_MAX < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#if YYMAXDEPTH == 0
# undef YYMAXDEPTH
#endif

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif



#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined (__GLIBC__) && defined (_STRING_H)
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
static YYSIZE_T
#   if defined (__STDC__) || defined (__cplusplus)
yystrlen (const char *yystr)
#   else
yystrlen (yystr)
     const char *yystr;
#   endif
{
  register const char *yys = yystr;

  while (*yys++ != '\0')
    continue;

  return yys - yystr - 1;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined (__GLIBC__) && defined (_STRING_H) && defined (_GNU_SOURCE)
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
#   if defined (__STDC__) || defined (__cplusplus)
yystpcpy (char *yydest, const char *yysrc)
#   else
yystpcpy (yydest, yysrc)
     char *yydest;
     const char *yysrc;
#   endif
{
  register char *yyd = yydest;
  register const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

#endif /* !YYERROR_VERBOSE */



#if YYDEBUG
/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yysymprint (FILE *yyoutput, int yytype, YYSTYPE *yyvaluep)
#else
static void
yysymprint (yyoutput, yytype, yyvaluep)
    FILE *yyoutput;
    int yytype;
    YYSTYPE *yyvaluep;
#endif
{
  /* Pacify ``unused variable'' warnings.  */
  (void) yyvaluep;

  if (yytype < YYNTOKENS)
    {
      YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
# ifdef YYPRINT
      YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# endif
    }
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  switch (yytype)
    {
      default:
        break;
    }
  YYFPRINTF (yyoutput, ")");
}

#endif /* ! YYDEBUG */
/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yydestruct (int yytype, YYSTYPE *yyvaluep)
#else
static void
yydestruct (yytype, yyvaluep)
    int yytype;
    YYSTYPE *yyvaluep;
#endif
{
  /* Pacify ``unused variable'' warnings.  */
  (void) yyvaluep;

  switch (yytype)
    {

      default:
        break;
    }
}


/* Prevent warnings from -Wmissing-prototypes.  */

#ifdef YYPARSE_PARAM
# if defined (__STDC__) || defined (__cplusplus)
int yyparse (void *YYPARSE_PARAM);
# else
int yyparse ();
# endif
#else /* ! YYPARSE_PARAM */
#if defined (__STDC__) || defined (__cplusplus)
int yyparse ( SqlParser_t * pParser );
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */






/*----------.
| yyparse.  |
`----------*/

#ifdef YYPARSE_PARAM
# if defined (__STDC__) || defined (__cplusplus)
int yyparse (void *YYPARSE_PARAM)
# else
int yyparse (YYPARSE_PARAM)
  void *YYPARSE_PARAM;
# endif
#else /* ! YYPARSE_PARAM */
#if defined (__STDC__) || defined (__cplusplus)
int
yyparse ( SqlParser_t * pParser )
#else
int
yyparse (pParser)
     SqlParser_t * pParser ;
#endif
#endif
{
  /* The lookahead symbol.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;

/* Number of syntax errors so far.  */
int yynerrs;

  register int yystate;
  register int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken = 0;

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack.  */
  short	yyssa[YYINITDEPTH];
  short *yyss = yyssa;
  register short *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  register YYSTYPE *yyvsp;



#define YYPOPSTACK   (yyvsp--, yyssp--)

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;


  /* When reducing, the number of symbols on the RHS of the reduced
     rule.  */
  int yylen;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  yyssp = yyss;
  yyvsp = yyvs;

  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed. so pushing a state here evens the stacks.
     */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack. Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	short *yyss1 = yyss;


	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow ("parser stack overflow",
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),

		    &yystacksize);

	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyoverflowlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyoverflowlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	short *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyoverflowlab;
	YYSTACK_RELOCATE (yyss);
	YYSTACK_RELOCATE (yyvs);

#  undef YYSTACK_RELOCATE
	if (yyss1 != yyssa)
	  YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;


      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
		  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
	YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

/* Do appropriate processing given the current state.  */
/* Read a lookahead token if we need one and don't already have one.  */
/* yyresume: */

  /* First try to decide what to do without reference to lookahead token.  */

  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = YYLEX;
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YYDSYMPRINTF ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yyn == 0 || yyn == YYTABLE_NINF)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Shift the lookahead token.  */
  YYDPRINTF ((stderr, "Shifting token %s, ", yytname[yytoken]));

  /* Discard the token being shifted unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  *++yyvsp = yylval;


  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  yystate = yyn;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     `$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 8:

    {
			pParser->m_pStmt->m_eStmt = STMT_SELECT;
			pParser->m_pQuery->m_sIndexes.SetBinary ( pParser->m_pBuf+yyvsp[-6].m_iStart, yyvsp[-6].m_iEnd-yyvsp[-6].m_iStart );
		;}
    break;

  case 11:

    { pParser->AddItem ( &yyvsp[0], NULL ); ;}
    break;

  case 12:

    { pParser->AddItem ( &yyvsp[-2], &yyvsp[0] ); ;}
    break;

  case 13:

    { pParser->AddItem ( &yyvsp[-3], &yyvsp[0], SPH_AGGR_AVG ); ;}
    break;

  case 14:

    { pParser->AddItem ( &yyvsp[-3], &yyvsp[0], SPH_AGGR_MAX ); ;}
    break;

  case 15:

    { pParser->AddItem ( &yyvsp[-3], &yyvsp[0], SPH_AGGR_MIN ); ;}
    break;

  case 16:

    { pParser->AddItem ( &yyvsp[-3], &yyvsp[0], SPH_AGGR_SUM ); ;}
    break;

  case 17:

    { pParser->AddItem ( &yyvsp[0], NULL ); ;}
    break;

  case 18:

    {
			if ( !pParser->m_pQuery->m_sGroupDistinct.IsEmpty() )
			{
				yyerror ( pParser, "too many COUNT(DISTINCT) clauses" );
				YYERROR;

			} else
			{
				pParser->m_pQuery->m_sGroupDistinct = yyvsp[-1].m_sValue;
			}
		;}
    break;

  case 20:

    { yyval = yyvsp[-2]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 26:

    {
			if ( pParser->m_bGotQuery )
			{
				yyerror ( pParser, "too many MATCH() clauses" );
				YYERROR;

			} else
			{
				pParser->m_pQuery->m_sQuery = yyvsp[-1].m_sValue;
				pParser->m_bGotQuery = true;
			}
		;}
    break;

  case 27:

    {
			CSphFilterSettings tFilter;
			tFilter.m_sAttrName = "@id";
			tFilter.m_eType = SPH_FILTER_VALUES;
			tFilter.m_dValues.Add ( yyvsp[0].m_tInsval.m_iVal );
			pParser->m_pQuery->m_dFilters.Add ( tFilter );
		;}
    break;

  case 28:

    {
			CSphFilterSettings tFilter;
			tFilter.m_sAttrName = yyvsp[-2].m_sValue;
			tFilter.m_eType = SPH_FILTER_VALUES;
			tFilter.m_dValues.Add ( yyvsp[0].m_tInsval.m_iVal );
			pParser->m_pQuery->m_dFilters.Add ( tFilter );
		;}
    break;

  case 29:

    {
			CSphFilterSettings tFilter;
			tFilter.m_sAttrName = yyvsp[-2].m_sValue;
			tFilter.m_eType = SPH_FILTER_VALUES;
			tFilter.m_dValues.Add ( yyvsp[0].m_tInsval.m_iVal );
			tFilter.m_bExclude = true;
			pParser->m_pQuery->m_dFilters.Add ( tFilter );
		;}
    break;

  case 30:

    {
			CSphFilterSettings tFilter;
			tFilter.m_sAttrName = yyvsp[-4].m_sValue;
			tFilter.m_eType = SPH_FILTER_VALUES;
			tFilter.m_dValues = yyvsp[-1].m_dValues;
			pParser->m_pQuery->m_dFilters.Add ( tFilter );
		;}
    break;

  case 31:

    {
			CSphFilterSettings tFilter;
			tFilter.m_sAttrName = yyvsp[-5].m_sValue;
			tFilter.m_eType = SPH_FILTER_VALUES;
			tFilter.m_dValues = yyvsp[-2].m_dValues;
			tFilter.m_bExclude = true;
			pParser->m_pQuery->m_dFilters.Add ( tFilter );
		;}
    break;

  case 32:

    {
			AddUintRangeFilter ( pParser, yyvsp[-4].m_sValue, yyvsp[-2].m_tInsval.m_iVal, yyvsp[0].m_tInsval.m_iVal );
		;}
    break;

  case 33:

    {
			AddUintRangeFilter ( pParser, yyvsp[-2].m_sValue, yyvsp[0].m_tInsval.m_iVal+1, UINT_MAX );
		;}
    break;

  case 34:

    {
			AddUintRangeFilter ( pParser, yyvsp[-2].m_sValue, 0, yyvsp[0].m_tInsval.m_iVal-1 );
		;}
    break;

  case 35:

    {
			AddUintRangeFilter ( pParser, yyvsp[-2].m_sValue, yyvsp[0].m_tInsval.m_iVal, UINT_MAX );
		;}
    break;

  case 36:

    {
			AddUintRangeFilter ( pParser, yyvsp[-2].m_sValue, 0, yyvsp[0].m_tInsval.m_iVal );
		;}
    break;

  case 40:

    {
			yyerror ( pParser, "only >=, <=, and BETWEEN floating-point filter types are supported in this version" );
			YYERROR;
		;}
    break;

  case 41:

    {
			AddFloatRangeFilter ( pParser, yyvsp[-4].m_sValue, yyvsp[-2].m_tInsval.m_fVal, yyvsp[0].m_tInsval.m_fVal );
		;}
    break;

  case 42:

    {
			AddFloatRangeFilter ( pParser, yyvsp[-2].m_sValue, yyvsp[0].m_tInsval.m_fVal, FLT_MAX );
		;}
    break;

  case 43:

    {
			AddFloatRangeFilter ( pParser, yyvsp[-2].m_sValue, -FLT_MAX, yyvsp[0].m_tInsval.m_fVal );
		;}
    break;

  case 44:

    { yyval.m_tInsval.m_iType = TOK_CONST_INT; yyval.m_tInsval.m_iVal = yyvsp[0].m_iValue; ;}
    break;

  case 45:

    { yyval.m_tInsval.m_iType = TOK_CONST_INT; yyval.m_tInsval.m_iVal = -yyvsp[0].m_iValue; ;}
    break;

  case 46:

    { yyval.m_tInsval.m_iType = TOK_CONST_FLOAT; yyval.m_tInsval.m_fVal = yyvsp[0].m_fValue; ;}
    break;

  case 47:

    { yyval.m_tInsval.m_iType = TOK_CONST_FLOAT; yyval.m_tInsval.m_fVal = -yyvsp[0].m_fValue; ;}
    break;

  case 48:

    { yyval.m_dValues.Add ( yyvsp[0].m_tInsval.m_iVal ); ;}
    break;

  case 49:

    { yyval.m_dValues.Add ( yyvsp[0].m_tInsval.m_iVal ); ;}
    break;

  case 52:

    {
			pParser->m_pQuery->m_eGroupFunc = SPH_GROUPBY_ATTR;
			pParser->m_pQuery->m_sGroupBy = yyvsp[0].m_sValue;
		;}
    break;

  case 55:

    {
			pParser->m_pQuery->m_sSortBy.SetBinary ( pParser->m_pBuf+yyvsp[0].m_iStart, yyvsp[0].m_iEnd-yyvsp[0].m_iStart );
		;}
    break;

  case 58:

    {
			pParser->m_pQuery->m_sOrderBy.SetBinary ( pParser->m_pBuf+yyvsp[0].m_iStart, yyvsp[0].m_iEnd-yyvsp[0].m_iStart );
		;}
    break;

  case 60:

    { yyval = yyvsp[-2]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 62:

    { yyval = yyvsp[-1]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 63:

    { yyval = yyvsp[-1]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 66:

    {
			pParser->m_pQuery->m_iOffset = 0;
			pParser->m_pQuery->m_iLimit = yyvsp[0].m_iValue;
		;}
    break;

  case 67:

    {
			pParser->m_pQuery->m_iOffset = yyvsp[-2].m_iValue;
			pParser->m_pQuery->m_iLimit = yyvsp[0].m_iValue;
		;}
    break;

  case 73:

    { if ( !pParser->AddOption ( yyvsp[-2], yyvsp[0] ) ) YYERROR; ;}
    break;

  case 74:

    { if ( !pParser->AddOption ( yyvsp[-2], yyvsp[0] ) ) YYERROR; ;}
    break;

  case 78:

    { yyval = yyvsp[-1]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 79:

    { yyval = yyvsp[-1]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 80:

    { yyval = yyvsp[-2]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 81:

    { yyval = yyvsp[-2]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 82:

    { yyval = yyvsp[-2]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 83:

    { yyval = yyvsp[-2]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 84:

    { yyval = yyvsp[-2]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 85:

    { yyval = yyvsp[-2]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 86:

    { yyval = yyvsp[-2]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 87:

    { yyval = yyvsp[-2]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 88:

    { yyval = yyvsp[-2]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 89:

    { yyval = yyvsp[-2]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 90:

    { yyval = yyvsp[-2]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 91:

    { yyval = yyvsp[-2]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 92:

    { yyval = yyvsp[-2]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 93:

    { yyval = yyvsp[-2]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 94:

    { yyval = yyvsp[-2]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 96:

    { yyval = yyvsp[-3]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 97:

    { yyval = yyvsp[-3]; yyval.m_iEnd = yyvsp[0].m_iEnd; ;}
    break;

  case 98:

    { yyval = yyvsp[-2]; yyval.m_iEnd = yyvsp[0].m_iEnd ;}
    break;

  case 99:

    { yyval = yyvsp[-5]; yyval.m_iEnd = yyvsp[0].m_iEnd ;}
    break;

  case 100:

    { yyval = yyvsp[-5]; yyval.m_iEnd = yyvsp[0].m_iEnd ;}
    break;

  case 104:

    { pParser->m_pStmt->m_eStmt = STMT_SHOW_WARNINGS; ;}
    break;

  case 105:

    { pParser->m_pStmt->m_eStmt = STMT_SHOW_STATUS; ;}
    break;

  case 106:

    { pParser->m_pStmt->m_eStmt = STMT_SHOW_META; ;}
    break;

  case 107:

    {
			pParser->m_pStmt->m_eStmt = STMT_SET;
			pParser->m_pStmt->m_sSetName = yyvsp[-2].m_sValue;
			pParser->m_pStmt->m_iSetValue = yyvsp[0].m_iValue;
		;}
    break;

  case 108:

    { yyval.m_iValue = 1; ;}
    break;

  case 109:

    { yyval.m_iValue = 0; ;}
    break;

  case 110:

    {
			yyval.m_iValue = yyvsp[0].m_tInsval.m_iVal;
			if ( yyval.m_iValue!=0 && yyval.m_iValue!=1 )
			{
				yyerror ( pParser, "only 0 and 1 could be used as boolean values" );
				YYERROR;
			}
		;}
    break;

  case 111:

    { pParser->m_pStmt->m_eStmt = STMT_COMMIT; ;}
    break;

  case 112:

    { pParser->m_pStmt->m_eStmt = STMT_ROLLBACK; ;}
    break;

  case 113:

    { pParser->m_pStmt->m_eStmt = STMT_STARTTRANSACTION; ;}
    break;

  case 116:

    { yyval = yyvsp[0]; yyval.m_eStmt = STMT_INSERT; ;}
    break;

  case 117:

    { yyval = yyvsp[0]; yyval.m_eStmt = STMT_REPLACE; ;}
    break;

  case 118:

    {
			pParser->m_pStmt->m_eStmt = yyval.m_eStmt;
			pParser->m_pStmt->m_sInsertIndex = yyvsp[-3].m_sValue;
		;}
    break;

  case 121:

    { if ( !pParser->AddSchemaItem ( &yyvsp[0] ) ) { yyerror ( pParser, "unknown field" ); YYERROR; } ;}
    break;

  case 122:

    { if ( !pParser->AddSchemaItem ( &yyvsp[0] ) ) { yyerror ( pParser, "unknown field" ); YYERROR; } ;}
    break;

  case 123:

    { if ( !pParser->AddSchemaItem ( &yyvsp[0] ) ) { yyerror ( pParser, "unknown field" ); YYERROR; } ;}
    break;

  case 124:

    { if ( !pParser->AddSchemaItem ( &yyvsp[0] ) ) { yyerror ( pParser, "unknown field" ); YYERROR; } ;}
    break;

  case 127:

    { if ( !pParser->m_pStmt->CheckInsertIntegrity() ) { yyerror ( pParser, "wrong number of values here" ); YYERROR; } ;}
    break;

  case 128:

    { pParser->m_pStmt->m_dInsertValues.Add ( yyvsp[0].m_tInsval ); ;}
    break;

  case 129:

    { pParser->m_pStmt->m_dInsertValues.Add ( yyvsp[0].m_tInsval ); ;}
    break;

  case 132:

    { yyval.m_tInsval.m_iType = TOK_QUOTED_STRING; yyval.m_tInsval.m_sVal = yyvsp[0].m_sValue; ;}
    break;

  case 133:

    {
			pParser->m_pStmt->m_eStmt = STMT_DELETE;
			pParser->m_pStmt->m_sDeleteIndex = yyvsp[-4].m_sValue;
			pParser->m_pStmt->m_iDeleteID = yyvsp[0].m_tInsval.m_iVal;
		;}
    break;


    }

/* Line 991 of yacc.c.  */


  yyvsp -= yylen;
  yyssp -= yylen;


  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;


  /* Now `shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*------------------------------------.
| yyerrlab -- here on detecting error |
`------------------------------------*/
yyerrlab:
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if YYERROR_VERBOSE
      yyn = yypact[yystate];

      if (YYPACT_NINF < yyn && yyn < YYLAST)
	{
	  YYSIZE_T yysize = 0;
	  int yytype = YYTRANSLATE (yychar);
	  char *yymsg;
	  int yyx, yycount;

	  yycount = 0;
	  /* Start YYX at -YYN if negative to avoid negative indexes in
	     YYCHECK.  */
	  for (yyx = yyn < 0 ? -yyn : 0;
	       yyx < (int) (sizeof (yytname) / sizeof (char *)); yyx++)
	    if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	      yysize += yystrlen (yytname[yyx]) + 15, yycount++;
	  yysize += yystrlen ("syntax error, unexpected ") + 1;
	  yysize += yystrlen (yytname[yytype]);
	  yymsg = (char *) YYSTACK_ALLOC (yysize);
	  if (yymsg != 0)
	    {
	      char *yyp = yystpcpy (yymsg, "syntax error, unexpected ");
	      yyp = yystpcpy (yyp, yytname[yytype]);

	      if (yycount < 4)
		{
		  yycount = 0;
		  for (yyx = yyn < 0 ? -yyn : 0;
		       yyx < (int) (sizeof (yytname) / sizeof (char *));
		       yyx++)
		    if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
		      {
			const char *yyq = ! yycount ? ", expecting " : " or ";
			yyp = yystpcpy (yyp, yyq);
			yyp = yystpcpy (yyp, yytname[yyx]);
			yycount++;
		      }
		}
	      else
		{
		  for (yyx = yyn < 0 ? -yyn : 0;
		       yyx < (int) (sizeof (yytname) / sizeof (char *));
		       yyx++)
		    if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
		      {
			snprintf (yyp, (int)(yysize - (yyp - yymsg)), ", expecting %s (or %d other tokens)", yytname[yyx], yycount - 1);
			while (*yyp++);
			break;
		      }
		}

	      yyerror (pParser, yymsg);
	      YYSTACK_FREE (yymsg);
	    }
	  else
	    yyerror (pParser, "syntax error; also virtual memory exhausted");
	}
      else
#endif /* YYERROR_VERBOSE */
	yyerror (pParser, "syntax error");
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
	 error, discard it.  */

      /* Return failure if at end of input.  */
      if (yychar == YYEOF)
        {
	  /* Pop the error token.  */
          YYPOPSTACK;
	  /* Pop the rest of the stack.  */
	  while (yyss < yyssp)
	    {
	      YYDSYMPRINTF ("Error: popping", yystos[*yyssp], yyvsp, yylsp);
	      yydestruct (yystos[*yyssp], yyvsp);
	      YYPOPSTACK;
	    }
	  YYABORT;
        }

      YYDSYMPRINTF ("Error: discarding", yytoken, &yylval, &yylloc);
      yydestruct (yytoken, &yylval);
      yychar = YYEMPTY;

    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab2;


/*----------------------------------------------------.
| yyerrlab1 -- error raised explicitly by an action.  |
`----------------------------------------------------*/
yyerrlab1:

  /* Suppress GCC warning that yyerrlab1 is unused when no action
     invokes YYERROR.  */
#if defined (__GNUC_MINOR__) && 2093 <= (__GNUC__ * 1000 + __GNUC_MINOR__)
//  __attribute__ ((__unused__))
#endif


  goto yyerrlab2;


/*---------------------------------------------------------------.
| yyerrlab2 -- pop states until the error token can be shifted.  |
`---------------------------------------------------------------*/
yyerrlab2:
  yyerrstatus = 3;	/* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (yyn != YYPACT_NINF)
	{
	  yyn += YYTERROR;
	  if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
	    {
	      yyn = yytable[yyn];
	      if (0 < yyn)
		break;
	    }
	}

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
	YYABORT;

      YYDSYMPRINTF ("Error: popping", yystos[*yyssp], yyvsp, yylsp);
      yydestruct (yystos[yystate], yyvsp);
      yyvsp--;
      yystate = *--yyssp;

      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  YYDPRINTF ((stderr, "Shifting error token, "));

  *++yyvsp = yylval;


  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;

#ifndef yyoverflow
/*----------------------------------------------.
| yyoverflowlab -- parser overflow comes here.  |
`----------------------------------------------*/
yyoverflowlab:
  yyerror (pParser, "parser stack overflow");
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  return yyresult;
}





#if USE_WINDOWS
#pragma warning(pop)
#endif

