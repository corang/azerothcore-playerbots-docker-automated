// Standalone: g++ -std=c++17 -Wall -Wextra -Werror -o /tmp/aq40huh tests/test_aq40_huhuran_rules.cpp && /tmp/aq40huh
// Run from the overlay repo root AFTER patch 0016 is applied to the fork (the header lives there).
#include "../azerothcore-wotlk/modules/mod-playerbots/src/Ai/Raid/Aq40/Aq40HuhuranRules.h"
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

using namespace Aq40HuhuranRules;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static float Dist(Pt a, Pt b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

static std::vector<Pt> HealerRing(uint32_t n, float radius)
{
    std::vector<Pt> v;
    for (uint32_t k = 0; k < n; ++k)
        v.push_back(RingSpot(Pt{0, 0}, radius, n, k, 0.0f));
    return v;
}

static void TestRingSpotStable()
{
    Pt const c{0, 0};
    for (uint32_t k = 0; k < 17; ++k)
        CHECK(std::fabs(Dist(RingSpot(c, 17, 17, k, 0), c) - 17.0f) < 0.01f);
    Pt const a = RingSpot(c, 17, 17, 3, 0);
    Pt const b = RingSpot(c, 17, 17, 3, 0);
    CHECK(a.x == b.x && a.y == b.y);
    float const chord = 2.0f * 17.0f * std::sin(PI_F / 17.0f);
    for (uint32_t k = 0; k + 1 < 17; ++k)
        CHECK(std::fabs(Dist(RingSpot(c, 17, 17, k, 0), RingSpot(c, 17, 17, k + 1, 0)) - chord) < 0.01f);
}

static void TestHealerSplash()
{
    for (uint32_t n : {7u, 8u, 9u})
    {
        std::vector<Pt> const healers = HealerRing(n, HEALER_RADIUS);
        for (int d = 0; d < 360; ++d)
        {
            float const a = static_cast<float>(d) * PI_F / 180.0f;
            Pt const probe{RANGED_RADIUS * std::cos(a), RANGED_RADIUS * std::sin(a)};
            CHECK(PointsWithin(probe, healers, SPLASH_REACH) <= 1);
        }
    }
    // Documents why the earlier 20 y inner ring was rejected: midway between two healers it splashes both.
    std::vector<Pt> const healers = HealerRing(9, HEALER_RADIUS);
    uint32_t worst = 0;
    for (int d = 0; d < 360; ++d)
    {
        float const a = static_cast<float>(d) * PI_F / 180.0f;
        Pt const probe{20.0f * std::cos(a), 20.0f * std::sin(a)};
        uint32_t const hit = PointsWithin(probe, healers, SPLASH_REACH);
        if (hit > worst)
            worst = hit;
    }
    CHECK(worst >= 2);
}

static void TestHealerChord()
{
    std::vector<Pt> const healers = HealerRing(9, HEALER_RADIUS);
    for (size_t k = 0; k < healers.size(); ++k)
        CHECK(Dist(healers[k], healers[(k + 1) % healers.size()]) >= 17.0f);
}

static std::vector<std::pair<float, bool>> Bodies(uint32_t melee, uint32_t ranged, uint32_t healers)
{
    std::vector<std::pair<float, bool>> v;
    for (uint32_t i = 0; i < melee; ++i)
        v.emplace_back(3.0f + 7.0f * static_cast<float>(i) / 13.0f, false);
    for (uint32_t i = 0; i < ranged; ++i)
        v.emplace_back(RANGED_RADIUS, false);
    for (uint32_t i = 0; i < healers; ++i)
        v.emplace_back(HEALER_RADIUS, true);
    return v;
}

static void TestBoltOrdering()
{
    CHECK(HEALER_RADIUS - RANGED_RADIUS >= 8.0f);
    CHECK(HealersAmongNearest(Bodies(14, 17, 9), BOLT_TARGETS) == 0);
    CHECK(HealersAmongNearest(Bodies(4, 17, 9), BOLT_TARGETS) == 0);
    CHECK(HealersAmongNearest(Bodies(0, 6, 9), BOLT_TARGETS) == 9);
}

static void TestOpenerHold()
{
    // Rev 2 (spec §7.2 #1): held until she has been parked once this attempt, failing open at 45 s. Her victim
    // no longer matters.
    CHECK(HoldFireOnHuhuran(false, 0));
    CHECK(!HoldFireOnHuhuran(true, 0));
    CHECK(HoldFireOnHuhuran(false, 20000));
    CHECK(HoldFireOnHuhuran(false, 44999));
    CHECK(!HoldFireOnHuhuran(false, 45000));
    CHECK(!HoldFireOnHuhuran(true, 44999));
}

static void TestSpotLatch()
{
    // Rev 4 (spec §9.2): spots latch at engage - true from the first view, whatever her distance or the clock.
    // Supersedes the 8 y / 30 s-within-20 y rules (pull 3's run-in clump).
    CHECK(SpotsLatch(false));
    CHECK(SpotsLatch(true));
}

static void TestParkedHysteresis()
{
    // Rev 2 (spec §7.2 #3): hysteresis 6 / 8 (was 6 / 10).
    CHECK(REPARK_RADIUS == 8.0f);
    CHECK(!UpdateParked(false, 7));
    CHECK(UpdateParked(false, 6));
    CHECK(UpdateParked(true, 7.9f));
    CHECK(UpdateParked(true, 8.0f));
    CHECK(!UpdateParked(true, 8.1f));
    CHECK(!UpdateParked(true, 9.9f));
}

static void TestThreatDiscipline()
{
    ThreatRole const D = ThreatRole::Other;
    // DPS (spec §7.2 #2): veto at own + 1500 >= 0.80 x denom; hard stop at a plain own >= 0.90 x denom.
    CHECK(!ThreatVetoed(D, 0.0f, 10000.0f));
    CHECK(!ThreatVetoed(D, 6499.0f, 10000.0f));
    CHECK(ThreatVetoed(D, 6500.0f, 10000.0f));   // 6500 + 1500 = 8000 = 0.80 x 10000
    CHECK(ThreatVetoed(D, 7000.0f, 10000.0f));
    // Early in the fight the headroom dominates: 1500 >= 0.80 x 1800, so even zero own threat is vetoed.
    CHECK(ThreatVetoed(D, 0.0f, 1800.0f));
    CHECK(!ThreatVetoed(D, 0.0f, 1900.0f));
    CHECK(!ThreatHardStop(D, 8999.0f, 10000.0f));
    CHECK(ThreatHardStop(D, 9000.0f, 10000.0f));
    CHECK(!ThreatHardStop(D, 1499.0f, 1800.0f));  // the hard stop carries no headroom
    // No cap when the denominator is 0.
    CHECK(!ThreatVetoed(D, 0.0f, 0.0f));
    CHECK(!ThreatVetoed(D, 50000.0f, 0.0f));
    CHECK(!ThreatHardStop(D, 50000.0f, 0.0f));

    // Off-tanks (rev 3, spec §8.2 #3): veto at own + 1500 >= 1.00 x MT threat, hard stop at own >= 1.05 x.
    ThreatRole const O = ThreatRole::OffTank;
    CHECK(ThreatLimit(O) == 1.00f && ThreatStop(O) == 1.05f);
    CHECK(ThreatLimit(D) == 0.80f && ThreatStop(D) == 0.90f);
    CHECK(!ThreatVetoed(O, 8499.0f, 10000.0f));
    CHECK(ThreatVetoed(O, 8500.0f, 10000.0f));   // 8500 + 1500 = 10000 = 1.00 x 10000
    CHECK(!ThreatHardStop(O, 10499.0f, 10000.0f));
    CHECK(ThreatHardStop(O, 10500.0f, 10000.0f));
    // An off-tank at 7000 vs an MT at 10000 is no longer vetoed; the DPS rule still would veto it.
    CHECK(!ThreatVetoed(O, 7000.0f, 10000.0f) && ThreatVetoed(D, 7000.0f, 10000.0f));
    // The 1.05 hard stop stays under her 110 % melee victim-switch threshold.
    CHECK(OFFTANK_THREAT_STOP < 1.10f);
    CHECK(!ThreatVetoed(O, 99999.0f, 0.0f) && !ThreatHardStop(O, 99999.0f, 0.0f));
}

static void TestThreatDenominator()
{
    // Fix round 1 (R11): order MT > backup > DPS, the Nefarian rule (BwlNefarianRules::ThreatRatio).
    CHECK(ThreatDenominator(ThreatRole::MainTank, 10000.0f, 5000.0f) == 0.0f);   // the MT is never capped
    CHECK(ThreatDenominator(ThreatRole::OffTank, 10000.0f, 5000.0f) == 10000.0f); // off-tanks vs the MT
    CHECK(ThreatDenominator(ThreatRole::Other, 10000.0f, 5000.0f) == 5000.0f);    // DPS vs the top off-tank
    CHECK(ThreatDenominator(ThreatRole::Other, 10000.0f, 0.0f) == 10000.0f);      // no off-tank threat: the MT
    CHECK(ThreatDenominator(ThreatRole::OffTank, 0.0f, 5000.0f) == 0.0f);         // no MT threat: no cap
    CHECK(ThreatDenominator(ThreatRole::Other, 0.0f, 0.0f) == 0.0f);
    // A DPS at 4000 is under the MT's cap but over the off-tank's: it is held, so the off-tank stays above it.
    ThreatRole const D = ThreatRole::Other;
    CHECK(!ThreatVetoed(D, 4000.0f, ThreatDenominator(ThreatRole::OffTank, 10000.0f, 5000.0f)));
    CHECK(ThreatVetoed(D, 4000.0f, ThreatDenominator(D, 10000.0f, 5000.0f)));
    CHECK(!ThreatHardStop(D, 4400.0f, ThreatDenominator(D, 10000.0f, 5000.0f)));
    CHECK(ThreatHardStop(D, 4500.0f, ThreatDenominator(D, 10000.0f, 5000.0f)));
    // The MT is never capped: its denominator is 0 whatever the ratios.
    CHECK(!ThreatVetoed(ThreatRole::MainTank, 99999.0f, ThreatDenominator(ThreatRole::MainTank, 10000.0f, 5000.0f)));
}

static void TestOffTankSpot()
{
    float const deg = PI_F / 180.0f;
    // Spec §7.2 #3: +-20 deg per off-tank by rank, alternating sides, widening.
    CHECK(std::fabs(OffTankBearingOffset(0) - 20.0f * deg) < 1e-5f);
    CHECK(std::fabs(OffTankBearingOffset(1) + 20.0f * deg) < 1e-5f);
    CHECK(std::fabs(OffTankBearingOffset(2) - 40.0f * deg) < 1e-5f);
    CHECK(std::fabs(OffTankBearingOffset(3) + 40.0f * deg) < 1e-5f);
    // Her position + (combat reach + 1.5) along her -> MT, rotated by the offset.
    Pt const boss{10.0f, 10.0f};
    Pt const mt{10.0f, 20.0f};  // due +y of her
    for (uint32_t r = 0; r < 4; ++r)
    {
        Pt const s = OffTankSpot(boss, mt, 3.0f, r);
        CHECK(std::fabs(Dist(s, boss) - 4.5f) < 1e-3f);
        float const bearing = std::atan2(s.y - boss.y, s.x - boss.x);
        CHECK(std::fabs(bearing - (PI_F / 2.0f + OffTankBearingOffset(r))) < 1e-4f);
    }
    // Rank 0 and 1 mirror each other across the her -> MT line.
    Pt const a = OffTankSpot(boss, mt, 3.0f, 0);
    Pt const b = OffTankSpot(boss, mt, 3.0f, 1);
    CHECK(std::fabs((a.x - boss.x) + (b.x - boss.x)) < 1e-3f && std::fabs(a.y - b.y) < 1e-3f);
    // The MT on top of her: still a finite spot at the right distance.
    Pt const d = OffTankSpot(boss, boss, 3.0f, 0);
    CHECK(std::isfinite(d.x) && std::isfinite(d.y) && std::fabs(Dist(d, boss) - 4.5f) < 1e-3f);
}

static void TestTankHealers()
{
    // Spec §7.2 #4: the first ceil(n / 2) healers by rank.
    CHECK(TankHealerCount(0) == 0);
    CHECK(TankHealerCount(1) == 1);
    CHECK(TankHealerCount(2) == 1);
    CHECK(TankHealerCount(8) == 4);
    CHECK(TankHealerCount(9) == 5);
    CHECK(IsTankHealer(0, 9) && IsTankHealer(4, 9) && !IsTankHealer(5, 9) && !IsTankHealer(8, 9));
    CHECK(!IsTankHealer(0, 0));

    // Fix round 1 (R12): the ceil(n/2) healers NEAREST the MT, ties by GUID - not a contiguous ring arc.
    std::vector<HealerDist> const h = {{30.0f, 1}, {10.0f, 2}, {38.0f, 3}, {12.0f, 4}, {25.0f, 5}};
    CHECK(IsNearestTankHealer(h, 2));   // 10
    CHECK(IsNearestTankHealer(h, 4));   // 12
    CHECK(IsNearestTankHealer(h, 5));   // 25: the 3rd of ceil(5/2) = 3
    CHECK(!IsNearestTankHealer(h, 1));  // 30
    CHECK(!IsNearestTankHealer(h, 3));  // 38: the far side is never a tank healer
    CHECK(!IsNearestTankHealer(h, 9));  // not in the list
    std::vector<HealerDist> const tie = {{20.0f, 7}, {20.0f, 3}};  // ceil(2/2) = 1: the lower GUID wins
    CHECK(IsNearestTankHealer(tie, 3) && !IsNearestTankHealer(tie, 7));
    CHECK(!IsNearestTankHealer({}, 1));
}

static void TestTankHealTarget()
{
    // The MT under 90 % wins; else the lowest-HP% role tank under 90 %; else nothing.
    CHECK(PickTankHealTarget({{true, 89.0f}, {false, 10.0f}}) == 0);
    CHECK(PickTankHealTarget({{false, 50.0f}, {true, 95.0f}, {false, 40.0f}}) == 2);
    CHECK(PickTankHealTarget({{false, 95.0f}, {true, 90.0f}, {false, 99.0f}}) == -1);
    CHECK(PickTankHealTarget({{false, 89.9f}}) == 0);
    CHECK(PickTankHealTarget({}) == -1);
}

static void TestBerserkDpsUnload()
{
    // Rev 5 §10.3 + fix round 1 (R14): in Berserk everyone goes against the MT with no headroom, keeping
    // MT > off-tank > DPS: off-tanks 1.05 / 1.08, DPS 1.00 / 1.03.
    ThreatRole const D = ThreatRole::Other;
    ThreatRole const O = ThreatRole::OffTank;
    CHECK(ThreatDenominator(D, 10000.0f, 5000.0f, true) == 10000.0f);   // MT, not the 5000 off-tank
    CHECK(ThreatDenominator(D, 10000.0f, 5000.0f, false) == 5000.0f);   // unchanged outside Berserk
    CHECK(ThreatDenominator(D, 0.0f, 5000.0f, true) == 0.0f);           // MT has no threat: no cap
    CHECK(ThreatLimit(D, true) == 1.00f && ThreatStop(D, true) == 1.03f && ThreatHeadroom(D, true) == 0.0f);
    CHECK(ThreatLimit(O, true) == 1.05f && ThreatStop(O, true) == 1.08f && ThreatHeadroom(O, true) == 0.0f);
    CHECK(!ThreatVetoed(D, 9999.0f, 10000.0f, true));
    CHECK(ThreatVetoed(D, 10000.0f, 10000.0f, true));     // no headroom: exactly 1.00
    CHECK(!ThreatVetoed(D, 0.0f, 1000.0f, true));         // the early-fight headroom no longer applies
    CHECK(!ThreatHardStop(D, 10299.0f, 10000.0f, true));
    CHECK(ThreatHardStop(D, 10300.0f, 10000.0f, true));
    CHECK(ThreatDenominator(O, 10000.0f, 5000.0f, true) == 10000.0f);
    CHECK(!ThreatVetoed(O, 10499.0f, 10000.0f, true));
    CHECK(ThreatVetoed(O, 10500.0f, 10000.0f, true));
    CHECK(!ThreatHardStop(O, 10799.0f, 10000.0f, true));
    CHECK(ThreatHardStop(O, 10800.0f, 10000.0f, true));
    // The order holds: a DPS is stopped before an off-tank would even be vetoed.
    CHECK(ThreatStop(D, true) < ThreatLimit(O, true));
    // Invariant: all four Berserk ratios are under her 110 % melee switch.
    CHECK(BERSERK_DPS_THREAT_LIMIT < 1.10f && BERSERK_DPS_THREAT_STOP < 1.10f);
    CHECK(BERSERK_OFFTANK_THREAT_LIMIT < 1.10f && BERSERK_OFFTANK_THREAT_STOP < 1.10f);
    // Pre-Berserk is unchanged.
    CHECK(ThreatLimit(O) == 1.00f && ThreatStop(O) == 1.05f && ThreatHeadroom(O) == THREAT_HEADROOM);
    CHECK(ThreatLimit(D) == 0.80f && ThreatStop(D) == 0.90f && ThreatHeadroom(D) == THREAT_HEADROOM);
    // The MT is unaffected by Berserk.
    CHECK(ThreatDenominator(ThreatRole::MainTank, 10000.0f, 5000.0f, true) == 0.0f);
}

int main()
{
    TestRingSpotStable();
    TestHealerSplash();
    TestHealerChord();
    TestBoltOrdering();
    TestOpenerHold();
    TestSpotLatch();
    TestParkedHysteresis();
    TestThreatDiscipline();
    TestThreatDenominator();
    TestOffTankSpot();
    TestTankHealers();
    TestTankHealTarget();
    TestBerserkDpsUnload();
    if (!g_fail)
        std::printf("OK\n");
    return g_fail != 0;
}
