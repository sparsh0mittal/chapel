/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2009, 2010  Université de Bordeaux 1
 * Copyright (C) 2010, 2011  Centre National de la Recherche Scientifique
 *
 * StarPU is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * StarPU is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

#ifndef __DW_CHOLESKY_MODELS_H__
#define __DW_CHOLESKY_MODELS_H__

void chol_codelet_update_u11(float* mat, unsigned nx, unsigned ld)
	__attribute__ ((task));

void chol_codelet_update_u21(const float *sub11, float *sub21, unsigned ld11,
			     unsigned ld21, unsigned nx21, unsigned ny21)
	__attribute__ ((task));

void chol_codelet_update_u22(const float *left, const float *right, float *center,
			     unsigned dx, unsigned dy, unsigned dz,
			     unsigned ld21, unsigned ld12, unsigned ld22)
	__attribute__ ((task));

#endif // __DW_CHOLESKY_MODELS_H__