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
