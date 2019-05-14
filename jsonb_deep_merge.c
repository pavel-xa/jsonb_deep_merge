#include "postgres.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/jsonb.h"
PG_MODULE_MAGIC;

/* internal functions */
static int	lengthCompareJsonbStringValue(const void* a, const void* b);
static bool JsonbValueNotNull(JsonbValue* scalarVal);
static bool pushJsonbValue1(JsonbParseState** pstate, JsonbValue* gkey, JsonbValue* key, JsonbIteratorToken seq, JsonbValue* scalarVal);
static bool pushJsonbValueGr(JsonbParseState** pstate, JsonbValue* gkey, JsonbIterator** it);

PG_FUNCTION_INFO_V1(jsonb_deep_merge);
Datum
jsonb_deep_merge(PG_FUNCTION_ARGS)
{
#ifdef PG_GETARG_JSONB_P
	Jsonb* jb1 = PG_GETARG_JSONB_P(0);
	Jsonb* jb2 = PG_GETARG_JSONB_P(1);
#else
	Jsonb* jb1 = PG_GETARG_JSONB(0);
	Jsonb* jb2 = PG_GETARG_JSONB(1);
#endif
	JsonbIterator* it1,
		* it2;
	JsonbValue	v1,
		v2,
		k,  //key
		g;	//group_key
	JsonbIteratorToken r1,
		r2;
	JsonbValue* res;
	JsonbParseState* state = NULL;

	/* Here we create the new jsonb */
	int			nestedLevel = 0;
	bool		group_start = false;

	if (jb1 == NULL)
#ifdef PG_RETURN_JSONB_P
		PG_RETURN_JSONB_P(jb2);
#else
		PG_RETURN_JSONB(jb2);
#endif
	if (jb2 == NULL)
#ifdef PG_RETURN_JSONB_P
		PG_RETURN_JSONB_P(jb1);
#else
		PG_RETURN_JSONB(jb1);
#endif
	if (!JB_ROOT_IS_OBJECT(jb1) || !JB_ROOT_IS_OBJECT(jb2))
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("Can only merge objects")));

	it1 = JsonbIteratorInit(&jb1->root);
	it2 = JsonbIteratorInit(&jb2->root);

	r1 = JsonbIteratorNext(&it1, &v1, false);
	r2 = JsonbIteratorNext(&it2, &v2, false);

	if (r1 != WJB_BEGIN_OBJECT || r2 != WJB_BEGIN_OBJECT)
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("Iterator was not an object")));

	pushJsonbValue(&state, r1, NULL);
	/* Start object */
	r1 = JsonbIteratorNext(&it1, &v1, false);
	r2 = JsonbIteratorNext(&it2, &v2, false);

	while (!(r1 == WJB_END_OBJECT && r2 == WJB_END_OBJECT && nestedLevel == 0))
	{
		int			difference;

		if (r1 == WJB_END_OBJECT && r2 == WJB_END_OBJECT && nestedLevel > 0)
		{
			if (group_start)
				pushJsonbValue(&state, WJB_END_OBJECT, NULL);
			group_start = false;
			r1 = JsonbIteratorNext(&it1, &v1, false);
			r2 = JsonbIteratorNext(&it2, &v2, false);
			nestedLevel--;
			continue;
		}
		if (r1 == WJB_END_OBJECT)
		{
			k = v2;
			r2 = JsonbIteratorNext(&it2, &v2, false);
			if (nestedLevel > 0 && !group_start)
				group_start = pushJsonbValue1(&state, &g, &k, r2, &v2);
			else
			{
				if ((&v2)->type == jbvObject)
				{
					if ((int)(&v2)->val.object.nPairs > 0)
						(void)pushJsonbValueGr(&state, &k, &it2);
				}
				else
					(void)pushJsonbValue1(&state, NULL, &k, r2, &v2);
			}
			r2 = JsonbIteratorNext(&it2, &v2, true);
			continue;
		}
		if (r2 == WJB_END_OBJECT)
		{
			k = v1;
			r1 = JsonbIteratorNext(&it1, &v1, false);
			if (nestedLevel > 0 && !group_start)
				group_start = pushJsonbValue1(&state, &g, &k, r1, &v1);
			else
			{
				if ((&v1)->type == jbvObject)
				{
					if ((int)(&v1)->val.object.nPairs > 0)
						(void)pushJsonbValueGr(&state, &k, &it1);
				}
				else
					(void)pushJsonbValue1(&state, NULL, &k, r1, &v1);
			}
			r1 = JsonbIteratorNext(&it1, &v1, true);
			continue;
		}
		difference = lengthCompareJsonbStringValue(&v1, &v2);
		/* first key is smaller */
		if (difference < 0)
		{
			k = v1;
			r1 = JsonbIteratorNext(&it1, &v1, false);
			if (nestedLevel > 0 && !group_start)
				group_start = pushJsonbValue1(&state, &g, &k, r1, &v1);
			else
			{
				if ((&v1)->type == jbvObject)
				{
					if ((int)(&v1)->val.object.nPairs > 0)
						(void)pushJsonbValueGr(&state, &k, &it1);
				}
				else
				{
					(void)pushJsonbValue1(&state, NULL, &k, r1, &v1);
				}
			}
			r1 = JsonbIteratorNext(&it1, &v1, true);
			continue;
		}
		else if (difference > 0)
		{
			k = v2;
			r2 = JsonbIteratorNext(&it2, &v2, false);
			if (nestedLevel > 0 && !group_start)
				group_start = pushJsonbValue1(&state, &g, &k, r2, &v2);
			else
			{
				if ((&v2)->type == jbvObject)
				{
					if ((int)(&v2)->val.object.nPairs > 0)
						(void)pushJsonbValueGr(&state, &k, &it2);
				}
				else
				{
					(void)pushJsonbValue1(&state, NULL, &k, r2, &v2);
				}
			}
			r2 = JsonbIteratorNext(&it2, &v2, true);
			continue;
		}
		k = v1;
		r2 = JsonbIteratorNext(&it2, &v2, false);
		r1 = JsonbIteratorNext(&it1, &v1, false);
		if ((&v1)->type != jbvObject && (&v2)->type != jbvObject)
		{
			if (nestedLevel > 0 && !group_start)
				group_start = pushJsonbValue1(&state, &g, &k, r1, &v2);
			else
			{
				(void)pushJsonbValue1(&state, NULL, &k, r1, &v2);
			}
		}
		else if ((&v1)->type == jbvObject && (&v2)->type == jbvObject)
		{
			g = k;
			group_start = false;
			nestedLevel++;
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("Incompatible types")));
		}
		r2 = JsonbIteratorNext(&it2, &v2, false);
		r1 = JsonbIteratorNext(&it1, &v1, false);

	}

	res = pushJsonbValue(&state, WJB_END_OBJECT, NULL);

	Assert(res != NULL);

