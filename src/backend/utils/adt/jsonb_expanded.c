/*-------------------------------------------------------------------------
*
 * jsonb_expanded.c
 *	  Basic functions for manipulating expanded jsonb.
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/utils/adt/jsonb_expanded.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/tupmacs.h"
#include "utils/expandeddatum.h"
#include "utils/jsonb.h"
#include "utils/jsonfuncs.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"


/* "Methods" required for an expanded object */
static Size EA_get_flat_size(ExpandedObjectHeader *eohptr);
static void EA_flatten_into(ExpandedObjectHeader *eohptr,
                            void *result, Size allocated_size);

static const ExpandedObjectMethods EA_methods =
{
    EA_get_flat_size,
    EA_flatten_into
};

static Size
EA_get_flat_size(ExpandedObjectHeader *eohptr)
{
    ExpandedJsonbHeader *ejbh = (ExpandedJsonbHeader *) eohptr;
    /* If we remember flat_size then no deconstruction was done,
     * The complexity to get the size is similar to converting it to flat value.
     * Rather than do it twice, convert it here.
     */
    Jsonb *jsonb = ejbh->is_expanded == true ? JsonbValueToJsonb(ejbh->value) : ejbh->fvalue;

    ejbh->flat_size = VARSIZE(jsonb);
    ejbh->fvalue = jsonb;

    return ejbh->flat_size;
}

static void
EA_flatten_into(ExpandedObjectHeader *eohptr,
                void *result, Size allocated_size)
{
    ExpandedJsonbHeader *ejbh = (ExpandedJsonbHeader *) eohptr;

    /* allocation should match previous get_flat_size result */
    Assert(allocated_size == ejbh->flat_size);

	memcpy(result, ejbh->fvalue, allocated_size);
}

Datum
expand_jsonb(Datum jsonbdatum, MemoryContext parentcontext)
{
    ExpandedJsonbHeader *ejbh;
    Jsonb *jsonb;
    MemoryContext objcxt;
    MemoryContext oldcxt;

    /*
     * Allocate private context for expanded object.  We start by assuming
     * that the array won't be very large; but if it does grow a lot, don't
     * constrain aset.c's large-context behavior.
     */
    objcxt = AllocSetContextCreate(parentcontext,
                                   "expanded jsonb",
                                   ALLOCSET_START_SMALL_SIZES);

    /* Set up expanded jsonb header */
    ejbh = (ExpandedJsonbHeader *)
        MemoryContextAlloc(objcxt, sizeof(ExpandedJsonbHeader));

    EOH_init_header(&ejbh->hdr, &EA_methods, objcxt);

    /*
     * Detoast and copy source jsonb into private context, as a flat jsonb.
     *
     * Note that this coding risks leaking some memory in the private context
     * if we have to fetch data from a TOAST table; however, experimentation
     * says that the leak is minimal.  Doing it this way saves a copy step,
     * which seems worthwhile, especially if the jsonb is large enough to need
     * external storage.
     */
    oldcxt = MemoryContextSwitchTo(objcxt);
    jsonb = DatumGetJsonbPCopy(jsonbdatum);
    MemoryContextSwitchTo(oldcxt);

    ejbh->is_expanded = false;
    ejbh->flat_size = VARSIZE(jsonb);

	/*
	 * we don't make a deconstructed representation now
     * remember we have a flat representation
     */
    ejbh->fvalue = jsonb;
    ejbh->fstartptr = (char *) jsonb;
    ejbh->fendptr = ((char *) jsonb) + VARSIZE(jsonb);

    /* return a R/W pointer to the expanded jsonb */
    return EOHPGetRWDatum(&ejbh->hdr);
}

/*
 * Create the Datum/isnull representation of an expanded jsonb object
 * if we didn't do so previously
 */
void
deconstruct_expanded_jsonb(ExpandedJsonbHeader *ejbh)
{
    if (ejbh->is_expanded)
        return;

    ejbh->flat_size = 0;
    ejbh->is_expanded = true;

    ejbh->value = JsonbToDecomposedJsonbValue(ejbh->fvalue, ejbh->hdr.eoh_context);
}

/*
 * Turn Jsonb* into a fully decomposed JsonbValue
 * Unlike JsonbToJsonbValue() we don't stop at the conversion to binary
 * but do a full decomposition to each type.
 */
JsonbValue *
JsonbToDecomposedJsonbValue(Jsonb *jsonb, MemoryContext parentcontext)
{
    JsonbValue      *jbval;
    JsonbParseState *st = NULL;
    JsonbIterator   *it = JsonbIteratorInit(&jsonb->root);
    bool	         pn[1] = {false};
    MemoryContext    oldcxt = MemoryContextSwitchTo(parentcontext);

    /*
     * We abuse the setPath function to create an expanded/decomposed jsonbValue
     */
    jbval = setPath(&it, NULL, pn, 0, &st, 0, NULL, JB_PATH_DELETE);

    MemoryContextSwitchTo(oldcxt);

    return jbval;
}

/*
 * Support function for jsonb getters.
 * With each arrow nesting, we need to return a valid Datum.
 * Rather than automatically convert to Jsonb* return pointer to Expanded JsonbValue.
 * This way, we can keep using expanded version as long as we nest deeper and flatten only when needed.
 */
Datum
create_nested_expanded_jsonb(JsonbValue *val, MemoryContext parentcontext)
{
    ExpandedJsonbHeader *ejbh;
    MemoryContext objcxt;

    /*
     * Allocate private context for expanded object.  We start by assuming
     * that the array won't be very large; but if it does grow a lot, don't
     * constrain aset.c's large-context behavior.
     */
    objcxt = AllocSetContextCreate(parentcontext,
                                   "expanded jsonb",
                                   ALLOCSET_START_SMALL_SIZES);

    /* Set up expanded jsonb header */
    ejbh = (ExpandedJsonbHeader *)
        MemoryContextAlloc(objcxt, sizeof(ExpandedJsonbHeader));

    EOH_init_header(&ejbh->hdr, &EA_methods, objcxt);

    ejbh->value = val;

    ejbh->flat_size = 0;
    ejbh->is_expanded = true;

    /* return a R/W pointer to the expanded jsonb */
    return EOHPGetRWDatum(&ejbh->hdr);
}