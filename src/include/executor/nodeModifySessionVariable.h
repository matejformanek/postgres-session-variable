/*-------------------------------------------------------------------------
 *
 * nodeModifySessionVariable.h
 *
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/executor/nodeModifySessionVariable.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef PGSQL_NODEMODIFYSESSIONVARIABLE_H
#define PGSQL_NODEMODIFYSESSIONVARIABLE_H

#include "nodes/execnodes.h"

extern ModifySessionVariableState *ExecInitSetSessionVariable(ModifySessionVariable *node, EState *estate, int eflags);
extern void ExecEndSetSessionVariable(ModifySessionVariableState *node);
extern void ExecReScanSetSessionVariable(ModifySessionVariableState *node);

#endif //PGSQL_NODEMODIFYSESSIONVARIABLE_H
