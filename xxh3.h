#ifndef XXH3_H
#define XXH3_H


/* ===   Dependencies   === */

#undef XXH_INLINE_ALL   /* in case it's already defined */
#define XXH_INLINE_ALL
#include "xxhash.h"

#define NDEBUG
#include <assert.h>


/* ===   Compiler versions   === */

#if !(defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L)   /* C99+ */
#  define restrict   /* disable */
#endif

#if defined(__GNUC__)
#  if defined(__SSE2__)
#    include <x86intrin.h>
#  elif defined(__ARM_NEON__) || defined(__ARM_NEON)
#    define inline __inline__ /* clang bug */
#    include <arm_neon.h>
#    undef inline
#  endif
#  define ALIGN(n)      __attribute__ ((aligned(n)))
#elif defined(_MSC_VER)
#  include <intrin.h>
#  define ALIGN(n)      __declspec(align(n))
#else
#  define ALIGN(n)   /* disabled */
#endif



/* ==========================================
 * Vectorization detection
 * ========================================== */
#define XXH_SCALAR 0
#define XXH_SSE2   1
#define XXH_AVX2   2
#define XXH_NEON   3

#ifndef XXH_VECTOR    /* can be defined on command line */
#  if defined(__AVX2__)
#    define XXH_VECTOR XXH_AVX2
#  elif defined(__SSE2__)
#    define XXH_VECTOR XXH_SSE2
/* msvc support maybe later */
#  elif defined(__GNUC__) && (defined(__ARM_NEON__) || defined(__ARM_NEON))
#    define XXH_VECTOR XXH_NEON
#  else
#    define XXH_VECTOR XXH_SCALAR
#  endif
#endif




/* ==========================================
 * XXH3 default settings
 * ========================================== */

#define KEYSET_DEFAULT_SIZE 48   /* minimum 32 */


ALIGN(64) static const U32 kKey[KEYSET_DEFAULT_SIZE] = {
    0xb8fe6c39,0x23a44bbe,0x7c01812c,0xf721ad1c,
    0xded46de9,0x839097db,0x7240a4a4,0xb7b3671f,
    0xcb79e64e,0xccc0e578,0x825ad07d,0xccff7221,
    0xb8084674,0xf743248e,0xe03590e6,0x813a264c,
    0x3c2852bb,0x91c300cb,0x88d0658b,0x1b532ea3,
    0x71644897,0xa20df94e,0x3819ef46,0xa9deacd8,
    0xa8fa763f,0xe39c343f,0xf9dcbbc7,0xc70b4f1d,
    0x8a51e04b,0xcdb45931,0xc89f7ec9,0xd9787364,

    0xeac5ac83,0x34d3ebc3,0xc581a0ff,0xfa1363eb,
    0x170ddd51,0xb7f0da49,0xd3165526,0x29d4689e,
    0x2b16be58,0x7d47a1fc,0x8ff8b8d1,0x7ad031ce,
    0x45cb3a8f,0x95160428,0xafd7fbca,0xbb4b407e,
};

XXH_FORCE_INLINE U64
XXH3_mul128(U64 ll1, U64 ll2)
{
  __uint128_t lll = (__uint128_t)ll1 * ll2;
  return (U64)lll + (lll >> 64);
}

static U64 XXH64_avalanche2(U64 h64)
{
    h64 ^= h64 >> 29;
    h64 *= PRIME64_3;
    h64 ^= h64 >> 32;
    return h64;
}

/* ==========================================
 * Short keys
 * ========================================== */
XXH_FORCE_INLINE U64
XXH3_len_1to3_64b(const void* data, size_t len, const void* keyPtr)
{
    assert(data != NULL);
    assert(len > 0 && len <= 3);
    assert(keyPtr != NULL);
    {   const U32* const key32 = (const U32*) keyPtr;
        BYTE const c1 = ((const BYTE*)data)[0];
        BYTE const c2 = ((const BYTE*)data)[len >> 1];
        BYTE const c3 = ((const BYTE*)data)[len - 1];
        U32  const l1 = (U32)(c1) + ((U32)(c2) << 8);
        U32  const l2 = (U32)(len) + ((U32)(c3) << 2);
        U64  const ll3 = (U64)(l1 + key32[0]) * (l2 + key32[1]);
        return XXH64_avalanche2(ll3);
    }
}


