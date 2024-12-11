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

void initSessionVariables(void);

void SaveVariable(sessionVariable *result, Node *expr, bool exists);

void typeNegotiation(Param *param, Oid *currentType, Oid targetType);

/*
 * Returns Const value of a session variable
 * type allows you to define the desired type you want the Const to be coerced to.
 * If the value can not be coerced returns NULL.
 * To skip coercion set type = UNKNOWNOID
 **/
Const *
getConstSessionVariable(char *name, Oid type){
    Const *result;
    Node *coerced = NULL;
    sessionVariable *variable;
    Oid outputFunction;
    bool typeIsVarlen;
    char *cstringValue;
    
    if(CurrentSession == NULL || CurrentSession->variables == NULL)
        return NULL;

    variable = (sessionVariable *) hash_search(CurrentSession->variables, name, HASH_FIND, NULL);
    
    if(!variable)
        return NULL;
    
    result = (Const *) variable->expr;

    /* UNKNOWNOID if you just want to return the current value
     * If the requested type is given try to coerce
     **/
    if(type != UNKNOWNOID && type != result->consttype && result->constisnull == false){
        /*
         * If type is not UNKNOWNOID it means that the default type assigned
         * on creation is not desired -> Get the CString format and try whether it can 
         * be coerced as the specified type.
         * 
         * It would be impractical to save all the variables with UNKNOWNOID
         * Rather save them with (best match)/(user type definition) and coerce as last option
         **/
        if(result->consttype != UNKNOWNOID){
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

Param *
getParamSessionVariable(char *name){
    Param *param = makeNode(Param);
    sessionVariable *variable;
    Const *con;
    
    if(CurrentSession == NULL || CurrentSession->variables == NULL)
        return NULL;
    
    variable = (sessionVariable *) hash_search(CurrentSession->variables, name, HASH_FIND, NULL);
    
    if(!variable || !variable->expr)
        return NULL;
    
    con = (Const *) variable->expr; 
    
    param->paramkind = PARAM_SESSION_VARIABLE;
    param->paramsesvarid = name;
    param->paramtype = con->consttype;
    param->paramtypmod = con->consttypmod;
    param->paramcollid = con->constcollid;
    
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

void
SaveVariable(sessionVariable *result, Node *expr, bool exists) {
    MemoryContext oldContext;
    
    Assert(result);
    
    /* Free Node if we are rewriting new data */
    if(exists)
        pfree(result->expr);
    
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

    if (ref == NULL) 
        elog(ERROR, "Could not allocate space for session variable");

    SaveVariable(ref, expr, found);
}

/*
 * Find the best matching type for the variable.
 * This function does NOT change the variable itself only
 * changes the recommended type for future coercion.
 **/
void typeNegotiation(Param *param, Oid *currentType, Oid targetType){
    switch (targetType) {
        case INT2OID:
        case INT4OID:
        case INT8OID:
        case FLOAT4OID:
        case FLOAT8OID:
        case NUMERICOID:
        case MONEYOID:
            /*
             * If the target type requires any kind of numeric coercion
             * always try to match it to the NUMERIC type.
             * This makes the number format provided by user very benevolent.
             * 
             * If we didn't implement this then following case would throw ERROR
             * of us not being able to coerce the variable to type INTEGER:
             *  
             *  SET @var := '10.5';
             *  SELECT @var * 2;
             **/
            param->paramtype = *currentType = NUMERICOID;
            break;
        case DATEOID:
        case INTERVALOID:
            /*
             * Example case we need to cover '2024-01-01' + '1 YEAR'::INTERVAL
             * In this case the default behavior is to try and coerce DATE to INTERVAL -> ERROR
             * 
             * Let's try to get a step ahead and check whether DATE or INTERVAL is desired
             **/
            break;
        default:
            break;
    }
}

/*
 * Inside a binary expression we can get a SESVAR variable
 * Due to the philosophy of SEVAR being type-free it is our job
 * to find the best type match when given a value with UNKNOWNOID
 **/
void
sesvarBinaryExprType(Node *ltree, Node *rtree, Oid *ltypeId, Oid *rtypeId) {
    Param *lparam = IsA(ltree, Param) &&
                    ((Param *) ltree)->paramkind == PARAM_SESSION_VARIABLE ? (Param *) ltree
                                                                           : NULL;
    Param *rparam = IsA(rtree, Param) &&
                    ((Param *) rtree)->paramkind == PARAM_SESSION_VARIABLE ? (Param *) rtree
                                                                           : NULL;
    Param *param;
    Oid targetType, *currentType;

    /*
     * No SESVAR or already defined types -> nothing to do
     * 
     * Note: Defined types could be in future check for compatibility here
     * and coerced if not.
     **/
    if((!lparam && !rparam) || (*ltypeId != UNKNOWNOID && *rtypeId != UNKNOWNOID))
        return;

    if (lparam && rparam){
        /*
         * Both variables are SESVAR
         * For now let the user specify the type of at least one
         * to make the expression work
         * 
         * Note: In the future maybe try if it is a NUMERIC 
         * and if not just throw the unknown type ERROR
         **/
        return;
    }

    param = lparam ? lparam : rparam;
    currentType = lparam ? ltypeId : rtypeId;
    targetType = lparam ? *rtypeId : *ltypeId;

    typeNegotiation(param, currentType, targetType);
}