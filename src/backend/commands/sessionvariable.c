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
#include "utils/fmgrprotos.h"
#include "utils/guc_hooks.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"
#include "utils/timestamp.h"
#include "utils/tzparser.h"
#include "utils/varlena.h"

void SetSessionVariable(SessionVariableStmt *stmt){
    if (CurrentSession == NULL)
        elog(ERROR, "Session components are not initialized!");

}