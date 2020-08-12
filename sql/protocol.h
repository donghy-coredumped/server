#ifndef PROTOCOL_INCLUDED
#define PROTOCOL_INCLUDED

/* Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
   Copyright (c) 2020, MariaDB Corporation.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335  USA */

#ifdef USE_PRAGMA_INTERFACE
#pragma interface			/* gcc class implementation */
#endif

#include "sql_error.h"
#include "my_decimal.h"                         /* my_decimal */
#include "sql_type.h"

class i_string;
class Field;
class Send_field;
class THD;
class Item_param;
struct TABLE_LIST;
typedef struct st_mysql_field MYSQL_FIELD;
typedef struct st_mysql_rows MYSQL_ROWS;

class Protocol
{
protected:
  String *packet;
  /* Used by net_store_data() for charset conversions */
  String *convert;
  uint field_pos;
#ifndef DBUG_OFF
  const Type_handler **field_handlers;
  bool valid_handler(uint pos, protocol_send_type_t type) const
  {
    return field_handlers == 0 ||
           field_handlers[field_pos]->protocol_send_type() == type;
  }
#endif
  uint field_count;
#ifndef EMBEDDED_LIBRARY
  bool net_store_data(const uchar *from, size_t length);
  bool net_store_data_cs(const uchar *from, size_t length,
                      CHARSET_INFO *fromcs, CHARSET_INFO *tocs);
#else
  virtual bool net_store_data(const uchar *from, size_t length);
  virtual bool net_store_data_cs(const uchar *from, size_t length,
                      CHARSET_INFO *fromcs, CHARSET_INFO *tocs);
  char **next_field;
  MYSQL_FIELD *next_mysql_field;
  MEM_ROOT *alloc;
#endif
  bool needs_conversion(CHARSET_INFO *fromcs,
                        my_repertoire_t from_repertoire,
                        CHARSET_INFO *tocs) const
  {
    // 'tocs' is set 0 when client issues SET character_set_results=NULL
    return tocs && !my_charset_same(fromcs, tocs) &&
           fromcs != &my_charset_bin &&
           tocs != &my_charset_bin &&
           (from_repertoire != MY_REPERTOIRE_ASCII ||
           (fromcs->state & MY_CS_NONASCII) ||
           (tocs->state & MY_CS_NONASCII));
  }
  /* 
    The following two are low-level functions that are invoked from
    higher-level store_xxx() funcs.  The data is stored into this->packet.
  */
  bool store_string_aux(const char *from, size_t length,
                        CHARSET_INFO *fromcs,
                        my_repertoire_t from_repertoire,
                        CHARSET_INFO *tocs);

  virtual bool send_ok(uint server_status, uint statement_warn_count,
                       ulonglong affected_rows, ulonglong last_insert_id,
                       const char *message);

  virtual bool send_eof(uint server_status, uint statement_warn_count);

  virtual bool send_error(uint sql_errno, const char *err_msg,
                          const char *sql_state);

  CHARSET_INFO *character_set_results() const;

public:
  THD	 *thd;
  Protocol(THD *thd_arg) { init(thd_arg); }
  virtual ~Protocol() {}
  void init(THD* thd_arg);

  enum { SEND_NUM_ROWS= 1, SEND_EOF= 2 };
  virtual bool send_result_set_metadata(List<Item> *list, uint flags);
  bool send_list_fields(List<Field> *list, const TABLE_LIST *table_list);
  bool send_result_set_row(List<Item> *row_items);

  bool store(I_List<i_string> *str_list);
  /* This will be deleted in future commit */
  bool store(const char *from, CHARSET_INFO *cs)
  {
    return store_string_or_null(from, cs);
  }
  bool store_string_or_null(const char *from, CHARSET_INFO *cs);
  bool store_warning(const char *from, size_t length);
  String *storage_packet() { return packet; }
  inline void free() { packet->free(); }
  virtual bool write();
  inline  bool store(int from)
  { return store_long((longlong) from); }
  inline  bool store(uint32 from)
  { return store_long((longlong) from); }
  inline  bool store(longlong from)
  { return store_longlong((longlong) from, 0); }
  inline  bool store(ulonglong from)
  { return store_longlong((longlong) from, 1); }
  inline bool store(String *str)
  { return store((char*) str->ptr(), str->length(), str->charset()); }
  inline bool store(const LEX_CSTRING *from, CHARSET_INFO *cs)
  {
    return store(from->str, from->length, cs);
  }

