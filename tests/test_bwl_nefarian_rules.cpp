// Standalone: g++ -std=c++17 -Wall -Wextra -Werror -o /tmp/bwlnef tests/test_bwl_nefarian_rules.cpp && /tmp/bwlnef
// Run from the overlay repo root AFTER patch 0020 is applied to the fork (the header lives there).
#include "../azerothcore-wotlk/modules/mod-playerbots/src/Ai/Raid/BWL/BwlNefarianRules.h"
#include <cmath>
#include <cstdio>

using namespace BwlNefarianRules;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static Pt At(Pt o, float facing, float bearingDeg, float r)
{
    float const a = facing + bearingDeg * DEG;
    return Pt{o.x + r * std::cos(a), o.y + r * std::sin(a)};
}

static void TestPhase()
{
    CHECK(GetPhase(false, 100.0f, false) == Phase::None);
    CHECK(GetPhase(false, 10.0f, true) == Phase::None);     // not landed: never a phase
    CHECK(GetPhase(true, 100.0f, false) == Phase::P2);
    CHECK(GetPhase(true, 20.5f, false) == Phase::P2);
    CHECK(GetPhase(true, 20.0f, false) == Phase::P3);       // HP rule
    CHECK(GetPhase(true, 60.0f, true) == Phase::P3);        // any construct alive
}

static void TestKinds()
{
    CHECK(KindOfEntry(14662) == Kind::Totem && KindOfEntry(14663) == Kind::Totem);
    CHECK(KindOfEntry(14664) == Kind::Totem && KindOfEntry(14666) == Kind::Totem);
    CHECK(KindOfEntry(14667) == Kind::Other);               // the generic "Corrupted Totem" helper
    CHECK(KindOfEntry(14668) == Kind::Infernal);
    CHECK(KindOfEntry(14261) == Kind::Drakonid && KindOfEntry(14265) == Kind::Drakonid);
    CHECK(KindOfEntry(14302) == Kind::Drakonid);
    CHECK(KindOfEntry(14605) == Kind::Construct);
    CHECK(KindOfEntry(11583) == Kind::Nefarian);
}

static void TestPickTarget()
{
    Cand const p2[] = {{Kind::Nefarian, 5.0f}, {Kind::Infernal, 30.0f}, {Kind::Totem, 40.0f}, {Kind::Totem, 20.0f}};
    CHECK(PickTarget(Phase::P2, false, p2, 4) == 3);        // nearest totem first
    Cand const p2b[] = {{Kind::Nefarian, 5.0f}, {Kind::Infernal, 30.0f}, {Kind::Drakonid, 2.0f}};
    CHECK(PickTarget(Phase::P2, false, p2b, 3) == 1);       // infernal; drakonid excluded without siphon
    Cand const sip[] = {{Kind::Nefarian, 5.0f}, {Kind::Drakonid, 12.0f}};
    CHECK(PickTarget(Phase::P2, true, sip, 2) == 1);        // siphon + physical: leftover drakonid
    CHECK(PickTarget(Phase::P2, false, sip, 2) == 0);       // non-physical: stays on Nefarian
    Cand const nefOnly[] = {{Kind::Nefarian, 5.0f}};
    CHECK(PickTarget(Phase::P2, true, nefOnly, 1) == 0);    // siphon, nothing else up: Nefarian
    Cand const p3[] = {{Kind::Nefarian, 3.0f}, {Kind::Construct, 25.0f}, {Kind::Construct, 15.0f}};
    CHECK(PickTarget(Phase::P3, false, p3, 3) == 2);        // P3: nearest construct before Nefarian
    CHECK(PickTarget(Phase::P2, false, p3, 3) == 0);        // constructs are no P2 target
    Cand const p3t[] = {{Kind::Construct, 2.0f}, {Kind::Totem, 50.0f}};
    CHECK(PickTarget(Phase::P3, false, p3t, 2) == 1);       // totems outrank constructs
    CHECK(PickTarget(Phase::P2, false, nullptr, 0) == NONE);
}

