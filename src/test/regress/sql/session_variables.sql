-- Set variable
SET @var_int := 53;

-- Set multiple variables at once of different types
SET @var_string := 'Text',
    @var_null := NULL,
    @var_date := '2024-05-01',
    @var_array := ARRAY [1,5,3,4];

-- Selection plus operations with values
SELECT @var_int,
       @var_int + 5,
       @var_null,
       @var_string,
       @var_string || ' Concat',
       @var_date,
       @var_date::DATE + '1 MONTH'::INTERVAL,
       @var_array,
       (@var_array::anyarray)[2];

-- Check that the variable values stayed the same
SELECT @var_string,
       @var_null,
       @var_date,
       @var_array;

-- Self assigning
SET @var := 5,
    @var := (@var * @var) + @var - 1;

SELECT @var;

-- Assigning from different variables
SET @char_int := '5',
    @created_int := @char_int + 3,
    @multiple := @char_int * @created_int;

SELECT @char_int,
       @created_int,
       @multiple;

CREATE TABLE test
(
    col_int  INTEGER,
    col_char VARCHAR(32)
);

-- DDL
INSERT INTO test (col_int, col_char)
VALUES (@char_int, @char_int),
       (@multiple / @char_int, @var_string || ' Concat'),
       (6, '5'),
       (6, '5'),
       (6, 'Blocker'),
       (6, 'Blocker'),
       (6, '5'),
       (6, '5'),
       (6, '5');

-- WHERE clause
SELECT *
FROM test
WHERE col_int = @char_int
   OR col_int = @created_int;

-- FROM & LIMIT clause
SELECT *
FROM test t1
         JOIN test t2 ON t1.col_char = @char_int AND t2.col_int = @char_int + 1
LIMIT @char_int - 3;

DROP TABLE test;

-- Transaction
SET @var := 1;
BEGIN;
SET @var := 2;
ROLLBACK;
SELECT @var;

SET @pl := 5;

-- DO block
DO
$$
    DECLARE
    BEGIN
        SET @pl := 3;
        ROLLBACK;
    END;
$$ LANGUAGE plpgsql;

SELECT @pl;

-- PROCEDURE
CREATE OR REPLACE PROCEDURE set_session_variables()
AS
$$
DECLARE
BEGIN
    SET @pl := 1;
END;
$$ LANGUAGE plpgsql;

CALL set_session_variables();

SELECT @pl;

DROP PROCEDURE set_session_variables();