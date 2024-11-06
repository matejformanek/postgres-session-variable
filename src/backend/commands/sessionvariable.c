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

void initSessionVariables(void);

void
initSessionVariables()
{
    HASHCTL ctl;

    ctl.keysize = VARIABLE_SIZE;
    ctl.entrysize = sizeof(sessionVariable);
    ctl.hcxt = TopMemoryContext;

    CurrentSession->variables = hash_create("Session variables", 16, &ctl,
                                            HASH_ELEM | HASH_CONTEXT | HASH_STRINGS);

    Assert(CurrentSession->variables != NULL);
}

void
SaveVariable(SessionVariableStmt *stmt)
{
    ListCell *tl;
    MemoryContext oldContext;

    if (CurrentSession->variables == NULL)
        initSessionVariables();

    oldContext = MemoryContextSwitchTo(TopMemoryContext);

    foreach(tl, stmt->variables) {
        bool found;
        Node *transformedExpr;
        sessionVariableDef *tle;
        sessionVariable *result;
        ParseState *pstate = make_parsestate(NULL);
        tle = (sessionVariableDef *) lfirst(tl);

        result = (sessionVariable *) hash_search(CurrentSession->variables, tle->name, HASH_ENTER, &found);

        if (result == NULL) elog(ERROR, "Could not allocate space for session variable");

        transformedExpr = transformExpr(pstate, tle->expr, EXPR_KIND_SESSION_VARIABLE);

        result->expr = (Node *) copyObject(transformedExpr);
    }

    MemoryContextSwitchTo(oldContext);
}

void
SetSessionVariable(SessionVariableStmt *stmt)
{
    if (CurrentSession == NULL)
        elog(ERROR, "Session components are not initialized!");

    SaveVariable(stmt);
}