// Standalone: g++ -std=c++17 -Wall -Wextra -Werror -o /tmp/bwlwg tests/test_bwl_wyrmguard_rules.cpp && /tmp/bwlwg
// Run from the overlay repo root AFTER patch 0020 is applied to the fork (the header lives there).
#include "../azerothcore-wotlk/modules/mod-playerbots/src/Ai/Raid/BWL/BwlWyrmguardRules.h"
#include <cmath>
#include <cstdio>

using namespace BwlWyrmguardRules;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

// Every slot settled for the full skull lead time.
static uint32_t const LED[SLOTS] = {SKULL_LEAD_MS, SKULL_LEAD_MS, SKULL_LEAD_MS};

// Three guards sitting on stations 35-40 y apart, each attacking its holder.
static PackState Spread()
{
    PackState p{};
    Pt const st[SLOTS] = {{0.0f, 0.0f}, {40.0f, 0.0f}, {20.0f, 35.0f}};
    for (int s = 0; s < SLOTS; ++s)
    {
        p.station[s] = st[s];
        p.guard[s] = st[s];
        p.alive[s] = true;
        p.onHolder[s] = true;
    }
    return p;
}

static void TestAssign()
{
    bool const g[SLOTS] = {true, true, true};
    {
        bool const t[ROLES] = {true, true, true, true};
        Assignment const a = Assign(t, g);
        CHECK(a.holderRole[0] == 0 && a.holderRole[1] == 1 && a.holderRole[2] == 2);
        CHECK(a.station[0] == 0 && a.station[1] == 1 && a.station[2] == 2);
    }
    {
        bool const t[ROLES] = {true, true, true, false};   // no spare: nothing changes
        Assignment const a = Assign(t, g);
        CHECK(a.holderRole[2] == 2 && a.station[2] == 2);
    }
    {
        bool const t[ROLES] = {true, true, false, false};  // two tanks: slot 2 folds onto the main tank
        Assignment const a = Assign(t, g);
        CHECK(a.holderRole[2] == 0 && a.station[2] == 0);
        CHECK(IsFolded(a, 2) && !IsFolded(a, 0) && !IsFolded(a, 1));
    }
    {
        bool const t[ROLES] = {true, false, false, false}; // one tank: both extras fold
        Assignment const a = Assign(t, g);
        CHECK(a.holderRole[1] == 0 && a.station[1] == 0 && a.holderRole[2] == 0 && a.station[2] == 0);
    }
    {
        bool const t[ROLES] = {true, false, true, true};   // slot-1 tank dead: the spare covers slot 1
        Assignment const a = Assign(t, g);
        CHECK(a.holderRole[1] == SPARE_ROLE && a.station[1] == 1 && a.holderRole[2] == 2);
    }
    {
        bool const t[ROLES] = {true, false, false, true};  // the spare covers ONE slot; the other folds
        Assignment const a = Assign(t, g);
        CHECK(a.holderRole[1] == SPARE_ROLE && a.holderRole[2] == 0 && a.station[2] == 0);
    }
    {
        bool const t[ROLES] = {true, true, true, true};
        bool const g2[SLOTS] = {true, false, true};          // a dead guard has no holder
        Assignment const a = Assign(t, g2);
        CHECK(a.holderRole[1] == NONE && !IsFolded(a, 1));
    }
}

static void TestSettled()
{
    bool const t[ROLES] = {true, true, true, true};
    bool const g[SLOTS] = {true, true, true};
    Assignment const a = Assign(t, g);

    PackState p = Spread();
    CHECK(IsSettled(p, a, 0) && IsSettled(p, a, 1) && IsSettled(p, a, 2));

    p.guard[1] = {40.0f + 14.9f, 0.0f};
    CHECK(IsSettled(p, a, 1));
    p.guard[1] = {40.0f + 15.1f, 0.0f};
    CHECK(!IsSettled(p, a, 1));

    p = Spread();
    p.onHolder[0] = false;
    CHECK(!IsSettled(p, a, 0));

    p = Spread();                                           // slots 0 and 1 only 24.9 y apart
    p.station[1] = {24.9f, 0.0f};
    p.guard[1] = {24.9f, 0.0f};
    CHECK(!IsSettled(p, a, 0) && !IsSettled(p, a, 1));
    p.station[1] = {25.1f, 0.0f};
    p.guard[1] = {25.1f, 0.0f};
    CHECK(IsSettled(p, a, 0) && IsSettled(p, a, 1));

    bool const t2[ROLES] = {true, true, false, false};      // folded guards never block each other
    Assignment const f = Assign(t2, g);
    p = Spread();
    p.guard[2] = {1.0f, 1.0f};
    CHECK(IsSettled(p, f, 0) && IsSettled(p, f, 2));
}

