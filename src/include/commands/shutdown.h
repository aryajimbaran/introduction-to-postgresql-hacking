/*-------------------------------------------------------------------------
 *
 * shutdown.h
 *	  prototypes for shutdown.c.
 *
 *
 * Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/commands/shutdown.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef SHUTDOWN_H
#define SHUTDOWN_H

#include "nodes/parsenodes.h"

extern void ExecShutdownStmt(ShutdownStmt *parsetree);

#endif   /* SHUTDOWN_H */
