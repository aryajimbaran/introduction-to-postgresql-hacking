/*-------------------------------------------------------------------------
 *
 * varint.c
 *		  Variable-width integers.
 *
 * When storing integers that may be large but are more commonly small,
 * it may be useful to store them using a variable-width encoding.  The
 * numeric data type can be used for this purpose, but it is slow and uses
 * too much space.
 *
 * Our representation is simple: we store integers in little-endian
 * notation using the smallest number of bytes possible and two's
 * complement arithmetic.
 *
 * We support integers of up to 256 bits (32 bytes) in length.  For larger
 * integers, use numeric.
 *
 * Copyright (c) 2010-2015, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *		  contrib/varint/varint.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/hash.h"
#include "fmgr.h"
#include "libpq/pqformat.h"

/*
 * Working storage for arithmatic on variable-width integers, least
 * significant word first.
 */
#define VB_MAX_WORDS		8
#define VB_LAST_WORD		(VB_MAX_WORDS - 1)
typedef struct vb_register
{
	uint32	word[VB_MAX_WORDS];
} vb_register;

#define VB_IS_NEGATIVE(r) \
	((r)->word[VB_LAST_WORD] > PG_INT32_MAX)

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(varint_in);
PG_FUNCTION_INFO_V1(varint_out);
PG_FUNCTION_INFO_V1(varint_recv);
PG_FUNCTION_INFO_V1(varint_send);
PG_FUNCTION_INFO_V1(int2_varint);
PG_FUNCTION_INFO_V1(int4_varint);
PG_FUNCTION_INFO_V1(int8_varint);
PG_FUNCTION_INFO_V1(varint_int2);
PG_FUNCTION_INFO_V1(varint_int4);
PG_FUNCTION_INFO_V1(varint_int8);
PG_FUNCTION_INFO_V1(varint_eq);
PG_FUNCTION_INFO_V1(varint_ne);
PG_FUNCTION_INFO_V1(varint_lt);
PG_FUNCTION_INFO_V1(varint_le);
PG_FUNCTION_INFO_V1(varint_gt);
PG_FUNCTION_INFO_V1(varint_ge);
PG_FUNCTION_INFO_V1(varint_cmp);
PG_FUNCTION_INFO_V1(varint_smaller);
PG_FUNCTION_INFO_V1(varint_larger);
PG_FUNCTION_INFO_V1(varint_hash);
PG_FUNCTION_INFO_V1(varint_uminus);
PG_FUNCTION_INFO_V1(varint_add);
PG_FUNCTION_INFO_V1(varint_sub);

static int compare_varint(Datum a, Datum b);
static int64 flatten_varint(Datum d);
static Datum make_varint(int64 arg);
static void vb_overflow(void);
static void vb_register_add_uint32(vb_register *r, uint32 n);
static void vb_register_in(vb_register *r, Datum d);
static uint32 vb_register_divmod_uint32(vb_register *r, uint32 n);
static void vb_register_mul_uint32(vb_register *r, uint32 n);
static uint32 vb_register_negate(vb_register *r);
static Datum vb_register_out(vb_register *r);
static void vb_register_sub_uint32(vb_register *r, uint32 n);

/*
 * Type input function.
 */
Datum
varint_in(PG_FUNCTION_ARGS)
{
	char   *s = PG_GETARG_CSTRING(0);
	char   *p = s;
	char   *t;
	int		plen = 0;
	bool	isneg = false;
	int32	tenspower = 1;
	int32	accumulator = 0;
	vb_register r;

	/* Remember, and then skip, any leading sign indicator. */
	if (*p == '+' || *p == '-')
	{
		isneg = (*p == '-');
		++p;
	}

	/* Count digits; bail out if we see a non-digit. */
	for (t = p; *t != '\0'; ++t)
	{
		if (*t < '0' || *t > '9')
			break;
		++plen;
	}

	/* Error out if we hit a non-digit or found no digits. */
	if (*t != '\0' || plen == 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
				 errmsg("invalid input syntax for integer: \"%s\"", s)));

	/*
	 * Scan the string.  To avoid calling multiply_words_by_int32 and add_words
	 * for every iteration, we use a 32-bit accumulator which is merged into
	 * words[] after every 9 digits, or at the end of the input.
	 */
	memset(&r, 0, sizeof(vb_register));
	for (t = p; *t != '\0'; ++t)
	{
		accumulator = (accumulator * 10) + (*t - '0');
		tenspower = tenspower * 10;

		if (tenspower == 1000000000 || t[1] == '\0')
		{
			vb_register_mul_uint32(&r, tenspower);
			if (isneg)
				vb_register_sub_uint32(&r, accumulator);
			else
				vb_register_add_uint32(&r, accumulator);
			accumulator = 0;
			tenspower = 1;
		}
	}

	/* Generate result. */
	PG_RETURN_DATUM(vb_register_out(&r));
}