static void TestNextSkull()
{
    bool const t[ROLES] = {true, true, true, true};
    bool const g[SLOTS] = {true, true, true};
    Assignment const a = Assign(t, g);

    PackState p = Spread();
    CHECK(NextSkull(p, a, NONE, LED) == 0);
    CHECK(NextSkull(p, a, 2, LED) == 2);                    // an existing skull on a living guard is kept

    p.alive[0] = false;
    CHECK(NextSkull(p, a, 0, LED) == 1);                    // skull died: next in order

    p = Spread();
    p.guard[0] = {0.0f, 16.0f};                             // slot 0 not on its station yet
    CHECK(NextSkull(p, a, NONE, LED) == 1);

    p = Spread();
    for (int s = 0; s < SLOTS; ++s)
        p.onHolder[s] = false;
    CHECK(NextSkull(p, a, NONE, LED) == NONE);              // nothing settled: no skull yet

    bool const t2[ROLES] = {true, true, false, false};
    Assignment const f = Assign(t2, g);
    p = Spread();
    p.guard[2] = {1.0f, 1.0f};
    CHECK(NextSkull(p, f, NONE, LED) == 2);                 // the folded extra dies first
}

// Spec §4.8a: a guard is skulled only after SKULL_LEAD_MS continuously settled.
static void TestSkullLead()
{
    bool const t[ROLES] = {true, true, true, true};
    bool const g[SLOTS] = {true, true, true};
    Assignment const a = Assign(t, g);
    PackState p = Spread();

    uint32_t const none[SLOTS] = {0, 0, 0};
    CHECK(NextSkull(p, a, NONE, none) == NONE);             // just settled: no skull yet
    uint32_t const short0[SLOTS] = {SKULL_LEAD_MS - 1, SKULL_LEAD_MS, 0};
    CHECK(NextSkull(p, a, NONE, short0) == 1);              // slot 0 below the lead: slot 1 first
    uint32_t const at0[SLOTS] = {SKULL_LEAD_MS, 0, 0};
    CHECK(NextSkull(p, a, NONE, at0) == 0);                 // at the lead: marked
    CHECK(NextSkull(p, a, 2, none) == 2);                   // an existing skull on a living guard is kept

    p.guard[0] = {0.0f, 16.0f};                             // led but no longer settled: never skulled
    uint32_t const led0[SLOTS] = {SKULL_LEAD_MS * 2, 0, 0};
    CHECK(NextSkull(p, a, NONE, led0) == NONE);

    // The last living guard is exempt from the lead (as from every IsSettled check).
    bool const g2[SLOTS] = {false, false, true};
    Assignment const b = Assign(t, g2);
    p = Spread();
    p.alive[0] = p.alive[1] = false;
    p.onHolder[2] = false;
    CHECK(NextSkull(p, b, 1, none) == 2);
    // ... but with two alive the lead still applies.
    bool const g12[SLOTS] = {false, true, true};
    Assignment const c = Assign(t, g12);
    p = Spread();
    p.alive[0] = false;
    CHECK(NextSkull(p, c, 0, none) == NONE);
    uint32_t const led2[SLOTS] = {0, 0, SKULL_LEAD_MS};
    CHECK(NextSkull(p, c, 0, led2) == 2);
}

static void TestMelee()
{
    bool const t[ROLES] = {true, true, true, true};
    bool const g[SLOTS] = {true, true, true};
    Assignment const a = Assign(t, g);

    PackState p = Spread();
    CHECK(MeleeHeld(p, a, NONE, 0));                        // no skull: held
    CHECK(!MeleeHeld(p, a, 0, 0));                          // skull separated: released

    p.guard[1] = {20.0f, 0.0f};                             // slot 1 still 20 y from skull
    CHECK(MeleeHeld(p, a, 0, MELEE_TIMEOUT_MS - 1));
    CHECK(!MeleeHeld(p, a, 0, MELEE_TIMEOUT_MS));           // the timeout relaxes to 20 y: released
    p.guard[1] = {19.0f, 0.0f};
    CHECK(MeleeHeld(p, a, 0, MELEE_TIMEOUT_MS));            // below 20 y no melee spot is safe: held
    CHECK(MeleeHeld(p, a, 0, MELEE_TIMEOUT_MS * 10));       // ... however long it has been
    p.guard[1] = {40.0f, 0.0f};
    CHECK(!MeleeHeld(p, a, 0, MELEE_TIMEOUT_MS));
    CHECK(MeleeHeld(p, a, NONE, MELEE_TIMEOUT_MS));         // no skull: held even after the timeout

    p = Spread();
    CHECK(StompThreat(p, a, Pt{5.0f, 0.0f}, NONE) == 0);    // held: even skull is avoided
    CHECK(StompThreat(p, a, Pt{5.0f, 0.0f}, 0) == NONE);    // released on skull 0: its station exempt
    CHECK(StompThreat(p, a, Pt{30.0f, 0.0f}, 0) == 1);      // 10 y from slot 1: step away
    CHECK(StompThreat(p, a, Pt{20.0f, 0.0f}, 0) == NONE);   // exactly 20 y from both: clear

    bool const t2[ROLES] = {true, true, false, false};
    Assignment const f = Assign(t2, g);
    p.guard[2] = {1.0f, 1.0f};
    CHECK(StompThreat(p, f, Pt{3.0f, 0.0f}, 0) == NONE);    // folded guard on skull's station exempt
}

