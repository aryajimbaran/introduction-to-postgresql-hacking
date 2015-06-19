/* contrib/varint/varint--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION varint" to load this file. \quit

--
-- Shell type
--

CREATE TYPE varint;

--
--  Input and output functions.
--
CREATE FUNCTION varint_in(cstring)
RETURNS varint
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION varint_out(varint)
RETURNS cstring
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION varint_recv(internal)
RETURNS varint
AS 'MODULE_PATHNAME'
LANGUAGE C STABLE STRICT;

CREATE FUNCTION varint_send(varint)
RETURNS bytea
AS 'MODULE_PATHNAME'
LANGUAGE C STABLE STRICT;

--
--  The type itself.
--

CREATE TYPE varint (
    INPUT          = varint_in,
    OUTPUT         = varint_out,
    RECEIVE        = varint_recv,
    SEND           = varint_send,
    INTERNALLENGTH = VARIABLE,
    STORAGE        = extended,
    CATEGORY       = 'N',
    PREFERRED      = false,
    COLLATABLE     = false
);

--
-- Type casting functions.
--

CREATE FUNCTION varint(int2)
RETURNS varint
AS 'MODULE_PATHNAME', 'int2_varint'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION varint(int4)
RETURNS varint
AS 'MODULE_PATHNAME', 'int4_varint'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION varint(int8)
RETURNS varint
AS 'MODULE_PATHNAME', 'int8_varint'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION varint_int2(varint)
RETURNS int2
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION varint_int4(varint)
RETURNS int4
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION varint_int8(varint)
RETURNS int8
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

--
--  Implicit and assignment type casts.
--

CREATE CAST (varint AS numeric)
	WITH INOUT AS ASSIGNMENT;
CREATE CAST (varint AS int8)
	WITH FUNCTION varint_int8(varint) AS ASSIGNMENT;
CREATE CAST (varint AS int4)
	WITH FUNCTION varint_int4(varint) AS ASSIGNMENT;
CREATE CAST (varint AS int2)
	WITH FUNCTION varint_int2(varint) AS ASSIGNMENT;

CREATE CAST (numeric AS varint) WITH INOUT AS ASSIGNMENT;
CREATE CAST (int8 AS varint) WITH FUNCTION varint(int8) AS ASSIGNMENT;
CREATE CAST (int4 AS varint) WITH FUNCTION varint(int4) AS ASSIGNMENT;
CREATE CAST (int2 AS varint) WITH FUNCTION varint(int2) AS ASSIGNMENT;

--
-- Operator Functions.
--

CREATE FUNCTION varint_eq(varint, varint)
RETURNS bool
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION varint_ne(varint, varint)
RETURNS bool
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION varint_lt(varint, varint)
RETURNS bool
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION varint_le(varint, varint)
RETURNS bool
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION varint_gt(varint, varint)
RETURNS bool
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION varint_ge(varint, varint)
RETURNS bool
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION varint_uminus(varint)
RETURNS varint
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION varint_add(varint, varint)
RETURNS varint
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION varint_sub(varint, varint)
RETURNS varint
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

--
-- Operators.
--

CREATE OPERATOR = (
    LEFTARG    = varint,
    RIGHTARG   = varint,
    COMMUTATOR = =,
    NEGATOR    = <>,
    PROCEDURE  = varint_eq,
    RESTRICT   = eqsel,
    JOIN       = eqjoinsel,
    HASHES,
    MERGES
);

CREATE OPERATOR <> (
    LEFTARG    = varint,
    RIGHTARG   = varint,
    NEGATOR    = =,
    COMMUTATOR = <>,
    PROCEDURE  = varint_ne,
    RESTRICT   = neqsel,
    JOIN       = neqjoinsel
);

CREATE OPERATOR < (
    LEFTARG    = varint,
    RIGHTARG   = varint,
    NEGATOR    = >=,
    COMMUTATOR = >,
    PROCEDURE  = varint_lt,
    RESTRICT   = scalarltsel,
    JOIN       = scalarltjoinsel
);

CREATE OPERATOR <= (
    LEFTARG    = varint,
    RIGHTARG   = varint,
    NEGATOR    = >,
    COMMUTATOR = >=,
    PROCEDURE  = varint_le,
    RESTRICT   = scalarltsel,
    JOIN       = scalarltjoinsel
);

CREATE OPERATOR >= (
    LEFTARG    = varint,
    RIGHTARG   = varint,
    NEGATOR    = <,
    COMMUTATOR = <=,
    PROCEDURE  = varint_ge,
    RESTRICT   = scalargtsel,
    JOIN       = scalargtjoinsel
);

CREATE OPERATOR > (
    LEFTARG    = varint,
    RIGHTARG   = varint,
    NEGATOR    = <=,
    COMMUTATOR = <,
    PROCEDURE  = varint_gt,
    RESTRICT   = scalargtsel,
    JOIN       = scalargtjoinsel
);

CREATE OPERATOR - (
    RIGHTARG   = varint,
    PROCEDURE  = varint_uminus
);

CREATE OPERATOR + (
	LEFTARG    = varint,
    RIGHTARG   = varint,
    PROCEDURE  = varint_add
);

CREATE OPERATOR - (
	LEFTARG    = varint,
    RIGHTARG   = varint,
    PROCEDURE  = varint_sub
);

--
-- Support functions for indexing.
--

CREATE FUNCTION varint_cmp(varint, varint)
RETURNS int4
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION varint_hash(varint)
RETURNS int4
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

--
-- The btree indexing operator class.
--

CREATE OPERATOR CLASS varint_ops
DEFAULT FOR TYPE varint USING btree AS
    OPERATOR    1   <  (varint, varint),
    OPERATOR    2   <= (varint, varint),
    OPERATOR    3   =  (varint, varint),
    OPERATOR    4   >= (varint, varint),
    OPERATOR    5   >  (varint, varint),
    FUNCTION    1   varint_cmp(varint, varint);

--
-- The hash indexing operator class.
--

CREATE OPERATOR CLASS varint_ops
DEFAULT FOR TYPE varint USING hash AS
    OPERATOR    1   =  (varint, varint),
    FUNCTION    1   varint_hash(varint);

--
-- Aggregates.
--

CREATE FUNCTION varint_smaller(varint, varint)
RETURNS varint
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION varint_larger(varint, varint)
RETURNS varint
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE AGGREGATE min(varint)  (
    SFUNC = varint_smaller,
    STYPE = varint,
    SORTOP = <
);

CREATE AGGREGATE max(varint)  (
    SFUNC = varint_larger,
    STYPE = varint,
    SORTOP = >
);