XXH_FORCE_INLINE U64
XXH3_len_4to8_64b(const void* data, size_t len, const void* keyPtr)
{
    assert(data != NULL);
    assert(len >= 4 && len <= 8);
    {   const U32* const key32 = (const U32*) keyPtr;
        U64 acc = PRIME64_1 * len;
        U64 const l1 = XXH_read32(data) + key32[0];
        U64 const l2 = XXH_read32((const BYTE*)data + len - 4) + key32[1];
        acc += (U64)l1 * l2;
        return XXH64_avalanche2(acc);
    }
}

XXH_FORCE_INLINE U64
XXH3_len_9to16_64b(const void* data, size_t len, const void* keyPtr)
{
    assert(data != NULL);
    assert(key != NULL);
    assert(len >= 9 && len <= 16);
    {   const U64* const key64 = (const U64*) keyPtr;
        U64 acc = PRIME64_1 * len;
        U64 const ll1 = XXH_read64(data) + key64[0];
        U64 const ll2 = XXH_read64((const BYTE*)data + len - 8) + key64[1];
        acc += XXH3_mul128(ll1, ll2);
        return XXH64_avalanche2(acc);
    }
}

XXH_FORCE_INLINE U64 XXH3_len_0to16_64b(const void* data, size_t len)
{
    assert(data != NULL);
    assert(len <= 16);
    {   if (len > 8) return XXH3_len_9to16_64b(data, len, kKey);
        if (len >= 4) return XXH3_len_4to8_64b(data, len, kKey);
        if (len) return XXH3_len_1to3_64b(data, len, kKey);
        return 0;
    }
}


/* ==========================================
 * Long keys
 * ========================================== */

#define STRIPE_LEN 64
#define STRIPE_ELTS (STRIPE_LEN / sizeof(U32))
#define ACC_NB (STRIPE_LEN / sizeof(U64))

