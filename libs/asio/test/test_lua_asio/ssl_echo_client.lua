--
-- Copyright (c) 2009-2015 Nous Xiong (348944179 at qq dot com)
--
-- Distributed under the Boost Software License, Version 1.0. (See accompanying
-- file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
--
-- See https://github.com/nousxiong/gce for latest version.
--

local gce = require('gce')
local asio = require('asio')

local function verify_cb(preverified, ctx)
  return true
end

gce.actor(
  function ()
    local ec, sender, args, msg, err

    local ecount = 10
    local ssl_opt = asio.ssl_option()
    ssl_opt.verify_file = 'ssl_pem/ca.pem'
    local ssl_ctx = asio.ssl_context(asio.sslv23, ssl_opt)

    ssl_opt.verify_peer = 1
    local skt = asio.ssl_stream(ssl_ctx, ssl_opt)
    skt:set_verify_callback(verify_cb)

    skt:async_connect('127.0.0.1', '23333')
    ec, sender, args = gce.match(asio.as_conn).recv(gce.errcode)
    err = args[1]
    assert (err == gce.err_nil, tostring(err))

    skt:async_handshake(asio.client)
    ec, sender, args = gce.match(asio.as_handshake).recv(gce.errcode)
    err = args[1]
    assert (err == gce.err_nil, tostring(err))

    local str = 'hello!'
    local ty_int = 0
    local length = string.len(str)
    local ch = gce.chunk(str)

    for i=1, ecount do
      local m = gce.message(asio.as_send, length, ch)
      local ms = m:size()
      skt:async_write(m)
      ec, sender, args = gce.match(asio.as_send).recv(ty_int, ch, gce.errcode)
      err = args[3]
      assert (err == gce.err_nil, tostring(err))

      skt:async_read(ms)
      ec, sender, args, msg = gce.match(asio.as_recv).recv(gce.errcode)
      err = args[1]
      assert (err == gce.err_nil, tostring(err))

      args = gce.unpack(msg, ty_int, ch)

      local echo = args[2]
      assert (echo:to_string() == str)
    end

    str = 'bye'
    length = string.len(str)
    ch:from_string(str)
    local m = gce.message(asio.as_send, length, ch)
    skt:async_write(m)
    ec, sender, args = gce.match(asio.as_send).recv(ty_int, ch, gce.errcode)
    err = args[3]
    assert (err == gce.err_nil, tostring(err))
  end)
