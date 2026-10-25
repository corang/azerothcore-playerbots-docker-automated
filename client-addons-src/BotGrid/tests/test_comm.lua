return function(H)
  local NS = H.load("Comm.lua")
  local C = NS.Comm

  -- buildMessage
  H.eq(C.buildMessage("HELLO", "1"), "HELLO~1", "build with payload")
  H.eq(C.buildMessage("GET", "STATES"), "GET~STATES", "build get")
  H.eq(C.buildMessage("PONG", nil), "PONG", "build no payload")

  -- splitOnce
  local a, b = C.splitOnce("STATE~rest~more", "~")
  H.eq(a, "STATE", "splitOnce head")
  H.eq(b, "rest~more", "splitOnce tail keeps separators")

  -- parseDetail
  local d = C.parseDetail("Boband~Orc~0~Warrior~80~3~5~61~4200")
  H.eq(d.name, "Boband", "detail name")
  H.eq(d.classToken, "WARRIOR", "detail class uppercased token")
  H.eq(d.talent1, 3, "detail talent1")
  H.eq(d.talent3, 61, "detail talent3")
  H.eq(d.score, 4200, "detail score")

  -- parseState
  local s = C.parseState("Boband~tank~follow")
  H.eq(s.name, "Boband", "state name")
  H.eq(s.combat, "tank", "state combat")
  H.eq(s.normal, "follow", "state normal")

  -- parseRosterEntry
  local r = C.parseRosterEntry("Boband,1,80,571,1,93,40")
  H.eq(r.name, "Boband", "roster name")
  H.eq(r.classId, 1, "roster classId")
  H.eq(r.alive, true, "roster alive")
  H.eq(r.hpPct, 93, "roster hpPct")

  -- parseList splits on ';'
  local list = C.parseList("a;b;c")
  H.eq(#list, 3, "parseList count")
  H.eq(list[2], "b", "parseList element")

  -- channelFor: pure decision from injected group-state
  H.eq(C.channelFor({raid=true}), "RAID", "raid channel")
  H.eq(C.channelFor({party=true}), "PARTY", "party channel")
  H.eq(C.channelFor({}), "WHISPER", "solo whisper channel")

  -- buildRun: single bot, BOT scope, url-encoded fields
  H.eq(C.buildRun("COMBAT", "Boband", "do attack my target", 7),
    "RUN~COMBAT~BOT~Boband~7~do attack my target", "buildRun single bot BOT scope")
  H.eq(C.urlEncode("a~b%c"), "a%7Eb%25c", "urlEncode escapes ~ and %")

  -- nextToken increments
  C._token = 0
  H.eq(C.nextToken(), 1, "token 1")
  H.eq(C.nextToken(), 2, "token 2")

  -- urlDecode reverses urlEncode
  H.eq(C.urlDecode("a%7Eb%25c"), "a~b%c", "urlDecode")

  -- framed STATES: request gating + token
  local asm = C.newStateAssembler()
  H.truthy(C.canRequestStates(asm, 0), "can request when idle")
  C._token = 0
  local tok = C.beginStatesRequest(asm, 0)
  H.eq(tok, "bg1", "states token")
  H.eq(C.canRequestStates(asm, 5), false, "blocked while in flight")
  H.truthy(C.canRequestStates(asm, 10), "allowed after timeout")

  -- framed STATES: full batch, out-of-order items, encoded name, foreign token ignored
  H.eq(C.applyStateFrame(asm, "STATES_BEGIN", "bg1~2"), nil, "begin -> nil")
  H.eq(C.applyStateFrame(asm, "STATE_BEGIN", "bg1~Bo%7Eb~2~1"), nil, "bot begin")
  C.applyStateFrame(asm, "STATE_ITEM", "bg1~Bo%7Eb~C~2~dps assist")
  C.applyStateFrame(asm, "STATE_ITEM", "bg1~Bo%7Eb~C~1~tank")
  C.applyStateFrame(asm, "STATE_ITEM", "bg1~Bo%7Eb~N~1~follow")
  C.applyStateFrame(asm, "STATE_ITEM", "mb7~Bo%7Eb~C~3~FOREIGN")   -- MultiBot's token
  C.applyStateFrame(asm, "STATE_END", "bg1~Bo%7Eb~2~1")
  C.applyStateFrame(asm, "STATE_BEGIN", "bg1~Ann~0~0")
  C.applyStateFrame(asm, "STATE_END", "bg1~Ann~0~0")
  H.eq(C.applyStateFrame(asm, "STATES_END", "mb7~2"), nil, "foreign STATES_END ignored")
  local done = C.applyStateFrame(asm, "STATES_END", "bg1~2")
  H.truthy(done, "batch completes")
  H.eq(done["Bo~b"].combat, "tank, dps assist", "combat joined in index order")
  H.eq(done["Bo~b"].normal, "follow", "normal joined")
  H.eq(done["Ann"].combat, "", "empty strategy lists")
  H.truthy(C.canRequestStates(asm, 0), "idle again after STATES_END")

  -- framed STATES: abort + missing frame tolerated
  C.beginStatesRequest(asm, 0)
  local t2 = asm.token
  H.eq(C.applyStateFrame(asm, "STATE_ABORT", t2 .. "~~TOO_MANY_BOTS"), false, "abort -> false")
  H.truthy(C.canRequestStates(asm, 0), "idle after abort")
  C.beginStatesRequest(asm, 0)
  local t3 = asm.token
  C.applyStateFrame(asm, "STATES_BEGIN", t3 .. "~1")
  C.applyStateFrame(asm, "STATE_BEGIN", t3 .. "~Cy~3~0")
  C.applyStateFrame(asm, "STATE_ITEM", t3 .. "~Cy~C~1~a")
  C.applyStateFrame(asm, "STATE_ITEM", t3 .. "~Cy~C~3~c")       -- item 2 lost
  local d3 = C.applyStateFrame(asm, "STATES_END", t3 .. "~1")
  H.eq(d3["Cy"].combat, "a, c", "gap in items tolerated")
end