static void TestPickOtTarget()
{
    Cand const c[] = {{Kind::Nefarian, 2.0f}, {Kind::Totem, 3.0f}, {Kind::Construct, 10.0f}, {Kind::Infernal, 40.0f}};
    CHECK(PickOtTarget(c, 4) == 3);                         // infernal first, never Nefarian/totem
    Cand const d[] = {{Kind::Nefarian, 2.0f}, {Kind::Totem, 3.0f}};
    CHECK(PickOtTarget(d, 2) == NONE);
}

static void TestCones()
{
    Pt const nef{0.0f, 0.0f};
    float const facing = 1.0f;
    CHECK(InFrontCone(nef, facing, At(nef, facing, 40.0f, 30.0f), FRONT_RANGE, 0.0f));
    CHECK(!InFrontCone(nef, facing, At(nef, facing, 42.0f, 30.0f), FRONT_RANGE, 0.0f));
    CHECK(!InFrontCone(nef, facing, At(nef, facing, 0.0f, 46.0f), FRONT_RANGE, 0.0f));
    CHECK(InBackCone(nef, facing, At(nef, facing, 180.0f, 29.0f), BACK_RANGE, 0.0f));
    CHECK(InBackCone(nef, facing, At(nef, facing, -140.0f, 10.0f), BACK_RANGE, 0.0f));
    CHECK(!InBackCone(nef, facing, At(nef, facing, 138.0f, 10.0f), BACK_RANGE, 0.0f));
    CHECK(InDanger(nef, facing, At(nef, facing, 48.0f, 20.0f)));    // inside the 10-degree avoid margin
    CHECK(InDanger(nef, facing, At(nef, facing, 0.0f, 49.0f)));     // inside FRONT_AVOID
    CHECK(!InDanger(nef, facing, At(nef, facing, 90.0f, 5.0f)));
    CHECK(!InDanger(nef, facing, At(nef, facing, 180.0f, 36.0f)));  // past BACK_AVOID
    CHECK(StationUsable(nef, facing, At(nef, facing, 90.0f, 12.0f)));   // a flank station off his side
    CHECK(!StationUsable(nef, facing, At(nef, facing, 30.0f, 12.0f)));  // he turned: it is in the front cone
    CHECK(!StationUsable(nef, facing, At(nef, facing, 160.0f, 12.0f))); // ... or the tail cone
    for (float b : {61.0f, 90.0f, 119.0f, -61.0f, -119.0f})
        CHECK(InSafeArc(nef, facing, At(nef, facing, b, 20.0f), STATION_MARGIN_DEG));
    CHECK(!InSafeArc(nef, facing, At(nef, facing, 60.0f, 20.0f), STATION_MARGIN_DEG));
    CHECK(!InSafeArc(nef, facing, At(nef, facing, 120.0f, 20.0f), STATION_MARGIN_DEG));
    for (float b : {95.0f, 115.0f})                                   // the melee ring (spec §4.2)
        CHECK(InSafeArc(nef, facing, At(nef, facing, b, 5.0f), STATION_MARGIN_DEG));
}

static void TestEscape()
{
    Pt const nef{100.0f, 50.0f};
    float const facing = 2.5f;
    for (int side : {1, -1})
    {
        Pt const e = EscapePoint(nef, facing, side, RANGED_ESCAPE_RADIUS);
        CHECK(std::fabs(Dist(nef, e) - RANGED_ESCAPE_RADIUS) < 0.01f);
        CHECK(!InDanger(nef, facing, e));
        CHECK(std::fabs(RelBearingDeg(nef, facing, e) - 90.0f) < 0.1f);
        CHECK(SideOf(nef, facing, e) == side);
        // Stable: a 1 y Nefarian drift moves the point by ~1 y, never flips it to the other flank.
        Pt const e2 = EscapePoint(Pt{nef.x + 1.0f, nef.y}, facing, side, RANGED_ESCAPE_RADIUS);
        CHECK(Dist(e, e2) < 1.01f);
    }
    CHECK(FlankSide(0) == 1 && FlankSide(1) == -1 && FlankSide(2) == 1);
    Slot const s = SlotFor(7, 5);
    CHECK(s.side == -1 && s.index == 3);
    CHECK(SlotFor(10, 5).index == 0);                       // wraps when the roster outnumbers stations
    CHECK(SlotFor(3, 0).index == NONE);
}

