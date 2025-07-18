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
    /* If we remember flat_size then no deconstruction was done  */
    Jsonb *jsonb = ejbh->flat_size == 0 ? JsonbValueToJsonb(&ejbh->value) : ejbh->fvalue;

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
    elog(WARNING, "Flattening");
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
    if (ejbh->flat_size != 0)
    {
        MemoryContext oldcxt = MemoryContextSwitchTo(ejbh->hdr.eoh_context);

        JsonbToJsonbValue(ejbh->fvalue, &ejbh->value);

        ejbh->flat_size = 0;
        MemoryContextSwitchTo(oldcxt);
    }
}
