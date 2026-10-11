// as_rcon — Plugify port of the Pisex cs2-admin_system module "Rcon" (as_rcon.cpp).
//
// Behaviour kept identical:
//   * Console command `mm_rcon <command line>`: the server executes the argument line through the
//     engine, and every use is published to the other admin plugins through
//     admin_system::SendAction(slot, "rcon", command).
//   * A client needs the @admin/rcon permission; the server console (-1) always passes.
//   * Without arguments the caller gets the translated "UsageRcon" text on their console (the
//     server console gets it as a log line).
//   * The command is registered with the Metamod flags the original used
//     (FCVAR_GAMEDLL | FCVAR_SERVER_CAN_EXECUTE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_NOTIFY), so
//     connected clients can execute it from their console exactly like before.
//
// Port notes:
//   * the call to the engine's server-command interface -> s2sdk::ServerCommand(...)
//   * the raw remainder of the command line -> the s2sdk argv[1..] joined with single spaces; the
//     engine tokenises the line anyway, so only the whitespace *between* the tokens is normalised.
//   * The original also fetched g_pPlayers/g_pUtils in AllPluginsLoaded; the body itself never
//     used them, so only pisexcompat is linked for the printers and the per-frame pump
//     (pisex::Poll()) that the admin_system API needs.
//   * No client is queried here without a running map: the command can only reach us from the
//     server console or from a connected client, and pisexcompat's printers are no-ops before the
//     first map.

#include <admin_api.hpp>
#include <pisex/pisex.hpp>
#include <plg/plugin.hpp>
#include <plg/string.hpp>
#include <plugin_export.h>
#include <s2sdk/s2sdk.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

namespace {

admin_system::AdminApi* g_pAdminApi = &admin_system::AdminApi::Get();

constexpr const char* kRconCommand = "mm_rcon";

// The flags of the Metamod CON_COMMAND_F + ConVar_Register pair of the original.
constexpr s2sdk::ConVarFlag kRconCommandFlags =
        static_cast<s2sdk::ConVarFlag>(static_cast<int64_t>(s2sdk::ConVarFlag::GameDll) |
                                       static_cast<int64_t>(s2sdk::ConVarFlag::ServerCanExecute) |
                                       static_cast<int64_t>(s2sdk::ConVarFlag::ClientCanExecute) |
                                       static_cast<int64_t>(s2sdk::ConVarFlag::Notify));

void LogMessage(std::string_view message, plg::Severity severity) {
	if (plg::Log) {
		plg::Log(message, severity, plg::source_location::current());
	}
}

void LogInfo(std::string_view message) {
	LogMessage(message, plg::Severity::Info);
}

void LogError(std::string_view message) {
	LogMessage(message, plg::Severity::Error);
}

// Same as ArgS(): everything the caller wrote after the command name, space-joined.
std::string JoinArguments(const plg::vector<plg::string>& args, std::size_t first) {
	std::string joined;
	for (std::size_t i = first; i < args.size(); ++i) {
		if (!joined.empty()) {
			joined.push_back(' ');
		}
		joined.append(args[i].data(), args[i].size());
	}
	return joined;
}

class RconPlugin final : public plg::Plugin {
public:
	plg::PluginResult OnPluginStart() override;
	plg::PluginResult OnPluginUpdate(std::chrono::milliseconds dt) override;
	plg::PluginResult OnPluginEnd() override;

