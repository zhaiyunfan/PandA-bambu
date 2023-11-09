/*
 *
 *                   _/_/_/    _/_/   _/    _/ _/_/_/    _/_/
 *                  _/   _/ _/    _/ _/_/  _/ _/   _/ _/    _/
 *                 _/_/_/  _/_/_/_/ _/  _/_/ _/   _/ _/_/_/_/
 *                _/      _/    _/ _/    _/ _/   _/ _/    _/
 *               _/      _/    _/ _/    _/ _/_/_/  _/    _/
 *
 *             ***********************************************
 *                              PandA Project
 *                     URL: http://panda.dei.polimi.it
 *                       Politecnico di Milano - DEIB
 *                        System Architectures Group
 *             ***********************************************
 *              Copyright (C) 2023 Politecnico di Milano
 *
 *   This file is part of the PandA framework.
 *
 *   The PandA framework is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
/**
 * @file mdpi_driver.h
 *
 * @author Michele Fiorito <michele.fiorito@polimi.it>
 * $Revision$
 * $Date$
 * Last modified by $Author$
 *
 */
#ifndef __MDPI_DRIVER_H
#define __MDPI_DRIVER_H

#include "mdpi_types.h"

#include <stddef.h>

#define MDPI_MEMMAP_DEVICE 0
#define MDPI_MEMMAP_SHARED 1

EXTERN_C void __m_arg_init(uint8_t argcount);
EXTERN_C void __m_arg_fini();
EXTERN_C void __m_setarg(uint8_t index, void* bits, uint16_t bitsize);
EXTERN_C void __m_setptrarg(uint8_t index, bptr_t* bits, uint16_t bitsize);
EXTERN_C void __m_memmap_init(int map_mode);
EXTERN_C int __m_memmap(ptr_t dst, void* src, size_t bytes);
EXTERN_C void __m_param_alloc(uint8_t idx, size_t size);
EXTERN_C size_t __m_param_size(uint8_t idx);

EXTERN_C void __m_init();
EXTERN_C void __m_sim_start();
EXTERN_C unsigned int __m_sim_end();

EXTERN_C void __m_exit(int __status);
EXTERN_C void __m_abort();
EXTERN_C void __m_assert_fail(const char* __assertion, const char* __file, unsigned int __line, const char* __function);

#define __m_setargptr(index, bits, bitsize) \
   bptr_t __ptrval_##index = (bptr_t)bits;  \
   __m_setptrarg(index, &__ptrval_##index, bitsize)

#endif // __MDPI_DRIVER_H