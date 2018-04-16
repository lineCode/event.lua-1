local event = require "event"
local util = require "util"
local model = require "model"
local protocol = require "protocol"
local http = require "http"
local route = require "route"
local channel = require "channel"
local startup = import "server.startup"
local id_builder = import "module.id_builder"
local agent_handler = import "handler.agent_handler"

model.register_binder("scene_channel","id")
model.register_binder("client","id")
model.register_value("client_manager")


local function client_data(cid,message_id,data,size)
	local user = model.fetch_agent_user_with_cid(cid)
	if not user then
		route.dispatch_client(message_id,data,size,cid)
	else
		route.dispatch_client(message_id,data,size,user)
	end
end

local function client_accept(id,addr)
	model.bind_client_with_id(id,{login = false,addr = addr})
	agent_handler.enter(id,addr)
end

local function client_close(id)
	model.unbind_client_with_id(id)
	agent_handler.leave(id)
end


event.fork(function ()
	startup.run(env.mongodb)

	startup.connect_server("login")
	startup.connect_server("world")
	startup.connect_server("master")

	local client_manager = event.client_manager(1024)
	client_manager:set_callback(client_accept,client_close,client_data)
	local port,reason = client_manager:start("0.0.0.0",0)
	if not port then
		event.breakout(string.format("%s %s",env.name,reason))
		os.exit(1)
	end
	model.set_client_manager(client_manager)

	local result,reason = http.post_master("/apply_id")
	if not result then
		print(reason)
		os.exit(1)
	end
	env.dist_id = result.id
	id_builder:init(env.dist_id)

	local login_channel = model.get_login_channel()
	login_channel:send("module.server_manager","register_agent_server",{ip = "0.0.0.0",port = port})

	local master_channel = model.get_master_channel()
	master_channel:send("module.server_manager","register_agent_server",{ip = "0.0.0.0",port = port})

	local world_channel = model.get_world_channel()
	world_channel:send("module.server_manager","register_agent_server",{ip = "0.0.0.0",port = port})

	event.error("agent start success")
end)
