local event = require "event"
local util = require "util"
local model = require "model"
local protocol = require "protocol"
local mongo = require "mongo"
local channel = require "channel"

local startup = import "server.startup"
local login_handler = import "handler.login_handler"
local server_handler = import "handler.server_handler"

model.register_value("client_manager")
model.register_binder("channel","name")
model.register_binder("login_info","cid")


local function channel_accept(_,channel)
	print("channel_accept",channel)
end

local function client_data(cid,message_id,data,size)
	route.dispatch_client(cid,message_id,data,size)
end

local function client_accept(cid,addr)
	login_handler.enter(cid,addr)
end

local function client_close(cid)
	login_handler.leave(cid)
end

event.fork(function ()
	startup.run(env.mongodb)

	local ok,reason = event.listen(env.login,4,channel_accept)
	if not ok then
		event.breakout(reason)
		return
	end

	local client_manager = event.client_manager(5000)
	client_manager:set_callback(client_accept,client_close,client_data)
	local ok,reason = client_manager:start("0.0.0.0",1989)
	if not ok then
		event.breakout(reason)
		return
	end
	model.set_client_manager(client_manager)
end)