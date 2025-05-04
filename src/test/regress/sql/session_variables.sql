-------------------------------------------- SET ----------------------------------------------------------
-- Simple assigning ------------------------
-- Set variable
SET @var_int := (SELECT 53);

-- Set multiple variables at once of different types
SET @var_string := 'Text',
    @var_null := NULL,
    @var_date := '2024-05-01',
    @var_array := ARRAY [1,5,3,4],
    @var_int_sci := 7e-5;

-- Selection plus operations with values
SELECT @var_int,
       @var_int + 5,
       @var_int_sci,
       @var_null,
       @var_int IS NULL,
       @var_null IS NULL,
       @var_string,
       @var_string || ' Concat',
       @var_date,
       @var_date::DATE + '1 MONTH'::INTERVAL,
       @var_array,
       @var_array[2:3],
       (@var_array::ANYARRAY)[2];

-- Check that the variable values stayed the same
SELECT @var_string,
       @var_int,
       @var_int_sci,
       @var_null,
       @var_date,
       @var_array;

-- empty string
SET @a := '';
SELECT @a;

SET @a := (5, 2);

SELECT @a;

SET @a := test; -- should fail

SET @gs := GENERATE_SERIES(1, 5, 10); -- should fail

-- Self assigning --------------------------
SET @var := 5;
SET @var := (@var * @var) + @var - 1;

SELECT @var;

SET @sa := @sa + 1; -- should fail

SET @sa := @nonexistent; -- should fail
    
SELECT @sa; -- should fail
        