static void TestApproach()
{
    Pt const nef{100.0f, 50.0f};
    float const facing = 2.5f;
    // Far bot on his flank: the on-line point at the radius, toward the bot.
    Pt const flank = At(nef, facing, 90.0f, 80.0f);
    Pt const a = ApproachPoint(nef, facing, flank, APPROACH_RADIUS_HEALER, SideOf(nef, facing, flank));
    CHECK(std::fabs(Dist(nef, a) - APPROACH_RADIUS_HEALER) < 0.01f);
    CHECK(std::fabs(RelBearingDeg(nef, facing, a) - 90.0f) < 0.1f);
    CHECK(!InDanger(nef, facing, a));
    // Far bot straight in front: the on-line point is in the cone, so the bot's side escape point.
    Pt const front = At(nef, facing, -10.0f, 100.0f);
    int const side = SideOf(nef, facing, front);
    Pt const b = ApproachPoint(nef, facing, front, APPROACH_RADIUS_RANGED, side);
    CHECK(!InDanger(nef, facing, b));
    CHECK(std::fabs(Dist(nef, b) - APPROACH_RADIUS_RANGED) < 0.01f);
    CHECK(SideOf(nef, facing, b) == side && side == -1);
    // Behind him (tail cone) for melee: also the side point.
    Pt const back = At(nef, facing, 170.0f, 60.0f);
    Pt const c = ApproachPoint(nef, facing, back, APPROACH_RADIUS_MELEE, SideOf(nef, facing, back));
    CHECK(!InDanger(nef, facing, c) && SideOf(nef, facing, c) == 1);
    // Degenerate: bot on top of him.
    CHECK(std::fabs(Dist(nef, ApproachPoint(nef, facing, nef, 5.0f, 1)) - 5.0f) < 0.01f);
    CHECK(NeedsApproach(20.1f, APPROACH_RADIUS_HEALER, true));
    CHECK(!NeedsApproach(20.0f, APPROACH_RADIUS_HEALER, true));
    CHECK(NeedsApproach(1.0f, APPROACH_RADIUS_MELEE, false));   // no LOS: always
}

static void TestParkedAndHold()
{
    CHECK(Parked(Pt{0.0f, 0.0f}, Pt{11.9f, 0.0f}));
    CHECK(!Parked(Pt{0.0f, 0.0f}, Pt{12.1f, 0.0f}));
    CHECK(HoldFireOnNefarian(false, 0, 0.0f));              // MT not his victim yet
    CHECK(HoldFireOnNefarian(false, 19999, 0.1f));
    CHECK(!HoldFireOnNefarian(false, 20000, 0.1f));         // fail-open after 20 s
    CHECK(!HoldFireOnNefarian(true, 0, 0.89f));
    CHECK(HoldFireOnNefarian(true, 60000, 0.90f));          // hard stop
}

static void TestThreatRatio()
{
    CHECK(ThreatRatio(ThreatRole::MainTank, 5000.0f, 1000.0f, 0.0f) == 0.0f);   // the MT is never capped
    CHECK(ThreatRatio(ThreatRole::OffTank, 800.0f, 1000.0f, 5000.0f) == 0.8f);  // OT: against the MT only
    CHECK(ThreatRatio(ThreatRole::OffTank, 800.0f, 0.0f, 900.0f) == 0.0f);      // no MT threat: no cap
    CHECK(ThreatRatio(ThreatRole::Other, 450.0f, 1000.0f, 500.0f) == 0.9f);     // DPS: against the backup
    CHECK(ThreatRatio(ThreatRole::Other, 450.0f, 1000.0f, 0.0f) == 0.45f);      // no backup threat: the MT
    CHECK(ThreatRatio(ThreatRole::Other, 450.0f, 0.0f, 0.0f) == 0.0f);          // no tank threat: no cap
}

