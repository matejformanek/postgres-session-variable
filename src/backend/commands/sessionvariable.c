/*-------------------------------------------------------------------------
 *
 * sessionvariable.c
 *      User defined session variables.
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/commands/sessionvariable.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <ctype.h>

#include "access/htup_details.h"
#include "access/parallel.h"
#include "access/session.h"
#include "access/xact.h"
#include "access/xlog.h"
#include "access/xlogprefetcher.h"
#include "catalog/pg_authid.h"
#include "commands/sessionvariable.h"
#include "common/string.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "postmaster/postmaster.h"
#include "postmaster/syslogger.h"
#include "storage/bufmgr.h"
#include "utils/acl.h"
#include "utils/backend_status.h"
#include "utils/datetime.h"
#include "utils/dynahash.h"
#include "utils/fmgrprotos.h"
#include "utils/guc_hooks.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"
#include "utils/timestamp.h"
#include "utils/tzparser.h"
#include "utils/varlena.h"
#include "utils/memutils.h"
#include "common/hashfn.h"
#include "parser/parse_expr.h"
#include "rewrite/rewriteHandler.h"
#include "tcop/tcopprot.h"
#include "executor/executor.h"
#include "utils/datum.h"
#include "utils/lsyscache.h"

void initSessionVariables(void);

void SaveVariable(sessionVariable *result, Node *expr);

Node *
makeConstSessionVariable(Oid typid, int32 typmod, Oid collid, bool typByVal, int16 typLen, bool isnull, Datum value) {
    Const *expr;
    expr = makeNode(Const);

    expr->consttype = typid;
    expr->consttypmod = typmod;
    expr->constcollid = collid;
    expr->constbyval = typByVal;
    expr->constlen = typLen;
    expr->constisnull = isnull;
    expr->constvalue = value;

    return (Node *) expr;
}

void
SaveVariable(sessionVariable *result, Node *expr) {
    MemoryContext oldContext;

    Assert(result);

    oldContext = MemoryContextSwitchTo(TopMemoryContext);

    result->expr = (Node *) copyObject(expr);

    MemoryContextSwitchTo(oldContext);
}

void
initSessionVariables() {
    HASHCTL ctl;

    ctl.keysize = VARIABLE_SIZE;
    ctl.entrysize = sizeof(sessionVariable);
    ctl.hcxt = TopMemoryContext;

    CurrentSession->variables = hash_create("Session variables", 16, &ctl,
                                            HASH_ELEM | HASH_CONTEXT | HASH_STRINGS);

    Assert(CurrentSession->variables != NULL);
}

void SetSessionVariable(char *varname, Node *expr) {
    sessionVariable *ref;
    bool found;
    
    if (CurrentSession == NULL)
        elog(ERROR, "Session components are not initialized!");

    if (CurrentSession->variables == NULL)
        initSessionVariables();
    
    ref = (sessionVariable *) hash_search(CurrentSession->variables, varname, HASH_ENTER, &found);

    if (ref == NULL) elog(ERROR, "Could not allocate space for session variable");

    SaveVariable(ref, expr);
}