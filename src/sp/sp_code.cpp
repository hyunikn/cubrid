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
// sp_code.cpp
//

#include "sp_code.hpp"

#include <unordered_map>
#include <string>

#include <cstring>

#include "dbtype.h"
#include "heap_file.h"
#include "log_impl.h"
#include "object_representation_sr.h"
#include "oid.h"
#include "schema_system_catalog_constants.h"
#include "sp_constants.hpp"
#include "storage_common.h"
#include "xserver_interface.h"

// XXX: SHOULD BE THE LAST INCLUDE HEADER
#include "memory_wrapper.hpp"

ATTR_ID spcode_Attrs_id[NUM_SP_CODE_ATTR];
int spcode_Num_attrs = -1;

static int sp_load_sp_code_attribute_info (THREAD_ENTRY *thread_p);
static void sp_code_attr_init ();
static int sp_get_attrid (THREAD_ENTRY *thread_p, int attr_index, ATTR_ID &attrid);
static int sp_get_attr_idx (const std::string &attr_name);

using sp_code_attr_map_type = std::unordered_map <std::string, int>;
static sp_code_attr_map_type attr_idx_map;

static void
sp_code_attr_init ()
{
#define MAP_LIST_ITEM(item)     attr_idx_map [SP_CODE_ATTR_##item] = INDEX_SP_CODE_ATTR_##item;
  SP_CODE_ATTR_LIST
#undef MAP_LIST_ITEM
}

static int
sp_get_attr_idx (const std::string &attr_name)
{
  if (attr_idx_map.size () == 0)
    {
      sp_code_attr_init ();
    }

  auto idx_it = attr_idx_map.find (attr_name);
  if (idx_it == attr_idx_map.end ())
    {
      return -1;
    }
  else
    {
      return idx_it->second;
    }
}

int
sp_get_code_attr (THREAD_ENTRY *thread_p, const std::string &attr_name, const OID *sp_oidp, DB_VALUE *result)
{
  int ret = NO_ERROR;
  HEAP_SCANCACHE scan_cache;
  SCAN_CODE scan;
  RECDES recdesc = RECDES_INITIALIZER;
  HEAP_CACHE_ATTRINFO attr_info, *attr_info_p = NULL;
  ATTR_ID attrid;
  DB_VALUE *cur_val;
  OID *sp_class_oid = oid_Sp_code_class_oid;
  int idx = -1;

  heap_scancache_quick_start_with_class_oid (thread_p, &scan_cache, sp_class_oid);
  /* get record into record desc */
  scan = heap_get_visible_version (thread_p, sp_oidp, sp_class_oid, &recdesc, &scan_cache, PEEK, NULL_CHN);
  if (scan != S_SUCCESS)
    {
      if (er_errid () == ER_PB_BAD_PAGEID)
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_HEAP_UNKNOWN_OBJECT, 3, sp_oidp->volid, sp_oidp->pageid,
		  sp_oidp->slotid);
	}
      else
	{
	  er_set (ER_ERROR_SEVERITY, ARG_FILE_LINE, ER_QPROC_CANNOT_FETCH_SERIAL, 0);
	}
      goto exit_on_error;
    }

  /* retrieve attribute */
  idx = sp_get_attr_idx (attr_name);
  if (idx == -1)
    {
      goto exit_on_error;
    }

  if (sp_get_attrid (thread_p, idx, attrid) != NO_ERROR)
    {
      goto exit_on_error;
    }

  assert (attrid != -1);

  ret = heap_attrinfo_start (thread_p, sp_class_oid, 1, &attrid, &attr_info);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  attr_info_p = &attr_info;

  ret = heap_attrinfo_read_dbvalues (thread_p, sp_oidp, &recdesc, attr_info_p);
  if (ret != NO_ERROR)
    {
      goto exit_on_error;
    }

  cur_val = heap_attrinfo_access (attrid, attr_info_p);

  db_value_clone (cur_val, result);

  heap_attrinfo_end (thread_p, attr_info_p);

  heap_scancache_end (thread_p, &scan_cache);

  return NO_ERROR;

exit_on_error:

  if (attr_info_p != NULL)
    {
      heap_attrinfo_end (thread_p, attr_info_p);
    }

  heap_scancache_end (thread_p, &scan_cache);

  ret = (ret == NO_ERROR && (ret = er_errid ()) == NO_ERROR) ? ER_FAILED : ret;
  return ret;
}

