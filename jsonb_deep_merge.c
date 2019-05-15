#include "postgres.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/jsonb.h"

PG_MODULE_MAGIC;

/* export */
PG_FUNCTION_INFO_V1(jsonb_deep_merge);

/* internal functions */
static int	lengthCompareJsonbStringValue(const void* a, const void* b);
static bool JsonbValueNotNull(const JsonbValue* scalarVal, const bool strip_false);
static bool pushJsonbKey(JsonbParseState** pstate, JsonbValue* gkey, JsonbValue* key, const JsonbIteratorToken seq, JsonbValue* scalarVal, const bool strip_false);
static bool pushJsonbValueGr(JsonbParseState** pstate, JsonbValue* gkey, JsonbIterator** it, const bool strip_false);

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

	bool	strip_false = PG_NARGS() > 2 ? PG_GETARG_BOOL(2) : true;
	int		lvl = 0;
	bool	group_start = false;

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

	/* Here we create the new jsonb */
	pushJsonbValue(&state, r1, NULL);

	/* Start object */
	r1 = JsonbIteratorNext(&it1, &v1, false);
	r2 = JsonbIteratorNext(&it2, &v2, false);

	while (!(r1 == WJB_END_OBJECT && r2 == WJB_END_OBJECT && lvl == 0))
	{
		int			difference;

		if (r1 == WJB_END_OBJECT && r2 == WJB_END_OBJECT && lvl > 0)
		{
			if (group_start)
				pushJsonbValue(&state, WJB_END_OBJECT, NULL);
			group_start = false;
			r1 = JsonbIteratorNext(&it1, &v1, false);
			r2 = JsonbIteratorNext(&it2, &v2, false);
			lvl--;
			continue;
		}
		if (r1 == WJB_END_OBJECT)
		{
			Assert(r2 == WJB_KEY);

			k = v2;
			r2 = JsonbIteratorNext(&it2, &v2, false);

			if (lvl > 0 && !group_start)
				group_start = pushJsonbKey(&state, &g, &k, r2, &v2, strip_false);
			else
			{
				if ((&v2)->type == jbvObject)
				{
					if ((int)(&v2)->val.object.nPairs > 0)
						(void)pushJsonbValueGr(&state, &k, &it2, strip_false);
				}
				else
					(void)pushJsonbKey(&state, NULL, &k, r2, &v2, strip_false);
			}
			r2 = JsonbIteratorNext(&it2, &v2, false);
			continue;
		}
		if (r2 == WJB_END_OBJECT)
		{
			Assert(r1 == WJB_KEY);

			k = v1;
			r1 = JsonbIteratorNext(&it1, &v1, false);

			if (lvl > 0 && !group_start)
				group_start = pushJsonbKey(&state, &g, &k, r1, &v1, false);
			else
			{
				if ((&v1)->type == jbvObject)
				{
					if ((int)(&v1)->val.object.nPairs > 0)
						(void)pushJsonbValueGr(&state, &k, &it1, false);
				}
				else
					(void)pushJsonbKey(&state, NULL, &k, r1, &v1, false);
			}
			r1 = JsonbIteratorNext(&it1, &v1, false);
			continue;
		}

		Assert(r1 == WJB_KEY);
		Assert(r2 == WJB_KEY);

		difference = lengthCompareJsonbStringValue(&v1, &v2);
		/* first key is smaller */
		if (difference < 0)
		{
			k = v1;
			r1 = JsonbIteratorNext(&it1, &v1, false);

			if (lvl > 0 && !group_start)
				group_start = pushJsonbKey(&state, &g, &k, r1, &v1, false);
			else
			{
				if ((&v1)->type == jbvObject)
				{
					if ((int)(&v1)->val.object.nPairs > 0)
						(void)pushJsonbValueGr(&state, &k, &it1, false);
				}
				else
					(void)pushJsonbKey(&state, NULL, &k, r1, &v1, false);
			}
			r1 = JsonbIteratorNext(&it1, &v1, false);
			continue;
		}
		/* first key is bigger */
		else if (difference > 0)
		{
			
			k = v2;
			r2 = JsonbIteratorNext(&it2, &v2, false);

			if (lvl > 0 && !group_start)
				group_start = pushJsonbKey(&state, &g, &k, r2, &v2, strip_false);
			else
			{
				if ((&v2)->type == jbvObject)
				{
					if ((int)(&v2)->val.object.nPairs > 0)
						(void)pushJsonbValueGr(&state, &k, &it2, strip_false);
				}
				else
					(void)pushJsonbKey(&state, NULL, &k, r2, &v2, strip_false);
			}
			r2 = JsonbIteratorNext(&it2, &v2, false);
			continue;
		}

		/* keys match */
		k = v1;
		r2 = JsonbIteratorNext(&it2, &v2, false);
		r1 = JsonbIteratorNext(&it1, &v1, false);

		if ((&v2)->type != jbvObject && (&v2)->type != jbvArray)
		{
			if (lvl > 0 && !group_start)
				group_start = pushJsonbKey(&state, &g, &k, r2, &v2, strip_false);
			else
				(void)pushJsonbKey(&state, NULL, &k, r2, &v2, strip_false);

			if ((&v1)->type == jbvObject)
				while (r1 != WJB_END_OBJECT)
				{
					r1 = JsonbIteratorNext(&it1, &v1, false);
				}
		}
		else if ((&v1)->type != jbvObject && (&v1)->type != jbvArray && (&v2)->type == jbvObject)
		{
			if ((int)(&v2)->val.object.nPairs > 0)
				(void)pushJsonbValueGr(&state, &k, &it2, strip_false);
		}
		else if ((&v1)->type == jbvObject && (&v2)->type == jbvObject)
		{
			g = k;
			group_start = false;
			lvl++;
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
JsonbValueNotNull(const JsonbValue* scalarVal, const bool strip_false)
{
	if ((scalarVal)->type != jbvNull)
	{
		if ((scalarVal)->type == jbvBool && strip_false)
			return DatumGetBool(scalarVal->val.boolean);
		else
			return true;
	}
	return false;
}

static bool
pushJsonbKey(JsonbParseState** pstate, JsonbValue* gkey, JsonbValue* key,
	const JsonbIteratorToken seq, JsonbValue* scalarVal, const bool strip_false)
{
	Assert(key->type == jbvString);

	if (JsonbValueNotNull(scalarVal, strip_false))
	{
		if (gkey != NULL)
		{
			Assert(gkey->type == jbvString);

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
				JsonbIterator** it, const bool strip_false)
{
	JsonbIteratorToken r;
	JsonbValue v, 
			   k; //key
	bool group_start = false;

	r = JsonbIteratorNext(it, &k, true);
		
	while (r != WJB_END_OBJECT)
	{
		Assert(r == WJB_KEY);

		r = JsonbIteratorNext(it, &v, true);

		if (!group_start)
			group_start = pushJsonbKey(pstate, gkey, &k, r, &v, strip_false);
		else
			(void)pushJsonbKey(pstate, NULL, &k, r, &v, strip_false);

		r = JsonbIteratorNext(it, &k, true);
	}

	if (group_start)
		pushJsonbValue(pstate, WJB_END_OBJECT, NULL);

	return group_start;
}