/*
 * Type output function.
 */
Datum
varint_out(PG_FUNCTION_ARGS)
{
	Datum	d = PG_GETARG_DATUM(0);
	vb_register r;
	bool	isneg;
	char	buf[11 * VB_MAX_WORDS + 1];
	char   *e = buf + sizeof buf;

	/* Slurp the value, record the sign. */
	vb_register_in(&r, d);
	isneg = VB_IS_NEGATIVE(&r);

	/* NUL-terminate the buffer */
	--e;
	*e = '\0';

	/* Loop, extracting digits. */
	for (;;)
	{
		uint32	remainder;
		int		i;

		/* Extract a group of nine digits. */
		remainder = vb_register_divmod_uint32(&r, 1000000000);

		/* If we got zero, there may be no more digits. */
		if (remainder == 0)
		{
			bool	done = true;

			for (i = 0; i < VB_MAX_WORDS; ++i)
				if (r.word[i] != 0)
					done = false;
			if (done)
				break;
		}

		/* Emit the digits we got. */
		for (i = 0; i < 9; ++i)
		{
			--e;
			Assert(e >= buf);
			*e = '0' + remainder % 10;
			remainder = remainder / 10;
		}
	}

	/* If we got no digits, insert a single '0'. */
	if (e == buf + sizeof buf - 1)
	{
		--e;
		*e = '0';
	}
	else
	{
		/* We may have some leading zeroes. */
		while (*e == '0')
			++e;
		Assert(*e >= '1' && *e <= '9');
	}

	/* Add a sign indicator if needed. */
	if (isneg)
	{
		--e;
		Assert(e >= buf);
		*e = '-';
	}

	PG_RETURN_CSTRING(pstrdup(e));
}

/*
 * Type send function.
 */
Datum
varint_recv(PG_FUNCTION_ARGS)
{
	StringInfo      buf = (StringInfo) PG_GETARG_POINTER(0);
	char		   *result;
	char		   *data;
	unsigned		len;

	len = pq_getmsgint(buf, 1);
	if (len > sizeof(uint32) * VB_MAX_WORDS)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
				 errmsg("external \"varint\" value too long")));

	result = palloc(1 + len);
	SET_VARSIZE_SHORT(result, len);
	data = VARDATA_ANY(result);
	pq_copymsgbytes(buf, data, len);

	if (data[len - 1] == '\0')
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
				 errmsg("external \"varint\" value ends with invalid NUL byte")));

	PG_RETURN_POINTER(result);
}

/*
 * Type send function.
 */
