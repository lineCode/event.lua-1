local event = require "event"
local cjson = require "cjson"
local model = require "model"
local util = require "util"

local database_object = import "database_object"


cls_agent_user = database_object.cls_database:inherit("agent_user","uid","cid")

function __init__(self)
	self.cls_agent_user:save_field("user_info")
	self.cls_agent_user:save_field("scene_info")
end


function cls_agent_user:create(cid,uid)
	self.cid = cid
	self.uid = uid
	model.bind_agent_user_with_uid(uid,self)
end

function cls_agent_user:destroy()
	model.unbind_agent_user_with_uid(uid,self)
end

function cls_agent_user:db_index()
	return {uid = self.uid}
end

function cls_agent_user:enter_game()
	local world = model.get_world_channel()
	world:send("handler.world_handler","enter_world",{uid = self.uid})

	local scene_master = model.get_master_channel()
	scene_master:send("handler.scene_master_handler","enter_scene",{scene_id = self.scene_info.id,pos = self.scene_info.pos})
end

function cls_agent_user:leave_game()
	local world = model.get_world_channel()
	world:call("handler.world_handler","leave_world",{uid = self.uid})

	local scene_master = model.get_master_channel()
	scene_master:call("handler.scene_master_handler","leave_scene",{uid = self.uid})
end

function cls_agent_user:transfer_scene(scene_id)
	local scene_master = model.get_master_channel()
	scene_master:send("handler.scene_master_handler","enter_scene",{scene_id = scene_id})
end

function cls_agent_user:user_enter_scene(scene_id,scene_uid,server_id,server_addr)
	self.scene_server = server_id
	self.scene_server_addr = server_addr
end

function cls_agent_user:forward_scene(message_id,message)
	self:send_scene("handler.scene_handler","forward",{uid = self.uid,message_id = mesasge_id,mesasge = message})
end

function cls_agent_user:send_scene(file,method,args)
	local scene_channel = model.fetch_scene_channel_with_id(self.scene_server)
	if not scene_channel then
		local channel,reason = event.connect(self.scene_server_addr,4,true)
		if not channel then
			print(string.format("connect scene server:%d faield:%s",self.scene_server,reason))
			return
		end
		model.bind_scene_channel_with_id(self.scene_server,channel)
		scene_channel = channel
	end
	scene_channel:send(file,method,args)
end