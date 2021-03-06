/* This file read a Java(TM) .class file.
   It is not stand-alone:  It depends on tons of macros, and the
   intent is you #include this file after you've defined the macros.

   Copyright (C) 1996  Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  

Java and all Java-based marks are trademarks or registered trademarks
of Sun Microsystems, Inc. in the United States and other countries.
The Free Software Foundation is independent of Sun Microsystems, Inc.  */

#include "jcf.h"
#include "zipfile.h"

int
DEFUN(get_attribute, (jcf),
      JCF *jcf)
{
  uint16 attribute_name = (JCF_FILL (jcf, 6), JCF_readu2 (jcf));
  uint32 attribute_length = JCF_readu4 (jcf);
  uint32 start_pos = JCF_TELL(jcf);
  int name_length;
  unsigned char *name_data;
  JCF_FILL (jcf, (long) attribute_length);
  if (attribute_name <= 0 || attribute_name >= JPOOL_SIZE(jcf))
    return -2;
  if (JPOOL_TAG (jcf, attribute_name) != CONSTANT_Utf8)
    return -2;
  name_length = JPOOL_UTF_LENGTH (jcf, attribute_name);
  name_data = JPOOL_UTF_DATA (jcf, attribute_name);

#ifdef IGNORE_ATTRIBUTE
   if (IGNORE_ATTRIBUTE (jcf, attribute_name, attribute_length))
     {
       JCF_SKIP (jcf, attribute_length);
     }
   else
#endif
#ifdef HANDLE_SOURCEFILE
  if (name_length == 10 && memcmp (name_data, "SourceFile", 10) == 0)
    {
      uint16 sourcefile_index = JCF_readu2 (jcf);
      HANDLE_SOURCEFILE(sourcefile_index);
    }
  else
#endif
#ifdef HANDLE_CONSTANTVALUE
  if (name_length == 13 && memcmp (name_data, "ConstantValue", 13) == 0)
    {
      uint16 constantvalue_index = JCF_readu2 (jcf);
      if (constantvalue_index <= 0 || constantvalue_index >= JPOOL_SIZE(jcf))
	return -2;
      HANDLE_CONSTANTVALUE(constantvalue_index);
    }
  else
#endif
#ifdef HANDLE_CODE_ATTRIBUTE
  if (name_length == 4 && memcmp (name_data, "Code", 4) == 0)
    {
      uint16 j;
      uint16 max_stack ATTRIBUTE_UNUSED = JCF_readu2 (jcf);
      uint16 max_locals ATTRIBUTE_UNUSED = JCF_readu2 (jcf);
      uint32 code_length = JCF_readu4 (jcf);
      uint16 exception_table_length, attributes_count;
      if (code_length + 12 > attribute_length)
	return -1;
      HANDLE_CODE_ATTRIBUTE(max_stack, max_locals, code_length);
      JCF_SKIP (jcf, code_length);
      exception_table_length = JCF_readu2 (jcf);
      if (code_length + 8 * exception_table_length + 12 > attribute_length)
	return -1;
#ifdef HANDLE_EXCEPTION_TABLE
      HANDLE_EXCEPTION_TABLE (jcf->read_ptr, exception_table_length);
#endif
      JCF_SKIP (jcf, 2 * 4 * exception_table_length);
      attributes_count = JCF_readu2 (jcf);
      for (j = 0; j < attributes_count; j++)
	{
	  int code = get_attribute (jcf);
	  if (code != 0)
	    return code;
	}
    }
  else
#endif /* HANDLE_CODE_ATTRIBUTE */
#ifdef HANDLE_EXCEPTIONS_ATTRIBUTE
  if (name_length == 10 && memcmp (name_data, "Exceptions", 10) == 0)
    {
      uint16 count = JCF_readu2 (jcf);
      HANDLE_EXCEPTIONS_ATTRIBUTE (count);
    }
  else
#endif
#ifdef HANDLE_LINENUMBERTABLE_ATTRIBUTE
  if (name_length == 15 && memcmp (name_data, "LineNumberTable", 15) == 0)
    {
      uint16 count = JCF_readu2 (jcf);
      HANDLE_LINENUMBERTABLE_ATTRIBUTE (count);
    }
  else
#endif
#ifdef HANDLE_LOCALVARIABLETABLE_ATTRIBUTE
  if (name_length == 18 && memcmp (name_data, "LocalVariableTable", 18) == 0)
    {
      uint16 count = JCF_readu2 (jcf);
      HANDLE_LOCALVARIABLETABLE_ATTRIBUTE (count);
    }
  else
#endif
    {
#ifdef PROCESS_OTHER_ATTRIBUTE
      PROCESS_OTHER_ATTRIBUTE(jcf, attribute_name, attribute_length);
#else
      JCF_SKIP (jcf, attribute_length);
#endif
    }
  if ((long) (start_pos + attribute_length) != JCF_TELL(jcf))
    return -1;
  return 0;
}

