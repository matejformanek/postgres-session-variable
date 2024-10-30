/*-------------------------------------------------------------------------
 *
 * sessionvariable.h
 *	  User defined session variables.
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/commands/sessionvariable.h
 *
 *-------------------------------------------------------------------------
 */

#ifndef PGSQL_SESSIONVARIABLE_H
#define PGSQL_SESSIONVARIABLE_H

#include "nodes/parsenodes.h"

extern void SetSessionVariable(SessionVariableStmt *stmt);

#endif //PGSQL_SESSIONVARIABLE_H
