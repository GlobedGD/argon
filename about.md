# Argon

API for authenticating Geometry Dash accounts. Made by creators of Globed, and is completely free and open-source, including the [server](https://github.com/GlobedGD/argon-server). Can be selfhosted (for example for a GDPS), but we provide our server at https://argon.globed.dev and this library uses it by default.

Benefits compared to some of the other auth APIs (Globed, DashAuth, GDAuth):

* Does not send your GJP or password to any 3rd party server
* Tokens are stored globally, meaning that authentication is done only once even if the user has multiple mods installed that use Argon
* Smooth UX for switching accounts (will not try to use or overwrite the token of the other account)
* Our official instance is whitelisted by RobTop, which means authentication is faster and more reliable against IP blocks
* Ability to retrieve user's username without making requests to the GD server (with just their token)
* Automatic troubleshooter for figuring out the cause of auth issues (for example invalid session)