static void TestRearSpot()
{
    bool const t[ROLES] = {true, true, true, true};
    bool const g[SLOTS] = {true, true, true};
    Assignment const a = Assign(t, g);

    PackState p = Spread();
    Pt const rear = RearPoint(p.guard[0], 0.0f);            // skull 0 faces +x: rear spot at (-4, 0)
    CHECK(std::fabs(rear.x + REAR_SPOT_DISTANCE) < 0.001f && std::fabs(rear.y) < 0.001f);
    CHECK(RearSpotClear(p, a, 0, rear));                    // other guards 40+ y away

    p.guard[1] = {10.0f, 0.0f};                             // slot 1 14 y from the rear spot
    CHECK(!RearSpotClear(p, a, 0, rear));
    p.guard[1] = {12.0f, 0.0f};                             // exactly 16 y: clear
    CHECK(RearSpotClear(p, a, 0, rear));
    p.alive[1] = false;
    p.guard[1] = {-4.0f, 1.0f};                             // a dead guard never blocks
    CHECK(RearSpotClear(p, a, 0, rear));
    CHECK(!RearSpotClear(p, a, NONE, rear));                // no skull: nothing to stand behind

    bool const t2[ROLES] = {true, true, false, false};
    Assignment const f = Assign(t2, g);
    p = Spread();
    p.guard[2] = {1.0f, 1.0f};                              // folded onto skull's station: exempt
    CHECK(RearSpotClear(p, f, 0, rear));
}

static void TestDistToPath()
{
    Pt const path[3] = {{0.0f, 0.0f}, {10.0f, 0.0f}, {10.0f, 10.0f}};
    CHECK(std::fabs(DistToPath(Pt{5.0f, 3.0f}, path, 3) - 3.0f) < 0.001f);    // beside segment 1
    CHECK(std::fabs(DistToPath(Pt{12.0f, 5.0f}, path, 3) - 2.0f) < 0.001f);   // beside segment 2
    CHECK(std::fabs(DistToPath(Pt{-4.0f, -3.0f}, path, 3) - 5.0f) < 0.001f);  // beyond the start
    CHECK(std::fabs(DistToPath(Pt{13.0f, 14.0f}, path, 3) - 5.0f) < 0.001f);  // beyond the end
    CHECK(std::fabs(DistToPath(Pt{5.0f, 5.0f}, path, 3) - 5.0f) < 0.001f);    // equidistant corner
    CHECK(std::fabs(DistToPath(Pt{3.0f, 4.0f}, path, 1) - 5.0f) < 0.001f);    // one-point path
    CHECK(EBONROC_KEEPOUT_EXIT > EBONROC_KEEPOUT_ENTER);
}

