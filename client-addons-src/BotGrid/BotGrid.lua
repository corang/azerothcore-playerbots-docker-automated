local NS = _G.BotGridNS or {}
_G.BotGridNS = NS

local C = NS.Comm
local store = { roster = {}, details = {}, states = {}, recs = {}, rti = {} }
NS.store = store

local STATES_INTERVAL = 3.0  -- seconds between throttled STATES refreshes
local connected = false

-- ---- incoming bridge messages -------------------------------------------
local function onAddonMessage(prefix, message)
  if prefix ~= C.prefix then return end
  local opcode, payload = C.splitOnce(message, "~")
  opcode = string.upper(C.trim(opcode))

  if opcode == "HELLO_ACK" then
    connected = true
    NS.SetBanner(false)
    C.requestRoster(); C.requestStates(); C.requestDetails()
  elseif opcode == "PONG" then
    connected = true
    NS.SetBanner(false)
  elseif opcode == "ROSTER" then
    connected = true
    store.roster = {}
    for _, e in ipairs(C.parseList(payload)) do
      local r = C.parseRosterEntry(e); if r then store.roster[#store.roster + 1] = r end
    end
    local present = {}
    for _, r in ipairs(store.roster) do present[r.name] = true end
    for k in pairs(store.states) do if not present[k] then store.states[k] = nil end end
    for k in pairs(store.details) do if not present[k] then store.details[k] = nil end end
    C.requestDetails(); C.requestStates()
    NS.Rebuild()
  elseif opcode == "STATES" then
    for _, e in ipairs(C.parseList(payload)) do
      local s = C.parseState(e); if s then store.states[s.name] = s end
    end
    NS.Rebuild()
  elseif opcode == "DETAILS" then
    for _, e in ipairs(C.parseList(payload)) do
      local d = C.parseDetail(e); if d then store.details[d.name] = d end
    end
    NS.Rebuild()
  elseif opcode == "STATE" then
    -- the bridge streams one STATE per bot (not just the STATES batch)
    local s = C.parseState(payload); if s then store.states[s.name] = s end
    NS.Rebuild()
  elseif opcode == "STATES_BEGIN" or opcode == "STATE_BEGIN" or opcode == "STATE_ITEM"
      or opcode == "STATE_END" or opcode == "STATES_END" or opcode == "STATE_ABORT" then
    -- LOCAL: framed STATES reply (see Comm.applyStateFrame); foreign tokens are ignored
    local done = C.applyStateFrame(C.statesAsm, opcode, payload)
    if done then
      for name, s in pairs(done) do store.states[name] = s end
      NS.Rebuild()
    end
  elseif opcode == "DETAIL" then
    -- the bridge streams one DETAIL per bot (not just the DETAILS batch)
    local d = C.parseDetail(payload); if d then store.details[d.name] = d end
    NS.Rebuild()
  end
  if NS.debug then
    DEFAULT_CHAT_FRAME:AddMessage("|cff88ccffBotGrid RX|r " .. tostring(message))
  end
end

-- ---- merge + redraw ------------------------------------------------------
-- Resolve a bot name to its native unit token (raid1.. / party1.. / player).
local function unitForName(name)
  local nr = (GetNumRaidMembers and GetNumRaidMembers()) or 0
  if nr > 0 then
    for i = 1, nr do local u = "raid" .. i; if UnitName(u) == name then return u end end
    return nil
  end
  if UnitName("player") == name then return "player" end
  local np = (GetNumPartyMembers and GetNumPartyMembers()) or 0
  for i = 1, np do local u = "party" .. i; if UnitName(u) == name then return u end end
  return nil
end
NS.UnitForName = unitForName   -- exposed for Menu (main-tank raid flag)

function NS.Rebuild()
  store.recs = NS.Model.buildRecords(store.roster, store.details, store.states)
  for name, rec in pairs(store.recs) do
    local u = unitForName(name)
    if u then
      local hmax = UnitHealthMax(u) or 0
      if hmax > 0 then rec.hpPct = math.floor(UnitHealth(u) / hmax * 100) end
      local mmax = UnitManaMax(u) or 0
      rec.mpPct = (mmax > 0) and math.floor(UnitMana(u) / mmax * 100) or 0
      if UnitIsDeadOrGhost then rec.alive = not UnitIsDeadOrGhost(u) end
      -- Real raid marker, so the cell's marker slot reflects what's on the bot.
      if GetRaidTargetIndex then rec.rti = GetRaidTargetIndex(u) end
    end
  end
  if NS.Grid.Panel() and NS.Grid.Panel():IsShown() then
    NS.Grid.Refresh(store.recs)
  end
end

-- ---- click router: left = select, right = menu ---------------------------
function NS.OnCellClick(name, button)
  if button == "RightButton" then
    NS.Menu.Open(name, store.recs)
    return
  end
  NS.Selection.click(name, IsControlKeyDown(), IsShiftKeyDown(), NS.Grid.flatOrder)
  NS.Grid.Refresh(store.recs)
end

-- ---- offline banner ------------------------------------------------------
local banner
function NS.SetBanner(show)
  local panel = NS.Grid.Ensure()
  if not banner then
    -- On the overlay frame: a panel's own regions draw UNDER its child frames,
    -- so a banner on `panel` would hide behind the role plates.
    local host = panel.overlay or panel
    banner = host:CreateFontString(nil, "OVERLAY")
    NS.Style.Font(banner, "body", 13, "OUTLINE")
    banner:SetPoint("CENTER", panel, "CENTER", 0, 0)
    banner:SetText("BRIDGE OFFLINE — enable mod-multibot-bridge")
    banner:SetTextColor(NS.Style.unpackColor(NS.Style.color.danger))
  end
  if show then banner:Show() else banner:Hide() end
end

-- ---- frame, events, timers ----------------------------------------------
local f = CreateFrame("Frame")
f:RegisterEvent("ADDON_LOADED")
f:RegisterEvent("PLAYER_ENTERING_WORLD")
f:RegisterEvent("CHAT_MSG_ADDON")
f:RegisterEvent("RAID_ROSTER_UPDATE")
f:RegisterEvent("PARTY_MEMBERS_CHANGED")
f:RegisterEvent("UNIT_HEALTH")
f:RegisterEvent("UNIT_MANA")
f:RegisterEvent("RAID_TARGET_UPDATE")

local sinceStates = 0
local sinceHello = 0
local dirty = false
local sinceDraw = 0
local pendingPulls = {}

-- Schedule detail+state re-pulls after a roster op (sync/syncone/etc). The server needs a
-- moment to apply respecs, and a full sync of many bots isn't instant, so pull twice.
function NS.RequestSoon()
  local now = (GetTime and GetTime() or 0)
  pendingPulls = { now + 1.5, now + 4.0 }
end

f:SetScript("OnUpdate", function(_, elapsed)
  if dirty then
    sinceDraw = sinceDraw + elapsed
    if sinceDraw >= 0.2 then
      sinceDraw = 0; dirty = false
      if NS.Grid.Panel() and NS.Grid.Panel():IsShown() then NS.Rebuild() end
    end
  end
  if pendingPulls[1] and (GetTime and GetTime() or 0) >= pendingPulls[1] then
    table.remove(pendingPulls, 1)
    if connected then C.requestDetails(); C.requestStates() end
  end
  sinceStates = sinceStates + elapsed
  if sinceStates >= STATES_INTERVAL then
    sinceStates = 0
    if connected and NS.Grid.Panel() and NS.Grid.Panel():IsShown() then
      C.requestStates()
    end
  end
  if not connected then
    sinceHello = sinceHello + elapsed
    if sinceHello >= 2.0 then sinceHello = 0; C.sendHello() end
  end
end)

f:SetScript("OnEvent", function(_, event, a1, a2)
  if event == "ADDON_LOADED" and a1 == "BotGrid" then
    BotGridDB = BotGridDB or { minimapPos = 200 }
    NS.db = BotGridDB
  elseif event == "PLAYER_ENTERING_WORLD" then
    connected = false
    C.sendHello()
  elseif event == "CHAT_MSG_ADDON" then
    onAddonMessage(a1, a2)
  elseif event == "RAID_ROSTER_UPDATE" or event == "PARTY_MEMBERS_CHANGED" then
    if connected then C.requestRoster() end
  elseif event == "UNIT_HEALTH" or event == "UNIT_MANA" or event == "RAID_TARGET_UPDATE" then
    dirty = true
  end
end)

-- ---- minimap button (mirrors RaidRoster pattern) -------------------------
local mb = CreateFrame("Button", "BotGridMinimapButton", Minimap)
mb:SetWidth(31); mb:SetHeight(31); mb:SetFrameStrata("MEDIUM"); mb:SetFrameLevel(8)
mb:RegisterForClicks("LeftButtonUp"); mb:RegisterForDrag("LeftButton")
local micon = mb:CreateTexture(nil, "BACKGROUND")
micon:SetWidth(20); micon:SetHeight(20); micon:SetPoint("CENTER")
micon:SetTexture("Interface\\Icons\\INV_Misc_GroupNeedMore")
local mborder = mb:CreateTexture(nil, "OVERLAY")
mborder:SetWidth(53); mborder:SetHeight(53); mborder:SetPoint("TOPLEFT")
mborder:SetTexture("Interface\\Minimap\\MiniMap-TrackingBorder")
local function mbPos()
  local angle = math.rad((NS.db and NS.db.minimapPos) or 200)
  mb:ClearAllPoints()
  mb:SetPoint("CENTER", Minimap, "CENTER", 80 * math.cos(angle), 80 * math.sin(angle))
end
mb:SetScript("OnDragStart", function(self)
  self:SetScript("OnUpdate", function()
    local mx, my = Minimap:GetCenter()
    local scale = Minimap:GetEffectiveScale()
    local px, py = GetCursorPosition(); px, py = px/scale, py/scale
    NS.db.minimapPos = math.deg(math.atan2(py - my, px - mx)); mbPos()
  end)
end)
mb:SetScript("OnDragStop", function(self) self:SetScript("OnUpdate", nil) end)
mb:SetScript("OnClick", function()
  NS.Grid.Toggle()
  if NS.Grid.Panel():IsShown() then
    if not NS._barAttached then NS.RosterBar.Attach(NS.Grid.Panel()); NS._barAttached = true end
    C.requestRoster(); C.requestStates(); C.requestDetails()
    NS.Rebuild()
  end
end)
mb:SetScript("OnEnter", function(self)
  GameTooltip:SetOwner(self, "ANCHOR_LEFT")
  GameTooltip:AddLine("BotGrid"); GameTooltip:AddLine("Click to toggle the grid.", 1,1,1)
  GameTooltip:Show()
end)
mb:SetScript("OnLeave", function() GameTooltip:Hide() end)
mbPos()

-- ---- slash command -------------------------------------------------------
SLASH_BOTGRID1 = "/botgrid"
SlashCmdList["BOTGRID"] = function(msg)
  msg = string.lower(C.trim(msg or ""))
  if msg == "debug" then
    NS.debug = not NS.debug
    DEFAULT_CHAT_FRAME:AddMessage("BotGrid debug: " .. (NS.debug and "on" or "off"))
  else
    mb:GetScript("OnClick")()
  end
end