static void TestTankHeal()
{
    TankInfo t[3] = {};
    t[0] = {true, false, false, false, 50.0f};              // off-tank holding an add
    t[1] = {true, true, false, false, 90.0f};               // main tank
    t[2] = {false, false, false, false, 10.0f};             // not eligible (a non-tank / idle tank)
    TankHealPick p = PickTankHeal(t, 3);
    CHECK(p.index == 1 && p.spell == TankHeal::Shield);     // MT first
    t[1].weakenedSoul = true;
    p = PickTankHeal(t, 3);
    CHECK(p.index == 1 && p.spell == TankHeal::Renew);
    t[1].myRenew = true;
    p = PickTankHeal(t, 3);
    CHECK(p.index == 0 && p.spell == TankHeal::Shield);     // then the off-tank
    t[0].weakenedSoul = true;
    t[0].myRenew = true;
    p = PickTankHeal(t, 3);
    CHECK(p.index == NONE && p.spell == TankHeal::None);    // never index 2, however low it is
    t[1] = {true, true, true, false, 99.0f};                // MT nearly full, no Renew: shield only rule
    p = PickTankHeal(t, 3);
    CHECK(p.index == NONE);                                 // no Renew at/above RENEW_BELOW_PCT
}

static void TestDispel()
{
    DispelInfo d[4] = {{false, false, 20.0f}, {false, true, 80.0f}, {true, false, 95.0f}, {false, true, 40.0f}};
    CHECK(PickDispel(d, 4) == 2);                           // MT first
    d[2].mainTank = false;
    CHECK(PickDispel(d, 4) == 3);                           // then the lowest healer
    CHECK(PickDispel(nullptr, 0) == NONE);
}

static void TestSpellLists()
{
    for (char const* s : {"lesser heal", "heal", "greater heal", "flash heal", "binding heal", "circle of healing",
                          "prayer of healing", "prayer of mending", "penance", "divine hymn", "desperate prayer"})
        CHECK(IsCorruptedHealingSpell(s));
    for (char const* s : {"renew", "power word: shield", "fear ward", "dispel magic", ""})
        CHECK(!IsCorruptedHealingSpell(s));
    for (char const* s : {"claw", "rake", "shred", "mangle (cat)", "rip", "ferocious bite", "tiger's fury", "savage roar"})
        CHECK(IsCatAbility(s));
    for (char const* s : {"rejuvenation", "cancel cat form", "bear form", "regrowth"})
        CHECK(!IsCatAbility(s));
    CHECK(IsStanceSpell("defensive stance") && IsStanceSpell("battle stance") && !IsStanceSpell("berserker stance"));
    CHECK(IsHunterShot("auto shot") && IsHunterShot("steady shot") && !IsHunterShot("raptor strike"));
    CHECK(IsNonTremorEarthTotem("stoneskin totem") && IsNonTremorEarthTotem("strength of earth totem"));
    CHECK(!IsNonTremorEarthTotem("tremor totem") && !IsNonTremorEarthTotem("searing totem"));
}

int main()
{
    TestPhase();
    TestKinds();
    TestPickTarget();
    TestPickOtTarget();
    TestCones();
    TestEscape();
    TestApproach();
    TestParkedAndHold();
    TestThreatRatio();
    TestTankHeal();
    TestDispel();
    TestSpellLists();
    if (g_fail)
    {
        std::printf("%d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("all BWL nefarian rule tests passed\n");
    return 0;
}