static void TestAdopt()
{
    bool const g[SLOTS] = {true, true, true};
    {
        // At the pull the main tank has all three: only slot 0 stays with it, the others keep
        // their assigned off-tanks (who taunt them away).
        bool const t[ROLES] = {true, true, true, true};
        Assignment a = Assign(t, g);
        PackState const p = Spread();
        int const v[SLOTS] = {0, 0, 0};
        AdoptByVictim(a, p, v);
        CHECK(a.holderRole[0] == 0 && a.holderRole[1] == 1 && a.holderRole[2] == 2);
        CHECK(a.station[0] == 0 && a.station[1] == 1 && a.station[2] == 2);
    }
    {
        // Victim already the holder: nothing changes.
        bool const t[ROLES] = {true, true, true, true};
        Assignment a = Assign(t, g);
        PackState const p = Spread();
        int const v[SLOTS] = {0, 1, 2};
        AdoptByVictim(a, p, v);
        CHECK(a.holderRole[0] == 0 && a.holderRole[1] == 1 && a.holderRole[2] == 2);
    }
    {
        // Main tank dead holding the last guard (slot 0); no spare, so Assign folds it onto the
        // dead main tank. Role 1 picked it up via tank assist: it adopts, station unchanged.
        bool const t[ROLES] = {false, true, true, false};
        bool const g0[SLOTS] = {true, false, false};
        Assignment a = Assign(t, g0);
        PackState p = Spread();
        p.alive[1] = p.alive[2] = false;
        int const v[SLOTS] = {1, NONE, NONE};
        AdoptByVictim(a, p, v);
        CHECK(a.holderRole[0] == 1 && a.station[0] == 0);
        CHECK(a.holderRole[1] == NONE && a.holderRole[2] == NONE);
    }
    {
        // The spare covers slot 2 but role 1 has its guard: role 1 already holds the living
        // slot 1, so it cannot adopt slot 2.
        bool const t[ROLES] = {false, true, false, true};
        bool const g2[SLOTS] = {false, true, true};
        Assignment a = Assign(t, g2);
        CHECK(a.holderRole[1] == 1 && a.holderRole[2] == SPARE_ROLE);  // spare covers slot 2
        PackState p = Spread();
        p.alive[0] = false;
        int const v[SLOTS] = {NONE, 1, 1};
        AdoptByVictim(a, p, v);
        CHECK(a.holderRole[1] == 1 && a.holderRole[2] == SPARE_ROLE);
    }
    {
        // Spare covering slot 1 while role 2's tank has slot 1's guard: role 2 already holds the
        // living slot 2, so it cannot adopt slot 1.
        bool const t[ROLES] = {true, false, true, true};
        Assignment a = Assign(t, g);
        PackState const p = Spread();
        int const v[SLOTS] = {0, 2, 2};
        AdoptByVictim(a, p, v);
        CHECK(a.holderRole[1] == SPARE_ROLE && a.holderRole[2] == 2);
    }
    {
        // An adoption made earlier in the pass counts: once role 1 adopts slot 0 it cannot also
        // adopt slot 2.
        bool const t[ROLES] = {false, true, true, true};
        bool const g01[SLOTS] = {true, false, true};
        Assignment a = Assign(t, g01);
        CHECK(a.holderRole[0] == SPARE_ROLE && a.holderRole[2] == 2);
        PackState p = Spread();
        p.alive[1] = false;
        int const v[SLOTS] = {1, NONE, 1};                  // role 1 has both living guards
        AdoptByVictim(a, p, v);
        CHECK(a.holderRole[0] == 1);                        // slot 0 adopted (role 1 held nothing)
        CHECK(a.holderRole[2] == 2);                        // slot 2: role 1 now holds slot 0
    }
    {
        // A dead guard is never adopted.
        bool const t[ROLES] = {true, true, true, true};
        bool const g1[SLOTS] = {true, false, true};
        Assignment a = Assign(t, g1);
        PackState p = Spread();
        p.alive[1] = false;
        int const v[SLOTS] = {0, 3, 2};
        AdoptByVictim(a, p, v);
        CHECK(a.holderRole[1] == NONE);
    }
}

static void TestLastGuard()
{
    bool const t[ROLES] = {false, true, true, false};
    bool const g0[SLOTS] = {true, false, false};
    Assignment const a = Assign(t, g0);
    PackState p = Spread();
    p.alive[1] = p.alive[2] = false;
    p.guard[0] = {60.0f, 60.0f};                            // far off its station
    p.onHolder[0] = false;                                  // and not on its holder
    CHECK(IsSettled(p, a, 0));
    CHECK(NextSkull(p, a, NONE, LED) == 0);
    CHECK(!IsSettled(p, a, 1));                             // a dead guard is never settled

    bool const t4[ROLES] = {true, true, true, true};
    bool const g2[SLOTS] = {false, false, true};
    Assignment const b = Assign(t4, g2);
    p = Spread();
    p.alive[0] = p.alive[1] = false;
    p.guard[2] = {-100.0f, 0.0f};
    p.onHolder[2] = false;
    CHECK(NextSkull(p, b, NONE, LED) == 2);

    bool const g01[SLOTS] = {true, true, false};
    p = Spread();                                           // two alive: rules still apply
    p.alive[2] = false;
    p.onHolder[0] = false;
    CHECK(!IsSettled(p, Assign(t4, g01), 0));
}

// One living guard at the origin (slot 0); the other two dead.
static PackState OneGuard()
{
    PackState p{};
    p.alive[0] = true;
    p.guard[0] = {0.0f, 0.0f};
    return p;
}

