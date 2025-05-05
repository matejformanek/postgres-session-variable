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
#include "parser/parse_coerce.h"
#include "rewrite/rewriteHandler.h"
#include "tcop/tcopprot.h"
#include "executor/executor.h"
#include "utils/datum.h"
#include "utils/lsyscache.h"
#include "utils/inval.h"

void initSessionVariables(void);

void saveSessionVariable(sessionVariable *result, Node *expr, bool exists, List *indirection, bool new_strict_type);

/*
 * Returns Const value of a session variable
 * type allows you to define the desired type you want the Const to be coerced to.
 * If the value can not be coerced returns NULL.
 * To skip coercion set type = UNKNOWNOID
 **/
Const *
getConstSessionVariable(char *name, Oid type) {
    Const *result;
    Node * coerced = NULL;
    sessionVariable *variable;
    Oid outputFunction;
    bool typeIsVarlen;
    char *cstringValue;

    if (CurrentSession == NULL || CurrentSession->variables == NULL)
        return NULL;

    variable = (sessionVariable *) hash_search(CurrentSession->variables, name, HASH_FIND, NULL);

    if (!variable)
        return NULL;

    result = (Const *) variable->expr;

    /* UNKNOWNOID if you just want to return the current value
     * If the requested type is given try to coerce
     **/
    if (type != UNKNOWNOID && type != result->consttype && result->constisnull == false) {
        /*
         * If type is not UNKNOWNOID it means that the default type assigned
         * on creation is not desired -> Get the CString format and try whether it can 
         * be coerced as the specified type.
         * 
         * It would be impractical to save all the variables with UNKNOWNOID
         * Rather save them with (best match)/(user type definition) and coerce as last option
         **/
        if (result->consttype != UNKNOWNOID) {
            result = (Const *) copyObject(variable->expr);

            getTypeOutputInfo(result->consttype, &outputFunction, &typeIsVarlen);

            cstringValue = OidOutputFunctionCall(outputFunction, result->constvalue);

            result->consttype = UNKNOWNOID;
            result->constvalue = CStringGetDatum(cstringValue);
            result->constlen = -2;
            result->constbyval = false;
        }

        coerced = coerce_type(NULL,
                              (Node *) result,
                              result->consttype,
                              type,
                              -1,
                              COERCION_IMPLICIT,
                              COERCE_IMPLICIT_CAST,
                              -1);
    }

    return coerced ? (Const *) coerced : (Const *) variable->expr;
}

/*
 * Returns Param node of SESVAR
 * 
 * Even though we save the value as a Const to handle the variable through parsing and later
 * execution using Const would be a crucial mistake because the value of the sesvar inside a query
 * can be altered (e.g. Cumulative sum).
 **/
Param *
getParamSessionVariable(char *name) {
    Param *param = makeNode(Param);
    sessionVariable *variable;
    Const *con;

    if (CurrentSession == NULL || CurrentSession->variables == NULL)
        initSessionVariables();

    variable = (sessionVariable *) hash_search(CurrentSession->variables, name, HASH_FIND, NULL);

    param->paramkind = PARAM_SESSION_VARIABLE;
    param->paramsesvarid = name;

    /*
     * Return dummy template if the sesvar is not yet saved. This way we can reference
     * sevar's that has been created previously in the same chain.
     * 
     * SET @a := 5, @b := 5 + @a;
     * 
     * If the sesvar has not been initiated before this variable is evaluated we will throw ERROR: unrecognized.
     **/
    if (!variable || !variable->expr) {
        param->paramtype = UNKNOWNOID;
        param->paramtypmod = -1;
        param->paramcollid = InvalidOid;
    } else {
        con = (Const *) variable->expr;

        param->paramtype = con->consttype;
        param->paramtypmod = con->consttypmod;
        param->paramcollid = con->constcollid;
    }

    return param;
}

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

