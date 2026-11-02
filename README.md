# Argon

API for authenticating Geometry Dash accounts. Made by creators of Globed, and is completely free and open-source, including the [server](https://github.com/GlobedGD/argon-server). Can be selfhosted (for example for a GDPS), but we provide our server at https://argon.globed.dev and this library uses it by default.

Benefits compared to some of the other auth APIs (Globed, DashAuth, GDAuth):

* Does not send your GJP or password to any 3rd party server
* Tokens are stored globally, meaning that authentication is done only once even if the user has multiple mods installed that use Argon
* Smooth UX for switching accounts (will not try to use or overwrite the token of the other account)
* Our official instance is whitelisted by RobTop, which means authentication is faster and more reliable against IP blocks
* Ability to retrieve user's username without making requests to the GD server (with just their token)
* Automatic troubleshooter for figuring out the cause of auth issues (for example invalid session)

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
        raise ValueError(f"Invalid token: {resp["cause"]}")

    # if resp["valid"] is true, this means the token is *valid*.
    # however, it's also important to check *who* the token belongs to, to avoid impersonation.
    account_data = resp["account_data"]

    # account_data will contain account_id, user_id and username, which confirm the user's identity to you
    # make sure to always check them instead of trusting client-supplied id/username!

    return
```