	// Body of the s2sdk console-command callback (game thread).
	s2sdk::ResultType HandleRcon(int32_t caller, const plg::vector<plg::string>& args);

private:
	bool m_commandRegistered = false;
};

RconPlugin g_rconPlugin;

s2sdk::ResultType OnRconCommand(int32_t caller, s2sdk::ConCommandContext context,
								const plg::vector<plg::string>& args) {
	(void)context;
	try {
		return g_rconPlugin.HandleRcon(caller, args);
	} catch (const std::exception& ex) {
		LogError(std::string("as_rcon: exception in 'mm_rcon': ") + ex.what());
	} catch (...) {
		LogError("as_rcon: unknown exception in 'mm_rcon'");
	}
	return s2sdk::ResultType::Handled;
}

plg::PluginResult RconPlugin::OnPluginStart() {
	try {
		// Never fatal (only logs): returning Failed here stops every plugify plugin.
		pisex::Init("as_rcon");

		if (!__s2sdk_AddConsoleCommand) {
			LogError("as_rcon: the s2sdk bindings are unavailable - 'mm_rcon' is not registered");
			return {};
		}
		m_commandRegistered = s2sdk::AddConsoleCommand(kRconCommand,
													   "Executes a server command (admin rcon)",
													   kRconCommandFlags, &OnRconCommand,
													   s2sdk::HookMode::Pre);
		if (m_commandRegistered) {
			LogInfo("as_rcon: ready; 'mm_rcon' registered");
		} else {
			LogError("as_rcon: registering the 'mm_rcon' console command failed (continuing without it)");
		}
	} catch (const std::exception& ex) {
		LogError(std::string("as_rcon: OnPluginStart failed: ") + ex.what());
	} catch (...) {
		LogError("as_rcon: OnPluginStart failed with an unknown exception");
	}
	return {};
}

plg::PluginResult RconPlugin::OnPluginUpdate(std::chrono::milliseconds dt) {
	(void)dt;
	try {
		pisex::Poll(); // timers / deferred work / maps of pisexcompat; also marks "a map is running"
	} catch (const std::exception& ex) {
		// An exception must not unwind into the module (the other plugins of the family guard the
		// pump the same way).
		LogError(std::string("as_rcon: Poll() failed: ") + ex.what());
	} catch (...) {
		LogError("as_rcon: Poll() failed with an unknown exception");
	}
	return {};
}

plg::PluginResult RconPlugin::OnPluginEnd() {
	try {
		if (m_commandRegistered) {
			m_commandRegistered = false;
			if (__s2sdk_RemoveCommand) {
				s2sdk::RemoveCommand(kRconCommand, &OnRconCommand);
			}
		}
	} catch (const std::exception& ex) {
		LogError(std::string("as_rcon: OnPluginEnd failed: ") + ex.what());
	} catch (...) {
		LogError("as_rcon: OnPluginEnd failed with an unknown exception");
	}
	pisex::Shutdown();
	return {};
}

s2sdk::ResultType RconPlugin::HandleRcon(int32_t caller, const plg::vector<plg::string>& args) {
	const bool bConsole = caller == -1;
	// No permission: the original returned without a word (and without executing anything).
	if (!bConsole && !g_pAdminApi->HasPermission(caller, "@admin/rcon")) {
		return s2sdk::ResultType::Handled;
	}

	// args[0] is the command as typed (s2sdk fills the arguments from the engine's CCommand).
	if (args.size() < 2) {
		const std::string title = (!args.empty() && !args[0].empty())
										  ? std::string(args[0].data(), args[0].size())
										  : std::string(kRconCommand);
		const std::string usage = pisex::FormatString(g_pAdminApi->GetTranslation("UsageRcon"), title.c_str());
		// A negative slot prints to the server console (the original's META_CONPRINTF path).
		pisex::PrintToConsole(caller, "%s\n", usage.c_str());
		return s2sdk::ResultType::Handled;
	}

	const std::string command = JoinArguments(args, 1);
	if (__s2sdk_ServerCommand) {
		try {
			s2sdk::ServerCommand(plg::string(command));
		} catch (const std::exception& ex) {
			LogError(std::string("as_rcon: ServerCommand failed: ") + ex.what());
		}
	}
	g_pAdminApi->SendAction(caller, "rcon", command.c_str());
	return s2sdk::ResultType::Handled;
}

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_rconPlugin)