void handleArrayIndirection(sessionVariable *result, Node *expr, bool exists, A_Indices *indirection) {
    Oid array_type;
    Oid elem_type;
    ArrayType *array;
    ArrayType *new_array;
    int ndim;
    int *dims;
    int nelems;
    int lidx, uidx;
    Node *subexpr;
    int16 elmlen;
    bool  elmbyval;
    char  elmalign;
    Datum *elem_values;
    Datum new_datum;
    bool *elem_nulls;
    ParseState *pstate = make_parsestate(NULL);
    
    if(!exists)
        elog(ERROR, "Can not use array indirection on non-existing value.");
        
    array_type = ((Const *) result->expr)->consttype;
    elem_type = get_element_type(array_type);
    
    if(elem_type == InvalidOid)
        elog(ERROR, "Can not use array indirection on non-array value.");
    
    if(((Const *) expr)->constisnull)
        elog(ERROR, "Can not assign NULL value as an array item");
    
    array = DatumGetArrayTypeP(((Const *) result->expr)->constvalue);
    ndim = ARR_NDIM(array);
    dims = ARR_DIMS(array);
    nelems = ArrayGetNItems(ndim, dims);
    
    /* Get indirection array index */
    subexpr = transformExpr(pstate, indirection->uidx, EXPR_KIND_SELECT_TARGET);
    subexpr = coerce_to_target_type(pstate,
                                    subexpr, exprType(subexpr),
                                    INT4OID, -1,
                                    COERCION_ASSIGNMENT,
                                    COERCE_IMPLICIT_CAST,
                                    -1);
    uidx = lidx = DatumGetInt32(((Const *) subexpr)->constvalue) - 1;
    if(indirection->lidx != NULL) {
        subexpr = transformExpr(pstate, indirection->lidx, EXPR_KIND_SELECT_TARGET);
        subexpr = coerce_to_target_type(pstate,
                                        subexpr, exprType(subexpr),
                                        INT4OID, -1,
                                        COERCION_ASSIGNMENT,
                                        COERCE_IMPLICIT_CAST,
                                        -1);
        lidx = DatumGetInt32(((Const *) subexpr)->constvalue) - 1;
    }
            
    if(uidx >= nelems || lidx < 0 || uidx < 0 || lidx > uidx)
        elog(ERROR, "Array indirection out of bounds.");
        
    /* Make sure the element is of correct type */
    expr = coerce_to_target_type(pstate,
                                 expr, exprType(expr),
                                 elem_type, -1,
                                 COERCION_ASSIGNMENT,
                                 COERCE_IMPLICIT_CAST,
                                 -1);
    if(expr == NULL)
        elog(ERROR, "Invalid type of expression.");
    
    get_typlenbyvalalign(elem_type, &elmlen, &elmbyval, &elmalign);
    deconstruct_array(array, elem_type, elmlen, elmbyval, elmalign,
                      &elem_values, &elem_nulls, &nelems);
    
    for(int i = lidx; i <= uidx; ++i)
        elem_values[i] = ((Const *) expr)->constvalue;
    
    new_array = construct_array(elem_values, nelems,
                                elem_type, elmlen, elmbyval, elmalign);
    new_datum = PointerGetDatum(new_array);

    saveSessionVariable(result, makeConstSessionVariable(array_type, -1, InvalidOid,
                                                         false, -1, false, new_datum), true, NIL, false);
}

void
saveSessionVariable(sessionVariable *result, Node *expr, bool exists, List *indirection, bool new_strict_type) {
    MemoryContext oldContext;
    Node * oldExpr = NULL;

    Assert(result);

    if(indirection != NIL) {
        handleArrayIndirection(result, expr, exists, (A_Indices *) linitial(indirection));
        return;
    }
    
    if (exists) {
        /* Invalidate cached plans if we are changing data type */
        if (((Const *) result->expr)->consttype != ((Const *) expr)->consttype)
            InvalidateSesvarCache(result->key);

        oldExpr = result->expr;
        if(new_strict_type)
            result->strict_type = true;
        
        if (!new_strict_type && result->strict_type && ((Const *) result->expr)->consttype != ((Const *) expr)->consttype)
            expr = coerce_type(NULL,
                               expr,
                               ((Const *) expr)->consttype,
                               ((Const *) result->expr)->consttype,
                               -1,
                               COERCION_IMPLICIT,
                               COERCE_IMPLICIT_CAST,
                               -1);
    }

    oldContext = MemoryContextSwitchTo(TopMemoryContext);

    result->expr = (Node *) copyObject(expr);

    MemoryContextSwitchTo(oldContext);

    /* Free old Expr-Node if we rewrote it with new data */
    if (oldExpr)
        pfree(oldExpr);
}

void
initSessionVariables() {
    HASHCTL ctl;

    Assert(CurrentSession != NULL);

    ctl.keysize = SESVAR_SIZE;
    ctl.entrysize = sizeof(sessionVariable);
    ctl.hcxt = TopMemoryContext;

    CurrentSession->variables = hash_create("Session variables", 16, &ctl,
                                            HASH_ELEM | HASH_CONTEXT | HASH_STRINGS);

    Assert(CurrentSession->variables != NULL);
}

void setSessionVariable(char *varname, Node *expr, List *indirection, bool new_strict_type) {
    sessionVariable *ref;
    bool found;

    if (CurrentSession == NULL)
        elog(ERROR, "Session components are not initialized!");

    if (CurrentSession->variables == NULL)
        initSessionVariables();

    ref = (sessionVariable *) hash_search(CurrentSession->variables, varname, HASH_ENTER_NULL, &found);

    if (ref == NULL)
        elog(ERROR, "Could not allocate space for session variable");

    if(!found)
        ref->strict_type = new_strict_type;
    
    saveSessionVariable(ref, expr, found, indirection, new_strict_type);
}