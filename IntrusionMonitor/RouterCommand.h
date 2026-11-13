#pragma once
// GL.iNet's documented local clients.get_list RPC, run inside the authenticated
// SSH connection. Reduction happens on the router; no neighbor/lease inference.
constexpr const char ROUTER_INVENTORY_COMMAND[] = R"SH(set -o pipefail
if command -v lua >/dev/null 2>&1; then nm_lua=lua
elif command -v luajit >/dev/null 2>&1; then nm_lua=luajit
else echo 'GL.iNet client API requires its Lua runtime' >&2; exit 1; fi
curl --noproxy '*' -k -L -f -s -S --connect-timeout 3 --max-time 20 --max-redirs 1 -H 'glinet: 1' -H 'Content-Type: application/json' --data '{"jsonrpc":"2.0","id":1,"method":"call","params":["","clients","get_list",{}]}' http://127.0.0.1/rpc | "$nm_lua" -e '
local ok,j=pcall(require,"cjson.safe")
if not ok then ok,j=pcall(require,"cjson") end
if not ok then ok,j=pcall(require,"luci.jsonc") end
assert(ok,"GL.iNet JSON library unavailable")
local decode=j.decode or j.parse
local encode=j.encode or j.stringify
local root=assert(decode(io.read("*a")),"Invalid GL.iNet API JSON")
assert(not root.error and root.id==1 and type(root.result)=="table","GL.iNet clients.get_list RPC failed")
local clients=root.result.clients
assert(type(clients)=="table","GL.iNet client list missing")
local function text(v) if type(v)=="string" then return v else return "" end end
-- Read association ages only from AP interfaces on this router.
local ages={}
local interfaces=io.popen("LC_ALL=C iw dev 2>/dev/null")
if interfaces then
  local ap={}
  local iface
  for line in interfaces:lines() do
    local name=line:match("^%s*Interface%s+([%w_.-]+)%s*$")
    if name then iface=name end
    if iface and line:match("^%s*type%s+AP%s*$") then ap[#ap+1]=iface end
  end
  interfaces:close()
  for _,name in ipairs(ap) do
    local stations=io.popen("LC_ALL=C iw dev "..name.." station dump 2>/dev/null")
    if stations then
      local mac
      for line in stations:lines() do
        local station=line:match("^Station%s+([%x:]+)%s+")
        if station then mac=station:lower() end
        local seconds=line:match("^%s+connected time:%s+(%d+)%s+seconds%s*$")
        if mac and seconds and not ages[mac] then ages[mac]=tonumber(seconds) end
      end
      stations:close()
    end
  end
end
print("NM2_GLCLIENTS")
local count=0
for key,c in pairs(clients) do
  assert(type(c)=="table","Invalid GL.iNet client")
  local online=c.online
  if online==1 then online=true elseif online==0 then online=false end
  assert(type(online)=="boolean","GL.iNet client online flag missing")
  local mac=text(c.mac)
  if mac=="" and type(key)=="string" then mac=key end
  local name=text(c.alias)
  if name=="" then name=text(c.name) end
  if name=="" then name=text(c.hostname) end
  local iface=text(c.iface)
  if iface=="" then iface=text(c.interface) end
  print(assert(encode({mac=mac,ip=text(c.ip),name=name,online=online,connection=iface,sessionSeconds=online and ages[mac:lower()] or nil})))
  count=count+1
end
print("NM2_DONE "..count)
'
)SH";