#ifdef PG_RETURN_JSONB_P
	PG_RETURN_JSONB_P(JsonbValueToJsonb(res));
#else
	PG_RETURN_JSONB(JsonbValueToJsonb(res));
#endif
}

/* Taken from src/backend/adt/jsonb_utils.c
 * Compare two jbvString JsonbValue values, a and b.
 *
 * This is a special qsort() comparator used to sort strings in certain
 * internal contexts where it is sufficient to have a well-defined sort order.
 * In particular, object pair keys are sorted according to this criteria to
 * facilitate cheap binary searches where we don't care about lexical sort
 * order.
 *
 * a and b are first sorted based on their length.  If a tie-breaker is
 * required, only then do we consider string binary equality.
 */
static int
lengthCompareJsonbStringValue(const void* a, const void* b)
{
	const JsonbValue* va = (const JsonbValue*)a;
	const JsonbValue* vb = (const JsonbValue*)b;
	int			res;

	Assert(va->type == jbvString);
	Assert(vb->type == jbvString);

	if (va->val.string.len == vb->val.string.len)
	{
		res = memcmp(va->val.string.val, vb->val.string.val, va->val.string.len);
	}
	else
	{
		res = (va->val.string.len > vb->val.string.len) ? 1 : -1;
	}

	return res;
}

static bool
JsonbValueNotNull(JsonbValue* scalarVal)
{
	if ((scalarVal)->type != jbvNull)
	{
		if ((scalarVal)->type == jbvBool)
		{
			if (DatumGetBool(scalarVal->val.boolean))
				return true;
		}
		else
			return true;
	}
	return false;
}

static bool
pushJsonbValue1(JsonbParseState** pstate, JsonbValue* gkey, JsonbValue* key,
	JsonbIteratorToken seq, JsonbValue* scalarVal)
{
	if (JsonbValueNotNull(scalarVal))
	{
		if (gkey != NULL)
		{
			pushJsonbValue(pstate, WJB_KEY, gkey);
			pushJsonbValue(pstate, WJB_BEGIN_OBJECT, NULL);
		}
		pushJsonbValue(pstate, WJB_KEY, key);
		pushJsonbValue(pstate, seq, scalarVal);
		return true;
	}
	return false;
}

static bool
pushJsonbValueGr(JsonbParseState** pstate, JsonbValue* gkey,
				JsonbIterator** it)
{
	JsonbIteratorToken r;
	JsonbValue v, 
			   k;
	bool group_start = false;

	r = JsonbIteratorNext(it, &v, false);
	while (r != WJB_END_OBJECT)
	{
		k = v;
		r = JsonbIteratorNext(it, &v, false);
		if (JsonbValueNotNull(&v))
		{
			if (!group_start)
			{
				pushJsonbValue(pstate, WJB_KEY, gkey);
				pushJsonbValue(pstate, WJB_BEGIN_OBJECT, NULL);
				group_start = true;
			}
			pushJsonbValue(pstate, WJB_KEY, &k);
			pushJsonbValue(pstate, r, &v);
		}
		r = JsonbIteratorNext(it, &v, false);
	}
	if (group_start)
	{
		pushJsonbValue(pstate, WJB_END_OBJECT, NULL);
		return true;
	}

	return false;
}