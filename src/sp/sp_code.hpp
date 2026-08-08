/*
 *
 * Copyright 2016 CUBRID Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */

//
// sp_code.hpp
//

#ifndef _SP_CODE_HPP_
#define _SP_CODE_HPP_

#ident "$Id$"

#include <vector>

#include "dbtype_def.h"
#include "query_list.h" /* QUERY_ID, QFILE_LIST_ID */
#include "query_manager.h"

int sp_get_code_attr (THREAD_ENTRY *thread_p, const std::string &attr_name, const OID *sp_oidp, DB_VALUE *result);

// result of looking up object code by (generated) class name
enum SP_CODE_FETCH_STATUS
{
  SP_CODE_FETCH_NOT_FOUND = 0,	// no SP/package with the given class name (e.g. dropped)
  SP_CODE_FETCH_UNCHANGED = 1,	// stored compile_id equals the requested one
  SP_CODE_FETCH_CHANGED = 2	// object code returned along with its compile_id
};

// Look up the object code (ocode) of a stored procedure or package by its generated Java class
// name (Proc_.../Func_.../Pckg_...). Auth is bypassed - a referenced unit's code must be
// loadable regardless of the caller's privileges.
int sp_get_code_by_name (THREAD_ENTRY *thread_p, const std::string &class_name,
			 const std::string &req_compile_id, int &status, std::string &out_compile_id,
			 std::string &out_ocode);
#endif				/* _SP_CODE_HPP_ */
