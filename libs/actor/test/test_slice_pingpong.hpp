﻿///
/// Copyright (c) 2009-2014 Nous Xiong (348944179 at qq dot com)
///
/// Distributed under the Boost Software License, Version 1.0. (See accompanying
/// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
///
/// See https://github.com/nousxiong/gce for latest version.
///

namespace gce
{
class slice_pingpong_ut
{
static std::size_t const msg_size = 100000;
public:
  static void run()
  {
    std::cout << "slice_pingpong_ut begin." << std::endl;
    test();
    std::cout << "slice_pingpong_ut end." << std::endl;
  }

private:
  static void pong_actor(actor<stackful>& self)
  {
    message msg;
    while (true)
    {
      aid_t sender = self.recv(msg);
      if (msg.get_type() == 2)
      {
        break;
      }
      else
      {
        self.send(sender, msg);
      }
    }
  }

  static void test()
  {
    try
    {
      attributes attrs;
      attrs.thread_num_ = 2;
      context ctx(attrs);

      actor<nonblocked> ping = spawn<nonblocked>(ctx);
      
      aid_t pong_id = 
        spawn(
          ctx,
          boost::bind(
            &slice_pingpong_ut::pong_actor, _1
            )
          );

      boost::timer::auto_cpu_timer t;
      message m(1);
      for (std::size_t i=0; i<msg_size; ++i)
      {
        ping.send(pong_id, m);
        ping.recv(m);
      }
      send(ping, pong_id, 2);
    }
    catch (std::exception& ex)
    {
      std::cerr << ex.what() << std::endl;
    }
  }
};
}