static int
sp_get_attrid (THREAD_ENTRY *thread_p, int attr_index, ATTR_ID &attrid)
{
  attrid = -1; // NOT FOUND

  if (spcode_Num_attrs < 0)
    {
      int error = sp_load_sp_code_attribute_info (thread_p);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  return error;
	}
    }

  if (attr_index >= 0 && attr_index <= spcode_Num_attrs)
    {
      attrid = spcode_Attrs_id[attr_index];
    }
  return NO_ERROR;
}

static int
sp_load_sp_code_attribute_info (THREAD_ENTRY *thread_p)
{
  HEAP_SCANCACHE scan;
  RECDES class_record;
  HEAP_CACHE_ATTRINFO attr_info;
  int i, error = NO_ERROR;
  char *attr_name_p, *string = NULL;
  int alloced_string = 0;
  int attr_idx = -1;

  if (spcode_Num_attrs != -1)
    {
      // already retrived
      return error;
    }

  OID *sp_code_oid_class = oid_Sp_code_class_oid;
  spcode_Num_attrs = -1;

  if (heap_scancache_quick_start_with_class_oid (thread_p, &scan, sp_code_oid_class) != NO_ERROR)
    {
      return ER_FAILED;
    }
  if (heap_get_class_record (thread_p, sp_code_oid_class, &class_record, &scan, PEEK) != S_SUCCESS)
    {
      heap_scancache_end (thread_p, &scan);
      return ER_FAILED;
    }

  error = heap_attrinfo_start (thread_p, sp_code_oid_class, -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      (void) heap_scancache_end (thread_p, &scan);
      return error;
    }

  for (i = 0; i < attr_info.num_values; i++)
    {
      string = NULL;
      alloced_string = 0;

      error = or_get_attrname (&class_record, i, &string, &alloced_string);
      if (error != NO_ERROR)
	{
	  ASSERT_ERROR ();
	  goto exit_on_error;
	}

      attr_name_p = string;
      if (attr_name_p == NULL)
	{
	  error = ER_FAILED;
	  goto exit_on_error;
	}

      attr_idx = sp_get_attr_idx (attr_name_p);
      if (attr_idx != -1)
	{
	  spcode_Attrs_id [attr_idx] = i;
	}

      if (string != NULL && alloced_string)
	{
	  db_private_free_and_init (NULL, string);
	}

    }
  spcode_Num_attrs = attr_info.num_values;

  heap_attrinfo_end (thread_p, &attr_info);
  error = heap_scancache_end (thread_p, &scan);

  return error;

exit_on_error:

  heap_attrinfo_end (thread_p, &attr_info);
  (void) heap_scancache_end (thread_p, &scan);

  return error;
}

// resolve the ordinal attribute id of attr_name within class_oid (-1 on not found)
static int
sp_find_attrid_by_name (THREAD_ENTRY *thread_p, const OID *class_oid, const char *attr_name, ATTR_ID &attrid_out)
{
  HEAP_SCANCACHE scan;
  RECDES class_record;
  HEAP_CACHE_ATTRINFO attr_info;
  int i, error = NO_ERROR;
  char *string = NULL;
  int alloced_string = 0;
  ATTR_ID attrid = -1;

  if (heap_scancache_quick_start_with_class_oid (thread_p, &scan, (OID *) class_oid) != NO_ERROR)
    {
      return ER_FAILED;
    }
  if (heap_get_class_record (thread_p, class_oid, &class_record, &scan, PEEK) != S_SUCCESS)
    {
      heap_scancache_end (thread_p, &scan);
      return ER_FAILED;
    }
  error = heap_attrinfo_start (thread_p, class_oid, -1, NULL, &attr_info);
  if (error != NO_ERROR)
    {
      heap_scancache_end (thread_p, &scan);
      return error;
    }

  for (i = 0; i < attr_info.num_values; i++)
    {
      string = NULL;
      alloced_string = 0;

      if (or_get_attrname (&class_record, i, &string, &alloced_string) != NO_ERROR)
	{
	  error = ER_FAILED;
	  break;
	}

      bool match = (string != NULL && strcmp (string, attr_name) == 0);

      if (string != NULL && alloced_string)
	{
	  db_private_free_and_init (thread_p, string);
	}

      if (match)
	{
	  attrid = i;
	  break;
	}
    }

  heap_attrinfo_end (thread_p, &attr_info);
  heap_scancache_end (thread_p, &scan);

  if (error != NO_ERROR)
    {
      return error;
    }
  if (attrid == -1)
    {
      return ER_FAILED;
    }
  attrid_out = attrid;
  return NO_ERROR;
}

