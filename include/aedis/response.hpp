/* Copyright (c) 2019 - 2020 Marcelo Zimbres Silva (mzimbres at gmail dot com)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <set>
#include <list>
#include <vector>
#include <string>
#include <numeric>
#include <type_traits>
#include <string_view>
#include <charconv>

template <class Iter>
void print(Iter begin, Iter end)
{
   for (; begin != end; ++begin)
     std::cout << *begin << " ";
   std::cout << std::endl;
}

template <class Range>
void print(Range const& v)
{
   using std::cbegin;
   using std::cend;
   print(cbegin(v), cend(v));
}

namespace aedis { namespace resp {

struct response_noop {
   void select_array(int n) {}
   void select_push(int n) {}
   void select_set(int n) {}
   void select_map(int n) {}
   void select_attribute(int n) {}

   void on_simple_string(std::string_view s) {}
   void on_simple_error(std::string_view s) {}
   void on_number(std::string_view s) {}
   void on_double(std::string_view s) {}
   void on_bool(std::string_view s) {}
   void on_big_number(std::string_view s) {}
   void on_null() {}
   void on_blob_error(std::string_view s = {}) {}
   void on_verbatim_string(std::string_view s = {}) {}
   void on_blob_string(std::string_view s = {}) {}
   void on_streamed_string_part(std::string_view s = {}) {}
   ~response_noop() {}
};

using response = response_noop;

enum class error
{ simple_error
, blob_error
, none
};

class response_base {
private:
   error err_ = error::none;
   bool is_null_ = false;
   std::string err_msg_;

public:
   // Error is dealt with in the base class.
   auto get_error() const noexcept {return err_;}
   auto const& message() const noexcept {return err_msg_;}

   void on_simple_error(std::string_view s)
   {
      err_ = error::simple_error;
      err_msg_ = s;
   }

   void on_blob_error(std::string_view s = {})
   {
      err_ = error::blob_error;
      err_msg_ = s;
   }

   void on_null() {is_null_ = true; }

   // Function derived classes can overwrite.
   virtual void select_push(int n) { throw std::runtime_error("select_push: Has not been overridden."); }
   virtual void select_attribute(int n) { throw std::runtime_error("select_attribute: Has not been overridden."); }

   virtual void select_array(int n) { throw std::runtime_error("select_array: Has not been overridden."); }
   virtual void select_set(int n) { throw std::runtime_error("select_set: Has not been overridden."); }
   virtual void select_map(int n) { throw std::runtime_error("select_map: Has not been overridden."); }

   virtual void on_simple_string(std::string_view s) { throw std::runtime_error("on_simple_string: Has not been overridden."); }
   virtual void on_number(std::string_view s) { throw std::runtime_error("on_number: Has not been overridden."); }
   virtual void on_double(std::string_view s) { throw std::runtime_error("on_double: Has not been overridden."); }
   virtual void on_bool(std::string_view s) { throw std::runtime_error("on_bool: Has not been overridden."); }
   virtual void on_big_number(std::string_view s) { throw std::runtime_error("on_big_number: Has not been overridden."); }
   virtual void on_verbatim_string(std::string_view s = {}) { throw std::runtime_error("on_verbatim_string: Has not been overridden."); }
   virtual void on_blob_string(std::string_view s = {}) { throw std::runtime_error("on_blob_string: Has not been overridden."); }
   virtual void on_streamed_string_part(std::string_view s = {}) { throw std::runtime_error("on_streamed_string_part: Has not been overridden."); }
   virtual ~response_base() {}
};

template <class T>
std::enable_if<std::is_integral<T>::value, void>::type
from_string_view(std::string_view s, T& n)
{
   auto r = std::from_chars(s.data(), s.data() + s.size(), n);
   if (r.ec == std::errc::invalid_argument)
      throw std::runtime_error("from_chars: Unable to convert");
}

void from_string_view(std::string_view s, std::string& r)
   { r = s; }

template <class T>
struct response_number : response_base {
   static_assert(std::is_integral<T>::value);
   T result;
   void on_number(std::string_view s) override
      { from_string_view(s, result); }
};

template<
   class CharT,
   class Traits = std::char_traits<CharT>,
   class Allocator = std::allocator<CharT>>
struct response_basic_string : response_base {
   std::basic_string<CharT, Traits, Allocator> result;

   void on_simple_string(std::string_view s) override { from_string_view(s, result); }
   void on_verbatim_string(std::string_view s) override { from_string_view(s, result); }
   void on_blob_string(std::string_view s) override { from_string_view(s, result); }
};

using response_string = response_basic_string<char>;

template<
   class CharT = char,
   class Traits = std::char_traits<CharT>,
   class Allocator = std::allocator<CharT>>
struct response_blob_string : response_base {
   std::basic_string<CharT, Traits, Allocator> result;
   void on_blob_string(std::string_view s) override
      { from_string_view(s, result); }
};

template<
   class CharT = char,
   class Traits = std::char_traits<CharT>,
   class Allocator = std::allocator<CharT>>
struct response_simple_string : response_base {
   std::basic_string<CharT, Traits, Allocator> result;
   void on_simple_string(std::string_view s) override
      { from_string_view(s, result); }
};

// Big number use strings at the moment as the underlying storage.
template<
   class CharT = char,
   class Traits = std::char_traits<CharT>,
   class Allocator = std::allocator<CharT>>
struct response_big_number : response_base {
   std::basic_string<CharT, Traits, Allocator> result;
   void on_big_number(std::string_view s) override
      { from_string_view(s, result); }
};

// TODO: Use a double instead of string.
template<
   class CharT = char,
   class Traits = std::char_traits<CharT>,
   class Allocator = std::allocator<CharT>>
struct response_double : response_base {
   std::basic_string<CharT, Traits, Allocator> result;
   void on_double(std::string_view s) override
      { from_string_view(s, result); }
};

template <
   class T,
   class Allocator = std::allocator<T>>
struct response_list : response_base {
   std::list<T, Allocator> result;
   void select_array(int n) override { }
   void on_blob_string(std::string_view s) override
   {
      T r;
      from_string_view(s, r);
      result.push_back(std::move(r));
   }
};

template<
   class CharT = char,
   class Traits = std::char_traits<CharT>,
   class Allocator = std::allocator<CharT>>
struct response_verbatim_string : response_base {
   std::basic_string<CharT, Traits, Allocator> result;
   void on_verbatim_string(std::string_view s) override
      { from_string_view(s, result); }
};

template<
   class CharT = char,
   class Traits = std::char_traits<CharT>,
   class Allocator = std::allocator<CharT>>
struct response_streamed_string : response_base {
   std::basic_string<CharT, Traits, Allocator> result;
   void on_streamed_string_part(std::string_view s) override
      { result += s; }
};

template<
   class Key,
   class Compare = std::less<Key>,
   class Allocator = std::allocator<Key>>
class response_set : public response_base {
private:
   void add(std::string_view s)
   {
      Key r;
      from_string_view(s, r);
      result.insert(std::end(result), std::move(r));
   }

public:
   std::set<Key, Compare, Allocator> result;
   void select_set(int n) override { }
   void on_blob_string(std::string_view s) override { add(s); }
   void on_simple_string(std::string_view s) override { add(s); }
};

struct response_bool : response_base {
   bool result;
   void on_bool(std::string_view s) override
   {
      if (std::ssize(s) != 1) {
	 // We can't hadle an error in redis.
	 throw std::runtime_error("Bool has wrong size");
      }

      result = s[0] == 't';
   }
};

template<
   class Key,
   class Compare = std::less<Key>,
   class Allocator = std::allocator<Key>>
struct response_unordered_set : response_base {
   std::set<Key, Compare, Allocator> result;
   void select_array(int n) override { }
   void select_set(int n) override { }
   void on_blob_string(std::string_view s) override
   {
      Key r;
      from_string_view(s, r);
      result.insert(std::end(result), std::move(r));
   }
};

template <class T, class Allocator = std::allocator<T>>
struct response_array : response_base {
private:
   void add(std::string_view s = {})
   {
      T r;
      from_string_view(s, r);
      result.emplace_back(std::move(r));
   }

public:
   std::vector<T, Allocator> result;

   void clear() { result.clear(); }
   auto size() const noexcept { return std::size(result); }

   void select_array(int n) override { }
   void select_push(int n) override { }
   void select_set(int n) override { }
   void select_map(int n) override { }
   void select_attribute(int n) override { }

   void on_simple_string(std::string_view s) override { add(s); }
   void on_number(std::string_view s) override { add(s); }
   void on_double(std::string_view s) override { add(s); }
   void on_bool(std::string_view s) override { add(s); }
   void on_big_number(std::string_view s) override { add(s); }
   void on_verbatim_string(std::string_view s = {}) override { add(s); }
   void on_blob_string(std::string_view s = {}) override { add(s); }
   void on_streamed_string_part(std::string_view s = {}) override { add(s); }
};

template <class T, class Allocator = std::allocator<T>>
using response_flat_map = response_array<T, Allocator>;

template <class T, class Allocator = std::allocator<T>>
using response_flat_set = response_array<T, Allocator>;

} // resp
} // aedis
