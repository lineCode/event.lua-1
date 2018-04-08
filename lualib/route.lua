
local _M = {}


local _load_module = {}

function _M.dispatch(file,method,...)
	local lm = _load_module[file]
	if not lm then
		lm = import(file)
		if not lm then
			error(string.format("no such file:%s",file))
		end
		_load_module[file] = lm
	end

	local func = lm[method]
	if not func then
		error(string.format("no such method:%s",method))
	end
	return func(...)
end


return _M