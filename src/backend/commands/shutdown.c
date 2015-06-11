/*-------------------------------------------------------------------------
 *
 * shutdown.c
 *	  ALTER SYSTEM SHUTDOWN
 *
 * Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/commands/shutdown.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "commands/shutdown.h"
#include "miscadmin.h"

#include <signal.h>

typedef enum
{
	SHUTDOWN_SMART,				/* actually stupid, but... */
	SHUTDOWN_FAST,
	SHUTDOWN_IMMEDIATE
} ShutdownType;

static void perform_shutdown(ShutdownType t);

void
ExecShutdownStmt(ShutdownStmt *parsetree)
{
	char *stype = parsetree->stype;
	ShutdownType t;

	if (stype == NULL)
		t = SHUTDOWN_FAST;
	else if (strcmp(stype, "smart") == 0)
		t = SHUTDOWN_SMART;
	else if (strcmp(stype, "fast") == 0)
		t = SHUTDOWN_FAST;
	else if (strcmp(stype, "immediate") == 0)
		t = SHUTDOWN_IMMEDIATE;
	else
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("unrecognized shutdown type: \"%s\"", stype)));

	perform_shutdown(t);
}

static void
perform_shutdown(ShutdownType t)
{
	switch (t)
	{
		case SHUTDOWN_SMART:
			kill(PostmasterPid, SIGTERM);
			break;
		case SHUTDOWN_FAST:
			kill(PostmasterPid, SIGINT);
			break;
		case SHUTDOWN_IMMEDIATE:
			kill(PostmasterPid, SIGQUIT);
			break;
		default:
			elog(ERROR, "invalid shutdown type: %d", (int) t);
	}
}