static void TestHealerRules()
{
    // Tank counts: range is HEALER_RANGE inclusive, 2D.
    Pt const tanks[3] = {{0.0f, 0.0f}, {36.0f, 0.0f}, {36.1f, 0.0f}};
    CHECK(TanksReached(Pt{0.0f, 0.0f}, tanks, 3) == 2);
    CHECK(TanksReached(Pt{18.0f, 0.0f}, tanks, 3) == 3);
    CHECK(TanksReached(Pt{0.0f, 0.0f}, tanks, 0) == 0);

    // Stomp boundary.
    PackState p = OneGuard();
    CHECK(!StompClear(Pt{17.9f, 0.0f}, p));
    CHECK(StompClear(Pt{18.1f, 0.0f}, p));
    p.alive[0] = false;
    CHECK(StompClear(Pt{1.0f, 0.0f}, p));                   // dead guards never count

    // Walk passing a guard is rejected; a walk around it is not.
    p = OneGuard();
    CHECK(!WalkClear(Pt{-20.0f, 5.0f}, Pt{20.0f, 5.0f}, p));
    CHECK(WalkClear(Pt{-20.0f, 17.0f}, Pt{20.0f, 17.0f}, p));
    CHECK(WalkClear(Pt{-20.0f, 5.0f}, Pt{-30.0f, 5.0f}, p));  // walking away from it
    // Starting inside KEEP_AWAY: leaving is fine, closing in is not.
    CHECK(WalkClear(Pt{10.0f, 0.0f}, Pt{30.0f, 0.0f}, p));
    CHECK(!WalkClear(Pt{10.0f, 0.0f}, Pt{5.0f, 0.0f}, p));

    // Current spot kept when nothing is better: stomp clear, both tanks in range.
    Pt const two[2] = {{40.0f, 0.0f}, {40.0f, 20.0f}};
    Pt const cands[3] = {{25.0f, 0.0f}, {30.0f, 10.0f}, {20.0f, 30.0f}};
    CHECK(BestSpot(Pt{25.0f, 10.0f}, cands, 3, two, 2, p) == NONE);

    // In stomp range: the nearest stomp-clear candidate wins, even over a farther one.
    Pt const out[3] = {{30.0f, 0.0f}, {22.0f, 0.0f}, {10.0f, 0.0f}};
    CHECK(BestSpot(Pt{17.0f, 0.0f}, out, 3, two, 2, p) == 1);

    // More tanks beats a shorter move.
    Pt const pair[2] = {{-25.0f, 0.0f}, {25.0f, 0.0f}};    // from (-20,20) only the west tank reaches
    Pt const reach[2] = {{-20.0f, 25.0f}, {0.0f, 25.0f}};  // (0,25): both tanks (35.4 y), guard 25 y away
    CHECK(BestSpot(Pt{-20.0f, 20.0f}, reach, 2, pair, 2, p) == 1);

    // A better spot whose walk passes the guard is rejected.
    PackState q = OneGuard();
    q.guard[0] = {-15.0f, 10.0f};
    Pt const through[1] = {{0.0f, 25.0f}};                  // reaches both, the walk crosses the guard
    CHECK(StompClear(Pt{-32.0f, 0.0f}, q) && StompClear(through[0], q));
    CHECK(BestSpot(Pt{-32.0f, 0.0f}, through, 1, pair, 2, q) == NONE);

    // Fewer-tank fallback: no spot reaches both, so the healer takes the best it can (one) and
    // does not walk for nothing when it already has one.
    Pt const far[2] = {{-100.0f, 0.0f}, {100.0f, 0.0f}};
    Pt const fb[2] = {{-75.0f, 20.0f}, {-90.0f, 20.0f}};
    CHECK(BestSpot(Pt{-50.0f, 20.0f}, fb, 2, far, 2, p) == 0);         // 0 tanks -> 1 tank, the nearer one
    CHECK(BestSpot(Pt{-75.0f, 20.0f}, fb + 1, 1, far, 2, p) == NONE);  // already at 1: stay

    // Strict improvement only: an equal spot farther away never wins.
    SpotScore const here{true, true, 2, 0.0f};
    SpotScore const there{true, true, 2, 5.0f};
    CHECK(!BetterSpot(there, here) && BetterSpot(here, there));
    CHECK(!BetterSpot(here, here));
}

int main()
{
    TestAdopt();
    TestLastGuard();
    TestRearSpot();
    TestDistToPath();
    TestAssign();
    TestSettled();
    TestNextSkull();
    TestSkullLead();
    TestMelee();
    TestHealerRules();
    if (g_fail)
    {
        std::printf("%d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("all BWL wyrmguard rule tests passed\n");
    return 0;
}
