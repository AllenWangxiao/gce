﻿///
/// Copyright (c) 2009-2014 Nous Xiong (348944179 at qq dot com)
///
/// Distributed under the Boost Software License, Version 1.0. (See accompanying
/// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
///
/// See https://github.com/nousxiong/gce for latest version.
///

#include <gce/actor/actor.hpp>
#include <gce/actor/detail/cache_pool.hpp>
#include <gce/actor/mixin.hpp>
#include <gce/actor/detail/mailbox.hpp>
#include <gce/actor/message.hpp>
#include <gce/detail/scope.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>
#include <boost/variant/get.hpp>

namespace gce
{
///----------------------------------------------------------------------------
actor::actor(detail::actor_attrs attrs)
  : basic_actor(attrs.cache_match_size_)
  , stat_(ready)
  , stack_size_(make_stack_size(attrs.stack_scale_type_))
  , self_(*this)
  , user_(0)
  , f_(GCE_CACHE_ALIGNED_NEW(func_t), detail::cache_aligned_deleter())
  , recving_(false)
  , responsing_(false)
  , tmr_(*attrs.ctx_->get_io_service())
  , tmr_sid_(0)
  , yld_(0)
{
}
///----------------------------------------------------------------------------
actor::~actor()
{
  stat_ = closed;
  if (yld_cb_)
  {
    resume(detail::actor_normal);
  }
}
///----------------------------------------------------------------------------
aid_t actor::recv(message& msg, match const& mach)
{
  aid_t sender;
  detail::recv_t rcv;

  if (!mb_->pop(rcv, msg, mach.match_list_))
  {
    seconds_t tmo = mach.timeout_;
    if (tmo > zero)
    {
      detail::scope_flag<bool> scp(recving_);
      if (tmo < infin)
      {
        /// 超时计时器
        start_recv_timer(tmo);
      }
      curr_match_ = mach;
      detail::actor_code ac = yield();
      if (ac == detail::actor_timeout)
      {
        return sender;
      }

      rcv = recving_rcv_;
      msg = recving_msg_;
      recving_rcv_ = detail::recv_t();
      recving_msg_ = message();
    }
    else
    {
      return sender;
    }
  }

  if (aid_t* aid = boost::get<aid_t>(&rcv))
  {
    sender = *aid;
  }
  else if (detail::request_t* req = boost::get<detail::request_t>(&rcv))
  {
    sender = req->get_aid();
  }
  else if (detail::exit_t* ex = boost::get<detail::exit_t>(&rcv))
  {
    sender = ex->get_aid();
  }

  return sender;
}
///----------------------------------------------------------------------------
void actor::send(aid_t recver, message const& m)
{
  detail::pack* pk = base_type::alloc_pack(user_.get());
  pk->tag_ = get_aid();
  pk->recver_ = recver;
  pk->msg_ = m;

  basic_actor* a = recver.get_actor_ptr();
  a->on_recv(pk);
}
///----------------------------------------------------------------------------
response_t actor::request(aid_t target, message const& m)
{
  aid_t sender = get_aid();
  response_t res(new_request(), sender);
  detail::request_t req(res.get_id(), sender);

  detail::pack* pk = base_type::alloc_pack(user_.get());
  pk->tag_ = req;
  pk->recver_ = target;
  pk->msg_ = m;

  basic_actor* a = target.get_actor_ptr();
  a->on_recv(pk);
  return res;
}
///----------------------------------------------------------------------------
void actor::reply(aid_t recver, message const& m)
{
  basic_actor* a = recver.get_actor_ptr();
  detail::request_t req;
  detail::pack* pk = base_type::alloc_pack(user_.get());
  if (mb_->pop(recver, req))
  {
    response_t res(req.get_id(), get_aid());
    pk->tag_ = res;
    pk->recver_ = recver;
    pk->msg_ = m;
  }
  else
  {
    pk->tag_ = get_aid();
    pk->recver_ = recver;
    pk->msg_ = m;
  }
  a->on_recv(pk);
}
///----------------------------------------------------------------------------
aid_t actor::recv(response_t res, message& msg, seconds_t tmo)
{
  aid_t sender;

  if (!mb_->pop(res, msg))
  {
    if (tmo > zero)
    {
      detail::scope_flag<bool> scp(responsing_);
      if (tmo < infin)
      {
        /// 超时计时器
        start_recv_timer(tmo);
      }
      detail::actor_code ac = yield();
      if (ac == detail::actor_timeout)
      {
        return sender;
      }

      res = recving_res_;
      msg = recving_msg_;
      recving_res_ = response_t();
      recving_msg_ = message();
    }
    else
    {
      return sender;
    }
  }

  sender = res.get_aid();
  return sender;
}
///----------------------------------------------------------------------------
void actor::wait(seconds_t dur)
{
  if (dur < infin)
  {
    start_recv_timer(dur);
  }
  yield();
}
///----------------------------------------------------------------------------
void actor::link(aid_t target)
{
  base_type::link(detail::link_t(linked, target), user_.get());
}
///----------------------------------------------------------------------------
void actor::monitor(aid_t target)
{
  base_type::link(detail::link_t(monitored, target), user_.get());
}
///----------------------------------------------------------------------------
detail::cache_pool* actor::get_cache_pool()
{
  return user_.get();
}
///----------------------------------------------------------------------------
yield_t actor::get_yield()
{
  BOOST_ASSERT(yld_);
  return *yld_;
}
///----------------------------------------------------------------------------
void actor::start()
{
  strand_t* snd = user_->get_strand();
  if (!yld_cb_)
  {
    boost::asio::spawn(
      *snd,
      boost::bind(
        &actor::run, this, _1
        ),
      boost::coroutines::attributes(stack_size_)
      );
  }

  snd->dispatch(boost::bind(&actor::begin_run, this));
}
///----------------------------------------------------------------------------
void actor::init(
  detail::cache_pool* user, detail::cache_pool* owner,
  actor_func_t const& f,
  aid_t link_tgt
  )
{
  BOOST_ASSERT_MSG(stat_ == ready, "actor 状态不正确");
  user_ = user;
  owner_ = owner;
  *f_ = f;
  base_type::update_aid();

  if (link_tgt)
  {
    base_type::add_link(link_tgt);
  }
}
///----------------------------------------------------------------------------
void actor::on_free()
{
  base_type::on_free();

  stat_ = ready;
  f_->clear();
  curr_match_.clear();

  recving_ = false;
  responsing_ = false;
  recving_rcv_ = detail::recv_t();
  recving_res_ = response_t();
  recving_msg_ = message();
}
///----------------------------------------------------------------------------
void actor::on_recv(detail::pack* pk)
{
  strand_t* snd = user_->get_strand();
  snd->dispatch(
    boost::bind(
      &actor::handle_recv, this, pk
      )
    );
}
///----------------------------------------------------------------------------
std::size_t actor::make_stack_size(stack_scale_type stack_scale)
{
  std::size_t min_size =
    boost::coroutines::stack_allocator::minimum_stacksize();
  std::size_t size =
    boost::coroutines::stack_allocator::default_stacksize() /
    (std::size_t)stack_scale;

  if (size < min_size)
  {
    size = min_size;
  }
  return size;
}
///----------------------------------------------------------------------------
void actor::run(yield_t yld)
{
  yld_ = &yld;
  yield();

  if (stat_ == closed)
  {
    return;
  }

  do
  {
    try
    {
      detail::scope scp(boost::bind(&actor::stopped, this));
      stat_ = on;
      (*f_)(self_);
    }
    catch (boost::coroutines::detail::forced_unwind const&)
    {
      throw;
    }
    catch (...)
    {
    }
  }
  while (stat_ == ready);
}
///----------------------------------------------------------------------------
void actor::begin_run()
{
  resume(detail::actor_normal);
}
///----------------------------------------------------------------------------
void actor::resume(detail::actor_code ac)
{
  detail::scope scp(boost::bind(&actor::free_self, this));
  BOOST_ASSERT(yld_cb_);
  yld_cb_(ac);
  if (stat_ != off)
  {
    scp.reset();
  }
}
///----------------------------------------------------------------------------
detail::actor_code actor::yield()
{
  BOOST_ASSERT(yld_);
  boost::asio::detail::async_result_init<
    yield_t, void (detail::actor_code)> init(
      BOOST_ASIO_MOVE_CAST(yield_t)(*yld_));

  yld_cb_ = boost::bind<void>(init.handler, _1);
  return init.result.get();
}
///----------------------------------------------------------------------------
void actor::free_self()
{
  base_type::send_exit(exit_normal, user_.get());
  base_type::update_aid();
  user_->free_actor(owner_.get(), this);
}
///----------------------------------------------------------------------------
void actor::stopped()
{
  stat_ = off;
  yield();
}
///----------------------------------------------------------------------------
void actor::start_recv_timer(seconds_t dur)
{
  strand_t* snd = user_->get_strand();
  tmr_.expires_from_now(dur);
  tmr_.async_wait(
    snd->wrap(
      boost::bind(
        &actor::handle_recv_timeout, this,
        boost::asio::placeholders::error, ++tmr_sid_
        )
      )
    );
}
///----------------------------------------------------------------------------
void actor::handle_recv_timeout(errcode_t const& ec, std::size_t tmr_sid)
{
  if (!ec && tmr_sid == tmr_sid_)
  {
    resume(detail::actor_timeout);
  }
}
///----------------------------------------------------------------------------
void actor::handle_recv(detail::pack* pk)
{
  detail::scope scp(boost::bind(&basic_actor::dealloc_pack, user_.get(), pk));
  if (check(pk->recver_))
  {
    bool is_response = false;

    if (aid_t* aid = boost::get<aid_t>(&pk->tag_))
    {
      mb_->push(*aid, pk->msg_);
    }
    else if (detail::request_t* req = boost::get<detail::request_t>(&pk->tag_))
    {
      mb_->push(*req, pk->msg_);
    }
    else if (detail::link_t* link = boost::get<detail::link_t>(&pk->tag_))
    {
      add_link(link->get_aid());
      return;
    }
    else if (detail::exit_t* ex = boost::get<detail::exit_t>(&pk->tag_))
    {
      mb_->push(*ex, pk->msg_);
      base_type::remove_link(ex->get_aid());
    }
    else if (response_t* res = boost::get<response_t>(&pk->tag_))
    {
      is_response = true;
      mb_->push(*res, pk->msg_);
    }

    if (
      (recving_ && !is_response) ||
      (responsing_ && is_response)
      )
    {
      if (recving_ && !is_response)
      {
        bool ret = mb_->pop(recving_rcv_, recving_msg_, curr_match_.match_list_);
        if (!ret)
        {
          return;
        }
        curr_match_.clear();
      }

      if (responsing_ && is_response)
      {
        bool ret = mb_->pop(recving_res_, recving_msg_);
        if (!ret)
        {
          return;
        }
      }

      ++tmr_sid_;
      errcode_t ec;
      tmr_.cancel(ec);
      resume(detail::actor_normal);
    }
  }
  else
  {
    if (detail::link_t* link = boost::get<detail::link_t>(&pk->tag_))
    {
      /// send actor exit msg
      send(link->get_aid(), message(exit_already));
    }
  }
}
///----------------------------------------------------------------------------
}