/* Read and handle the pre-amble. */
int
DEFUN(jcf_parse_preamble, (jcf),
      JCF* jcf)
{
  uint32 magic = (JCF_FILL (jcf, 8), JCF_readu4 (jcf));
  uint16 minor_version ATTRIBUTE_UNUSED = JCF_readu2 (jcf);
  uint16 major_version ATTRIBUTE_UNUSED = JCF_readu2 (jcf);
#ifdef HANDLE_MAGIC
  HANDLE_MAGIC (magic, minor_version, major_version);
#endif
  if (magic != 0xcafebabe)
    return -1;
  else
    return 0;
}

/* Read and handle the constant pool.

   Return 0 if OK.
   Return -2 if a bad cross-reference (index of other constant) was seen.
*/
int
DEFUN(jcf_parse_constant_pool, (jcf),
      JCF* jcf)
{
  int i, n;
  JPOOL_SIZE (jcf) = (JCF_FILL (jcf, 2), JCF_readu2 (jcf));
  jcf->cpool.tags = ALLOC (JPOOL_SIZE (jcf));
  jcf->cpool.data = ALLOC (sizeof (jword) * JPOOL_SIZE (jcf));
  jcf->cpool.tags[0] = 0;
#ifdef HANDLE_START_CONSTANT_POOL
  HANDLE_START_CONSTANT_POOL (JPOOL_SIZE (jcf));
#endif
  for (i = 1; i < (int) JPOOL_SIZE (jcf); i++)
    {
      int constant_kind;
       
      /* Make sure at least 9 bytes are available.  This is enough
	 for all fixed-sized constant pool entries (so we don't need many
	 more JCF_FILL calls below), but is is small enough that
	 we are guaranteed to not hit EOF (in a valid .class file). */
      JCF_FILL (jcf, 9);
      constant_kind = JCF_readu (jcf);
      jcf->cpool.tags[i] = constant_kind;
      switch (constant_kind)
	{
	case CONSTANT_String:
	case CONSTANT_Class:
	  jcf->cpool.data[i] = JCF_readu2 (jcf);
	  break;
	case CONSTANT_Fieldref:
	case CONSTANT_Methodref:
	case CONSTANT_InterfaceMethodref:
	case CONSTANT_NameAndType:
	  jcf->cpool.data[i] = JCF_readu2 (jcf);
	  jcf->cpool.data[i] |= JCF_readu2 (jcf) << 16;
	  break;
	case CONSTANT_Integer:
	case CONSTANT_Float:
	  jcf->cpool.data[i] = JCF_readu4 (jcf);
	  break;
	case CONSTANT_Long:
	case CONSTANT_Double:
	  jcf->cpool.data[i] = JCF_readu4 (jcf);
	  i++; /* These take up two spots in the constant pool */
	  jcf->cpool.tags[i] = 0;
	  jcf->cpool.data[i] = JCF_readu4 (jcf);
	  break;
	case CONSTANT_Utf8:
	  n = JCF_readu2 (jcf);
	  JCF_FILL (jcf, n);
#ifdef HANDLE_CONSTANT_Utf8
	  HANDLE_CONSTANT_Utf8(jcf, i, n);
#else
	  jcf->cpool.data[i] = JCF_TELL(jcf) - 2;
	  JCF_SKIP (jcf, n);
#endif
	  break;
	default:
	  return i;
	}
    }
  return 0;
}

