/* -------------------------------------------------------------------------
 *
 * delete_needs_where.c
 *
 * Copyright (C) 2010-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		contrib/delete_needs_where/delete_needs_where.c
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "fmgr.h"				/* for PG_MODULE_MAGIC, etc. */
#include "parser/analyze.h"		/* for parse_analyze_hook_type, etc.e */
#include "utils/elog.h"			/* for ereport(), etc. */
#include "utils/guc.h"			/* for DefineCustomBoolVariable */

PG_MODULE_MAGIC;

void		_PG_init(void);

static post_parse_analyze_hook_type original_post_parse_analyze_hook = NULL;
static bool delete_needs_where_enabled = false;

static void
delete_needs_where_check(ParseState *pstate, Query *query)
{
	if (delete_needs_where_enabled && query->commandType == CMD_DELETE)
	{
		Assert(query->jointree != NULL);
		if (query->jointree->quals == NULL)
			ereport(ERROR,
					(errcode(ERRCODE_CARDINALITY_VIOLATION),
					 errmsg("DELETE requires a WHERE clause"),
					 errhint("To delete all rows, use \"WHERE true\".")));
	}

	if (original_post_parse_analyze_hook != NULL)
		(*original_post_parse_analyze_hook) (pstate, query);
}

void
_PG_init(void)
{
	DefineCustomBoolVariable("delete_needs_where.enabled",
							 "Require every DELETE statement to have a WHERE clause.",
							 NULL,
							 &delete_needs_where_enabled,
							 false,
							 PGC_SUSET,
							 false,
							 NULL,
							 NULL,
							 NULL);

	original_post_parse_analyze_hook = post_parse_analyze_hook;
	post_parse_analyze_hook = delete_needs_where_check;
}
