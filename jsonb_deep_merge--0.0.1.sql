\echo Use "Create extension json_deep_merge to load this file. \quit

CREATE FUNCTION jsonb_deep_merge (jsonb, jsonb)
RETURNS jsonb 
AS '$libdir/jsonb_deep_merge'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION jsonb_deep_merge (jsonb, jsonb, bool)
RETURNS jsonb 
AS '$libdir/jsonb_deep_merge'
LANGUAGE C IMMUTABLE STRICT;

CREATE AGGREGATE jsonb_deep_agg (jsonb)
(
    sfunc = jsonb_deep_merge,
    stype = jsonb,
    initcond = '{}'
);


