# jsonb_deep_merge

jsonb_deep_merge is a PostgreSQL extension to easily merge jsonb with removing empty keys, null keys, boolean keys with "false" value.
Boolean keys with a false value are deleted only from the right jsonb. If you do not need to delete them, you must pass "false" as the 3rd parameter of the function.

Provided functions:

* `jsonb_deep_merge(a jsonb, b jsonb, c bool)`  
`a` - jsonb object  
`b` - jsonb object  
`c` - when "true" (default) removing boolean keys with "false" value from the right jsonb

It also provides an aggregation function `jsonb_deep_agg`

    

## INSTALLATION

Clone source:

	git clone https://github.com/pavel-xa/jsonb_deep_merge.git
	cd jsonb-extend
    
In the directory where you downloaded jsonb_deep_merge, run

    make && make install
    
Make install will copy the extension files to the postgres folder, so make sure that you have the necessary permissions.
It might also happen that pgxs is not found. For that you might need to install postgresql-server-dev-all and postgresql-common [[link](https://github.com/travis-ci/travis-ci/issues/2864)].


Once you have successfully compiled the extension log into postgresql and do:

    CREATE EXTENSION jsonb_deep_merge;
    


## EXAMPLES
	SELECT jsonb_deep_merge('{"a": 1}', '{"a": 2}');
    > '{"a": 2}'

	SELECT jsonb_deep_merge('{"a": 1}', '{"a": null}');
    > '{}'

	SELECT jsonb_deep_merge('{"a": 1}', '{"a": false}');
    > '{}'

	SELECT jsonb_deep_merge('{"a": 1}', '{"a": false}', false);
	> '{"a": false}'
    
    SELECT jsonb_deep_merge('{"a": {"b": 1}}', '{"a": {"b": "3"}}');
    > '{"a": {"b": "3"}}'

	SELECT jsonb_deep_merge('{"a": {"b": 1}}', '{"a": {"b": null}}');
    > '{}'

	SELECT jsonb_deep_merge('{"a": {"b": 1}}', '{"a": {"b": null, "c": "3"}}');
    > '{"a": {"c": "3"}}'

	SELECT jsonb_deep_merge('{"a": {"b": 1}}', '{"a": {"b": false}}');
    > '{}'

	SELECT jsonb_deep_merge('{"a": {"b": 1}}', '{"a": {"b": false}}', false);
	> '{"a": {"b": false}}'

	CREATE TABLE simple_nested (data jsonb);
    INSERT INTO simple_nested VALUES ('{"a": 1}'), 
					('{"a": 2, "b": 3, "c": 7, "d": 9}'), 
					('{"a": 5, "c": null}'), 
					('{"a": 3, "b": 1, "d": false}'), 
					(NULL);
    SELECT jsonb_deep_agg(data) FROM simple_nested;
    > {"a": 3, "b": 1}



## TESTING

To run the tests use:

    make install && make installcheck

All the tests are in the sql directory.

## BENCHMARKING

## INTERNALS

JSONB is internall represented as a tree in which all levels are sorted.
 Postgres provides iterators to walk this tree in DFS which respects this sorting. The algorithm uses this order to perform a [sorted merge join](https://en.wikipedia.org/wiki/Sort-merge_join).
  
  
## LIMITATIONS

Right now, the algorithm supports JSONB only level 1 nesting.
