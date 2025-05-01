/*-------------------------------------------------------------------------
 *
 * nodeModifyTable.c
 *	  routines to handle ModifySessionVariable nodes.
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/executor/nodeModifySessionVariable.c
 *
 *-------------------------------------------------------------------------
 */
/* INTERFACE ROUTINES
 *		ExecInitModifySessionVariable - initialize the ModifySessionVariable node
 *		ExecModifySessionVariable		- retrieve the next tuple from the node
 *		ExecEndModifySessionVariable	- shut down the ModifySessionVariable node
 *		ExecReScanModifySessionVariable - rescan the ModifySessionVariable node
 */

#include "postgres.h"

#include "access/htup_details.h"
#include "access/tableam.h"
#include "access/xact.h"
#include "commands/trigger.h"
#include "commands/sessionvariable.h"
#include "executor/execPartition.h"
#include "executor/executor.h"
#include "executor/nodeModifySessionVariable.h"
#include "foreign/fdwapi.h"
#include "miscadmin.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/optimizer.h"
#include "rewrite/rewriteHandler.h"
#include "storage/lmgr.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"

/*
 * Context struct for a ModifySessionVariable operation, containing basic execution
 * state and some output variables populated by ExecUpdateAct() and
 * ExecDeleteAct() to report the result of their actions to callers.
 */
typedef struct ModifySessionVariableContext
{
    /* Operation state */
    ModifySessionVariableState *mtstate;
    EState	   *estate;

    /*
     * Slot containing tuple obtained from ModifySessionVariable's subplan.  Used to
     * access "junk" columns that are not going to be stored.
     */
    TupleTableSlot *planSlot;
} ModifySessionVariableContext;

/* ----------------------------------------------------------------
 *		ExecSetSessionVariable
 * ----------------------------------------------------------------
 */
static TupleTableSlot *
ExecSetSessionVariable(PlanState *pstate)
{
    ModifySessionVariableState *node = castNode(ModifySessionVariableState, pstate);
    ModifySessionVariableContext context;
    EState	   *estate = node->ps.state;
    PlanState  *subplanstate;
    TupleTableSlot *slot;

    /*
     * If we've already completed processing, don't try to do more.  We need
     * this test because ExecPostprocessPlan might call us an extra time, and
     * our subplan's nodes aren't necessarily robust against being called
     * extra times.
     */
    if (node->mt_done)
        return NULL;

    /* Preload local variables */
    subplanstate = outerPlanState(node);

    /* Set global context */
    context.mtstate = node;
    context.estate = estate;

    /*
	 * Fetch rows from subplan, and execute the required table modification
	 * for each row.
	 */
    for (int j = 0;;++j)
    {
        TupleDesc tupleDesc;

        /*
         * Reset the per-output-tuple exprcontext.  This is needed because
         * triggers expect to use that context as workspace.  It's a bit ugly
         * to do this below the top level of the plan, however.  We might need
         * to rethink this later.
         */
        ResetPerTupleExprContext(estate);

        /*
         * Reset per-tuple memory context used for processing on conflict and
         * returning clauses, to free any expression evaluation storage
         * allocated in the previous cycle.
         */
        if (pstate->ps_ExprContext)
            ResetExprContext(pstate->ps_ExprContext);

        /* Fetch the next row from subplan */
        context.planSlot = ExecProcNode(subplanstate);

        /* No more tuples to process? */
        if (TupIsNull(context.planSlot))
            break;
        else if(!TupIsNull(context.planSlot) && j != 0)
            elog(ERROR, "Can not assign more than 1 row into variable");

        slot = context.planSlot;
        slot_getallattrs(slot);

        tupleDesc = slot->tts_tupleDescriptor;
        for (int i = 0; i < tupleDesc->natts; ++i)
        {
            Form_pg_attribute attr = TupleDescAttr(tupleDesc, i);

            /*
             * Thanks to the code inside grammar and analyze we know that each column should have it's assigned varname
             * If not it means user had to try and save more then 1 value inside the variable thus making one column nameless
             **/
            if(NameStr(attr->attname)[0] != '@')
                elog(ERROR, "Can not assign more than 1 column into session variable");
        }
    }

    node->mt_done = true;

    return NULL;
}

/* ----------------------------------------------------------------
 *		ExecInitSetSessionVariable
 * ----------------------------------------------------------------
 */
ModifySessionVariableState *ExecInitSetSessionVariable(ModifySessionVariable *node, EState *estate, int eflags){
    ModifySessionVariableState *msvstate;
    Plan	   *subplan = outerPlan(node);

    /*
     * create state structure
     */
    msvstate = makeNode(ModifySessionVariableState);
    msvstate->ps.plan = (Plan *) node;
    msvstate->ps.state = estate;
    msvstate->ps.ExecProcNode = ExecSetSessionVariable;
    msvstate->mt_done = false;

    /*
     * Now we may initialize the subplan.
     */
    outerPlanState(msvstate) = ExecInitNode(subplan, estate, eflags);

    /*
     * We still must construct a dummy result tuple type, because InitPlan
     * expects one (maybe should change that?).
     */
    msvstate->ps.plan->targetlist = NIL;
    ExecInitResultTypeTL(&msvstate->ps);

    msvstate->ps.ps_ExprContext = NULL;

    return msvstate;
}

/* ----------------------------------------------------------------
 *		ExecEndSetSessionVariable
 *
 *		Shuts down the plan.
 *
 *		Returns nothing of interest.
 * ----------------------------------------------------------------
 */
void ExecEndSetSessionVariable(ModifySessionVariableState *node){
    /*
     * shut down subplan
     */
    ExecEndNode(outerPlanState(node));
}

void
ExecReScanSetSessionVariable(ModifySessionVariableState *node)
{
    /*
     * Currently, we don't need to support rescan on ModifySessionVariable nodes. The
     * semantics of that would be a bit debatable anyway.
     */
    elog(ERROR, "ExecReScanSetSessionVariable is not implemented");
}