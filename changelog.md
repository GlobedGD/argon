# 1.4.3

* Fix incorrect URL offsets on Android causing verification failure

# 1.4.2

* Fix Android builds by @hiimjasmine00 in #7

# 1.4.1

* Fix compilation on MSVC
* Use lower priority for the mod load listeners

# 1.4.0

* API slightly changed to be async-friendly
* Internals rewritten for much cleaner code

# 1.3.1

* Add some helper functions to check if the user is signed into a GD account and if any auth tokens are already cached

# 1.3.0

* Enforce strict thread safety, argon will now return errors in some functions when not invoked from main thread. See README for more details.
* Fix some internal thread safety bugs
* Fix a bug in task API that would start auth even if not logged into an account
* Include the mod version in telemetry

# 1.2.1

* Add automatic troubleshooter for account issues (invalid credentials, sent message limit). Now instead of "generic error", users will see a better error message telling them how to fix the issue.

# 1.2.0

* Add new task-based API for obtaining auth tokens, thanks [camila314](https://github.com/camila314/)!

# 1.1.9

* Fix not building on MSVC (#4, fixes #2), thanks [Prevter](https://github.com/Prevter)!

# 1.1.8

* Hopefully fix local MacOS building

# 1.1.7

* Fix crash when startAuth is invoked with a `Ref` in captures or anything else that's not thread safe to destruct

# 1.1.6

* Fix builds without precompiled headers (#3)
* Fix Debug builds on Windows (#3)

# 1.1.5

* Fix exception being thrown when error message is greater than 125 characters (oops)

# 1.1.4

* Fix `argon::setCertVerification(false)` not actually doing anything :p
* Improve error messages (again!)

# 1.1.3

* Improve error messages further
* Add a way to disable SSL certificate verification for web requests

# 1.1.2

* Slightly improve error messages
* Fix issues on Amazon Store version of GD

# 1.1.1

* Make library usable without precompiled headers

# 1.1.0

* Now return an error if a mod tries to perform auth multiple times at once with the same account
* Bump asp, hopefully should not cause compile issues on MSVC now
* Use a different internal mutex structure

# 1.0.1

Improve iOS support
