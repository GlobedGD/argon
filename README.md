# Argon

API for authenticating Geometry Dash accounts. Made by creators of Globed, and is completely free and open-source, including the [server](https://github.com/GlobedGD/argon-server). Can be selfhosted (for example for a GDPS), but we provide our server at https://argon.globed.dev and this library uses it by default.

Benefits compared to some of the other auth APIs (Globed, DashAuth, GDAuth):

* Never sends your GJP or password to any 3rd party server
* [Built-in linking for Discord accounts, easily usable by other mods](#discord-linking)
* Shared token storage, account verification is done once and the token is then reused by many mods
* Multi-account support, making you not need to worry about the user switching accounts
* Our official instance is whitelisted by RobTop, which means authentication is faster and more reliable against IP blocks
* Supports multiple authentication methods, to be able to fallback if an issue is encountered (e.g. user is comment banned)

## Note on v2 migration

If you've used Argon previously, please see the [migration guide](./docs/v2-migration.md).

## Usage (client-side)

First, add Argon as a dependency in your `mod.json`:
```json
"dependencies": {
    "dankmeme.argon": ">=2.0.0"
}
```

Auth is performed via the `argon::startAuth` function, which returns a future that resolves to a `Result<std::string>` with the user's token:

```cpp
#include <argon.hpp>
#include <Geode/Geode.hpp>

using namespace geode::prelude;

struct MyLayer : public CCLayer {
    async::TaskHolder<Result<std::string>> m_listener;

    void startAuth() {
        m_listener.spawn(
            argon::startAuth(),
            [](Result<std::string> result) {
                if (result.isOk()) {
                    auto token = std::move(result).unwrap();
                    log::info("Got token: {}", token);
                } else {
                    log::warn("Failed to authenticate: {}", result.unwrapErr());
                }
            }
        );
    }
};
```

If you want to authenticate without needing to be in a specific layer, you can use Geode's `async::spawn` instead:

```cpp
async::spawn(
    argon::startAuth(),
    [](Result<std::string> result) {
        // handle result
    }
);
```

If running into token validation issues, tokens should be cleared before attempting to authenticate again:

```cpp
argon::clearToken();
```

If you want to use a custom server instead of the one provided by us (for example to support GDPS), the URL can be set like so:

```cpp
argon::overrideArgonServer("http://localhost:4341");
```

Few more functions are provided for managing tokens and for ensuring thread safety, you can find out about the rest of the functionality by reading the docstrings in `include/argon/argon.hpp` header.

## Usage (server-side)

The base url for our Argon server is https://argon.globed.dev

After the user has generated an authtoken via `startAuth`, they should send it to your server, which in turn will send the token to the Argon server for validation. Below is an example in Python on how you would validate the token.

```py
import requests

BASE_URL = "https://argon.globed.dev/v2"

def validate(token: str):
    # This function will validate the account ID and token that your user sent to your server.

    r = requests.get(f"{BASE_URL}/v2/token/validate", json={
        "token": token
    })

    # make sure to check the status code is 200, otherwise return an error to the user!
    if r.status_code != 200:
        raise ValueError(f"Error from argon (code {r.status_code}): {r.text}")

    # check actual token validity
    resp = r.json()
    if not resp["valid"]:
        raise ValueError("Invalid token")

    # if resp["valid"] is true, this means the token is *valid*.
    # however, it's also important to check *who* the token belongs to, to avoid impersonation.
    account_data = resp["account_data"]

    # account_data will contain account_id, user_id and username, which confirm the user's identity to you
    # make sure to always check them instead of trusting client-supplied id/username!

    return
```

> [!NOTE]
> The validation endpoint has rate limits. For most smaller mods they are relatively high, but big mods may require an increased rate limit. Contact me on Discord (`@dank_meme01`) and you may qualify for a developer account with an API key that increases rate limits.

## Security practices

These are the practices that are highly recommended to follow when using Argon.

### Never store the token

> [!CAUTION]
> This is important.

There is only one correct flow for using Argon tokens: `startAuth()` -> send to your backend -> backend calls `/v2/token/validate` -> token is discarded. Do not implement any kind of token caching, or worse yet, store Argon tokens in a database. Tokens issued by `startAuth` are *access* tokens, they are valid for a short time period and used for proving your identity to a specific service one time (though not strictly one-use). Storing them wouldn't lead to a massive security issue, but it's likely unnecessary and will lead to logical pitfalls.

This includes client-side caching too: you should *always* call `startAuth` right before using the token to call an API, instead of caching the result somewhere. Argon itself does cache access tokens to reduce unnecessary requests, but your code can't know when the token expires and should be obtained again.

If you find yourself needing to make a token cache because a user makes many requests with the same access token and you want to reduce the amount of API calls to argon, there are two recommended solutions:
1. Don't, and request a better rate limit instead. This is the highest level of security, as it keeps Argon as your centralized Authentication & Authorization solution and makes it hard to make mistakes. In case of token theft, we can blacklist tokens on our backend to kill hijacking attempts.
2. Implement your own session system. Instead of using the Argon token for every action, use it once to login and generate your own session token/ID that is valid for only your service. This can be JWT or a regular secret-based ID, depending on what's best for your backend. This gives you more control over sessions, but it shifts more responsibilities on you, such as token validation, expiry and revocation.

### Always validate the account data

The token validation endpoint will always return the account data that's embedded in the token, so you *should* use that as the ground truth. In v1 we recommended that you make the client pass account details to your backend, and then call Argon with those details to verify if the identity in the token is valid. That was deemed unnecessary, so the API was simplified, and now it's instead recommended to *not* pass account data, and only pass the Argon token to your backend, and then use that to derive the user's account ID & username.

### Use TLS

Prefer using end-to-end encrypted transports like HTTPS or WSS for sending the token from the client to your backend.

## Discord linking

Argon allows users to link their Discord account to their Argon account, allowing you to use it as a centralized solution for this, without needing to reimplement the linking process in every mod you make. There are two flows to facilitate this process:

1. Open the account management website

```cpp
async::spawn(
    argon::openAccountPage(),
    [](auto result) { /* handle potential errors */ }
);
```

This will open the Argon account management page in their browser and automatically log the user in, where they can link/unlink their account and manage other settings.

2. Link programmatically

If you want a more fancy GUI for the process, you can implement one yourself and use direct API calls.

```cpp
#include <argon.hpp>

/// Returns a Result<std::optional<UserData>>
/// On success, this contains the linked discord account, if present
auto result = GEODE_CO_UNWRAP(co_await argon::discord::getLinkState());

if (result) {
    log::info("Linked to {} (id {})", result->username, result->id);
} else {
    log::info("Not linked to a Discord account");
}

/// Returns an OAuth2 authorization URL that the user needs to open in their browser and authorize
/// After the user clicks "authorize", if the process is successful, `getLinkState` will now return the Discord account data.
auto url = GEODE_CO_UNWRAP(co_await argon::discord::link());
geode::utils::web::openLinkInBrowser(url);

/// Unlinks the account
GEODE_CO_UNWRAP(co_await argon::discord::unlink());
```

### Receiving Discord account data on the backend

To check if the user has a linked Discord account, you can conveniently reuse the `POST /v2/token/validate` endpoint, by including `"include_discord": true`:
```json
{
    "token": "....",
    "include_discord": true
}
```

If the token is valid and the user is linked to a Discord account, the response will include a `discord_user` object:
```json
{
    "valid": true,
    "account_data": {
        "account_id": 29843187,
        "user_id": 256456125,
        "username": "globedbotxd13"
    },
    "discord_user": {
        "id": 612930885230002208,
        "username": "dank_meme01",
        "avatar_url": "https://cdn.discordapp.com/avatars/612930885230002208/7022f122ed40fa8837666706d12efc55.webp?size=1024"
    }
}
```
