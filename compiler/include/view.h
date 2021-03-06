/*
 * Copyright 2004-2014 Cray Inc.
 * Other additional copyright holders may be indicated within.
 * 
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 * 
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _VIEW_H_
#define _VIEW_H_

#include "baseAST.h"
#include "vec.h"

BaseAST*    aid(int id);
BaseAST*    aid09(int id);

void        list_view_noline(BaseAST* ast);
void        nprint_view(BaseAST* ast);

// These are not used by the compiler but are available for use in GDB
//
// The most commonly used functions are referenced in
// $CHPL_HOME/compiler/etc/gdb.commands and appear to be
//
//    aid
//    print_view
//    iprint_view
//    nprint_view
//    list_view
//    viewFlags
//    stringLoc

void        print_view(BaseAST* ast);
void        print_view_noline(BaseAST* ast);

void        iprint_view(int id);

void        nprint_view(int id);
void        nprint_view_noline(BaseAST* ast);

void        mark_view(BaseAST* ast, int id);

void        list_view(int id);
void        list_view(BaseAST* ast);

void        viewFlags(int id);

void        map_view(SymbolMap* map);
void        map_view(SymbolMap& map);

void        vec_view(Vec<Symbol*,   VEC_INTEGRAL_SIZE>* v);
void        vec_view(Vec<Symbol*,   VEC_INTEGRAL_SIZE>& v);
void        vec_view(Vec<FnSymbol*, VEC_INTEGRAL_SIZE>* v);
void        vec_view(Vec<FnSymbol*, VEC_INTEGRAL_SIZE>& v);

void        fnsWithName(const char* name);
void        fnsWithName(const char* name, Vec<FnSymbol*>& fnVec);

void        whocalls(int id);
void        whocalls(BaseAST* ast);

// NB these return the same static buffer
const char* stringLoc(int id);
const char* stringLoc(BaseAST* ast);

const char* shortLoc(int id);
const char* shortLoc(BaseAST* ast);

const char* debugLoc(int id);
const char* debugLoc(BaseAST* ast);

#endif
