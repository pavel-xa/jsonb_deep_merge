#include "postgres.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/jsonb.h"

PG_MODULE_MAGIC;

/* export */
PG_FUNCTION_INFO_V1(jsonb_deep_merge);

/* internal functions */
static JsonbIteratorToken JsonbNextToken(JsonbIterator** it, JsonbValue* val, bool skipNested);
static int	lengthCompareJsonbStringValue(const void* a, const void* b);
static bool JsonbValueNotNull(const JsonbValue* scalarVal, const bool strip_false);
static bool pushJsonbKey(JsonbParseState** pstate, JsonbValue* gkey, JsonbValue* key, const JsonbIteratorToken seq, JsonbValue* scalarVal,
		const int* lvl, const bool group_start, const bool strip_false);
static bool pushJsonbKeyRecursive(JsonbParseState** pstate, JsonbValue* gkey, JsonbValue* key, const JsonbIteratorToken seq, JsonbValue* val,
		JsonbIterator** it, int* lvl, bool group_start, const bool strip_false);

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
	JsonbIteratorToken tok1,
		tok2;
	JsonbValue* res;
	JsonbParseState* state = NULL;

	bool	strip_false = PG_NARGS() > 2 ? PG_GETARG_BOOL(2) : true;
	int		lvl = 0;
	bool	group_start = true;

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

	tok1 = JsonbNextToken(&it1, &v1, false);
	tok2 = JsonbNextToken(&it2, &v2, false);

	if (tok1 != WJB_BEGIN_OBJECT || tok2 != WJB_BEGIN_OBJECT)
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("Iterator was not an object")));

	/* Here we create the new jsonb */
	pushJsonbValue(&state, tok1, NULL);

	/* Start object */
	tok1 = JsonbNextToken(&it1, &v1, false);
	tok2 = JsonbNextToken(&it2, &v2, false);

	while (!(tok1 == WJB_END_OBJECT && tok2 == WJB_END_OBJECT && lvl == 0))
	{
		int			difference;

		if (tok1 == WJB_END_OBJECT && tok2 == WJB_END_OBJECT && lvl > 0)
		{
			if (group_start)
				pushJsonbValue(&state, WJB_END_OBJECT, NULL);
			group_start = true;
			tok1 = JsonbNextToken(&it1, &v1, false);
			tok2 = JsonbNextToken(&it2, &v2, false);
			lvl--;
			continue;
		}
		if (tok1 == WJB_END_OBJECT)
		{
			Assert(tok2 == WJB_KEY);

			group_start = pushJsonbKeyRecursive(&state, &g, &v2, tok2, NULL, &it2, &lvl, group_start, strip_false);
			tok2 = JsonbNextToken(&it2, &v2, false);
			continue;
		}
		if (tok2 == WJB_END_OBJECT)
		{
			Assert(tok1 == WJB_KEY);

			group_start = pushJsonbKeyRecursive(&state, &g, &v1, tok1, NULL, &it1, &lvl, group_start, false);
			tok1 = JsonbNextToken(&it1, &v1, false);
			continue;
		}

		Assert(tok1 == WJB_KEY);
		Assert(tok2 == WJB_KEY);

		difference = lengthCompareJsonbStringValue(&v1, &v2);
		/* first key is smaller */
		if (difference < 0)
		{
			group_start = pushJsonbKeyRecursive(&state, &g, &v1, tok1, NULL, &it1, &lvl, group_start, false);
			tok1 = JsonbNextToken(&it1, &v1, false);
			continue;
		}
		/* first key is bigger */
		else if (difference > 0)
		{
			group_start = pushJsonbKeyRecursive(&state, &g, &v2, tok2, NULL, &it2, &lvl, group_start, strip_false);
			tok2 = JsonbNextToken(&it2, &v2, false);
			continue;
		}

		/* keys equal */
		k = v2;
		tok1 = JsonbNextToken(&it1, &v1, false);
		tok2 = JsonbNextToken(&it2, &v2, false);

		if ((&v1)->type != jbvObject || (&v2)->type != jbvObject)
		{
			group_start = pushJsonbKeyRecursive(&state, &g, &k, tok2, &v2, &it2, &lvl, group_start, strip_false);

			if ((&v1)->type == jbvObject)
				while (tok1 != WJB_END_OBJECT)
				{
					tok1 = JsonbNextToken(&it1, &v1, true);
				}
		}
		else if ((&v1)->type == jbvObject && (&v2)->type == jbvObject)
		{
			if (lvl == 0)
				group_start = false;
			else
			{
				/* push WJB_BEGIN_OBJECT */
				Assert(tok2 == WJB_BEGIN_OBJECT);

				(void)pushJsonbKey(&state, &g, &k, tok2, NULL, &lvl, group_start, false);
				group_start = true;
			}
			g = k;
			lvl++;
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("Incompatible types")));
		}
		tok1 = JsonbNextToken(&it1, &v1, false);
		tok2 = JsonbNextToken(&it2, &v2, false);
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
	if (scalarVal != NULL)
	{
		if ((scalarVal)->type != jbvNull)
		{
			if ((scalarVal)->type == jbvBool && strip_false)
				return DatumGetBool(scalarVal->val.boolean);
			else
				return true;
		}
	}
	return false;
}