// read a string attribute (by name) of the object obj_oid into out
static int
sp_read_string_attr (THREAD_ENTRY *thread_p, const OID *class_oid, const OID *obj_oid, const char *attr_name,
		     std::string &out)
{
  ATTR_ID attrid;
  int error = sp_find_attrid_by_name (thread_p, class_oid, attr_name, attrid);
  if (error != NO_ERROR)
    {
      return error;
    }

  HEAP_SCANCACHE scan_cache;
  RECDES recdes = RECDES_INITIALIZER;
  HEAP_CACHE_ATTRINFO attr_info;
  DB_VALUE *cur_val;

  heap_scancache_quick_start_with_class_oid (thread_p, &scan_cache, (OID *) class_oid);
  if (heap_get_visible_version (thread_p, obj_oid, (OID *) class_oid, &recdes, &scan_cache, PEEK, NULL_CHN) != S_SUCCESS)
    {
      heap_scancache_end (thread_p, &scan_cache);
      return ER_FAILED;
    }

  error = heap_attrinfo_start (thread_p, class_oid, 1, &attrid, &attr_info);
  if (error != NO_ERROR)
    {
      heap_scancache_end (thread_p, &scan_cache);
      return error;
    }

  error = heap_attrinfo_read_dbvalues (thread_p, obj_oid, &recdes, &attr_info);
  if (error != NO_ERROR)
    {
      heap_attrinfo_end (thread_p, &attr_info);
      heap_scancache_end (thread_p, &scan_cache);
      return error;
    }

  cur_val = heap_attrinfo_access (attrid, &attr_info);
  if (cur_val != NULL && !DB_IS_NULL (cur_val) && db_get_string (cur_val) != NULL)
    {
      out.assign (db_get_string (cur_val), db_get_string_size (cur_val));
    }
  else
    {
      out.clear ();
    }

  heap_attrinfo_end (thread_p, &attr_info);
  heap_scancache_end (thread_p, &scan_cache);
  return NO_ERROR;
}

// find the object whose string attribute attr_name equals key; found_oid is set NULL if none
static int
sp_find_oid_by_string_attr (THREAD_ENTRY *thread_p, const OID *class_oid, const char *attr_name, const char *key,
			    OID *found_oid)
{
  HFID hfid;
  ATTR_ID attrid;
  int error = NO_ERROR;

  OID_SET_NULL (found_oid);

  if (heap_get_class_info (thread_p, class_oid, &hfid, NULL, NULL) != NO_ERROR)
    {
      return ER_FAILED;
    }
  error = sp_find_attrid_by_name (thread_p, class_oid, attr_name, attrid);
  if (error != NO_ERROR)
    {
      return error;
    }

  HEAP_SCANCACHE scan_cache;
  // use the transaction's MVCC snapshot so a CREATE OR REPLACE'd routine is seen at its current
  // (committed) version; a NULL snapshot may return a stale version of the code record
  MVCC_SNAPSHOT *mvcc_snapshot = logtb_get_mvcc_snapshot (thread_p);
  if (heap_scancache_start (thread_p, &scan_cache, &hfid, class_oid, true, mvcc_snapshot) != NO_ERROR)
    {
      return ER_FAILED;
    }

  HEAP_CACHE_ATTRINFO attr_info;
  if (heap_attrinfo_start (thread_p, class_oid, 1, &attrid, &attr_info) != NO_ERROR)
    {
      heap_scancache_end (thread_p, &scan_cache);
      return ER_FAILED;
    }

  OID oid;
  RECDES recdes = RECDES_INITIALIZER;
  SCAN_CODE scan;

  scan = heap_first (thread_p, &hfid, (OID *) class_oid, &oid, &recdes, &scan_cache, PEEK);
  while (scan == S_SUCCESS)
    {
      if (heap_attrinfo_read_dbvalues (thread_p, &oid, &recdes, &attr_info) != NO_ERROR)
	{
	  error = ER_FAILED;
	  break;
	}

      DB_VALUE *v = heap_attrinfo_access (attrid, &attr_info);
      if (v != NULL && !DB_IS_NULL (v) && db_get_string (v) != NULL && strcmp (db_get_string (v), key) == 0)
	{
	  COPY_OID (found_oid, &oid);
	  break;
	}

      scan = heap_next (thread_p, &hfid, (OID *) class_oid, &oid, &recdes, &scan_cache, PEEK);
    }

  heap_attrinfo_end (thread_p, &attr_info);
  heap_scancache_end (thread_p, &scan_cache);
  return error;
}

