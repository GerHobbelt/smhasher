/*
 * RH512
 * Copyright (C) 2026  Frank J. T. Wojcik
 * Copyright (C) 2026 ANTONIO GARC�A LEAL
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include "Platform.h"
#include "Hashlib.h"

template<unsigned mode, bool bswap>
static void RH512_256(const void* key, const size_t len, seed_t seed, void* out) {
    const uint8_t* data = (const uint8_t*)key;

    // Constantes "Pro": 8 Primos de dispersión masiva de 64-bit completos
    const uint64_t C0 = UINT64_C(0xff51afd7ed558ccd);
    const uint64_t C1 = UINT64_C(0xc4ceb9fe1a85ec53);
    const uint64_t C2 = UINT64_C(0x9e3779b97f4a7c15);
    const uint64_t C3 = UINT64_C(0x517cc1b727220a95);
    const uint64_t C4 = UINT64_C(0xa0761d6478bd642f);
    const uint64_t C5 = UINT64_C(0xe7037ed1a0b428db);
    const uint64_t C6 = UINT64_C(0x8ebc6af09c88c6e3);
    const uint64_t C7 = UINT64_C(0x589965cc75374cc3);

    // 1. ESTADO EN REGISTROS NATIVOS (Sin Arrays)
    uint64_t h0 = seed ^ C0, h1 = seed ^ C1, h2 = seed ^ C2, h3 = seed ^ C3;
    uint64_t h4 = len  ^ C4, h5 = len  ^ C5, h6 = len  ^ C6, h7 = len  ^ C7;

    size_t i = 0;

    // 2. BUCLE PRINCIPAL (Fully Unrolled - Cero Módulos)
    if (len >= 32) {
        for (; i <= len - 32; i += 32) {
            uint64_t k0, k1, k2, k3;
            // Carga secuencial rápida
            k0 = GET_U64<bswap>(data, i     ); k1 = GET_U64<bswap>(data, i +  8);
            k2 = GET_U64<bswap>(data, i + 16); k3 = GET_U64<bswap>(data, i + 24);

            // ABSORCIÓN CARA A + DIFUSIÓN CARA B
            // Paralelismo de instrucciones implícito masivo
            h0 ^= k0 * C0; h0 = ROTL64(h0, 23); h4 += h0; h4 = ROTL64(h4, 31);
            h1 ^= k1 * C1; h1 = ROTL64(h1, 24); h5 += h1; h5 = ROTL64(h5, 31);
            h2 ^= k2 * C2; h2 = ROTL64(h2, 25); h6 += h2; h6 = ROTL64(h6, 31);
            h3 ^= k3 * C3; h3 = ROTL64(h3, 26); h7 += h3; h7 = ROTL64(h7, 31);

            // RETROALIMENTACIÓN EN ANILLO PARALELO (Fix Sparse Test)
            // Congelamos el estado en registros temporales para mantener
            // la ejecución en paralelo (0 ciclos de penalización).
            uint64_t t0 = h0, t1 = h1, t2 = h2, t3 = h3;
            uint64_t t4 = h4, t5 = h5, t6 = h6, t7 = h7;

            // Todos los carriles se retroalimentan del vecino, mezclando
            // Cara A con Cara B y rotando para destruir alineaciones de bits.
            h0 ^= ROTL64(t7, 11); h4 += t3;
            h1 ^= ROTL64(t4, 11); h5 += t0;
            h2 ^= ROTL64(t5, 11); h6 += t1;
            h3 ^= ROTL64(t6, 11); h7 += t2;
        }
    }

    // 3. COLA Y PADDING (Fix BIC Test y Resonancia de bits)
    if (i < len) {
        uint8_t tail[32] = {0};
        memcpy(tail, data + i, len - i);

        // Bit de Parada para aniquilar los ceros al final
        tail[len - i] = 0x80;

        uint64_t tk0, tk1, tk2, tk3;
        tk0 = GET_U64<bswap>(tail,  0); tk1 = GET_U64<bswap>(tail,  8);
        tk2 = GET_U64<bswap>(tail, 16); tk3 = GET_U64<bswap>(tail, 24);

        // PRE-MEZCLA MASIVA: Destruimos la linealidad del bloque
        tk0 *= C4; tk0 = ROTL64(tk0, 31); tk0 *= C0;
        tk1 *= C5; tk1 = ROTL64(tk1, 31); tk1 *= C1;
        tk2 *= C6; tk2 = ROTL64(tk2, 31); tk2 *= C2;
        tk3 *= C7; tk3 = ROTL64(tk3, 31); tk3 *= C3;

        // INYECCIÓN DOBLE: Metemos la entropía en ambos polos a la vez
        h0 ^= tk0; h4 ^= tk0;
        h1 ^= tk1; h5 ^= tk1;
        h2 ^= tk2; h6 ^= tk2;
        h3 ^= tk3; h7 ^= tk3;
    }

    // SELLO DE LONGITUD FINAL
    // Mixeamos la longitud antes de inyectarla para evitar simetrías
    uint64_t flen = (uint64_t)len * C6;
    flen = ROTL64(flen, 27) * C7;

    h0 ^= flen; h4 ^= flen;
    h7 ^= flen; h3 ^= flen;

    // 4. FINALIZADOR "BUTTERFLY" (Full Unrolled - 3 Etapas Logarítmicas)

    // Etapa 1: Distancia 4 (Polos opuestos)
    h0 ^= ROTL64(h4, 29); h0 *= C0; h4 += h0; h4 = ROTL64(h4, 13);
    h1 ^= ROTL64(h5, 29); h1 *= C1; h5 += h1; h5 = ROTL64(h5, 13);
    h2 ^= ROTL64(h6, 29); h2 *= C2; h6 += h2; h6 = ROTL64(h6, 13);
    h3 ^= ROTL64(h7, 29); h3 *= C3; h7 += h3; h7 = ROTL64(h7, 13);

    // Etapa 2: Distancia 2 (Cuadrantes cruzados)
    h0 ^= ROTL64(h2, 29); h0 *= C4; h2 += h0; h2 = ROTL64(h2, 13);
    h1 ^= ROTL64(h3, 29); h1 *= C5; h3 += h1; h3 = ROTL64(h3, 13);
    h4 ^= ROTL64(h6, 29); h4 *= C6; h6 += h4; h6 = ROTL64(h6, 13);
    h5 ^= ROTL64(h7, 29); h5 *= C7; h7 += h5; h7 = ROTL64(h7, 13);

    // Etapa 3: Distancia 1 (Pares adyacentes)
    h0 ^= ROTL64(h1, 29); h0 *= C0; h1 += h0; h1 = ROTL64(h1, 13);
    h2 ^= ROTL64(h3, 29); h2 *= C1; h3 += h2; h3 = ROTL64(h3, 13);
    h4 ^= ROTL64(h5, 29); h4 *= C2; h5 += h4; h5 = ROTL64(h5, 13);
    h6 ^= ROTL64(h7, 29); h6 *= C3; h7 += h6; h7 = ROTL64(h7, 13);

    // Cascada de Avalancha Final (Doble etapa para aniquilar fallos BIC)
    #define AVALANCHE(x) \
        x ^= x >> 33; \
        x *= 0xff51afd7ed558ccdULL; \
        x ^= x >> 33; \
        x *= 0xc4ceb9fe1a85ec53ULL; \
        x ^= x >> 33;

    AVALANCHE(h0); AVALANCHE(h1); AVALANCHE(h2); AVALANCHE(h3);
    AVALANCHE(h4); AVALANCHE(h5); AVALANCHE(h6); AVALANCHE(h7);

    #undef AVALANCHE

    // 5. EXPORTACIÓN DEL ESTADO
    // IMPORTANTE: Dejamos 32 para que el SMHasher no desborde en modo 256.
    if (mode == 0) {
        PUT_U64<bswap>(h0, (uint8_t *)out,  0);
        PUT_U64<bswap>(h1, (uint8_t *)out,  8);
        PUT_U64<bswap>(h2, (uint8_t *)out, 16);
        PUT_U64<bswap>(h3, (uint8_t *)out, 24);
    } else if (mode == 1) {
        PUT_U64<bswap>(h4, (uint8_t *)out,  0);
        PUT_U64<bswap>(h5, (uint8_t *)out,  8);
        PUT_U64<bswap>(h6, (uint8_t *)out, 16);
        PUT_U64<bswap>(h7, (uint8_t *)out, 24);
    } else {
        PUT_U64<bswap>(h0, (uint8_t *)out,  0);
    }
}

REGISTER_FAMILY(rh512,
   $.src_url    = "",
   $.src_status = HashFamilyInfo::SRC_STABLEISH
);

REGISTER_HASH(RH512,
   $.desc            = "RH512 Pro Implementation (low 256 bits)",
   $.hash_flags      = 0;
   $.impl_flags      =
        FLAG_IMPL_MULTIPLY_64_64 |
        FLAG_IMPL_ROTATE         |
        FLAG_IMPL_LICENSE_MIT,
   $.bits            = 256,
   $.verification_LE = 0xD5E60849,
   $.verification_BE = 0xD8234405,
   $.hashfn_native   = RH512_256<0, false>,
   $.hashfn_bswap    = RH512_256<0, true>
);

REGISTER_HASH(RH512h,
   $.desc            = "RH512 Pro Implementation (high 256 bits)",
   $.hash_flags      = 0;
   $.impl_flags      =
        FLAG_IMPL_MULTIPLY_64_64 |
        FLAG_IMPL_ROTATE         |
        FLAG_IMPL_LICENSE_MIT,
   $.bits            = 256,
   $.verification_LE = 0xE42C2BF3,
   $.verification_BE = 0x4B190F9A,
   $.hashfn_native   = RH512_256<1, false>,
   $.hashfn_bswap    = RH512_256<1, true>
);

REGISTER_HASH(RH512_64,
   $.desc            = "RH512 Pro Implementation (low 64 bits)",
   $.hash_flags      = 0;
   $.impl_flags      =
        FLAG_IMPL_MULTIPLY_64_64 |
        FLAG_IMPL_ROTATE         |
        FLAG_IMPL_LICENSE_MIT,
   $.bits            = 64,
   $.verification_LE = 0x3C481EA3,
   $.verification_BE = 0x92EEF8A0,
   $.hashfn_native   = RH512_256<2, false>,
   $.hashfn_bswap    = RH512_256<2, true>
);