static bool
pushJsonbKey(JsonbParseState** pstate, JsonbValue* gkey, JsonbValue* key,
	const JsonbIteratorToken seq, JsonbValue* scalarVal,
	const int* lvl, const bool group_start, const bool strip_false)
{
	Assert(key->type == jbvString);

	if (seq == WJB_BEGIN_OBJECT || seq == WJB_END_OBJECT || JsonbValueNotNull(scalarVal, strip_false))
	{
		if (gkey != NULL && *lvl > 0 && !group_start)
		{
			Assert(gkey->type == jbvString);

			pushJsonbValue(pstate, WJB_KEY, gkey);
			pushJsonbValue(pstate, WJB_BEGIN_OBJECT, NULL);
		}

		pushJsonbValue(pstate, WJB_KEY, key);
		if (seq == WJB_BEGIN_OBJECT || seq == WJB_END_OBJECT)
			pushJsonbValue(pstate, seq, NULL);
		else
			pushJsonbValue(pstate, seq, scalarVal);
		return true;
	}
	return group_start;
}

static bool
pushJsonbKeyRecursive(JsonbParseState** pstate, JsonbValue* gkey, JsonbValue* key,
	const JsonbIteratorToken seq, JsonbValue* val, JsonbIterator** it,
	int* lvl, bool group_start, const bool strip_false)
{
	JsonbIteratorToken tok;
	JsonbValue v,
			   k; //key

	Assert(key->type == jbvString);

	if (val != NULL)
	{
		tok = seq;
		v = *val;
	}
	else
		tok = JsonbNextToken(it, &v, false);

	if ((&v)->type != jbvObject)
	{
		group_start = pushJsonbKey(pstate, gkey, key, tok, &v, lvl, group_start, strip_false);
	}
	else if ((int)(&v)->val.object.nPairs > 0) /* val->type = jbvObject */
	{
		if (*lvl == 0)
			group_start = false;
		else
		{
			/* push WJB_BEGIN_OBJECT */
			Assert(tok == WJB_BEGIN_OBJECT);

			(void)pushJsonbKey(pstate, gkey, key, tok, NULL, lvl, group_start, false);
			group_start = true;
		}
		(*lvl)++;

		tok = JsonbNextToken(it, &k, false);

		while (tok != WJB_END_OBJECT)
		{
			Assert(tok == WJB_KEY);

			group_start = pushJsonbKeyRecursive(pstate, key, &k, tok, NULL, it, lvl, group_start, strip_false);
			tok = JsonbNextToken(it, &k, false);
		}

		if (group_start)
			pushJsonbValue(pstate, WJB_END_OBJECT, NULL);
		(*lvl)--;
	}
	return group_start;
}

static JsonbIteratorToken
JsonbNextToken(JsonbIterator** it, JsonbValue* val, bool skipNested)
{
	JsonbIteratorToken tok = JsonbIteratorNext(it, val, skipNested);
	if (tok == WJB_BEGIN_ARRAY || tok == WJB_END_ARRAY || tok == WJB_ELEM)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("Arrays are not allowed")));
	}
	return tok;
}