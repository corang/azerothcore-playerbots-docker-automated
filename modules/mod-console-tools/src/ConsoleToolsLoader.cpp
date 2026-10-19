#include "Log.h"

void AddConsoleToolsCommand();

void Addmod_console_toolsScripts()
{
    LOG_INFO("server.loading", "[ConsoleTools] Registering console commands (.ctools).");
    AddConsoleToolsCommand();
}
