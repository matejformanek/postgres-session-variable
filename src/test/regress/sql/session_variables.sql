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

-- Number by default but convertible to text
SET @num := 5;
SELECT @num;
SELECT @num::TEXT;

-- Other way around as well
SET @num := '5';
SELECT @num;
SELECT @num::INT;

-- Self assigning
SET @var := 5;
SET @var := (@var * @var) + @var - 1;

SELECT @var;

-- Assigning from variables initiated earlier in the chain

-- Have to specify @created_int::INT here due to the parsing order, even though that at the point of evaluating
-- the last row we have the information that the @created_int sesvar is INT -> BUT when we firstly parsed Expr "*" 
-- we didn't have this information yet thus giving us unknown * unknown ERROR
SET @char_int := '5',
    @created_int := @char_int + 3,
    @multiple := @char_int * @created_int::INT;

SELECT @char_int,
       @created_int,
       @multiple;

SELECT @a := '5',
       @b := @a + 3,
       @c := @a * @b::INT;

-- DDL
CREATE TABLE test
(
    col_int  INTEGER,
    col_char VARCHAR(32)
);

-- DML
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

DO
$$
    DECLARE
        a INT := 3;
    BEGIN
        SET @pl := a + 3;
    END;
$$ LANGUAGE plpgsql;

SELECT @pl;

SET @pl := 0;

DO
$$
    DECLARE
        i INT;
    BEGIN
        FOR i IN 1..5
            LOOP
                SET @pl := @pl + i;
            END LOOP;
    END;
$$ LANGUAGE plpgsql;

SELECT @pl;


SET @pl := 0;

DO
$$
    DECLARE
        rec RECORD;
    BEGIN
        FOR rec IN SELECT col_int
                   FROM test
            LOOP
                SET @pl := @pl + rec.col_int;
            END LOOP;
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

CREATE OR REPLACE PROCEDURE set_session_variables_param(a INT)
AS
$$
DECLARE
BEGIN
    SET @pl := 1 + a;
END;
$$ LANGUAGE plpgsql;

CALL set_session_variables_param(5);

SELECT @pl;

DROP PROCEDURE set_session_variables_param;

-- SELECT simple assign
SELECT @var_int := 53;

SELECT @var_string := 'Text',
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

-- Cumulative query
SET @cum_int := 0,
    @cum_char := 'Hello';

SELECT @cum_int := @cum_int + num, @cum_char := @cum_char || ', hello again'
FROM GENERATE_SERIES(1, 5, 1) num;

SELECT @cum_int, @cum_char;

CREATE OR REPLACE FUNCTION overloaded_text_numeric(str TEXT)
    RETURNS TEXT
AS
$$
BEGIN
    RETURN str || ' Append text';
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION overloaded_text_numeric(str NUMERIC)
    RETURNS TEXT
AS
$$
BEGIN
    RETURN str || ' Append numeric';
END;
$$ LANGUAGE plpgsql;

-- Do automatic cache plan invalidation on type change to ensure correct usage 
DO
$$
    DECLARE
        t TEXT;
    BEGIN
        FOR i IN 1..10
            LOOP
                IF i % 2 = 1 THEN
                    SET @x := 10.5; -- numeric
                ELSE
                    SET @x := 'Ahoj'::TEXT;
                END IF;
                SELECT overloaded_text_numeric(@x) INTO t;
                RAISE NOTICE '%', t;
            END LOOP;
    END;
$$;

DROP FUNCTION overloaded_text_numeric(str TEXT);

DROP FUNCTION overloaded_text_numeric(str NUMERIC);

-- Custom operator "@" X SESVAR

INSERT INTO test VALUES (-11, 'Negative value');

SET @col_int := -9;

SELECT @col_int
FROM test
LIMIT 1;

SELECT @test.col_int
FROM test
WHERE col_int < 0
LIMIT 1;

SELECT @(col_int)
FROM test
WHERE col_int < 0
LIMIT 1;

SELECT @(-5), @(SELECT -3);

DROP TABLE test;