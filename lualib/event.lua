local event_core = require "ev.core"
local channel = require "channel"
local stream = require "stream"
local profiler = require "profiler.core"

local _event

local _co_pool = {}
local _co_wait = {}

local _session = 1
local _main_co = coroutine.running()

local EV_ERROR = 0
local EV_TIMEOUT = 1
local EV_ACCEPT = 2
local EV_CONNECT = 3
local EV_DATA = 4

local _listener_ctx = setmetatable({},{__mode = "k"})
local _channel_ctx = setmetatable({},{__mode = "k"})
local _timer_ctx = setmetatable({},{__mode = "k"})
local _udp_ctx = setmetatable({},{__mode = "k"})
local _mailbox_ctx = setmetatable({},{__mode = "k"})
local _client_manager_ctx = setmetatable({},{__mode = "k"})

local co_running = coroutine.running
local co_yield = coroutine.yield
local co_resume = profiler.resume

local tinsert = table.insert
local tremove = table.remove
local tunpack = table.unpack

local ipairs = ipairs

local _M = {}

local CO_STATE = {
	EXIT = 1,
	WAIT = 2	
}

local function co_create(func)
	local co = tremove(_co_pool)
	if co == nil then
		co = coroutine.create(function(...)
			func(...)
			while true do
				func = nil
				_co_pool[#_co_pool+1] = co
				func = co_yield(CO_STATE.EXIT)
				func(co_yield())
			end
		end)
	else
		co_resume(co, func)
	end
	return co
end

local function co_monitor(co,ok,state,session)
	if ok then
		if state == CO_STATE.WAIT then
			_co_wait[session] = co
		else
			assert(state == CO_STATE.EXIT)
		end
	else
		_M.error(debug.traceback(co,tostring(state)))
	end
end

local function create_channel(channel_class,buffer,addr)
	local channel_obj
	if channel_class then
		channel_obj = channel_class:new(buffer,addr)
	else
		channel_obj = channel:new(buffer,addr)
	end
	channel_obj:init()
	_channel_ctx[buffer] = channel_obj
	return channel_obj
end

local function resolve_addr(addr)
	local info = {}
	local start,over = addr:find("tcp://")
	if not start then
		start,over = addr:find("ipc://")
		if not start then
			return
		end
		local file = addr:sub(over+1)
		info.file = file
	else
		local ip,port = string.match(addr:sub(over+1),"(.+):(%d+)")
		info.ip = ip
		info.port = port
	end
	return info
end

function _M.listen(addr,header,callback,channel_class,multi)
	local listener
	local info = resolve_addr(addr)
	if not info then
		return false,string.format("error addr:%s",addr)
	end

	multi = multi or false

	local listener,reason = _event:listen(header,multi,info)

	if not listener then
		return false,reason
	end
	_listener_ctx[listener] = {callback = callback,channel_class = channel_class}
	return listener
end

function _M.connect(addr,header,channel_class)
	local co = co_running()
	assert(co ~= _main_co,string.format("cannot connect in main co"))
	local session = _M.gen_session()

	local info = resolve_addr(addr)
	if not info then
		return false,string.format("error addr:%s",addr)
	end
	local ok,err
	if info.file then
		ok,err = _event:connect(header,session,info)
	else
		ok,err = _event:connect(header,session,info)
	end

	if not ok then
		return false,err
	end
	local result,error_or_buffer = _M.wait(session)
	if result then
		return create_channel(channel_class,error_or_buffer,info.file or string.format("%s:%s",info.ip,info.port))
	else
		return false,error_or_buffer
	end
end

function _M.block_connect(addr,header,channel_class)
	local info = resolve_addr(addr)
	if not info then
		return false,string.format("error addr:%s",addr)
	end
	local buffer,reason
	if info.file then
		buffer,reason = _event:connect(header,0,info)
	else
		buffer,reason = _event:connect(header,0,info)
	end

	if not buffer then
		return false,reason
	end
	return create_channel(channel_class,buffer,info.file or string.format("%s:%s",info.ip,info.port))
end

function _M.bind(fd,channel_class)
	local buffer = _event:bind(fd)
	return create_channel(channel_class,buffer)
end

function _M.sleep(ti)
	local session = _M.gen_session()
	local timer = _event:timer(ti,true)
	_timer_ctx[timer]= session
	_M.wait(session)
end

function _M.timer(ti,callback)
	local timer = _event:timer(ti,false)
	_timer_ctx[timer]= callback
	return timer
end

function _M.udp(size,callback,ip,port)
	local udp_session,err = _event:udp(size,callback,ip,port)
	if udp_session then
		_udp_ctx[udp_session] = true
	end
	return udp_session,err
end

function _M.mailbox(func)
	local mailbox,fd = _event:mailbox(func)
	if mailbox then
		_mailbox_ctx[mailbox] = fd
	end
	return mailbox,fd
end

function _M.client_manager(max)
	local client_mgr = _event:client_manager(max)
	if client_mgr then
		_client_manager_ctx[client_mgr] = true
	end
	return client_mgr
end

function _M.run_process(cmd,line)
    local FILE = io.popen(cmd)
    local fd = FILE:fd()
    local ch = _M.bind(fd,stream)
    local result
    if line then
    	result = ch:wait_lines()
    else
    	result = ch:wait()
    end
    FILE:close()
    return result
end

function _M.fork(func,...)
	local co = co_create(func)
	co_monitor(co,co_resume(co,...))
end

function _M.wakeup(session,...)
	local co = _co_wait[session]
	_co_wait[session] = nil
	if co then
		co_monitor(co,co_resume(co,...))
	else
		_M.error(string.format("error wakeup:session:%s not found",session))
	end
end

function _M.wait(session)
	local co = co_running()
	if co == _main_co then
		error("cannot wait in main co,wait op should run in fork")
	end
	return co_yield(CO_STATE.WAIT,session)
end

function _M.gen_session()
	if _session >= math.maxinteger then
		_session = 1
	end
	local session = _session
	_session = _session + 1
	return session
end

function _M.co_clean()
	_co_pool = {}
end

function _M.error(...)
	print(...)
end

function _M.dispatch()
	local code = _event:dispatch()
	
	for timer in pairs(_timer_ctx) do
		if timer:alive() then
			timer:cancel()
		end
	end

	for listener in pairs(_listener_ctx) do
		if listener:alive() then
			listener:close()
		end
	end

	for buffer in pairs(_channel_ctx) do
		if buffer:alive() then
			buffer:close_immediately()
		end
	end

	for udp_session in pairs(_udp_ctx) do
		if udp_session:alive() then
			udp_session:destroy()
		end
	end

	for mailbox in pairs(_mailbox_ctx) do
		if mailbox:alive() then
			mailbox:release()
		end
	end

	for client_mgr in pairs(_client_manager_ctx) do
		client_mgr:release()
	end
	
	_event:release()
	return code
end

function _M.breakout()
	_event:breakout()
end

local EV = {}

EV[EV_TIMEOUT] = function (timer)
	local info = _timer_ctx[timer]
	if type(info) == "number" then
		timer:cancel()
		_timer_ctx[timer] = nil
		_M.wakeup(info)
	else
		info(timer)
	end
end

EV[EV_ACCEPT] = function (listener,buffer,addr)
	local info = _listener_ctx[listener]
	local channel_obj = create_channel(info.channel_class,buffer,addr)
	info.callback(listener,channel_obj)
end

EV[EV_CONNECT] = function (...)
	_M.wakeup(...)
end

EV[EV_DATA] = function (buffer,data,size)
	local channel = _channel_ctx[buffer]
	channel:data(data,size)
end

EV[EV_ERROR] = function (buffer)
	local channel = _channel_ctx[buffer]
	channel:disconnect()
end

local function event_dispatch(ev,...)
	local ev_func = EV[ev]
	if not ev_func then
		_M.error(string.format("no such ev:%d",ev))
		return
	end
	local ok,err = xpcall(ev_func,debug.traceback,...)
	if not ok then
		_M.error(err)
	end
end

function _M.prepare()
	_event = event_core.new(event_dispatch)
end

return _M