-- Assigning from variables initiated earlier in the chain
--
-- Thanks to the pstate->sesvar_changes we don't have to add typecast to
-- @char_int * @created_int previously gained expr types will be assigned to them ->
-- this way parser knows @created_int is an INT even though it has not yet been saved in memory
-- (the variable is still "not existing" at this point -> if it crashes it won't be saved logically).
SET @char_int := '5',
    @created_int := @char_int + 3,
    @multiple := @char_int * @created_int;

SELECT @char_int,
       @created_int,
       @multiple;

SET @a := 1,
    @a := 'Text',
    @a := '2024-01-01'::DATE;

SELECT @a;

SET @num := '5',
    @int := 5.5,
    @res := @num * @int;

SELECT @res;

SET @num := '5.5', -- should fail -> Tries to coerce to INT not NUMERIC 
    @int := 5,
    @res := @num * @int;

-- Overwrites everything before the ERROR
SELECT @num, @int, @res;

-- Variable name length --------------------
-- @ + 62 length is OK
SET @X2345678901234567890123456789012345678901234567890123456789012 := 12;
SELECT @x2345678901234567890123456789012345678901234567890123456789012 := 12;
SELECT @x2345678901234567890123456789012345678901234567890123456789012;

-- @ + 63 should be TRUNCATED
SET @X23456789012345678901234567890123456789012345678901234567890123 := 12;
SELECT @x23456789012345678901234567890123456789012345678901234567890123 := 12;
SELECT @x23456789012345678901234567890123456789012345678901234567890123;

-- Type ------------------------------------
-- Number by default but convertible to text
SET @num := 5;
SELECT @num;
SELECT @num::TEXT;

-- Other way around as well
SET @num := '5';
SELECT @num;
SELECT @num::INT;

-- Remember inline types
SET @dat := '2024-01-01'::DATE,
    @intv := '1 MONTH'::INTERVAL,
    @res := @dat + @intv;

SELECT @dat, @intv, @res;

SET @dat := CAST('2024-01-01' AS DATE),
    @intv := CAST('1 MONTH' AS INTERVAL),
    @res := @dat + @intv;

SELECT @dat, @intv, @res;

-- Type handling
CREATE TYPE TEST_TYPE AS
(
    a INT,
    b TEXT
);

SET @typ := (5, 'ahoj');

SELECT @typ, (@typ::TEST_TYPE).a, (@typ::TEST_TYPE).b;

SELECT (@typ).a; -- should fail

SET @typ := (5, 'ahoj')::TEST_TYPE;

SELECT @typ, (@typ).a, (@typ).b, (@typ).*;

SELECT @typ.a; -- should fail

SET @t := (@typ).*; -- should fail

SET @arr := ARRAY [8, 4 , 5];

SELECT (@arr)[2];

SELECT @arr[2], @arr, @arr[2:3];

SET @arr := '{5,2,3}'::INT [];

SELECT @arr[1];

SET @arr := '{5,2,3}';

SELECT @arr[1]; -- should fail -> we don't want to guess the type of array

SET @arr := 5;

SELECT @arr[2]; -- should fail -> not an array

SELECT @arr := '{3,2,5}'::INT[], @arr[1];

SELECT @arr[1] := 4;

SET @arr[2] := '4';

SELECT @arr, @arr[1], @arr[2:3];

SET @arr[1:2] := 5;

SELECT @arr;

SET @arr := ARRAY [(5, 'ahoj')::TEST_TYPE, (3, 'jakje')::TEST_TYPE];

SELECT @arr, (@arr[1]).a, (@arr[1]).b;

SELECT @arr[1] := 'Hello'; -- should fail

SELECT @arr[1] := NULL; -- should fail

SET @arr[4] := 4; -- should fail

SET @arr[-1] := 4; -- should fail

SELECT @arr_non_existent[1] := 5; -- should fail

SET @js := '{"col": "value"}'::JSON;

SELECT @js ->> 'col';

SET @js := '{"col": "value"}';

SELECT @js::JSON ->> 'col';

SELECT @js ->> 'col'; -- should fail

-- Collation -------------------------------
SET @txt1 := 'test', @"TXT" := 'TEST';

SELECT @txt1 < @"TXT"; -- should fail

SELECT @txt1 < @"TXT" COLLATE "POSIX";

SELECT @txt1 < @"TXT"; -- should fail

SET @"TXT" := 'TEST' COLLATE "POSIX";

SELECT @txt1 < @"TXT";


-- Un/Quoted names -------------------------
SET @TEST := 2, @test := 5, @"Test" := 3, @"Te.St" := 4;

SELECT @TEST, @test, @"Test", @"Te.St";

SELECT @"tEST"; -- should fail

SET "@t" := 5; -- should fail

SET @"@t" := 5,
    @"5 name starting with number and spaces." := 3,
    @"Quoted-special | 'test' ;" := 1,
    @"-" := 0,
    @d_s3 := '';

SELECT @"@t",
       @"5 name starting with number and spaces.",
       @"Quoted-special | 'test' ;",
       @"-",
       @d_s3;

SET @5ds := ''; -- should fail

SET @@ds := ''; -- should fail

SET @ds@ := ''; -- should fail

SET @"" := ''; -- should fail

SET @ := ''; -- should fail
    
-------------------------------------------- PREPARE DATA -----------------------------------------------
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

-------------------------------------------- SELECT -----------------------------------------------------
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
       (@var_array::ANYARRAY)[2];

SELECT (@v := col_int) <> (@v := 6), col_int, @v
FROM test;

-- Type ------------------------------------
-- TEXT type coercion with concate operator
-- Used to cause bugs due to text being saved in SESVAR as UNKNOWNOID
-- but coerce works only with TEXTOID
SELECT @b := '.hoj'::TEXT;
SELECT @b;
SELECT 'A' || @b, 'A' || @b;
SELECT @b := '.hoj' || '.A';
SELECT 'A' || @b := '.hoj', 'A' || @b;
SELECT 'A' || @b := NULL, 'A' || @b;

SELECT @a := '5',
       @b := @a + 3,
       @c := @a * @b;

-- Remember inline types
SELECT @dat := '2024-01-01'::DATE,
       @intv := '1 MONTH'::INTERVAL,
       @res := @dat + @intv;

-- Moreover to sesvar inline type changes
SET @a := 0;

-- The col #3 must not give parser return type INT because we know it will be changed in col before
SELECT @a, @a := 'hello', @a, @a := 3, @a, @a := 'hello again';

SELECT @a := 10,
       @b := 2,
       @a > @b,
       @a := '10',
       @b := 2,
       @a > @b,
       @a := 2,
       @b := 10,
       @a > @b,
       @a := '2',
       @b := '10',
       @a::INT > @b;

SET @a := 0;

SELECT @a, @a := 'hello', @a, @a := 3, @a, @a := 'hello again' -- should fail;
FROM GENERATE_SERIES(1, 3, 1) num;

SET @a := 0;

SELECT @a, @a := 'hello', @a, @a := 3, @a
FROM GENERATE_SERIES(1, 3, 1) num;

-- Expr inside expr ------------------------
SET @t1 := (@t2 := 1) + @t3 := 4;

SELECT @t1, @t2, @t3;

SET @t1 := @t2 := @t3 := 1;

SELECT @t1, @t2, @t3;

SELECT @t1 := (@t2 := 1) + @t3 := 4,
       @t1,
       @t2,
       @t3 * @t1;

SELECT @t1 := (@t3 := @t2 := 5 + @t3 := 2) + (@t2 := (@t3) * 2) + @t2,
       @t2,
       @t2 := 2,
       @t3,
       @t3 := @t3 + @t2;

SELECT @t1 := @t2 := @t3 := 1, @t1, @t2, @t3;

SELECT @a := @a := @a := @a := @a := 5;

SELECT ((@v := 5) <> (@v := 6)) = (@v := TRUE), @v;

-- Cumulative query ------------------------
SELECT @t1 := @t1 + (@t2 := @t2 * @t2), @t2 := @t2 + 1
FROM GENERATE_SERIES(1, 3, 1) num;

SET @cum_int := 0,
    @cum_char := 'Hello';

SELECT @cum_int := @cum_int + num, @cum_char := @cum_char || ', hello again'
FROM GENERATE_SERIES(1, 5, 1) num;

SELECT @cum_int, @cum_char;

SET @cum_int := 0,
    @cum_char := 'Hello';

SELECT @cum_int := @cum_int + col_int, @cum_char := @cum_char || ' ' || col_char
FROM test;

SELECT @c := 1, @c := @c + col_int, @c := 'Text', @c := @c || ' ' || col_char
FROM test;

-- Usage in queries ------------------------
SET @char_int := '5',
    @created_int := @char_int + 3,
    @multiple := @char_int * @created_int;
    
-- WHERE clause
SELECT *
FROM test
WHERE col_int = @char_int
   OR col_int = @created_int;

SELECT *
FROM test
WHERE col_int IN (@char_int, @created_int);

SELECT *, @created_int, @created_int := 3
FROM test
WHERE col_int = @char_int
   OR col_int = @created_int := 6;

SELECT *, @created_int
FROM test
WHERE col_int = @created_int := col_int;

-- FROM & LIMIT clause
SELECT *
FROM test t1
         JOIN test t2 ON t1.col_char = @char_int AND t2.col_int = @char_int + 1
LIMIT @char_int - 3;

SELECT @char_int := 5,
       @txt := '';

-- Expression madness
SELECT *, @char_int := @char_int + 1, @txt := @txt || 'S-'
FROM test t1
         JOIN test t2 ON t2.col_int < @char_int := @char_int + 1 AND (@txt := @txt || 'F-') IS NOT NULL
WHERE t2.col_int < @char_int := @char_int + 1
  AND (@txt := @txt || 'W-') IS NOT NULL
LIMIT @char_int := @char_int + 10 + COALESCE(NULLIF((@txt := @txt || 'L-') IS NOT NULL, TRUE), FALSE)::INT;

-- DDL & DML -------------------------------
CREATE TABLE tmp
(
    cc  INT,
    coo INT DEFAULT @df,
    co  INT DEFAULT @df := 2
);

INSERT INTO tmp (cc)
VALUES (@df := 1),
       (@df := 2),
       (@df := 3),
       (@df := 4);

SELECT *
FROM tmp;

SET @df := 'Text',
    @df2 := 5;

INSERT INTO tmp (cc)
VALUES (5); -- should fail

INSERT INTO tmp (cc, coo)
VALUES (5, 5);

ALTER TABLE tmp
    ALTER COLUMN coo SET DEFAULT @df2;

ALTER TABLE tmp
    ALTER COLUMN co SET DEFAULT @df2 := 3;

INSERT INTO tmp (cc)
VALUES (@df2 := 6);

SELECT *
FROM tmp;

SET @tt := 0;

UPDATE tmp
SET co = @tt := @tt + coo
WHERE co = 2;

SELECT *, @tt
FROM tmp
ORDER BY cc;

DELETE FROM tmp
WHERE co = @tt - 12;

SELECT *, @tt
FROM tmp
ORDER BY cc;

CREATE TABLE tmp2
(
    cc  INT,
    coo INT DEFAULT @d
);

INSERT INTO tmp2 (cc)
VALUES (5); -- should fail

DROP TABLE tmp;

DROP TABLE tmp2;

-- Aggregated functions --------------------
SELECT @min_ci := @b := MIN(col_int) * 2, @max_ci := MAX(col_int) + @b, @b
FROM test;

SELECT col_char, @sum_ci := SUM(col_int)
FROM test
GROUP BY col_char;

SELECT @sum_ci;

SET @agg := 0;

SELECT @agg := @agg + col_int, @cnt := COUNT(*)
FROM test
GROUP BY 1;

SELECT @agg, @cnt, @agg := 0;

SELECT @agg := @agg + col_int, @cnt := COUNT(*)
FROM test
GROUP BY @agg := @agg + col_int;

SELECT @agg, @cnt, @agg := 0;

SELECT @agg := @agg + col_int, @cnt := COUNT(*)
FROM test
GROUP BY col_int;

SELECT @agg, @cnt, @agg := 0;

SELECT @agg := col_int, @cnt := COUNT(*)
FROM test
GROUP BY 1;

-- @agg works over the data being prepared for aggregation
-- @cnt saves the result of aggregation
-- It means that @agg and @cnt do NOT have to be from the same row
SELECT @agg, @cnt, @agg := 0;

SELECT @agg := col_int, @cnt := COUNT(*)
FROM test
GROUP BY col_int;

SELECT @agg, @cnt, @agg := 0;

SET @a := 0;

SELECT col_int, @a, @a := @a + COUNT(*), COUNT(*), @a
FROM test
GROUP BY col_int;

-------------------------------------------- EXPLAIN -----------------------------------------------------
EXPLAIN
SELECT @agg := @bgg := @agg + col_int, COUNT(*)
FROM test
GROUP BY 1
ORDER BY 1;

EXPLAIN
SELECT @agg := (@agg + col_int), COUNT(*)
FROM test
GROUP BY 1
ORDER BY 1 DESC;

EXPLAIN
SELECT @agg := col_int, COUNT(*)
FROM test
GROUP BY 1
ORDER BY 1;

EXPLAIN
SELECT @agg := @bgg, COUNT(*)
FROM test
GROUP BY 1
ORDER BY 1;

EXPLAIN
SELECT @agg + col_int, COUNT(*)
FROM test
GROUP BY 1
ORDER BY 1 DESC;

-------------------------------------------- TRANSACTIONS -----------------------------------------------
-- More cases in PL/pgSQL part
SET @var := 1;

BEGIN;

SELECT @var;

SET @var := 'Changed';

ROLLBACK;

SELECT @var;

-------------------------------------------- PREPARED STMT ----------------------------------------------
SELECT @a := @a := 5;

PREPARE s1 AS SELECT $1 + @a;
EXECUTE s1(@a);
SET @a := 15;
EXECUTE s1(@a);
DEALLOCATE s1;

PREPARE s2 AS SELECT $1 + @a := 5 + $2;
EXECUTE s2(@a, 2);
SELECT @a;
DEALLOCATE s2;

-------------------------------------------- PL/pgSQL ---------------------------------------------------
-- DO BLOCKS -------------------------------
SET @pl := 5;

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

-- PROCEDURES ------------------------------
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

-- FUNCTIONS -------------------------------
-- Make sure SESVAR is given to the argument by value to not access freed pointer later on
SET @plpgsql_sv_v := 'abc';

CREATE FUNCTION ffunc()
    RETURNS TEXT AS
$$
BEGIN
    RETURN gfunc(@plpgsql_sv_v);
END
$$ LANGUAGE plpgsql;

CREATE FUNCTION gfunc(t TEXT)
    RETURNS TEXT AS
$$
BEGIN
    SET @plpgsql_sv_v := 'BOOM!';
    RETURN t;
END;
$$ LANGUAGE plpgsql;

SELECT ffunc();

DROP FUNCTION gfunc;
DROP FUNCTION ffunc;

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

-- Cache plan invalidation -----------------
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


-- SELECT ... INTO SESVAR ------------------
DO
$$
    DECLARE
        a INT;
    BEGIN
        SELECT '5s', 5.5, 3, 'TEST' COLLATE "POSIX", 'test' INTO @a, @b, a, @t1, @t2;

        RAISE NOTICE '% % % % %', @a, @b, a, @t1, @t2;
    END;
$$;

-- test correct type and collation
SELECT @a, @b, @b * 2, @t1, @t2, @t1 < @t2;

DO
$$
    DECLARE
        a INT;
    BEGIN
        SELECT col_int, col_int, col_char
        INTO a, @a, @b
        FROM test;
    END;
$$;

SELECT @a, @b;

DO
$$
    DECLARE
        a INT;
    BEGIN
        SELECT '5s', 5.5, 3, 'TEST' COLLATE "POSIX", 'test' INTO @a, @b, a, @t1, @t2;

        RAISE NOTICE '% % % % %', @a, @b, a, @t1, @t2;

        SELECT 5.5, 'Ahoj' INTO @a, @b; -- different type assignment
    END;
$$;

SELECT @a * 2, @b, @t1, @t2, @t1 < @t2;

SET @existing := 'Test';

DO -- self assigning
$$
    BEGIN
        SELECT @existing, @existing || ' 2' INTO @existing, @existing;
    END;
$$;

SELECT @existing;

DO -- should fail
$$
    DECLARE
        a INT;
    BEGIN
        SELECT col_int, col_int, col_char
        INTO STRICT a, @x, @y
        FROM test;
    END;
$$;

SELECT @x, @y; -- should fail

-- EXECUTE ---------------------------------
DO
$$
    BEGIN
        EXECUTE FORMAT('SELECT $1 || @a') USING @existing INTO @existing;

        RAISE NOTICE '%', @existing;
    END;
$$;

SET @existing := 'Test';

DO
$$
    DECLARE
        res TEXT;
    BEGIN
        EXECUTE 'SELECT @existing := 5' INTO res;

        RAISE NOTICE '%', res;
    END;
$$;

SELECT @existing;

-- Clean -----------------------------------
DROP TABLE test;