Datum
varint_send(PG_FUNCTION_ARGS)
{
	struct varlena *v = PG_DETOAST_DATUM_PACKED(PG_GETARG_DATUM(0));
	StringInfoData	buf;

	pq_begintypsend(&buf);
	pq_sendint(&buf, VARSIZE_ANY(v), 1);
	pq_sendbytes(&buf, VARDATA_ANY(v), VARSIZE_ANY(v));
	PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

/*
 * Convert int2 to varint.
 */
Datum
int2_varint(PG_FUNCTION_ARGS)
{
	int16		val = PG_GETARG_INT16(0);

	PG_RETURN_DATUM(make_varint((int64) val));
}

/*
 * Convert int4 to varint.
 */
Datum
int4_varint(PG_FUNCTION_ARGS)
{
	int32		val = PG_GETARG_INT32(0);

	PG_RETURN_DATUM(make_varint((int64) val));
}

/*
 * Convert int8 to varint.
 */
Datum
int8_varint(PG_FUNCTION_ARGS)
{
	int64		val = PG_GETARG_INT32(0);

	PG_RETURN_DATUM(make_varint(val));
}

/*
 * Convert varint to int2.
 */
Datum
varint_int2(PG_FUNCTION_ARGS)
{
	Datum		d = PG_GETARG_DATUM(0);
	int64		v = flatten_varint(d);

	if (v > PG_INT16_MAX || v < PG_INT16_MIN)
		vb_overflow();

	PG_RETURN_INT16(v);
}

/*
 * Convert varint to int4.
 */
Datum
varint_int4(PG_FUNCTION_ARGS)
{
	Datum		d = PG_GETARG_DATUM(0);
	int64		v = flatten_varint(d);

	if (v > PG_INT32_MAX || v < PG_INT32_MIN)
		vb_overflow();

	PG_RETURN_INT32(v);
}

/*
 * Convert varint to int4.
 */
Datum
varint_int8(PG_FUNCTION_ARGS)
{
	Datum		d = PG_GETARG_DATUM(0);
	int64		v = flatten_varint(d);

	PG_RETURN_INT64(v);
}

/*
 * Test for equality.
 */
Datum
varint_eq(PG_FUNCTION_ARGS)
{
	Datum		a = PG_GETARG_DATUM(0);
	Datum		b = PG_GETARG_DATUM(1);

	PG_RETURN_BOOL(compare_varint(a, b) == 0);
}

/*
 * Test for inequality.
 */
Datum
varint_ne(PG_FUNCTION_ARGS)
{
	Datum		a = PG_GETARG_DATUM(0);
	Datum		b = PG_GETARG_DATUM(1);

	PG_RETURN_BOOL(compare_varint(a, b) != 0);
}

/*
 * Less than.
 */
Datum
varint_lt(PG_FUNCTION_ARGS)
{
	Datum		a = PG_GETARG_DATUM(0);
	Datum		b = PG_GETARG_DATUM(1);

	PG_RETURN_BOOL(compare_varint(a, b) < 0);
}

/*
 * Less than or equal to.
 */
Datum
varint_le(PG_FUNCTION_ARGS)
{
	Datum		a = PG_GETARG_DATUM(0);
	Datum		b = PG_GETARG_DATUM(1);

	PG_RETURN_BOOL(compare_varint(a, b) <= 0);
}

/*
 * Greater than.
 */
Datum
varint_gt(PG_FUNCTION_ARGS)
{
	Datum		a = PG_GETARG_DATUM(0);
	Datum		b = PG_GETARG_DATUM(1);

	PG_RETURN_BOOL(compare_varint(a, b) > 0);
}

/*
 * Less than or equal to.
 */
Datum
varint_ge(PG_FUNCTION_ARGS)
{
	Datum		a = PG_GETARG_DATUM(0);
	Datum		b = PG_GETARG_DATUM(1);

	PG_RETURN_BOOL(compare_varint(a, b) >= 0);
}

/*
 * Compare.
 */
Datum
varint_cmp(PG_FUNCTION_ARGS)
{
	Datum		a = PG_GETARG_DATUM(0);
	Datum		b = PG_GETARG_DATUM(1);

	PG_RETURN_INT32(compare_varint(a, b));
}

/*
 * Return smaller of the two inputs.
 */
Datum
varint_larger(PG_FUNCTION_ARGS)
{
	Datum		a = PG_GETARG_DATUM(0);
	Datum		b = PG_GETARG_DATUM(1);

	PG_RETURN_DATUM(compare_varint(a, b) > 0 ? a : b);
}

/*
 * Return smaller of the two inputs.
 */
Datum
varint_smaller(PG_FUNCTION_ARGS)
{
	Datum		a = PG_GETARG_DATUM(0);
	Datum		b = PG_GETARG_DATUM(1);

	PG_RETURN_DATUM(compare_varint(a, b) < 0 ? a : b);
}

/*
 * Hash.
 */
Datum
varint_hash(PG_FUNCTION_ARGS)
{
	void   *v = PG_DETOAST_DATUM_PACKED(PG_GETARG_DATUM(0));
	int		len = VARSIZE_ANY_EXHDR(v);
	uint8  *data = (uint8 *) VARDATA_ANY(v);

	return hash_any(data, len);
}

/*
 * Unary minus.
 */
Datum
varint_uminus(PG_FUNCTION_ARGS)
{
	Datum		d = PG_GETARG_DATUM(0);
	vb_register r;

	vb_register_in(&r, d);
	if (vb_register_negate(&r))
		vb_overflow();
	PG_RETURN_DATUM(vb_register_out(&r));
}

/*
 * Addition.
 */
Datum
varint_add(PG_FUNCTION_ARGS)
{
	Datum		a = PG_GETARG_DATUM(0);
	Datum		b = PG_GETARG_DATUM(1);
	vb_register	ra;
	vb_register	rb;
	int			i;
	uint64		c = 0;
	bool		aneg;
	bool		bneg;

	vb_register_in(&ra, a);
	vb_register_in(&rb, b);
	aneg = VB_IS_NEGATIVE(&ra);
	bneg = VB_IS_NEGATIVE(&rb);

	for (i = 0; i < VB_MAX_WORDS; ++i)
	{
		c += ra.word[i];
		c += rb.word[i];
		ra.word[i] = c & 0xffffffff;
		c >>= 32;
	}

	/*
	 * If the inputs have different signs, the sum can't overflow; otherwise,
	 * sum should have the same sign as the inputs.
	 */
	if (aneg && bneg && !VB_IS_NEGATIVE(&ra))
		vb_overflow();
	if (!aneg && !bneg && VB_IS_NEGATIVE(&ra))
		vb_overflow();

	PG_RETURN_DATUM(vb_register_out(&ra));
}

/*
 * Subtraction.
 */
Datum
varint_sub(PG_FUNCTION_ARGS)
{
	Datum		a = PG_GETARG_DATUM(0);
	Datum		b = PG_GETARG_DATUM(1);
	vb_register	ra;
	vb_register	rb;
	int			i;
	int64		c = 0;
	bool		aneg;
	bool		bneg;

	vb_register_in(&ra, a);
	vb_register_in(&rb, b);
	aneg = VB_IS_NEGATIVE(&ra);
	bneg = VB_IS_NEGATIVE(&rb);

	for (i = 0; i < VB_MAX_WORDS; ++i)
	{
		c += ra.word[i];
		c -= rb.word[i];
		ra.word[i] = c & 0xffffffff;
		c >>= 32;
	}

	/*
	 * If the inputs have the same sign, the difference can't overflow;
	 * otherwise, it should have the same sign as the first input.
	 */
	if (aneg && !bneg && !VB_IS_NEGATIVE(&ra))
		vb_overflow();
	if (!aneg && bneg && VB_IS_NEGATIVE(&ra))
		vb_overflow();

	PG_RETURN_DATUM(vb_register_out(&ra));
}

/*
 * Compare two varints.
 */
static int
compare_varint(Datum a, Datum b)
{
	void   *va = PG_DETOAST_DATUM_PACKED(a);
	void   *vb = PG_DETOAST_DATUM_PACKED(b);
	int		alen = VARSIZE_ANY_EXHDR(va);
	int		blen = VARSIZE_ANY_EXHDR(vb);
	uint8  *adata = (uint8 *) VARDATA_ANY(va);
	uint8  *bdata = (uint8 *) VARDATA_ANY(vb);
	uint8	aa;
	uint8	bb;

	/*
	 * Since we don't allow unnecessary trailing bytes, values of unequal
	 * length can't be equal.  If the last byte of the longer string is
	 * negative, it is more negative than the other can possibly be; if the
	 * last byte of the longer string is positive, it is more positive than
	 * the other can possibly be.
	 */
	if (alen != blen)
	{
		if (alen > blen)
			return adata[alen - 1] >= 0x80 ? -1 : 1;
		else
			return bdata[blen - 1] >= 0x80 ? 1 : -1;
	}

	/* Compare high byte in a sign-aware fashion. */
	--alen;
	aa = (adata[alen] + 0x80) & 0xff;
	bb = (bdata[alen] + 0x80) & 0xff;
	if (aa != bb)
		return aa > bb ? 1 : -1;

	/* Compare remaining bytes normally. */
	while (alen >= 0)
	{
		--alen;
		if (adata[alen] != bdata[alen])
			return adata[alen] > bdata[alen] ? 1 : - 1;
	}

	/* Byte for byte equivalent, so equal. */
	return 0;
}

/*
 * Convert a varint to a 64-bit signed integer.
 */
static int64
flatten_varint(Datum d)
{
	vb_register	r;
	int64		v;

	vb_register_in(&r, d);
	v = (((uint64) r.word[1]) << 32) | (uint64) r.word[0];

	if (VB_IS_NEGATIVE(&r))
	{
		int		i;

		if (v >= 0)
			vb_overflow();
		for (i = 2; i < VB_MAX_WORDS; ++i)
			if (r.word[i] != 0xffffffff)
				vb_overflow();
	}
	else
	{
		int		i;

		if (v < 0)
			vb_overflow();
		for (i = 2; i < VB_MAX_WORDS; ++i)
			if (r.word[i] != 0)
				vb_overflow();
	}

	return v;
}

/*
 * Convert a 64-bit signed integer to a varint.
 */
static Datum
make_varint(int64 arg)
{
	vb_register	r;
	int		i;

	r.word[0] = arg & 0xffffffff;
	r.word[1] = (arg >> 32) & 0xffffffff;
	for (i = 2; i < VB_MAX_WORDS; ++i)
		r.word[i] = arg < 0 ? 0xffffffff : 0;
	return vb_register_out(&r);
}

/*
 * Report overflow error.
 */
static void
vb_overflow(void)
{
	ereport(ERROR,
			(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
			 errmsg("varint out of range")));
}

/*
 * Add an unsigned 32-bit integer to a vb_register.
 */
static void
vb_register_add_uint32(vb_register *r, uint32 n)
{
	int		i;
	uint64	a = n;
	bool	isneg = VB_IS_NEGATIVE(r);

	for (i = 0; a != 0 && i < VB_MAX_WORDS; ++i)
	{
		a += r->word[i];
		r->word[i] = a & 0xffffffff;
		a >>= 32;
	}

	if (!isneg && VB_IS_NEGATIVE(r))
		vb_overflow();
}

/*
 * Divide a register by an unsigned 32-bit integer, returning the remainder
 * as a positive number.
 */
static uint32
vb_register_divmod_uint32(vb_register *r, uint32 n)
{
	int		i;
	bool	isneg = VB_IS_NEGATIVE(r);
	uint64	a = 0;

	if (n == 0)
		ereport(ERROR,
				(errcode(ERRCODE_DIVISION_BY_ZERO),
				 errmsg("division by zero")));
	if (n == 1)
		return 0;

	if (isneg)
		vb_register_negate(r);

	/* This is just long division. */
	for (i = VB_LAST_WORD; i >= 0; --i)
	{
		Assert(a < n);
		a = (a << 32) | r->word[i];		/* Bring down next digit. */
		r->word[i] = a / n;				/* How many times does it go in? */
		a %= n;							/* And with what remainder? */
	}

	if (isneg)
		vb_register_negate(r);

	/* Return remainder. */
	return a;
}

/*
 * Multiply a register by an unsigned 32-bit integer.
 */
static void
vb_register_mul_uint32(vb_register *r, uint32 n)
{
	int		i;

	/* Special case: if n is one, we don't need to do anything. */
	if (n == 1)
		return;

	/* Another special case: if n is zero, zap everything. */
	if (n == 0)
	{
		memset(r, 0, sizeof(vb_register));
		return;
	}

	if (VB_IS_NEGATIVE(r))
	{
		/* We negate, multiply, and negate again. */
		uint64	a = 0;						/* multiplication carry */
		uint64	c1 = 1;						/* negate carry */
		uint64	c2 = 1;						/* renegate carry */

		for (i = 0; i < VB_MAX_WORDS; ++i)
		{
			uint64	t;

			t = ~r->word[i] + c1;			/* first negation */
			c1 = (c1 && (t >> 32)) ? 1 : 0;	/* save carry for next time */
			t = t & 0xffffffff;				/* retain low bits */

			a += t * n;						/* multiply */
			t = a & 0xffffffff;				/* retain low bits */
			a >>= 32;						/* save carry for next time */

			t = (~(uint32) t) + c2;			/* second negation */
			r->word[i] = t & 0xffffffff;	/* store low bits */
			c2 = (c2 && (t >> 32)) ? 1 : 0;	/* save carry for next time */
		}

		/*
		 * If a is non-zero, that is an overflow, except in one special case:
		 * if a == 1 and we ended up with all zeroes in r, then the answer is
		 * the largest negative number we can represent.
		 */
		if (a != 0)
		{
			if (a != 1)
				vb_overflow();

			for (i = 0; i < VB_MAX_WORDS; ++i)
				if (r->word[i] != 0)
					vb_overflow();

			r->word[VB_LAST_WORD] = 0x80000000;
		}

		if (!VB_IS_NEGATIVE(r))
			vb_overflow();
	}
	else
	{
		uint64	a = 0;

		for (i = 0; i < VB_MAX_WORDS; ++i)
		{
			a += r->word[i] * (uint64) n;	/* add new product to old carry */
			r->word[i] = a & 0xffffffff;	/* store low bits */
			a >>= 32;						/* compute next carry */
		}

		if (VB_IS_NEGATIVE(r))
			vb_overflow();
	}
}

/*
 * Negate the value stored in a vb_register.
 *
 * If we're asked to negate the largest possible integer value, we overflow;
 * return 1 if that happens, else 0.
 */
static uint32
vb_register_negate(vb_register *r)
{
	int		i;
	uint32	a = 1;
	bool	isneg = VB_IS_NEGATIVE(r);

	for (i = 0; i < VB_MAX_WORDS; ++i)
	{
		uint32	n = ~r->word[i] + a;
		r->word[i] = n;
		a = (a != 0 && r->word[i] == 0) ? 1 : 0;
	}

	return isneg == VB_IS_NEGATIVE(r) ? 1 : 0;
}

/*
 * Load a varbit Datum into a vb_register.
 */
static void
vb_register_in(vb_register *r, Datum d)
{
	void   *rawinput = PG_DETOAST_DATUM_PACKED(d);
	uint8  *input = (uint8 *) VARDATA_ANY(rawinput);
	int		length = VARSIZE_ANY_EXHDR(rawinput);
	int		i;
	uint8	pad;

	Assert(length <= VB_MAX_WORDS * sizeof(uint32));
	pad = (input[length - 1] & 0x80) ? 0xff : 0x00;

	memset(r, 0, sizeof(vb_register));
	for (i = 0; i < VB_MAX_WORDS * sizeof(uint32); ++i)
	{
		int		k = i / sizeof(uint32);
		int		b = i % sizeof(uint32);

		r->word[k] |= ((uint32) (i < length ? input[i] : pad)) << (8 * b);
	}
}

/*
 * Convert a vb_register to a varbit Datum.
 */
static Datum
vb_register_out(vb_register *r)
{
	int		i;
	int		bytes;
	int		hdr_bytes = offsetof(varattrib_1b, va_data);
	char   *result;

	/*
	 * Compute the number of bytes required to store this number.  Zero can
	 * be stored with no payload bytes at all, but any other number requires
	 * at least one byte.
	 */
	if (VB_IS_NEGATIVE(r))
	{
		for (i = VB_LAST_WORD; i > 0; --i)
			if (r->word[i] != 0xffffffff)
				break;
		bytes = i * sizeof(uint32);

		/* Be careful to allow enough bytes so that sign bit will be set. */
		if (r->word[i] >= 0xffffff80)
			bytes += 1;
		else if (r->word[i] >= 0xffff8000)
			bytes += 2;
		else if (r->word[i] >= 0xff800000)
			bytes += 3;
		else if (r->word[i] >= 0x80000000)
			bytes += 4;
		else
			bytes += 5;
	}
	else
	{
		for (i = VB_LAST_WORD; i >= 0; --i)
			if (r->word[i] != 0)
				break;
		if (i == -1)
			bytes = 0;
		else
		{
			bytes = i * sizeof(uint32);
			if (r->word[i] > 0x007fffff)
				bytes += 4;
			else if (r->word[i] > 0x00007fff)
				bytes += 3;
			else if (r->word[i] > 0x0000007f)
				bytes += 2;
			else if (r->word[i] > 0)
				bytes += 1;
		}
	}

	/* Allocate space for result and set size correctly. */
	result = palloc(hdr_bytes + bytes);
	SET_VARSIZE_SHORT(result, hdr_bytes + bytes);

	/* Copy the data bytes. */
	for (i = 0; i < bytes; ++i)
	{
		int		k = i / sizeof(uint32);
		int		b = i % sizeof(uint32);

		result[hdr_bytes + i] = (r->word[k] >> (8 * b)) & 0xff;
	}

	return PointerGetDatum(result);
}

/*
 * Add an unsigned 32-bit integer to a vb_register.
 */
static void
vb_register_sub_uint32(vb_register *r, uint32 n)
{
	int		i;
	bool	isneg = VB_IS_NEGATIVE(r);

	for (i = 0; i < VB_MAX_WORDS; ++i)
	{
		uint32	a = r->word[i];
		uint32	b = a - n;

		r->word[i] = b;
		if (a >= b)
			break;
		n = 1;
	}

	if (isneg && !VB_IS_NEGATIVE(r))
		vb_overflow();
}