XXH_FORCE_INLINE void
XXH3_accumulate_512(void* acc, const void *restrict data, const void *restrict key)
{
#if (XXH_VECTOR == XXH_AVX2)

    assert(((size_t)acc) & 31 == 0);
    {                   __m256i* const xacc  =       (__m256i *) acc;
                  const __m256i* const xdata = (const __m256i *) data;
        ALIGN(32) const __m256i* const xkey  = (const __m256i *) key;

        for (size_t i=0; i < STRIPE_LEN/sizeof(__m256i); i++) {
            __m256i const d   = _mm256_loadu_si256 (xdata+i);
            __m256i const k   = _mm256_loadu_si256 (xkey+i);
            __m256i const dk  = _mm256_add_epi32 (d,k);                                  /* uint32 dk[8]  = {d0+k0, d1+k1, d2+k2, d3+k3, ...} */
            __m256i const res = _mm256_mul_epu32 (dk, _mm256_shuffle_epi32 (dk,0x31));   /* uint64 res[4] = {dk0*dk1, dk2*dk3, ...} */
            xacc[i]           = _mm256_add_epi64(res, xacc[i]);                          /* xacc must be aligned on 32 bytes boundaries */
        }
    }

#elif (XXH_VECTOR == XXH_SSE2)

    assert(((size_t)acc) & 15 == 0);
    {                   __m128i* const xacc  =       (__m128i *) acc;
                  const __m128i* const xdata = (const __m128i *) data;
        ALIGN(16) const __m128i* const xkey  = (const __m128i *) key;

        size_t i;
        for (i=0; i < STRIPE_LEN/sizeof(__m128i); i++) {
            __m128i const d   = _mm_loadu_si128 (xdata+i);
            __m128i const k   = _mm_loadu_si128 (xkey+i);
            __m128i const dk  = _mm_add_epi32 (d,k);                               /* uint32 dk[4]  = {d0+k0, d1+k1, d2+k2, d3+k3} */
            __m128i const res = _mm_mul_epu32 (dk, _mm_shuffle_epi32 (dk,0x31));   /* uint64 res[2] = {dk0*dk1,dk2*dk3} */
            xacc[i]           = _mm_add_epi64(res, xacc[i]);                       /* xacc must be aligned on 16 bytes boundaries */
        }
    }

#elif (XXH_VECTOR == XXH_NEON)

    assert(((size_t)acc) & 15 == 0);
    {                 uint64x2_t* const xacc  = (uint64x2_t *)acc;
                  const uint32_t* const xdata = (const uint32_t *)data;
        ALIGN(16) const uint32_t* const xkey  = (const uint32_t *)key;

        size_t i;
        for (i=0; i < STRIPE_LEN / sizeof(uint64x2_t); i++) {
#if !defined(__aarch64__) && !defined(__arm64__) && !defined(XXH_NO_ARM32_HACK)
            /* On 32-bit ARM, we can take advantage of the packed registers.
             * This is not portable to aarch64!
             * Basically, on 32-bit NEON, registers are stored like so:
             *  .----------------------------------.
             *  |                q8                | // uint32x4_t
             *  |-----------------.----------------|
             *  |  d16 (.val[0])  |  d17 (.val[1]) | // uint32x2x2_t
             *  '-----------------'----------------'
             * vld2.32 will store its values into two double registers, returning
             * a uint32x2_t. In NEON, this will be stored in, for example, d16 and d17.
             * Reinterpret cast it to a uint32x4_t and you get q8 for free
             *
             * On aarch64, this was changed completely.
             *
             * aarch64 gave us 16 more quad registers, but they also removed this behavior,
             * instead matching smaller registers to the lower sections of the higher
             * registers and zeroing the rest.
             *  .----------------------------------..---------------------------------.
             *  |               v8.4s              |               v9.4s               |
             *  |-----------------.----------------|-----------------.-----------------|
             *  | v8.2s (.val[0]) |     <zero>     | v9.2s (.val[1]) |      <zero>     |
             *  '-----------------'----------------'-----------------'-----------------'
             * On aarch64, ld2 will put it into v8.2s and v9.2s. Reinterpreting
             * is not going to help us here, as half of it will end up being zero. */

            uint32x2x2_t d = vld2_u32(xdata + i * 4);     /* load and swap */
            uint32x2x2_t k = vld2_u32(xkey + i * 4);
            /* Not sorry about breaking the strict aliasing rule.
             * Using a union causes GCC to spit out nonsense, but an alias cast
             * does not. */
            uint32x4_t const dk = vaddq_u32(*(uint32x4_t*)&d, *(uint32x4_t*)&k);
            xacc[i] = vmlal_u32(xacc[i], vget_low_u32(dk), vget_high_u32(dk));
#else
            /* Portable, but slightly slower version */
            uint32x2x2_t const d = vld2_u32(xdata + i * 4);
            uint32x2x2_t const k = vld2_u32(xkey + i * 4);
            uint32x2_t const dkL = vadd_u32(d.val[0], k.val[0]);
            uint32x2_t const dkH = vadd_u32(d.val[1], k.val[1]);   /* uint32 dk[4]  = {d0+k0, d1+k1, d2+k2, d3+k3} */
            /* xacc must be aligned on 16 bytes boundaries */
            xacc[i] = vmlal_u32(xacc[i], dkL, dkH);                /* uint64 res[2] = {dk0*dk1,dk2*dk3} */
#endif
        }
    }
#else   /* scalar variant */

          U64* const xacc  =       (U64*) acc;
    const U32* const xdata = (const U32*) data;
    const U32* const xkey  = (const U32*) key;

    int i;
    for (i=0; i < (int)ACC_NB; i++) {
        int const left = 2*i;
        int const right= 2*i + 1;
        xacc[i] += (xdata[left] + xkey[left]) * (U64)(xdata[right] + xkey[right]);
    }

#endif
}

