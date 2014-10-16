--
-- Copyright (c) 2009-2014 Nous Xiong (348944179 at qq dot com)
--
-- Distributed under the Boost Software License, Version 1.0. (See accompanying
-- file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
--
-- See https://github.com/nousxiong/gce for latest version.
--

require("gce")

gce.run_actor(
  function ()
    local sender, args = gce.recv("init", gce.aid())
    local svr_aid = args[1]

  	local echo_num = 10
    for i=1, echo_num do
      gce.send(svr_aid, "echo")
      gce.recv("echo")
    end
    gce.send(svr_aid, "end")
  end)
