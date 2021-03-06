// Based on Fp finite field
#include "fp.inc"
#include "fe.hpp"

/*
 * 254-bit GF(p^2) optimal extension field (OEF) arithmetic
 *
 * This is simply bigint complex math (a + ib), with a,b in Fp.
 *
 * Before storing the results of these math functions, it is important to
 * call fe_complete_reduce() to produce fully-reduced output.
 */

// Load ufe from endian-neutral data bytes (32)
static void fe_load(const u8 *x, ufe &r) {
	fp_load(x, r.a);
	fp_load(x + 16, r.b);
}

// Save ufe to endian-neutral data bytes (32)
static void fe_save(const ufe &x, u8 *r) {
	fp_save(x.a, r);
	fp_save(x.b, r + 16);
}

// Check if r is zero
static CAT_INLINE bool fe_iszero(const ufe &r) {
	return fp_iszero(r.a) && fp_iszero(r.b);
}

// Check if a == b
static CAT_INLINE bool fe_isequal(const ufe &a, const ufe &b) {
	return fp_isequal(a.a, b.a) && fp_isequal(a.b, b.b);
}

// Validate that r is within field
static CAT_INLINE bool fe_infield(const ufe &r) {
	return fp_infield(r.a) && fp_infield(r.b);
}

// Reduce r
static CAT_INLINE void fe_complete_reduce(ufe &r) {
	// If a or b = 2^127-1, set that one to 0.
	// NOTE: The math functions already ensure that a,b < 2^127

	fp_complete_reduce(r.a);
	fp_complete_reduce(r.b);
}

// r = (k + 0i)
static CAT_INLINE void fe_set_smallk(const u32 k, ufe &r) {
	fp_set_smallk(k, r.a);
	fp_zero(r.b);
}

// r = 0
static CAT_INLINE void fe_zero(ufe &r) {
	fp_zero(r.a);
	fp_zero(r.b);
}

// r = a
static CAT_INLINE void fe_set(const ufe &a, ufe &r) {
	fp_set(a.a, r.a);
	fp_set(a.b, r.b);
}

// r = (mask == -1) ? a : r
static CAT_INLINE void fe_set_mask(const ufe &a, const u128 mask, ufe &r) {
	fp_set_mask(a.a, mask, r.a);
	fp_set_mask(a.b, mask, r.b);
}

// r ^= a & mask
static CAT_INLINE void fe_xor_mask(const ufe &a, const u128 mask, ufe &r) {
#if defined(CAT_ASM_ATT) && defined(CAT_WORD_64) && defined(CAT_ISA_X86)

	const u32 m = static_cast<const u32>( mask );

	CAT_ASM_BEGIN
		"testl %4, %4\n\t"
		"cmovnzq %5, %0\n\t"
		"cmovnzq %6, %1\n\t"
		"cmovnzq %7, %2\n\t"
		"cmovnzq %8, %3"
		: "+r" (r.a.i[0]), "+r" (r.a.i[1]), "+r" (r.b.i[0]), "+r" (r.b.i[1])
		: "r" (m),
		  "m" (a.a.i[0]), "m" (a.a.i[1]), "m" (a.b.i[0]), "m" (a.b.i[1])
		: "cc"
	CAT_ASM_END

#else

	fp_xor_mask(a.a, mask, r.a);
	fp_xor_mask(a.b, mask, r.b);

#endif
}

// r = a'
static CAT_INLINE void fe_conj(const ufe &a, ufe &r) {
	// Uses 1A

	fp_set(a.a, r.a);
	fp_neg(a.b, r.b);
}

// r = -a
static CAT_INLINE void fe_neg(const ufe &a, ufe &r) {
	// Uses 2A

	fp_neg(a.a, r.a);
	fp_neg(a.b, r.b);
}

// r = (mask==-1 ? r : -r)
static CAT_INLINE void fe_neg_mask(const u128 mask, ufe &r) {
	fp_neg_mask(mask, r.a);
	fp_neg_mask(mask, r.b);
}

// r = r + (k + 0i)
static CAT_INLINE void fe_add_smallk(const ufe &a, const u32 k, ufe &r) {
	// Uses 1A

	fp_add_smallk(a.a, k, r.a);
	fp_set(a.b, r.b);
}

// r = a + b
static CAT_INLINE void fe_add(const ufe &a, const ufe &b, ufe &r) {
	// Uses 2A

	// Seems about comparable to 2^^256-c in performance
	fp_add(a.a, b.a, r.a);
	fp_add(a.b, b.b, r.b);
}

// r = a - b
static CAT_INLINE void fe_sub(const ufe &a, const ufe &b, ufe &r) {
	// Uses 2A

	// Seems about comparable to 2^^256-c in performance
	fp_sub(a.a, b.a, r.a);
	fp_sub(a.b, b.b, r.b);
}

// r = a * u, u = 2 + i
static CAT_INLINE void fe_mul_u(const ufe &a, ufe &r) {
	// (a0 + ia1) * (2 + i)
	// = (a0*2 - a1) + i(a1*2 + a0)

	// t0 <- a - b
	ufp t0;
	fp_sub(a.a, a.b, t0);

	// t1 <- b + a
	ufp t1;
	fp_add(a.b, a.a, t1);

	// a' <- a + a - b = t0 + a
	fp_add(t0, a.a, r.a);

	// b' <- b + b + a = t1 + b
	fp_add(t1, a.b, r.b);
}

// r = a * b
static CAT_INLINE void fe_mul(const ufe &a, const ufe &b, ufe &r) {
	// Uses 3M 5A

	// (a0 + ia1) * (b0 + ib1)
	// = (a0b0 - a1b1) + i(a1b0 + a0b1)
	// = (a0b0 - a1b1) + i( (a1 + a0) * (b1 + b0) - a1b1 - a0b0 )

	ufp t0, t1;

	// Ordered to take advantage of pipeline ILP:

	fp_add(a.a, a.b, t0);
	fp_add(b.a, b.b, t1);
	fp_mul(a.a, b.a, r.a);
	fp_mul(t0, t1, t1);
	fp_mul(a.b, b.b, r.b);
	fp_sub(t1, r.a, t0);
	fp_sub(r.a, r.b, r.a);
	fp_sub(t0, r.b, r.b);
}

// r = a * b(small constant)
static CAT_INLINE void fe_mul_smallk(const ufe &a, const u32 b, ufe &r) {
	// Uses 2m

	fp_mul_smallk(a.a, b, r.a);
	fp_mul_smallk(a.b, b, r.b);
}

// r = a * a
static CAT_INLINE void fe_sqr(const ufe &a, ufe &r) {
	// Uses 2M 3A

	// (a + ib) * (a + ib)
	// = (aa - bb) + i(ab + ab)
	// = (a + b) * (a - b) + i(ab + ab)

	ufp t0, t1, t2;

	fp_add(a.a, a.b, t0);
	fp_sub(a.a, a.b, t1);
	fp_add(a.a, a.a, t2);
	fp_mul(t0, t1, r.a);
	fp_mul(t2, a.b, r.b);
}

// r = 1 / a
static void fe_inv(const ufe &a, ufe &r) {
	// Uses 2S 2M 2A 1I

	// 1/a = z'/(a*a + b*b)
	// NOTE: The inversion only needs to be done over a 2^^127 field instead of 2^^256

	ufp t0, t1, t2;

	fp_sqr(a.a, t0);
	fp_sqr(a.b, t1);
	fp_add(t0, t1, t2);

	fp_inv(t2, t0);

	fp_neg(a.b, t1);

	fp_mul(a.a, t0, r.a);
	fp_mul(t1, t0, r.b);
}

