//
// $Id$
//

/// @file sphinxutils.h
/// Declarations for the stuff shared by all Sphinx utilities.

#ifndef _sphinxutils_
#define _sphinxutils_

const char * g_dSphKeysCommon[] =
{
	"index_path",
	"morphology",
	"stopwords",
	NULL
};


const char * g_dSphKeysIndexer[] =
{
	"type",
	"sql_host",
	"sql_port",
	"sql_sock",
	"sql_user",
	"sql_pass",
	"sql_db",
	"sql_query_pre",
	"sql_query_range",
	"sql_query",
	"sql_query_post",
	"sql_group_column",
	"sql_range_step",
	"xmlpipe_command",
	NULL
};


const char * g_dSphKeysSearchd[] =
{
	"port",
	"log",
	"query_log",
	"read_timeout",
	"max_children",
	NULL
};

#endif // _sphinxutils_

//
// $Id$
//
