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

#define VARIABLE_SIZE (NAMEDATALEN + 1)

typedef struct sessionVariable {
    char key[VARIABLE_SIZE];
    Node *expr;
} sessionVariable;

extern void SaveVariable(sessionVariable *result, Node *expr, bool exists);

extern Node *
makeConstSessionVariable(Oid typid, int32 typmod, Oid collid, bool typByVal, int16 typLen, bool isnull, Datum value);

extern Param *
getParamSessionVariable(char *name);

extern Const *
getConstSessionVariable(char *name, Oid type);

extern void SetSessionVariable(char *varname, Node *expr);

extern void
sesvarBinaryExprType(Node *ltree, Node *rtree, Oid *ltypeId, Oid *rtypeId);

#endif //PGSQL_SESSIONVARIABLE_H