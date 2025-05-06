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
#include "parser/parse_node.h"
#include "nodes/params.h"
#include "tcop/dest.h"


extern void saveSessionVariable(sessionVariable *result, Node *expr, bool exists, List *indirection, bool new_strict_type);

extern Node *
makeConstSessionVariable(Oid typid, int32 typmod, Oid collid, bool typByVal, int16 typLen, bool isnull, Datum value);

extern Param *
getParamSessionVariable(char *name);

extern Const *
getConstSessionVariable(char *name, Oid type);

extern void setSessionVariable(char *varname, Node *expr, List *indirection, bool new_strict_type);

#endif //PGSQL_SESSIONVARIABLE_H