  virtual bool prepare_for_send(uint num_columns)
  {
    field_count= num_columns;
    return 0;
  }
  virtual bool flush();
  virtual void end_partial_result_set(THD *thd);
  virtual void prepare_for_resend()=0;

  virtual bool store_null()=0;
  virtual bool store_tiny(longlong from)=0;
  virtual bool store_short(longlong from)=0;
  virtual bool store_long(longlong from)=0;
  virtual bool store_longlong(longlong from, bool unsigned_flag)=0;
  virtual bool store_decimal(const my_decimal *)=0;
  virtual bool store_str(const char *from, size_t length,
                         CHARSET_INFO *fromcs,
                         my_repertoire_t from_repertoire,
                         CHARSET_INFO *tocs)=0;
  virtual bool store(float from, uint32 decimals, String *buffer)=0;
  virtual bool store(double from, uint32 decimals, String *buffer)=0;
  virtual bool store(MYSQL_TIME *time, int decimals)=0;
  virtual bool store_date(MYSQL_TIME *time)=0;
  virtual bool store_time(MYSQL_TIME *time, int decimals)=0;
  virtual bool store(Field *field)=0;

  // Various useful wrappers for the virtual store*() methods.
  // Backward wrapper for store_str()
  inline bool store(const char *from, size_t length, CHARSET_INFO *cs,
                    my_repertoire_t repertoire= MY_REPERTOIRE_UNICODE30)
  {
    return store_str(from, length, cs, repertoire, character_set_results());
  }
  inline bool store_lex_cstring(const LEX_CSTRING &s,
                                CHARSET_INFO *fromcs,
                                my_repertoire_t from_repertoire,
                                CHARSET_INFO *tocs)
  {
    return store_str(s.str, (uint) s.length, fromcs, from_repertoire, tocs);
  }
  inline bool store_binary_string(Binary_string *str,
                                  CHARSET_INFO *fromcs,
                                  my_repertoire_t from_repertoire)
  {
    return store_str(str->ptr(), (uint) str->length(), fromcs, from_repertoire,
                     &my_charset_bin);
  }
  bool store_ident(const LEX_CSTRING &s,
                   my_repertoire_t repertoire= MY_REPERTOIRE_UNICODE30)
  {
    return store_lex_cstring(s, system_charset_info, repertoire,
                             character_set_results());
  }
  // End of wrappers

  virtual bool send_out_parameters(List<Item_param> *sp_params)=0;
#ifdef EMBEDDED_LIBRARY
  bool begin_dataset();
  bool begin_dataset(THD *thd, uint numfields);
  virtual void remove_last_row() {}
#else
  void remove_last_row() {}
#endif
  enum enum_protocol_type
  {
    /*
      Before adding a new type, please make sure
      there is enough storage for it in Query_cache_query_flags.
    */
    PROTOCOL_TEXT= 0, PROTOCOL_BINARY= 1, PROTOCOL_LOCAL= 2,
    PROTOCOL_DISCARD= 3 /* Should be last, not used by Query_cache */
  };
  virtual enum enum_protocol_type type()= 0;

  void end_statement();

  friend int send_answer_1(Protocol *protocol, String *s1, String *s2,
                           String *s3);
  friend int send_header_2(Protocol *protocol, bool for_category);
};


/** Class used for the old (MySQL 4.0 protocol). */

class Protocol_text final :public Protocol
{
  bool store_numeric_string_aux(const char *from, size_t length);
public:
  Protocol_text(THD *thd_arg, ulong prealloc= 0)
   :Protocol(thd_arg)
  {
    if (prealloc)
      packet->alloc(prealloc);
  }
  void prepare_for_resend() override;
  bool store_null() override;
  bool store_tiny(longlong from) override;
  bool store_short(longlong from) override;
  bool store_long(longlong from) override;
  bool store_longlong(longlong from, bool unsigned_flag) override;
  bool store_decimal(const my_decimal *) override;
  bool store_str(const char *from, size_t length,
                 CHARSET_INFO *fromcs,
                 my_repertoire_t from_repertoire,
                 CHARSET_INFO *tocs) override;
  bool store(MYSQL_TIME *time, int decimals) override;
  bool store_date(MYSQL_TIME *time) override;
  bool store_time(MYSQL_TIME *time, int decimals) override;
  bool store(float nr, uint32 decimals, String *buffer) override;
  bool store(double from, uint32 decimals, String *buffer) override;
  bool store(Field *field) override;