/* Read various class flags and numbers. */

void
DEFUN(jcf_parse_class, (jcf),
      JCF* jcf)
{
  int i;
  uint16 interfaces_count;
  JCF_FILL (jcf, 8);
  jcf->access_flags = JCF_readu2 (jcf);
  jcf->this_class = JCF_readu2 (jcf);
  jcf->super_class = JCF_readu2 (jcf);
  interfaces_count = JCF_readu2 (jcf);

#ifdef HANDLE_CLASS_INFO
  HANDLE_CLASS_INFO(jcf->access_flags, jcf->this_class, jcf->super_class, interfaces_count);
#endif

  JCF_FILL (jcf, 2 * interfaces_count);

  /* Read interfaces. */
  for (i = 0; i < interfaces_count; i++)
    {
      uint16 index ATTRIBUTE_UNUSED = JCF_readu2 (jcf);
#ifdef HANDLE_CLASS_INTERFACE
      HANDLE_CLASS_INTERFACE (index);
#endif
    }
}

/* Read fields. */
int
DEFUN(jcf_parse_fields, (jcf),
      JCF* jcf)
{
  int i, j;
  uint16 fields_count;
  JCF_FILL (jcf, 2);
  fields_count = JCF_readu2 (jcf);

#ifdef HANDLE_START_FIELDS
  HANDLE_START_FIELDS (fields_count);
#endif
  for (i = 0; i < fields_count; i++)
    {
      uint16 access_flags = (JCF_FILL (jcf, 8), JCF_readu2 (jcf));
      uint16 name_index = JCF_readu2 (jcf);
      uint16 signature_index = JCF_readu2 (jcf);
      uint16 attribute_count = JCF_readu2 (jcf);
#ifdef HANDLE_START_FIELD
      HANDLE_START_FIELD (access_flags, name_index, signature_index,
		    attribute_count);
#endif
      for (j = 0; j < attribute_count; j++)
	{
	  int code = get_attribute (jcf);
	  if (code != 0)
	    return code;
	}
#ifdef HANDLE_END_FIELD
      HANDLE_END_FIELD ();
#endif
    }
#ifdef HANDLE_END_FIELDS
  HANDLE_END_FIELDS ();
#endif
  return 0;
}

/* Read methods. */

int
DEFUN(jcf_parse_one_method, (jcf),
      JCF* jcf)
{
  int i;
  uint16 access_flags = (JCF_FILL (jcf, 8), JCF_readu2 (jcf));
  uint16 name_index = JCF_readu2 (jcf);
  uint16 signature_index = JCF_readu2 (jcf);
  uint16 attribute_count = JCF_readu2 (jcf);
#ifdef HANDLE_METHOD
  HANDLE_METHOD(access_flags, name_index, signature_index, attribute_count);
#endif
  for (i = 0; i < attribute_count; i++)
    {
      int code = get_attribute (jcf);
      if (code != 0)
	return code;
    }
#ifdef HANDLE_END_METHOD
  HANDLE_END_METHOD ();
#endif
  return 0;
}

int
DEFUN(jcf_parse_methods, (jcf),
      JCF* jcf)
{
  int i;
  uint16 methods_count;
  JCF_FILL (jcf, 2);
  methods_count = JCF_readu2 (jcf);
#ifdef HANDLE_START_METHODS
  HANDLE_START_METHODS (methods_count);
#endif
  for (i = 0; i < methods_count; i++)
    {
      int code = jcf_parse_one_method (jcf);
      if (code != 0)
	return code;
    }
#ifdef HANDLE_END_METHODS
  HANDLE_END_METHODS ();
#endif
  return 0;
}

/* Read attributes. */
int
DEFUN(jcf_parse_final_attributes, (jcf),
      JCF *jcf)
{
  int i;
  uint16 attributes_count = (JCF_FILL (jcf, 2), JCF_readu2 (jcf));
#ifdef START_FINAL_ATTRIBUTES
  START_FINAL_ATTRIBUTES (attributes_count)
#endif
  for (i = 0; i < attributes_count; i++)
    {
      int code = get_attribute (jcf);
      if (code != 0)
	return code;
    }
  return 0;
}

