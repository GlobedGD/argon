# Argon

API for authenticating Geometry Dash accounts. Made by creators of Globed, and is completely free and open-source, including the [server](https://github.com/GlobedGD/argon-server). Can be selfhosted (for example for a GDPS), but we provide our server at https://argon.globed.dev and this library uses it by default.

Benefits compared to some of the other auth APIs (Globed, DashAuth, GDAuth):

* Does not send your GJP or password to any 3rd party server
* Tokens are stored globally, meaning that authentication is done only once even if the user has multiple mods installed that use Argon
* Integrated in your mod, user does not need to install any dependencies
* Smooth UX for switching accounts (will not try to use or overwrite the token of the other account)
* Our official instance is whitelisted by RobTop, which means authentication is faster and more reliable against IP blocks
* Challenges don't entirely rely on the IP address, but are still secured in other ways, preventing errors if the user has a weird ISP
* Ability to retrieve user's username without making requests to the GD server (with just their token)
* Automatic troubleshooter for figuring out the cause of auth issues (for example invalid session or too many sent messages)

## Usage (client-side)

First, add Argon to the `CMakeLists.txt` of your mod:

```cmake
CPMAddPackage("gh:GlobedGD/argon@1.4.7")
target_link_libraries(${PROJECT_NAME} argon)
```

Auth is performed via the `argon::startAuth` function, which returns a future that resolves to a `Result<std::string>` with the user's token:

```cpp
#include <argon/argon.hpp>
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

**NOTE:** Calling `argon::startAuth` is NOT thread-safe unless you supply account data yourself. When you call `startAuth()` with no arguments, Argon has to call `argon::getGameAccountData`, which can **only** happen in the main thread. In the rare case that you must create the auth future while inside an async task or another thread, do this:

```cpp
// On main thread, before authentication
auto accountData = argon::getGameAccountData();

// Simulate thread/task creation
async::spawn([data = std::move(accountData)] -> arc::Future<> {
    // This is fine now, startAuth is safe if we pass account data to it
    argon::startAuth(data);
});
```

If running into token validation issues, tokens should be cleared before attempting to authenticate again:

```cpp
argon::clearToken();
```

If you want to use a custom server instead of the one provided by us, the URL can be set like so:

```cpp
argon::setServerUrl("http://localhost:4341");
```

Few more functions are provided for managing tokens and for ensuring thread safety, you can find out about the rest of the functionality by reading the docstrings in `include/argon/argon.hpp` header.

## Usage (server-side)

The base url for our Argon server is https://argon.globed.dev

After the user has generated an authtoken, they should send it to your server, which in turn will send the token to the Argon server for validation. Server-side API has its own [documentation](https://github.com/GlobedGD/argon-server/blob/main/docs/server-api.md) which describes in detail how to use it. You should read these docs, but here's a quick start in Python that you can adapt to other languages:

```py
import requests

BASE_URL = "https://argon.globed.dev/v1"

def validate(account_id: int, token: str):
    # This function will validate the account ID and token that your user sent to your server.

    r = requests.get(f"{BASE_URL}/validation/check?account_id={account_id}&authtoken={token}")

    # make sure to check the status code is 200, otherwise return an error to the user!
    if r.status_code != 200:
        raise ValueError(f"Error from argon (code {r.status_code}): {r.text}")

    # check actual token validity
    resp = r.json()
    if not resp["valid"]:
        raise ValueError(f"Invalid token: {resp["cause"]}")

    # if resp["valid"] is true, this means the token is valid and we can give user access :)
    return

def validate_strong(account_id: int, user_id: int, username: str, token: str):
    # This function will validate the account ID, user ID and username that the user sent to you.
    # It's strongly advised that you read the notes section before using the strong endpoint,
    # because it has some caveats!
    # https://github.com/GlobedGD/argon-server/blob/main/docs/server-api.md#get-v1validationcheck_strong

    r = requests.get(
        f"{BASE_URL}/validation/check_strong?account_id={account_id}&user_id={user_id}&username={username}&authtoken={token}"
    )

    # make sure to check the status code
    if r.status_code != 200:
        raise ValueError(f"Error from argon (code {r.status_code}): {r.text}")

    # check actual token validity
    resp = r.json()

    strong_valid = resp["valid"]
    weak_valid = resp["valid_weak"]

    # if valid_weak is false, then the token is completely invalid or the user is impersonating
    if not weak_valid:
        raise ValueError(f"Invalid token: {resp["cause"]}")

    # if valid is false but valid_weak is true, the most likely reason is an invalid client-side username.
    # tell the user to refresh login in GD account settings.
    if not strong_valid:
        raise ValueError("Mismatched username, please refresh login in account settings")

    # if both valid and valid_weak are true, this means the username validation passed successfully :)
    # you can retrieve the user's *actual* username from the response as well:

    username = resp["username"]

    return
```
