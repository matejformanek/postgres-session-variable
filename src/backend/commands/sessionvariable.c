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

static Node *
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

static void
sessionVariableStartupReceiver(DestReceiver *self, int operation, TupleDesc typeinfo) {
    sessionVariableReceiver *svReceiver = (sessionVariableReceiver *) self;
    svReceiver->rows = 0;

    if (typeinfo->natts != 1)
        ereport(ERROR,
                (errcode(ERRCODE_TOO_MANY_COLUMNS),
                        errmsg("expression has more than one column")));
}

static bool
sessionVariableReceiveSlot(TupleTableSlot *slot, DestReceiver *self) {
    sessionVariableReceiver *svReceiver = (sessionVariableReceiver *) self;
    bool typByVal;
    int16 typLen;
    Node *expr;
    int32 typmod = TupleDescAttr(slot->tts_tupleDescriptor, 0)->atttypmod;
    Oid typeOid = TupleDescAttr(slot->tts_tupleDescriptor, 0)->atttypid;
    Oid collid = TupleDescAttr(slot->tts_tupleDescriptor, 0)->attcollation;

    svReceiver->result = slot_getattr(slot, 1, &svReceiver->isnull);

    get_typlenbyval(typeOid, &typLen, &typByVal);

    expr = makeConstSessionVariable(typeOid, typmod, collid, typByVal, typLen, svReceiver->isnull, svReceiver->result);

    /* Store the result directly into the session variable */
    SaveVariable(svReceiver->expr, expr);

    /* Count retrieved rows -> only 1 row allowed */
    svReceiver->rows += 1;

    if (svReceiver->rows > 1)
        ereport(ERROR,
                (errcode(ERRCODE_TOO_MANY_ROWS),
                        errmsg("expression returned more than one row")));

    return true;
}

static void
sessionVariableShutdownReceiver(DestReceiver *self) {
    if (((sessionVariableReceiver *) self)->rows == 0)
        ereport(ERROR,
                (errcode(ERRCODE_NO_DATA_FOUND),
                        errmsg("expression returned no rows")));
}

static void
sessionVariableDestroyReceiver(DestReceiver *self) {
    pfree(self);
}

DestReceiver *
CreateSessionVariableDestReceiver(sessionVariable *expr) {
    sessionVariableReceiver *svReceiver = palloc0(sizeof(sessionVariableReceiver));
    svReceiver->pub.rStartup = sessionVariableStartupReceiver;
    svReceiver->pub.receiveSlot = sessionVariableReceiveSlot;
    svReceiver->pub.rShutdown = sessionVariableShutdownReceiver;
    svReceiver->pub.rDestroy = sessionVariableDestroyReceiver;
    svReceiver->isnull = false;
    svReceiver->expr = expr;  /* Pass in the result to HTAB expr pointer -> we can save the value in Receive */

    return (DestReceiver *) svReceiver;
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

void
SaveVariable(sessionVariable *result, Node *expr) {
    MemoryContext oldContext;

    Assert(result);

    oldContext = MemoryContextSwitchTo(TopMemoryContext);

    result->expr = (Node *) copyObject(expr);

    MemoryContextSwitchTo(oldContext);
}

/**
 * Run query for each variable given to us and save it inside the session variable HTAB. 
 */
void
evaluateVariable(sessionVariableDef *variable, sessionVariable *result, ParseState *pstate, ParamListInfo params,
                 QueryEnvironment *queryEnv) {
    Query *query = castNode(Query, variable->query);
    List *rewritten;
    PlannedStmt *plan;
    DestReceiver *dest;
    QueryDesc *queryDesc;

    dest = CreateSessionVariableDestReceiver(result);

    rewritten = QueryRewrite(query);

    /* SELECT should never rewrite to more or less than one query */
    Assert (list_length(rewritten) == 1);

    query = linitial_node(Query, rewritten);

    Assert (query->commandType == CMD_SELECT);

    /* Plan the query, applying the specified options */
    plan = pg_plan_query(query, pstate->p_sourcetext, CURSOR_OPT_PARALLEL_OK, NULL);

    PushCopiedSnapshot(GetActiveSnapshot());
    UpdateActiveSnapshotCommandId();

    /* create a QueryDesc, redirecting output to our tuple receiver */
    queryDesc = CreateQueryDesc(plan, pstate->p_sourcetext,
                                GetActiveSnapshot(), InvalidSnapshot,
                                dest, params, queryEnv, 0);

    /* call ExecutorStart to prepare the plan for execution */
    ExecutorStart(queryDesc, 0);

    /*
     * Run the plan to completion.  The result should be only one row.  To
     * check if there are too many result rows, we try to fetch two.
     */
    ExecutorRun(queryDesc, ForwardScanDirection, 2L, true);

    /* and clean up */
    ExecutorFinish(queryDesc);
    ExecutorEnd(queryDesc);
    FreeQueryDesc(queryDesc);
    PopActiveSnapshot();
}

void
ExecuteSessionVariable(ParseState *pstate, SetSessionVariableStmt *stmt, ParamListInfo params,
                       QueryEnvironment *queryEnv) {
    ListCell *tl;

    if (CurrentSession == NULL)
        elog(ERROR, "Session components are not initialized!");

    if (CurrentSession->variables == NULL)
        initSessionVariables();

    foreach(tl, stmt->variables) {
        bool found;
        sessionVariable *expr;
        sessionVariableDef *variable = (sessionVariableDef *) lfirst(tl);

        expr = (sessionVariable *) hash_search(CurrentSession->variables, variable->name, HASH_ENTER, &found);

        if (expr == NULL) elog(ERROR, "Could not allocate space for session variable");

        evaluateVariable(variable, expr, pstate, params, queryEnv);
    }
}