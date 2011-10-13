
#include <stdarg.h>
#include <stdio.h>


#ifndef CHPL_NO_GMP
#include <gmp.h>


// set the GMP allocation routines to use chpl_malloc, etc.
void chpl_gmp_init(void);

void chpl_gmp_get_mpz(mpz_t ret, int32_t src_locale, __mpz_struct from);

void chpl_gmp_get_randstate(gmp_randstate_t not_inited_state, int32_t src_locale, __gmp_randstate_struct from);

uint64_t chpl_gmp_mpz_nlimbs(__mpz_struct from);

void chpl_gmp_mpz_print(mpz_t x);

chpl_string chpl_gmp_mpz_get_str(int32_t base, mpz_t x);

#endif
