local NS = _G.BotGridNS or {}
_G.BotGridNS = NS

local C = {}
NS.Comm = C

C.prefix = "MBOT"
C.version = "1"

local CLASS_TOKEN = { -- classId -> uppercase token used by RAID_CLASS_COLORS
  [1]="WARRIOR",[2]="PALADIN",[3]="HUNTER",[4]="ROGUE",[5]="PRIEST",
  [6]="DEATHKNIGHT",[7]="SHAMAN",[8]="MAGE",[9]="WARLOCK",[11]="DRUID",
}
C.CLASS_TOKEN = CLASS_TOKEN

function C.trim(s)
  s = s or ""
  return (string.gsub(s, "^%s*(.-)%s*$", "%1"))
end

-- Split a string on the FIRST occurrence of sep; tail keeps remaining separators.
function C.splitOnce(s, sep)
  s = s or ""
  local i = string.find(s, sep, 1, true)
  if not i then return s, "" end
  return string.sub(s, 1, i - 1), string.sub(s, i + #sep)
end

function C.buildMessage(opcode, payload)
  opcode = C.trim(opcode)
  if payload ~= nil and payload ~= "" then
    return opcode .. "~" .. tostring(payload)
  end
  return opcode
end

-- Split a payload into a list on ';'
function C.parseList(payload)
  local out = {}
  for item in string.gmatch(payload or "", "([^;]+)") do
    out[#out + 1] = item
  end
  return out
end

function C.parseDetail(entry)
  local name, r1 = C.splitOnce(entry, "~")
  local race, r2 = C.splitOnce(r1, "~")
  local gender, r3 = C.splitOnce(r2, "~")
  local className, r4 = C.splitOnce(r3, "~")
  local level, r5 = C.splitOnce(r4, "~")
  local t1, r6 = C.splitOnce(r5, "~")
  local t2, r7 = C.splitOnce(r6, "~")
  local t3, score = C.splitOnce(r7, "~")
  name = C.trim(name)
  if name == "" then return nil end
  return {
    name = name,
    race = race,
    className = className,
    classToken = (string.gsub(string.upper(C.trim(className)), "%s+", "")),
    level = tonumber(level) or 0,
    talent1 = tonumber(t1) or 0,
    talent2 = tonumber(t2) or 0,
    talent3 = tonumber(t3) or 0,
    score = tonumber(score) or 0,
  }
end

function C.parseState(entry)
  local name, rest = C.splitOnce(entry, "~")
  local combat, normal = C.splitOnce(rest, "~")
  name = C.trim(name)
  if name == "" then return nil end
  return { name = name, combat = C.trim(combat), normal = C.trim(normal) }
end

function C.parseRosterEntry(entry)
  local f = {}
  for v in string.gmatch(entry or "", "([^,]+)") do f[#f + 1] = v end
  if not f[1] then return nil end
  return {
    name = f[1],
    classId = tonumber(f[2]) or 0,
    level = tonumber(f[3]) or 0,
    mapId = tonumber(f[4]) or 0,
    alive = f[5] == "1",
    hpPct = tonumber(f[6]) or 0,
    mpPct = tonumber(f[7]) or 0,
  }
end

-- Pure: pick the addon-message channel from a group-state table (testable).
function C.channelFor(state)
  if state and state.raid then return "RAID" end
  if state and state.party then return "PARTY" end
  return "WHISPER"
end

-- Live group-state read (wraps WoW API; not used in headless tests).
function C.groupState()
  local raid = (IsInRaid and IsInRaid()) or
    (GetNumRaidMembers and GetNumRaidMembers() > 0) or false
  local party = (not raid) and (
    (IsInGroup and IsInGroup()) or
    (GetNumPartyMembers and GetNumPartyMembers() > 0)) or false
  return { raid = raid, party = party }
end

C._token = 0
function C.nextToken()
  C._token = (C._token or 0) + 1
  return C._token
end

-- Encode a field so '~' (the delimiter), '%', CR and LF survive transport.
-- Matches mod-multibot-bridge's expected encoding (MultiBot urlEncodeField).
function C.urlEncode(value)
  value = tostring(value or "")
  return (value:gsub("([%%~\r\n])", function(ch)
    return string.format("%%%02X", string.byte(ch))
  end))
end

-- Pure: build a RUN message for ONE bot (scope BOT, target = bot name).
-- The bridge validates scope in {ALL,GROUP,BOT} and url-decodes target/command.
function C.buildRun(verb, name, command, token)
  return string.format("RUN~%s~BOT~%s~%s~%s",
    verb, C.urlEncode(name), tostring(token), C.urlEncode(command))
end

-- Live send (wraps SendAddonMessage; no-op-safe headless).
function C.rawSend(message)
  if type(SendAddonMessage) ~= "function" then return false end
  local ch = C.channelFor(C.groupState())
  if ch == "WHISPER" then
    local me = UnitName and UnitName("player")
    if not me then return false end
    SendAddonMessage(C.prefix, message, "WHISPER", me)
  else
    SendAddonMessage(C.prefix, message, ch)
  end
  return true
end

-- Decode the bridge's field encoding (%XX for '%', '~', CR, LF — see urlEncode).
function C.urlDecode(value)
  return (tostring(value or ""):gsub("%%(%x%x)", function(h)
    return string.char(tonumber(h, 16))
  end))
end

-- ---- framed STATES (bridge STATE_FRAMING_V1) -------------------------------
-- LOCAL: a legacy "GET~STATES" makes the bridge send ONE packet per bot (name~combat~normal);
-- any bot whose strategy lists push that past the 255-byte addon-message limit is dropped
-- server-side ("MultiBotBridge STATE TX rejected"). The tokened form streams the reply as
-- STATES_BEGIN, then per bot STATE_BEGIN / STATE_ITEM (one strategy each) / STATE_END, then
-- STATES_END (or STATE_ABORT). Replies arrive on the shared MBOT prefix, so MultiBot's own
-- framed replies are visible here too — the token filter keeps them apart.
C.STATES_TIMEOUT = 10.0

function C.newStateAssembler()
  return { token = nil, sentAt = 0, bots = nil }
end

-- A new STATES request may go out when none is in flight, or the last one timed out.
function C.canRequestStates(asm, now)
  return asm.token == nil or (now - (asm.sentAt or 0)) >= C.STATES_TIMEOUT
end

function C.beginStatesRequest(asm, now)
  local token = "bg" .. C.nextToken()
  asm.token, asm.sentAt, asm.bots = token, now, nil
  return token
end

-- Split the first n-1 '~' fields; the n-th keeps any remainder.
local function splitFields(payload, n)
  local out, rest = {}, payload or ""
  for i = 1, n - 1 do
    out[i], rest = C.splitOnce(rest, "~")
  end
  out[n] = rest
  return out
end

-- Join an index->strategy map in index order, tolerating gaps (a lost frame).
local function joinIndexed(t)
  local max = 0
  for k in pairs(t) do if k > max then max = k end end
  local out = {}
  for i = 1, max do if t[i] then out[#out + 1] = t[i] end end
  return table.concat(out, ", ")   -- same ", " join as the legacy STATE payload
end

-- Feed one framed reply. Returns a { [name] = {name, combat, normal} } map when OUR batch
-- completes (STATES_END), false when it was aborted, nil otherwise (incl. foreign tokens).
function C.applyStateFrame(asm, opcode, payload)
  local token = C.splitOnce(payload or "", "~")
  if asm.token == nil or token ~= asm.token then return nil end

  if opcode == "STATES_BEGIN" then
    asm.bots = {}
  elseif opcode == "STATE_BEGIN" then
    local f = splitFields(payload, 4)
    if asm.bots then asm.bots[C.urlDecode(f[2])] = { c = {}, n = {} } end
  elseif opcode == "STATE_ITEM" then
    local f = splitFields(payload, 5)
    local b = asm.bots and asm.bots[C.urlDecode(f[2])]
    local idx = tonumber(f[4])
    if b and idx then
      local list = (f[3] == "C" and b.c) or (f[3] == "N" and b.n) or nil
      if list then list[idx] = C.urlDecode(f[5]) end
    end
  elseif opcode == "STATES_END" then
    local done = {}
    for name, b in pairs(asm.bots or {}) do
      done[name] = { name = name, combat = joinIndexed(b.c), normal = joinIndexed(b.n) }
    end
    asm.token, asm.bots = nil, nil
    return done
  elseif opcode == "STATE_ABORT" then
    asm.token, asm.bots = nil, nil
    return false
  end
  return nil
end

C.statesAsm = C.newStateAssembler()

function C.send(opcode, payload) return C.rawSend(C.buildMessage(opcode, payload)) end
function C.sendHello() return C.send("HELLO", C.version) end
function C.sendPing()  return C.send("PING", tostring(C.nextToken())) end
function C.requestRoster()  return C.send("GET", "ROSTER") end
function C.requestStates()
  local asm = C.statesAsm
  local now = (GetTime and GetTime()) or 0
  if not C.canRequestStates(asm, now) then return false end   -- one batch in flight at a time
  return C.send("GET", "STATES~" .. C.beginStatesRequest(asm, now))
end
function C.requestDetails() return C.send("GET", "DETAILS") end

-- Issue a playerbot command to a set of bots (one BOT-scoped message each).
function C.run(verb, names, command)
  if not names or #names == 0 then return false end
  local ok = false
  for _, name in ipairs(names) do
    if C.rawSend(C.buildRun(verb, name, command, C.nextToken())) then ok = true end
  end
  return ok
end
