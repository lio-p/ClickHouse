-- Tags: no-parallel
CREATE FUNCTION _02099_lambda_function AS x -> arrayMap(array_element -> array_element * 2, x);
SELECT _02099_lambda_function([1,2,3]);
DROP FUNCTION _02099_lambda_function;