  bool send_out_parameters(List<Item_param> *sp_params) override;
#ifdef EMBEDDED_LIBRARY
  void remove_last_row() override;
#endif
  bool store_field_metadata(const THD *thd, const Send_field &field,
                            CHARSET_INFO *charset_for_protocol,
                            uint pos);
  bool store_field_metadata(THD *thd, Item *item, uint pos);
  bool store_field_metadata_for_list_fields(const THD *thd, Field *field,
                                            const TABLE_LIST *table_list,
                                            uint pos);
  enum enum_protocol_type type() override { return PROTOCOL_TEXT; };
};


class Protocol_binary final :public Protocol
{
private:
  uint bit_fields;
public:
  Protocol_binary(THD *thd_arg) :Protocol(thd_arg) {}
  bool prepare_for_send(uint num_columns) override;
  void prepare_for_resend() override;
#ifdef EMBEDDED_LIBRARY
  bool write() override;
  bool net_store_data(const uchar *from, size_t length) override;
  bool net_store_data_cs(const uchar *from, size_t length,
                         CHARSET_INFO *fromcs, CHARSET_INFO *tocs) override;
#endif
  bool store_null() override;
  bool store_tiny(longlong from) override;
  bool store_short(longlong from) override;
  bool store_long(longlong from) override;
  bool store_longlong(longlong from, bool unsigned_flag) override;
  bool store_decimal(const my_decimal *) override;
  bool store_str(const char *from, size_t length,
                 CHARSET_INFO *fromcs,
                 my_repertoire_t from_repertoire,
                 CHARSET_INFO *tocs) override;
  bool store(MYSQL_TIME *time, int decimals) override;
  bool store_date(MYSQL_TIME *time) override;
  bool store_time(MYSQL_TIME *time, int decimals) override;
  bool store(float nr, uint32 decimals, String *buffer) override;
  bool store(double from, uint32 decimals, String *buffer) override;
  bool store(Field *field) override;

  bool send_out_parameters(List<Item_param> *sp_params) override;

  enum enum_protocol_type type() override { return PROTOCOL_BINARY; };
};


/*
  A helper for "ANALYZE $stmt" which looks a real network procotol but doesn't
  write results to the network.

  At first glance, class select_send looks like a more appropriate place to
  implement the "write nothing" hook. This is not true, because
    - we need to evaluate the value of every item, and do it the way
      select_send does it (i.e. call item->val_int() or val_real() or...)
    - select_send::send_data() has some other code, like telling the storage
      engine that the row can be unlocked. We want to keep that also.
  as a result, "ANALYZE $stmt" uses a select_send_analyze which still uses 
  select_send::send_data() & co., and also uses  Protocol_discard object.
*/

class Protocol_discard final : public Protocol
{
public:
  Protocol_discard(THD *thd_arg) : Protocol(thd_arg) {}
  bool write() override { return 0; }
  bool send_result_set_metadata(List<Item> *, uint) override { return 0; }
  bool send_eof(uint, uint) override { return 0; }
  void prepare_for_resend() override { IF_DBUG(field_pos= 0,); }
  bool send_out_parameters(List<Item_param> *sp_params) override { return false; }
  
  /* 
    Provide dummy overrides for any storage methods so that we
    avoid allocating and copying of data
  */
  bool store_null() override { return false; }
  bool store_tiny(longlong) override { return false; }
  bool store_short(longlong) override { return false; }
  bool store_long(longlong) override { return false; }
  bool store_longlong(longlong, bool) override { return false; }
  bool store_decimal(const my_decimal *) override { return false; }
  bool store_str(const char *, size_t, CHARSET_INFO *, my_repertoire_t,
                 CHARSET_INFO *) override
  {
    return false;
  }
  bool store(MYSQL_TIME *, int) override { return false; }
  bool store_date(MYSQL_TIME *) override { return false; }
  bool store_time(MYSQL_TIME *, int) override { return false; }
  bool store(float, uint32, String *) override { return false; }
  bool store(double, uint32, String *) override { return false; }
  bool store(Field *) override { return false; }
  enum enum_protocol_type type() override { return PROTOCOL_DISCARD; };
};


void send_warning(THD *thd, uint sql_errno, const char *err=0);
bool net_send_error(THD *thd, uint sql_errno, const char *err,
                    const char* sqlstate);
void net_send_progress_packet(THD *thd);
uchar *net_store_data(uchar *to,const uchar *from, size_t length);
uchar *net_store_data(uchar *to,int32 from);
uchar *net_store_data(uchar *to,longlong from);

#endif /* PROTOCOL_INCLUDED */