static void XXH3_scrambleAcc(void* acc, const void* key)
{
#if (XXH_VECTOR == XXH_AVX2)

    assert(((size_t)acc) & 31 == 0);
    {   __m256i* const xacc = (__m256i*) acc;
        const __m256i* const xkey  = (const __m256i *) key;

        __m256i const xor_p5 = _mm256_set1_epi64x(PRIME64_5);

        for (size_t i=0; i < STRIPE_LEN/sizeof(__m256i); i++) {
            __m256i data = xacc[i];
            __m256i const shifted = _mm256_srli_epi64(data, 47);
            data = _mm256_xor_si256(data, shifted);
            data = _mm256_xor_si256(data, xor_p5);

            {   __m256i const k   = _mm256_loadu_si256 (xkey+i);
                __m256i const dk  = _mm256_mul_epu32 (data,k);          /* U32 dk[4]  = {d0+k0, d1+k1, d2+k2, d3+k3} */

                __m256i const d2  = _mm256_shuffle_epi32 (data,0x31);
                __m256i const k2  = _mm256_shuffle_epi32 (k,0x31);
                __m256i const dk2 = _mm256_mul_epu32 (d2,k2);           /* U32 dk[4]  = {d0+k0, d1+k1, d2+k2, d3+k3} */

                xacc[i] = _mm256_xor_si256(dk, dk2);
        }   }
    }

#elif (XXH_VECTOR == XXH_SSE2)

    assert(((size_t)acc) & 15 == 0);
    {   __m128i* const xacc = (__m128i*) acc;
        const __m128i* const xkey  = (const __m128i *) key;
        __m128i const xor_p5 = _mm_set1_epi64((__m64)PRIME64_5);

        size_t i;
        for (i=0; i < STRIPE_LEN/sizeof(__m128i); i++) {
            __m128i data = xacc[i];
            __m128i const shifted = _mm_srli_epi64(data, 47);
            data = _mm_xor_si128(data, shifted);
            data = _mm_xor_si128(data, xor_p5);

            {   __m128i const k   = _mm_loadu_si128 (xkey+i);
                __m128i const dk  = _mm_mul_epu32 (data,k);          /* U32 dk[4]  = {d0+k0, d1+k1, d2+k2, d3+k3} */

                __m128i const d2  = _mm_shuffle_epi32 (data,0x31);
                __m128i const k2  = _mm_shuffle_epi32 (k,0x31);
                __m128i const dk2 = _mm_mul_epu32 (d2,k2);           /* U32 dk[4]  = {d0+k0, d1+k1, d2+k2, d3+k3} */

                xacc[i] = _mm_xor_si128(dk, dk2);
        }   }
    }

#elif (XXH_VECTOR == XXH_NEON)

    assert(((size_t)acc) & 15 == 0);
    {       uint64x2_t* const xacc =       (uint64x2_t*) acc;
        const uint32_t* const xkey  = (const uint32_t *) key;
        uint64x2_t xor_p5 = vdupq_n_u64(PRIME64_5);
        size_t i;
        /* Clang and GCC like to put NEON constant loads into the loop. */
        __asm__("" : "+w" (xor_p5));
        for (i=0; i < STRIPE_LEN/sizeof(uint64x2_t); i++) {
            uint64x2_t data = xacc[i];
            uint64x2_t const shifted = vshrq_n_u64(data, 47);
            data = veorq_u64(data, shifted);
            data = veorq_u64(data, xor_p5);

            {
                /* shuffle: 0, 1, 2, 3 -> 0, 2, 1, 3 */
                uint32x2x2_t const d =
                    vzip_u32(
                        vget_low_u32(vreinterpretq_u32_u64(data)),
                        vget_high_u32(vreinterpretq_u32_u64(data))
                    );
                uint32x2x2_t const k = vld2_u32 (xkey+i*4);              /* load and swap */
                uint64x2_t const dk  = vmull_u32(d.val[0],k.val[0]);     /* U64 dk[2]  = {d0 * k0, d2 * k2} */
                uint64x2_t const dk2 = vmull_u32(d.val[1],k.val[1]);     /* U64 dk2[2] = {d1 * k1, d3 * k3} */
                xacc[i] = veorq_u64(dk, dk2);                            /* xacc[i] = dk ^ dk2;             */
        }   }
    }

#else   /* scalar variant */

          U64* const xacc =       (U64*) acc;
    const U32* const xkey = (const U32*) key;

    int i;
    for (i=0; i < (int)ACC_NB; i++) {
        int const left = 2*i;
        int const right= 2*i + 1;
        xacc[i] ^= xacc[i] >> 47;
        xacc[i] ^= PRIME64_5;

        {   U64 p1 = (xacc[i] >> 32) * xkey[left];
            U64 p2 = (xacc[i] & 0xFFFFFFFF) * xkey[right];
            xacc[i] = p1 ^ p2;
    }   }

#endif
}

