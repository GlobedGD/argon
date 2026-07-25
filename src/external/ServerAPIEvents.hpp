// https://github.com/AlphiiGD/server-api/blob/main/include/ServerAPIEvents.hpp

#pragma once

// std
#include <string>
#include <utility>

#include <map>

// geode
#include <Geode/loader/Mod.hpp>
#include <Geode/loader/Dispatch.hpp>
#include <Geode/utils/StringMap.hpp>

#ifdef MY_MOD_ID
#undef MY_MOD_ID
#endif

#define MY_MOD_ID "km7dev.server_api"

struct ServerUpdatingEvent : ::geode::Event<ServerUpdatingEvent, bool(const ::geode::Mod*, const ::std::string&)> {
  using Event::Event;
};

// thx prevter for the help

namespace ServerAPIEvents {

/// @brief Represents server information
struct Server {
    /// @brief Handle for server info
    int id;
    /// @brief URL of the server (duh)
    ::std::string url;
    /// @brief ServerAPI will use the server with the highest priority for GD's online features
    int priority;
};

/// @brief Gets copy of the server currently in use by ServerAPI
/// @return Server struct representing server currently in use by ServerAPI
inline Server getCurrentServer() GEODE_EVENT_EXPORT_NORES(&getCurrentServer, ());

/// @brief Gets copy of server information from its ID
/// @return Server struct representing the server with the specified ID
inline Server getServerById(int id) GEODE_EVENT_EXPORT_NORES(&getServerById, (id));

/// @brief Registers a server for ServerAPI to use
/// @param url The server's URL (e.g. https://www.boomlings.com/)
/// @param priority Its priority with respect to other servers competing for GD to connect to
/// @return Server struct representing the newly registered server
inline Server registerServer(::std::string url, int priority = 0, ::geode::Mod* sender = ::geode::Mod::get()) GEODE_EVENT_EXPORT_NORES(&registerServer, (::std::move(url), priority, sender));

/// @brief Updates an existing server
/// @param id Handle to an already existing server
/// @param url The new url for the server to use
/// @param priority The new priority for ServerAPI
inline void updateServer(int id, ::std::string url, int priority, ::geode::Mod* sender = ::geode::Mod::get()) GEODE_EVENT_EXPORT_NORES(&updateServer, (id, ::std::move(url), priority, sender));

/// @brief Updates an existing server
/// @param server The information to update the server with, the ID being a handle to the already existing server
inline void updateServer(Server server, ::geode::Mod* sender = ::geode::Mod::get()) {
    updateServer(server.id, ::std::move(server.url), server.priority, sender);
}

/// @brief Updates an existing server without updating its priority
/// @param id Handle to an already existing server
/// @param url The new url for the server to use
inline void updateServer(int id, ::std::string url, ::geode::Mod* sender = ::geode::Mod::get()) {
    updateServer(id, ::std::move(url), 0, sender);
}

/// @brief Updates an existing server without updating its URL
/// @param id Handle to an already existing server
/// @param priority The new priority for ServerAPI
inline void updateServer(int id, int priority, ::geode::Mod* sender = ::geode::Mod::get()) {
    updateServer(id, "", priority, sender);
}

/// @brief Removes a server from the registry
/// @param id Handle to the server to remove
inline void removeServer(int id, ::geode::Mod* sender = ::geode::Mod::get()) GEODE_EVENT_EXPORT_NORES(&removeServer, (id, sender));

/// @brief Gets the base (https) URL for built-in servers
/// @return Base (https) URL for built-in servers (boomlings.com on vanilla GD)
inline ::std::string getBaseUrl() GEODE_EVENT_EXPORT_NORES(&getBaseUrl, ());

/// @brief Gets the secondary (http) URL for built-in servers
/// @return Secondary (http) URL for built-in servers (boomlings.com on vanilla GD)
inline ::std::string getSecondaryUrl() GEODE_EVENT_EXPORT_NORES(&getSecondaryUrl, ());

/// @brief Gets a copy the entire registry of servers
/// @return Map of int (ID) and pair of string (URL) and int (Priority)
inline ::std::map<int, ::std::pair<::std::string, int>> getRegisteredServers() GEODE_EVENT_EXPORT_NORES(&getRegisteredServers, ());
} // namespace ServerAPIEvents

namespace ServerAPITrust {
/// @brief Enum representing how trusted a Mod is to clean up after itself.
///
/// ServerAPI switches servers on a dime without warning other mods which
/// may affect crucial systems relying on the server GD is connected to
/// staying the same. While one mod may switch servers while preventing
/// GD from saving player save data to disk, another may not, so this
/// enum represents how trusted a mod is to use ServerAPI safely.
enum class TrustLevel {
    Untrusted = 0, Trusted, HighlyTrusted
};

/// @brief Gets a map of mod IDs to their TrustLevel representing how trusted they are
/// @return Map of mod IDs to their TrustLevel representing how trusted they are
inline ::geode::utils::StringMap<TrustLevel> getTrustedMods() GEODE_EVENT_EXPORT_NORES(&getTrustedMods, ());

/// @brief Gets the TrustLevel associated with a mod using its ID
/// @param modID Mod's ID from its mod.json
/// @return Its trurst level as a TrustLevel enum
inline TrustLevel trustLevelFor(::geode::ZStringView modID) {
    auto trustedMods = getTrustedMods();
    auto it = trustedMods.find(modID);
    if (it == trustedMods.end()) {
        return TrustLevel::Untrusted;
    }
    return it->second;
}

/// @brief Gets the TrustLevel associated with a mod
/// @param mod Pointer to the mod
/// @return Its trurst level as a TrustLevel enum
inline TrustLevel trustLevelFor(::geode::Mod* mod) {
    return trustLevelFor(mod->getID());
}

/// @param modID Mod's ID from its mod.json
/// @return True if the mod is HighlyTrusted
inline bool isHighlyTrusted(::geode::ZStringView modID) {
    auto trustedMods = getTrustedMods();
    auto it = trustedMods.find(modID);
    if (it == trustedMods.end()) {
        return false;
    }
    return it->second == TrustLevel::HighlyTrusted;
}

/// @param mod Pointer to the mod
/// @return True if the mod is HighlyTrusted
inline bool isHighlyTrusted(::geode::Mod* mod) {
    return isHighlyTrusted(mod->getID());
}

/// @param modID Mod's ID from its mod.json
/// @return True if the mod is Trusted or HighlyTrusted
inline bool isTrusted(::geode::ZStringView modID) {
    auto trustedMods = getTrustedMods();
    auto it = trustedMods.find(modID);
    if (it == trustedMods.end()) {
        return false;
    }
    return it->second >= TrustLevel::Trusted;
}

/// @param mod Pointer to the mod
/// @return True if the mod is Trusted or HighlyTrusted
inline bool isTrusted(::geode::Mod* mod) {
    return isTrusted(mod->getID());
}
} // namespace ServerAPITrust

#undef MY_MOD_ID