int
sp_get_code_by_name (THREAD_ENTRY *thread_p, const std::string &class_name, const std::string &req_compile_id,
		     int &status, std::string &out_compile_id, std::string &out_ocode)
{
  int error = NO_ERROR;
  bool is_pkg = (class_name.compare (0, 5, "Pckg_") == 0);

  OID code_class_oid;
  OID code_oid;
  std::string compile_id;

  status = SP_CODE_FETCH_NOT_FOUND;

  if (!is_pkg)
    {
      // stored procedure / function: _db_stored_procedure_code keyed by name (= class name)
      OID *sp_code_class = oid_Sp_code_class_oid;

      error = sp_find_oid_by_string_attr (thread_p, sp_code_class, SP_CODE_ATTR_NAME, class_name.c_str (), &code_oid);
      if (error != NO_ERROR)
	{
	  return error;
	}
      if (OID_ISNULL (&code_oid))
	{
	  return NO_ERROR;	// not found
	}

      COPY_OID (&code_class_oid, sp_code_class);
      error = sp_read_string_attr (thread_p, &code_class_oid, &code_oid, SP_CODE_ATTR_COMPILE_ID, compile_id);
      if (error != NO_ERROR)
	{
	  return error;
	}
    }
  else
    {
      // package: _db_package (target_class -> unique_name, compile_id), _db_package_code (pkg_unique_name -> ocode)
      OID pkg_class_oid;
      OID pkg_code_class_oid;
      OID pkg_oid;
      std::string unique_name;

      if (xlocator_find_class_oid (thread_p, CT_PACKAGE_NAME, &pkg_class_oid, NULL_LOCK) != LC_CLASSNAME_EXIST)
	{
	  return ER_FAILED;
	}
      error =
	      sp_find_oid_by_string_attr (thread_p, &pkg_class_oid, PKG_ATTR_TARGET_CLASS, class_name.c_str (), &pkg_oid);
      if (error != NO_ERROR)
	{
	  return error;
	}
      if (OID_ISNULL (&pkg_oid))
	{
	  return NO_ERROR;	// not found
	}

      error = sp_read_string_attr (thread_p, &pkg_class_oid, &pkg_oid, PKG_ATTR_UNIQUE_NAME, unique_name);
      if (error != NO_ERROR)
	{
	  return error;
	}
      error = sp_read_string_attr (thread_p, &pkg_class_oid, &pkg_oid, PKG_ATTR_COMPILE_ID, compile_id);
      if (error != NO_ERROR)
	{
	  return error;
	}

      if (xlocator_find_class_oid (thread_p, CT_PACKAGE_CODE_NAME, &pkg_code_class_oid, NULL_LOCK) != LC_CLASSNAME_EXIST)
	{
	  return ER_FAILED;
	}
      error =
	      sp_find_oid_by_string_attr (thread_p, &pkg_code_class_oid, PKG_CODE_ATTR_PKG_UNIQUE_NAME,
					  unique_name.c_str (), &code_oid);
      if (error != NO_ERROR)
	{
	  return error;
	}
      if (OID_ISNULL (&code_oid))
	{
	  return NO_ERROR;	// not found
	}
      COPY_OID (&code_class_oid, &pkg_code_class_oid);
    }

  // compare compile_id: if the caller already has the current version, skip shipping the (large) ocode
  if (!req_compile_id.empty () && req_compile_id == compile_id)
    {
      status = SP_CODE_FETCH_UNCHANGED;
      return NO_ERROR;
    }

  error =
	  sp_read_string_attr (thread_p, &code_class_oid, &code_oid, is_pkg ? PKG_CODE_ATTR_OCODE : SP_CODE_ATTR_OCODE,
			       out_ocode);
  if (error != NO_ERROR)
    {
      return error;
    }

  status = SP_CODE_FETCH_CHANGED;
  out_compile_id = compile_id;
  return NO_ERROR;
}
