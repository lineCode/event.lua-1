local event = require "event"
local util = require "util"
local object_mgr = import "object_manager"

class_ctx = class_ctx or {}

object_ctx = object_ctx or {}

counter = 0

cls_base = { __name = "root", __parent = nil, __childs = {}}

class_ctx[cls_base.__name] = cls_base

local function clean_cls_func(cls)
	for name in pairs(cls.__childs) do
		local child_cls = class_ctx[name]
		for method in pairs(child_cls.__method) do
			child_cls[method] = nil
		end
		child_cls.__method = {}
		
		clean_cls_func(child_cls)
	end
end

local function reset_object_meta(name)
	local cls = class_ctx[name]
	local object_set = object_ctx[name]
	if not object_set then
		return
	end
	for obj in pairs(object_set) do
		setmetatable(obj,{__index = cls, __tostring = function ()
			return obj:toString()
		end})
	end
end

function cls_base:inherit(name,...)
	local parent = class_ctx[self.__name]

	local old_cls = class_ctx[name]

	local cls = {}
	cls.__name = name
	cls.__parent = parent.__name
	cls.__childs = {}
	cls.__save_fields = {}
	cls.__method = {}

	assert(name ~= parent.__name)

	local meta = { __index = function (obj,method)
		local func = parent[method]
		cls.__method[method] = true
		cls[method] = func
		return func
	end}

	local cls = setmetatable(cls,meta)
	
	class_ctx[name] = cls

	if old_cls ~= nil then
		--热更
		clean_cls_func(old_cls)

		for name in pairs(old_cls.__childs) do
			local child_cls = class_ctx[name]
			local child_cls = setmetatable(child_cls,{ __index = function (obj,method)
				local func = cls[method]
				child_cls.__method[method] = true
				child_cls[method] = func
				return func
			end})
		end

		reset_object_meta(name)
	else
		if select("#",...) > 0 then
			object_mgr.register(name,...)
		end
	end
	parent.__childs[name] = true

	return cls
end

function cls_base:get_type()
	return self.__name
end

local function new_object(self,obj)
	counter = counter + 1

	obj.__uid = counter

	setmetatable(obj,{__index = self, __tostring = function ()
		return obj:tostring()
	end})

	local object_type = self:get_type()
	local object_set = object_ctx[object_type]
	if object_set == nil then
		object_set = setmetatable({},{__mode = "k"})
		object_ctx[object_type] = object_set
	end

	object_set[obj] = {time = os.time()}
end


function cls_base:new(...)
	local obj = {__event = {}}
	local self = class_ctx[self.__name]
	new_object(self,obj)

	obj:create(...)
	obj:init()

	return obj
end

function cls_base:instance_from(data)
	local object_type = self:get_type()
	local class = class_ctx[object_type]
	local object = {__event = {}}
	new_object(class,object)
	for k,v in pairs(data) do
		object[k] = v
	end
	object:init()
	return object
end

function cls_base:release()
	self:destroy()
end

--子类重写
function cls_base:create(...)

end

--子类重写
function cls_base:init()

end

--子类重写
function cls_base:destroy()

end

--子类重写,返回保存数据库的索引
function cls_base:save_index()
	return {id = self.__uid}
end

--子类重写
function cls_base:tostring()
	local time = object_ctx[self:get_type()][self].time
	local str = {}
	table.insert(str,string.format("obj.__uid:%d",self.__uid))
	table.insert(str,string.format("time:%s",os.date("%m-%d %H:%M:%S",math.floor(time/100))))
	return table.concat(str,",")
end

function cls_base:pack()
	return table.tostring(self)
end

function cls_base:unpack(...)
	local data = table.decode(...)
	return self:instance_from(data)
end



class = {}

function class.new(name,...)
	local cls = class.get(name)
	return cls:new(...)
end

function class.instance_from(name,data)
	local cls = class.get(name)
	return cls:instance_from(data)
end

function class.get(name)
	return class_ctx[name]
end

function class.detect_leak()
	collectgarbage("collect")

	local leak_obj = {}

	for name in pairs(class_ctx) do
		local object_set = object_ctx[name]
		if object_set then
			for weak_obj,info in pairs(object_set) do
				local alive_object_ctx = object_mgr[string.format("all%s",name)]()
				local alive = false

				if alive_object_ctx then
					local _,alive_object = next(alive_object_ctx)
					for _,obj in pairs(alive_object) do
						if weak_obj == obj then
							alive = true
							break
						end
					end
				end
				if not alive then
					leak_obj[weak_obj] = info
				end
			end
		end
	end
	
	local log = {}
	for obj,info in pairs(leak_obj) do
		table.insert(log,string.format("object __uid:%d,type:%s,create time:%s,debug info:%s",obj.__uid,obj:get_type(),os.date("%m-%d %H:%M:%S",math.floor(info.time/100)),obj.__debugInfo or "unknown"))
	end
	print("-----------------detect leak object-----------------")
	print(table.concat(log,"\n"))
end

_G["class"] = class