static void XXH3_accumulate(U64* acc, const void* restrict data, const U32* restrict key, size_t nbStripes)
{
    size_t n;
    for (n = 0; n < nbStripes; n++ ) {
        XXH3_accumulate_512(acc, (const BYTE*)data + n*STRIPE_LEN, key);
        key += 2;
    }
}

XXH_FORCE_INLINE U64 XXH3_mix16B(const void* data, const U64* key)
{
    return XXH3_mul128((XXH_read64(data) ^ key[0]), XXH_read64((const BYTE*)data+8) ^ key[1]);
}

static XXH64_hash_t XXH3_merge64B(const U64* data, const void* keyVoid, U64 len)
{
    const U64* const key = (const U64*)keyVoid;  /* presumed aligned */

    U64 acc = PRIME64_1 * len;
    acc += XXH3_mix16B(data+0, key+0);
    acc += XXH3_mix16B(data+2, key+2);
    acc += XXH3_mix16B(data+4, key+4);
    acc += XXH3_mix16B(data+6, key+6);

    return XXH64_avalanche2(acc);
}

__attribute__((noinline)) static U64    /* It's important for performance that XXH3_hashLong is not inlined. Not sure why (uop cache maybe ?), but difference is large and easily measurable */
XXH3_hashLong(const void* data, size_t len)
{
    ALIGN(64) U64 acc[ACC_NB] = { 0, PRIME64_1, PRIME64_2, PRIME64_3, PRIME64_4, PRIME64_5 };

    #define NB_KEYS ((KEYSET_DEFAULT_SIZE - STRIPE_ELTS) / 2)

    size_t const block_len = STRIPE_LEN * NB_KEYS;
    size_t const nb_blocks = len / block_len;

    size_t n;
    for (n = 0; n < nb_blocks; n++) {
        XXH3_accumulate(acc, (const BYTE*)data + n*block_len, kKey, NB_KEYS);
        XXH3_scrambleAcc(acc, kKey + (KEYSET_DEFAULT_SIZE - STRIPE_ELTS));
    }

    /* last partial block */
    assert(len > STRIPE_LEN);
    {   size_t const nbStripes = (len % block_len) / STRIPE_LEN;
        assert(nbStripes < NB_KEYS);
        XXH3_accumulate(acc, (const BYTE*)data + nb_blocks*block_len, kKey, nbStripes);

        /* last stripe */
        if (len & (STRIPE_LEN - 1)) {
            const BYTE* const p = (const BYTE*) data + len - STRIPE_LEN;
            XXH3_accumulate_512(acc, p, kKey + nbStripes*2);
    }   }

    /* converge into final hash */
    //return XXH3_finalMerge_8u64(acc[0] + len, acc[1], acc[2], acc[3], acc[4], acc[5], acc[6], acc[7] - len, PRIME64_2 + len*2);
    assert(sizeof(acc) == 64);
    return XXH3_merge64B(acc, kKey, len);
}



/* ==========================================
 * Public entry point
 * ========================================== */

XXH_PUBLIC_API XXH64_hash_t XXH3_64b(const void* data, size_t len)
{
    const BYTE* const p = (const BYTE*)data;
    const U64* const key = (const U64*)(const void*)kKey;

    if (len <= 16) return XXH3_len_0to16_64b(data, len);

    {   U64 acc = PRIME64_1 * len;
        if (len > 32) {
            if (len > 64) {
                if (len > 96) {
                    if (len > 128) return XXH3_hashLong(data, len);

                    acc += XXH3_mix16B(p+48, key+12);
                    acc += XXH3_mix16B(p+len-64, key+14);
                }

                acc += XXH3_mix16B(p+32, key+8);
                acc += XXH3_mix16B(p+len-48, key+10);
            }

            acc += XXH3_mix16B(p+16, key+4);
            acc += XXH3_mix16B(p+len-32, key+6);

        }

        acc += XXH3_mix16B(p+0, key+0);
        acc += XXH3_mix16B(p+len-16, key+2);

        return XXH64_avalanche2(acc);
    }
}



#endif  /* XXH3_H */
