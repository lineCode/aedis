/* Copyright (c) 2019 - 2020 Marcelo Zimbres Silva (mzimbres at gmail dot com)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <set>
#include <list>
#include <array>
#include <deque>
#include <queue>
#include <vector>
#include <string>
#include <cstdio>
#include <utility>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <iostream>
#include <algorithm>
#include <functional>
#include <type_traits>
#include <string_view>
#include <forward_list>
#include <unordered_set>

#include <boost/asio.hpp>

namespace aedis
{

namespace net = boost::asio;
namespace ip = net::ip;
using tcp = ip::tcp;

namespace resp
{

using buffer = std::string;

struct response {
   std::vector<std::string> res;

   void add(std::string_view s)
      { res.emplace_back(s.data(), std::size(s)); }

   void clear()
      { res.clear(); }

   auto size()
      { return std::size(res); }
};

inline
void make_bulky_item(std::string& to, std::string const& param)
{
   to += "$";
   to += std::to_string(std::size(param));
   to += "\r\n";
   to += param;
   to += "\r\n";
}

inline
void make_header(std::string& to, int size)
{
   to += "*";
   to += std::to_string(size);
   to += "\r\n";
}

struct accumulator {
   auto operator()(std::string a, std::string b) const
   {
      make_bulky_item(a, b);
      return a;
   }

   auto operator()(std::string a, int b) const
   {
      make_bulky_item(a, std::to_string(b));
      return a;
   }

   auto operator()(std::string a, std::pair<std::string, std::string> b) const
   {
      make_bulky_item(a, b.first);
      make_bulky_item(a, b.second);
      return a;
   }

   auto operator()(std::string a, std::pair<int, std::string> b) const
   {
      make_bulky_item(a, std::to_string(b.first));
      make_bulky_item(a, b.second);
      return a;
   }
};

inline
void assemble(std::string& ret, char const* cmd)
{
   make_header(ret, 1);
   make_bulky_item(ret, cmd);
}

template <class Iter>
auto assemble( std::string& ret
             , char const* cmd
             , std::initializer_list<std::string> key
             , Iter begin
             , Iter end
             , int size = 1)
{
   auto const d1 =
      std::distance( std::cbegin(key)
                   , std::cend(key));

   auto const d2 = std::distance(begin, end);

   // Perhaps, we would avoid some copying by passing ret to the
   // functions below instead of declaring a below.
   std::string a;
   make_header(a, 1 + d1 + size * d2);
   make_bulky_item(a, cmd);

   auto b =
      std::accumulate( std::cbegin(key)
                     , std::cend(key)
                     , std::move(a)
                     , accumulator{});

   ret +=
      std::accumulate( begin
                     , end
                     , std::move(b)
                     , accumulator{});
}

inline
void assemble(std::string& ret, char const* cmd, std::string const& key)
{
   std::initializer_list<std::string> dummy;
   assemble(ret, cmd, {key}, std::cbegin(dummy), std::cend(dummy));
}

// Converts a decimal number in ascii format to integer.
inline
std::size_t get_length(char const* p)
{
   std::size_t len = 0;
   while (*p != '\r') {
       len = (10 * len) + (*p - '0');
       p++;
   }
   return len;
}

void print(std::vector<std::string> const& v)
{
   for (auto const& o : v)
     std::cout << o << " ";
   std::cout << std::endl;
}

void print_command_raw(std::string const& data, int n)
{
  for (int i = 0; i < n; ++i) {
    if (data[i] == '\n') {
      std::cout << "\\n";
      continue;
    }
    if (data[i] == '\r') {
      std::cout << "\\r";
      continue;
    }
    std::cout << data[i];
  }
}

// The parser supports up to 5 levels of nested structures. The first
// element in the sizes stack is a sentinel and must be different from
// 1.
template <class AsyncReadStream>
struct parse_op {
   AsyncReadStream& socket;
   resp::buffer* buf = nullptr;
   resp::response* res = nullptr;
   int start = 1;
   int depth = 0;
   int sizes[6] = {2, 1, 1, 1, 1, 1};
   bool bulky = false;

   template <class Self>
   void operator()( Self& self
                  , boost::system::error_code const& ec = {}
                  , std::size_t n = 0)
   {
      switch (start) {
         for (;;) {
            case 1:
            start = 0;
            net::async_read_until( socket
                                 , net::dynamic_buffer(*buf)
                                 , "\r\n"
                                 , std::move(self));
            return; default:

            if (ec || n < 3)
               return self.complete(ec);

            auto str_flag = false;
            if (bulky) {
               res->add({&buf->front(), n - 2});
               --sizes[depth];
            } else {
               if (sizes[depth] != 0) {
                  switch (buf->front()) {
                     case '$':
                     {
                        // We may want to consider not pushing in the vector
                        // but find a way to report nil.
                        if (buf->compare(1, 2, "-1") == 0) {
                           res->add({});
                           --sizes[depth];
                        } else {
                           str_flag = true;
                        }
                     } break;
                     case '+':
                     case '-':
                     case ':':
                     {
                        res->add({&(*buf)[1], n - 3});
                        --sizes[depth];
                     } break;
                     case '*':
                     {
                        sizes[++depth] = get_length(buf->data() + 1);
                     } break;
                     default:
                        assert(false);
                  }
               }
            }

            
            //print_command_raw(*buf, n);
            buf->erase(0, n);

            while (sizes[depth] == 0)
               --sizes[--depth];

            if (depth == 0 && !str_flag) {
               //std::cout << std::endl;
               return self.complete({});
            }

            bulky = str_flag;
         }
      }
   }
};

template <
   class AsyncReadStream,
   class CompletionToken =
      net::default_completion_token_t<typename AsyncReadStream::executor_type>
   >
auto async_read(
   AsyncReadStream& stream,
   resp::buffer& buffer,
   resp::response& res,
   CompletionToken&& token =
      net::default_completion_token_t<typename AsyncReadStream::executor_type>{})
{
   return net::async_compose
      < CompletionToken
      , void(boost::system::error_code)
      >(parse_op<AsyncReadStream> {stream, &buffer, &res},
        token,
        stream);
}

struct pipeline {
   std::string payload;

public:
   void ping()
      { resp::assemble(payload, "PING"); }
   void quit()
      { resp::assemble(payload, "QUIT"); }
   void multi()
      { resp::assemble(payload, "MULTI"); }
   void exec()
      { resp::assemble(payload, "EXEC"); }
   void incr(std::string const& key)
      { resp::assemble(payload, "INCR", key); }
   auto auth(std::string const& pwd)
     { resp::assemble(payload, "AUTH", pwd); }
   auto bgrewriteaof()
     { resp::assemble(payload, "BGREWRITEAOF"); }
   auto role()
     { resp::assemble(payload, "ROLE"); }
   auto bgsave()
     { resp::assemble(payload, "BGSAVE"); }
   auto flushall()
     { resp::assemble(payload, "FLUSHALL"); }
   auto lpop(std::string const& key)
     { resp::assemble(payload, "LPOP", key); }
   auto subscribe(std::string const& key)
     { resp::assemble(payload, "SUBSCRIBE", key); }
   auto unsubscribe(std::string const& key)
     { resp::assemble(payload, "UNSUBSCRIBE", key); }
   auto get(std::string const& key)
     { resp::assemble(payload, "GET", key); }
   void hello(std::string const& key)
      { resp::assemble(payload, "HELLO", key); }
   
   auto sentinel(std::string const& arg, std::string const& name)
   {
      auto par = {name};
      resp::assemble(payload, "SENTINEL", {arg}, std::cbegin(par), std::cend(par));
   }
   
   auto append(std::string const& key, std::string const& msg)
   {
      auto par = {msg};
      resp::assemble(payload, "APPEND", {key}, std::cbegin(par), std::cend(par));
   }
   
   auto bitcount(std::string const& key, int start = 0, int end = -1)
   {
      auto par = {std::to_string(start), std::to_string(end)};
      resp::assemble( payload
   	            , "BITCOUNT"
   		    , {key}
   		    , std::cbegin(par)
   		    , std::cend(par));
   }
   
   template <class Iter>
   auto rpush(std::string const& key, Iter begin, Iter end)
     { resp::assemble(payload, "RPUSH", {key}, begin, end); }
   
   template <class T>
   auto rpush( std::string const& key
             , std::initializer_list<T> v)
     { return rpush(key, std::cbegin(v), std::cend(v)); } 

   template <class T, class Allocator>
   auto rpush( std::string const& key
             , std::vector<T, Allocator> const& v)
     { return rpush(key, std::cbegin(v), std::cend(v)); }
   
   template <class T, class Allocator>
   auto rpush( std::string const& key
             , std::deque<T, Allocator> const& v)
     { return rpush(key, std::cbegin(v), std::cend(v)); } 

   template <class T, std::size_t N>
   auto rpush( std::string const& key
             , std::array<T, N> const& a)
     { return rpush(key, std::cbegin(a), std::cend(a)); }
   
   template <class T, class Allocator>
   auto rpush( std::string const& key
             , std::list<T, Allocator> const& l)
     { return rpush(key, std::cbegin(l), std::cend(l)); }
   
   template <class T, class Allocator>
   auto rpush( std::string const& key
             , std::forward_list<T, Allocator> const& l)
     { return rpush(key, std::cbegin(l), std::cend(l)); }
   
   template <class T, class Compare, class Allocator>
   auto rpush( std::string const& key
             , std::set<T, Compare, Allocator> const& s)
     { return rpush(key, std::cbegin(s), std::cend(s)); }
   
   template <class T, class Compare, class Allocator>
   auto rpush( std::string const& key
             , std::multiset<T, Compare, Allocator> const& s)
     { return rpush(key, std::cbegin(s), std::cend(s)); }
   
   template <class T, class Hash, class KeyEqual, class Allocator>
   auto rpush( std::string const& key
             , std::unordered_set< T, Hash, KeyEqual, Allocator
                                 > const& s)
     { return rpush(key, std::cbegin(s), std::cend(s)); }
   
   template <class T, class Hash, class KeyEqual, class Allocator>
   auto rpush( std::string const& key
             , std::unordered_multiset< T, Hash, KeyEqual, Allocator
                                      > const& s)
     { return rpush(key, std::cbegin(s), std::cend(s)); }
   
   template <class Iter>
   auto lpush(std::string const& key, Iter begin, Iter end)
     { resp::assemble(payload, "LPUSH", {key}, begin, end); }
   
   auto psubscribe(std::initializer_list<std::string> l)
   {
      std::initializer_list<std::string> dummy;
      resp::assemble(payload, "PSUBSCRIBE", l, std::cbegin(dummy), std::cend(dummy));
   }
   
   auto publish(std::string const& key, std::string const& msg)
   {
      auto par = {msg};
      resp::assemble(payload, "PUBLISH", {key}, std::cbegin(par), std::cend(par));
   }
   
   auto set( std::string const& key , std::initializer_list<std::string> args)
     { resp::assemble(payload, "SET", {key}, std::cbegin(args), std::cend(args)); }

   auto hset( std::string const& key, std::initializer_list<std::string> l)
     { resp::assemble(payload, "HSET", {key}, std::cbegin(l), std::cend(l)); }

   template <class Key, class T, class Compare, class Allocator>
   auto hset( std::string const& key, std::map<Key, T, Compare, Allocator> const& m)
     { resp::assemble(payload, "HSET", {key}, std::cbegin(m), std::cend(m), 2); }
   
   auto hincrby(std::string const& key, std::string const& field, int by)
   {
      auto par = {field, std::to_string(by)};
      resp::assemble(payload, "HINCRBY", {key}, std::cbegin(par), std::cend(par));
   }
   
   auto hkeys(std::string const& key)
   {
      auto par = {""};
      resp::assemble(payload, "HKEYS", {key}, std::cbegin(par), std::cend(par));
   }
   
   auto hlen(std::string const& key)
     { resp::assemble(payload, "HLEN", {key}); }
   auto hgetall(std::string const& key)
     { resp::assemble(payload, "HGETALL", {key}); }
   auto hvals(std::string const& key)
     { resp::assemble(payload, "HVALS", {key}); }
   
   auto hget(std::string const& key, std::string const& field)
   {
      auto par = {field};
      resp::assemble(payload, "HGET", {key}, std::cbegin(par), std::cend(par));
   }
   
   auto hmget( std::string const& key
             , std::initializer_list<std::string> fields)
   {
      resp::assemble( payload
   	         , "HMGET"
   		 , {key}
   		 , std::cbegin(fields)
   		 , std::cend(fields));
   }
   
   auto expire(std::string const& key, int secs)
   {
      auto par = {std::to_string(secs)};
      resp::assemble(payload, "EXPIRE", {key}, std::cbegin(par), std::cend(par));
   }
   
   auto zadd(std::string const& key, int score, std::string const& value)
   {
      auto par = {std::to_string(score), value};
      resp::assemble(payload, "ZADD", {key}, std::cbegin(par), std::cend(par));
   }
   
   template <class Key, class T, class Compare, class Allocator>
   auto zadd( std::initializer_list<std::string> key
            , std::map<Key, T, Compare, Allocator> const& m)
     { resp::assemble(payload, "ZADD", key, std::cbegin(m), std::cend(m), 2); }
   
   auto zrange(std::string const& key, int min = 0, int max = -1)
   {
      auto par = {std::to_string(min), std::to_string(max)};
      resp::assemble(payload, "ZRANGE", {key}, std::cbegin(par), std::cend(par));
   }
   
   auto zrangebyscore(std::string const& key, int min, int max)
   {
      auto max_str = std::string {"inf"};
      if (max != -1)
         max_str = std::to_string(max);
   
      auto par = {std::to_string(min) , max_str};
      resp::assemble(payload, "zrangebyscore", {key}, std::cbegin(par), std::cend(par));
   }
   
   auto zremrangebyscore(std::string const& key, int score)
   {
      auto const s = std::to_string(score);
      auto par = {s, s};
      resp::assemble(payload, "ZREMRANGEBYSCORE", {key}, std::cbegin(par), std::cend(par));
   }
   
   auto lrange(std::string const& key, int min = 0, int max = -1)
   {
      auto par = { std::to_string(min) , std::to_string(max) };
      resp::assemble(payload, "LRANGE", {key}, std::cbegin(par), std::cend(par));
   }
   
   auto ltrim(std::string const& key, int min = 0, int max = -1)
   {
      auto par = { std::to_string(min) , std::to_string(max) };
      resp::assemble(payload, "LTRIM", {key}, std::cbegin(par), std::cend(par));
   }
   
   auto del(std::string const& key)
     { resp::assemble(payload, "DEL", key); }
   
   auto llen(std::string const& key)
     { resp::assemble(payload, "LLEN", key); }
};

}

struct instance {
   std::string host;
   std::string port;
   std::string name;
};

inline
auto operator==(instance const& a, instance const& b)
{
  return a.host == b.host
      && a.port == b.port
      && a.name == b.name;
}

// Still on development.
template <class AsyncReadStream>
class sentinel_op2 {
public:
  struct config {
     // A list of redis sentinels e.g. ip1 port1 ip2 port2 ...
     std::vector<std::string> sentinels {"127.0.0.1", "26379"};
     std::string name {"mymaster"};
     std::string role {"master"};
  };

private:
   enum class op_state
   { on_connect
   , on_write
   , on_read
   };

   struct impl {
      AsyncReadStream& stream;
      resp::buffer buffer;
      resp::response res;
      instance* inst;
      op_state opstate {op_state::on_connect};
      std::string cmd;
      config cfg;
      ip::tcp::resolver resv;

      impl(AsyncReadStream& s, config c, instance* i)
      : stream(s)
      , resv(s.get_executor())
      , inst(i)
      , cfg(c)
      {}
   };

   std::shared_ptr<impl> impl_;

public:
   sentinel_op2(AsyncReadStream& stream, config cfg, instance* inst)
   : impl_(std::make_shared<impl>(stream, cfg, inst))
   {
      auto const n = std::size(cfg.sentinels);
      if (n == 0 || (n % 2 != 0))
	 throw std::runtime_error("sentinel_op2: wrong size.");
   }

   template <class Self>
   void operator()( Self& self
                  , boost::system::error_code ec = {}
                  , std::size_t n = 0)
   {
      switch (impl_->opstate) {
      case op_state::on_connect:
      {
	 auto const n = std::size(impl_->cfg.sentinels) / 2;

	 unsigned i = 0;
	 for (i = 0; i < n; ++i) {
	    auto const res = impl_->resv
	       .resolve( impl_->cfg.sentinels[2 * i + 0]
		       , impl_->cfg.sentinels[2 * i + 1]
		       , ec);
	    if (ec)
	       return self.complete(ec);

	    net::connect(impl_->stream, res, ec);
	    if (!ec)
	       break;

	    if (ec && ((2 * (i + 1)) == std::size(impl_->cfg.sentinels)))
	       return self.complete(ec);
	 }

	 // The redis documentation recommends to put the first
	 // sentinel that replies in the start of the list. See
	 // https://redis.io/topics/sentinel-clients
	 //
	 // TODO: The sentinel that responded has to be returned to
	 // the user so he can do what the doc describes above.
	 // Example
	 //
	 //   std::swap(cfg.sentinels[0], cfg.sentinels[2 * i + 0]);
	 //   std::swap(cfg.sentinels[1], cfg.sentinels[2 * i + 1]);

         resp::pipeline p;
         p.sentinel("get-master-addr-by-name", impl_->inst->name);
         impl_->inst->name = impl_->cfg.name;
         impl_->cmd = p.payload;
         impl_->opstate = op_state::on_write;
         net::async_write( impl_->stream
                         , net::buffer(impl_->cmd)
                         , std::move(self));
      } break;
      case op_state::on_write:
      {
         if (ec)
            return self.complete(ec);

         impl_->opstate = op_state::on_read;

         resp::async_read(
	    impl_->stream,
	    impl_->buffer,
	    impl_->res,
	    std::move(self));

      } break;
      case op_state::on_read:
      {
         auto n = std::size(impl_->res.res);
         if (n > 1) {
            impl_->inst->host = impl_->res.res[0];
            impl_->inst->port = impl_->res.res[1];
         }
         self.complete(ec);
      } break;
      default: { }
      }
   }
};

template <
   class AsyncReadStream,
   class CompletionToken =
     net::default_completion_token_t<typename AsyncReadStream::executor_type>
   >
auto async_get_instance2(
  AsyncReadStream& stream,
  typename sentinel_op2<AsyncReadStream>::config cfg,
  instance& inst,
  CompletionToken&& token =
     net::default_completion_token_t<typename AsyncReadStream::executor_type>{})
{
   return net::async_compose
      < CompletionToken
      , void(boost::system::error_code)
      >(sentinel_op2<AsyncReadStream> {stream, cfg, &inst},
        token,
        stream);
}

}
