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
    Datum expr;
    Oid typid;
} sessionVariable;

typedef struct {
    DestReceiver pub;
    sessionVariable *expr;  // Pointer to the session variable
    Datum result;
    bool isnull;
    int rows;
} sessionVariableReceiver;

extern DestReceiver *CreateSessionVariableDestReceiver(sessionVariable *expr);

extern void SaveVariable(sessionVariable *result, Datum value, bool typByVal, int16 typLen, Oid typid);

extern void
evaluateVariable(sessionVariableDef *variable, sessionVariable *result, ParseState *pstate, ParamListInfo params,
                 QueryEnvironment *queryEnv);

extern void ExecuteSessionVariable(ParseState *pstate, SetSessionVariableStmt *stmt, ParamListInfo params,
                                   QueryEnvironment *queryEnv);

#endif //PGSQL_SESSIONVARIABLE_H