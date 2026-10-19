// mod-console-tools: admin commands for the WORLDSERVER CONSOLE only (SEC_CONSOLE),
// so normal player accounts can be fixed up without making them GMs. No in-game
// account can hold SEC_CONSOLE, so these are unreachable from chat.

#include "Chat.h"
#include "ChatCommand.h"
#include "CommandScript.h"
#include "DBCStores.h"
#include "Player.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include <algorithm>
#include <cctype>
#include <string>

using namespace Acore::ChatCommands;

namespace
{
    // Absolute standing at the START of the named rank (neutral -> 0, friendly -> 3000),
    // prefix-matched like .modify reputation. Not ReputationMgr::ReputationRankToStanding,
    // which returns the TOP of a rank.
    Optional<int32> RankStartStanding(std::string name)
    {
        static char const* const kRanks[MAX_REPUTATION_RANK] =
            { "hated", "hostile", "unfriendly", "neutral", "friendly", "honored", "revered", "exalted" };

        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (name.empty())
            return {};

        int32 standing = ReputationMgr::Reputation_Bottom;
        for (uint8 r = 0; r < MAX_REPUTATION_RANK; ++r)
        {
            if (std::string(kRanks[r]).compare(0, name.size(), name) == 0)
                return standing;
            standing += ReputationMgr::PointsInRank[r];
        }
        return {};
    }
}

class ConsoleToolsCommand : public CommandScript
{
public:
    ConsoleToolsCommand() : CommandScript("ConsoleToolsCommand") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable sub =
        {
            { "rep", HandleRep, SEC_CONSOLE, Console::Yes },
        };
        static ChatCommandTable root = { { "ctools", sub } };
        return root;
    }

    // .ctools rep <player> <factionId> <rank|standing>
    // Sets an ONLINE character's reputation to the start of a rank (e.g. "neutral") or
    // to an absolute standing (-42000..42999). Unlike stock .modify reputation it needs
    // no selection and accepts neutral/0 (stock treats an amount of 0 as an error).
    static bool HandleRep(ChatHandler* handler, PlayerIdentifier player, uint32 factionId,
                          Variant<int32, std::string> rank)
    {
        Player* target = player.GetConnectedPlayer();
        if (!target)
        {
            handler->SendErrorMessage("{} is not online. Log the character in, then re-run.", player.GetName());
            return false;
        }

        int32 standing = 0;
        if (rank.holds_alternative<std::string>())
        {
            Optional<int32> start = RankStartStanding(rank.get<std::string>());
            if (!start)
            {
                handler->SendErrorMessage("Unknown rank '{}'. Use hated/hostile/unfriendly/neutral/friendly/honored/revered/exalted or a number.",
                                          rank.get<std::string>());
                return false;
            }
            standing = *start;
        }
        else
            standing = std::clamp<int32>(rank.get<int32>(), ReputationMgr::Reputation_Bottom, ReputationMgr::Reputation_Cap);

        FactionEntry const* factionEntry = sFactionStore.LookupEntry(factionId);
        if (!factionEntry || factionEntry->reputationListID < 0)
        {
            handler->SendErrorMessage("Faction {} does not exist or has no reputation.", factionId);
            return false;
        }

        ReputationMgr& repMgr = target->GetReputationMgr();
        repMgr.SetOneFactionReputation(factionEntry, float(standing), false);
        repMgr.SendState(repMgr.GetState(factionEntry));

        handler->PSendSysMessage("{}: {} ({}) reputation is now {}.", target->GetName(),
                                 factionEntry->name[handler->GetSessionDbcLocale()], factionId,
                                 repMgr.GetReputation(factionEntry));
        return true;
    }
};

void AddConsoleToolsCommand()
{
    new ConsoleToolsCommand();
}
