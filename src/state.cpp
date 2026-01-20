#include "state.hpp"

#include "stages.hpp"
#include "storage.hpp"
#include "util.hpp"

#include <Geode/binding/GameManager.hpp>
#include <Geode/loader/Loader.hpp>
#include <matjson/std.hpp>
#include <asp/time/sleep.hpp>
#include <asp/data.hpp>
#include <asp/iter.hpp>

using namespace geode::prelude;
using namespace asp::time;
using namespace asp::data;
using enum std::memory_order;

namespace argon {

Task<void> sleepFor(auto duration) {
    return Task<void>::run([duration](auto, auto) {
        asp::time::sleep(duration);
        return true;
    });
}

static CowString truncate(std::string_view s, size_t maxSize = 128) {
    if (s.size() >= maxSize) {
        // genius
        std::string out{s.substr(0, maxSize)};
        out.push_back('.');
        out.push_back('.');
        out.push_back('.');

        return CowString::fromOwned(std::move(out));
    } else {
        return CowString::fromBorrowed(s);
    }
}

static std::string handleApiError(web::WebResponse* response, std::string_view what) {
    auto str = response->string().unwrapOrDefault();

    log::warn("(Argon) {} failed with code {}, dumping server response.", what, response->code());
    log::warn("Response: '{}'", str);
    log::warn("Curl (extra) error message: '{}'", response->errorMessage());

    if (response->code() == -1) {
        // curl error, request did not even reach the server
        std::string fullmsg = std::move(str);
        auto emsg = response->errorMessage();
        if (!emsg.empty()) {
            if (fullmsg.empty()) {
                fullmsg = emsg;
            } else {
                fullmsg += fmt::format(" ({})", emsg);
            }
        }

        if (fullmsg.empty()) {
            fullmsg = "(unknown error, response and error buffer are empty)";
        }

        auto msg = truncate(fullmsg);

        return fmt::format("Request error ({}): {}", what, msg.toBorrowed());
    } else {
        // server error

        auto resp = str;
        if (resp.empty()) {
            resp = "(no response body)";
        }

        auto msg = truncate(resp);
        return fmt::format("Server error ({}, code {}): {}", what, response->code(), msg.toBorrowed());
    }
}


static std::string handleGDError(web::WebResponse* response, std::string_view what) {
    return handleApiError(response, fmt::format("GD request ({})", what));
}

void PendingRequest::callback(geode::Result<std::string>&& value) {
    if (this->cancelled) {
        this->_callback(Err("Argon auth request was cancelled"));
    } else {
        this->_callback(std::move(value));
    }
}

struct Stage1ResponseData {
    std::string method;
    int id;
    uint32_t challengeId;
    int challenge;
    std::string ident;
};

} // namespace argon

template<>
struct matjson::Serialize<argon::Stage1ResponseData> {
    static Result<argon::Stage1ResponseData> fromJson(const Value& value) {
        auto method = value["method"].asString();
        auto id = value["id"].as<int>();
        auto challengeId = value["challengeId"].as<uint32_t>();
        auto challenge = value["challenge"].as<int>();
        auto ident = value["ident"].asString();

        if (!method || !id || !challengeId || !challenge || !ident) {
            return Err("Malformed Stage1ResponseData: missing required fields");
        }

        return Ok(argon::Stage1ResponseData {
            .method = std::move(method).unwrap(),
            .id = std::move(id).unwrap(),
            .challengeId = std::move(challengeId).unwrap(),
            .challenge = std::move(challenge).unwrap(),
            .ident = std::move(ident).unwrap()
        });
    }

    static Value toJson(const argon::Stage1ResponseData& value) {
        auto obj = Value::object();
        obj["method"] = value.method;
        obj["id"] = value.id;
        obj["challengeId"] = value.challengeId;
        obj["challenge"] = value.challenge;
        obj["ident"] = value.ident;
        return obj;
    }
};

namespace argon {

ArgonState::ArgonState() {
    (void) this->setServerUrl("https://argon.globed.dev").unwrap();
}

ArgonState::~ArgonState() {}

Result<> ArgonState::setServerUrl(std::string url) {
    auto lock = this->serverUrl.lock();

    if (pendingRequests.lock()->size()) {
        return Err("Cannot change server URL while there are pending requests");
    }

    *lock = std::move(url);

    // Strip trailing slash
    while (!lock->empty() && lock->back() == '/') {
        lock->pop_back();
    }

    return Ok();
}

void ArgonState::setCertVerification(bool state) {
    this->certVerification = state;
}

bool ArgonState::getCertVerification() const {
    return this->certVerification;
}

std::lock_guard<std::mutex> ArgonState::acquireConfigLock() {
    auto ptr = configLock.load(acquire);

    if (!ptr) {
        this->initConfigLock();
        ptr = configLock.load(acquire);
    }

    return std::lock_guard(*ptr);
}

void ArgonState::initConfigLock() {
    if (configLock.load(acquire)) return;

    // note: this function is horrible and really has to be thread safe :)

    static const std::string LOCK_KEY = "dankmeme.argon/_config_lock_v2_25ea8834";

    auto gm = GameManager::get();

    auto lockobj = typeinfo_cast<CCMutex*>(gm->getUserObject(LOCK_KEY));
    if (!lockobj) {
        lockobj = CCMutex::create();
        gm->setUserObject(LOCK_KEY, lockobj);
    }

    configLock.store(&lockobj->data(), release);
}

bool ArgonState::isConfigLockInitialized() {
    return configLock.load(acquire) != nullptr;
}

std::string ArgonState::getServerUrl() const {
    return *this->serverUrl.lock();
}

std::string ArgonState::makeUrl(std::string_view suffix) const {
    auto out = this->getServerUrl();

    if (!suffix.empty()) {
        if (suffix.front() != '/') {
            out.push_back('/');
        }
        out.append(suffix);
    }

    return out;
}

void ArgonState::progress(PendingRequest* req, AuthProgress progress) {
    if (req->progressCallback) {
        req->progressCallback(progress);
    }
}

void ArgonState::pushNewRequest(AuthCallback callback, AuthProgressCallback progress, AccountData account, web::WebTask req, bool forceStrong) {
    size_t id = this->getNextRequestId();

    auto preq = new PendingRequest {
        .id = id,
        ._callback = std::move(callback),
        .progressCallback = std::move(progress),
        .account = std::move(account),
        .forceStrong = forceStrong,
        .startedAuthAt = SystemTime::now(),
    };

    this->progress(preq, AuthProgress::RequestedChallenge);

    preq->stage1Listener.bind([preq](web::WebTask::Event* e) {
        auto& argon = ArgonState::get();

        if (web::WebResponse* value = e->getValue()) {
            argon.processStage1Response(preq, value);
        } else if (web::WebProgress* progress = e->getProgress()) {
            // idk we ignore progress for now
        } else if (e->isCancelled()) {
            argon.handleCancellation(preq);
        }
    });

    preq->stage1Listener.setFilter(std::move(req));

    this->pendingRequests.lock()->insert(preq);
}

void ArgonState::pushStage2Request(PendingRequest* preq, geode::utils::web::WebTask req) {
    preq->stage2Listener.bind([preq](web::WebTask::Event* e) {
        auto& argon = ArgonState::get();

        if (web::WebResponse* value = e->getValue()) {
            argon.processStage2Response(preq, value);
        } else if (web::WebProgress* progress = e->getProgress()) {
            // idk we ignore progress for now
        } else if (e->isCancelled()) {
            argon.handleCancellation(preq);
        }
    });

    preq->stage2Listener.setFilter(std::move(req));
}

void ArgonState::pushStage3Request(PendingRequest* preq, web::WebTask req) {
    preq->stage3Listener.bind([preq](web::WebTask::Event* e) {
        auto& argon = ArgonState::get();

        if (web::WebResponse* value = e->getValue()) {
            argon.processStage3Response(preq, value);
        } else if (web::WebProgress* progress = e->getProgress()) {
            // idk we ignore progress for now
        } else if (e->isCancelled()) {
            argon.handleCancellation(preq);
        }
    });

    preq->stage3Listener.setFilter(std::move(req));
}

PendingRequest* ArgonState::getRequestById(size_t id) {
    for (auto req : *this->pendingRequests.lock()) {
        if (req->id == id) {
            return req;
        }
    }

    return nullptr;
}

void ArgonState::cleanupRequest(PendingRequest* req) {
    this->pendingRequests.lock()->erase(req);

    // Delay this, some mods may capture stuff with non thread-safe destructors
    // when invoking startAuth, thus this may crash when called on a thread
    Loader::get()->queueInMainThread([req] {
        delete req;
    });
}

std::optional<Duration> ArgonState::isInProgress(int accountId) {
    auto req = this->pendingRequests.lock();

    for (auto r : *req) {
        if (r->account.accountId == accountId) {
            return r->startedAuthAt.elapsed();
        }
    }

    return std::nullopt;
}

void ArgonState::killAuthAttempt(int accountId) {
    log::warn("(Argon) aborting auth attempt for ID {}, is argon stuck?", accountId);

    auto reqs = this->pendingRequests.lock();

    for (auto r : *reqs) {
        if (r->account.accountId == accountId) {
            r->cancelled = true;
            r->stage1Listener.getFilter().cancel();
            r->stage2Listener.getFilter().cancel();
            r->stage3Listener.getFilter().cancel();

            return;
        }
    }
}

void ArgonState::processStage1Response(PendingRequest* req, web::WebResponse* response) {
    auto res = response->json();

    if (!res) {
        this->handleStage1Error(req, handleApiError(response, "stage 1 request"));
        return;
    }

    auto obj = std::move(res).unwrap();
    bool success = obj["success"].asBool().unwrapOr(false);

    if (!success) {
        std::string error = obj["error"].asString().unwrapOr("Malformed server response (no error message)");
        this->handleStage1Error(req, std::move(error));
        return;
    }

    auto datares = obj["data"].as<Stage1ResponseData>();
    if (!datares) {
        this->handleStage1Error(req, "Malformed server response ('data' key missing or format is invalid)");
        return;
    }

    auto data = std::move(datares).unwrap();

    // store some values
    req->serverIdent = std::move(data.ident);
    req->challengeId = data.challengeId;
    req->stage2ChosenMethod = data.method;

    // start stage 2
    this->progress(req, req->retrying ? AuthProgress::RetryingSolve : AuthProgress::SolvingChallenge);
    argon::stage2Start(req, data.id, data.challenge);
}

void ArgonState::processStage2Response(PendingRequest* req, web::WebResponse* response) {
    auto res = response->string().unwrapOrDefault();
    if (res.empty() || !response->ok()) {
        this->handleStage2Error(req, handleGDError(response, req->stage2ChosenMethod));
        return;
    }

    if (res == "-1") {
        this->troubleshootStage2Failure(req);
        return;
    }

    // we can assume that stage 2 succeeded now, begin stage 3 by asking the server if auth succeeded
    this->progress(req, req->retrying ? AuthProgress::RetryingVerify : AuthProgress::VerifyingChallenge);
    req->startedVerificationAt = SystemTime::now();
    argon::stage3Start(req);
}

void ArgonState::processStage3Response(PendingRequest* req, web::WebResponse* response) {
    auto res = response->json();

    if (!res) {
        this->handleStage3Error(req, handleApiError(response, "stage 3 request"));

        return;
    }

    auto obj = std::move(res).unwrap();

    // note: this is stage 3, success here does not mean we authenticated successfully,
    // but rather that the server verified our solution is correct and is now waiting to verify it with the GD server
    bool success = obj["success"].asBool().unwrapOr(false);

    if (!success) {
        std::string error = obj["error"].asString().unwrapOr("Malformed server response (no error message)");
        this->handleStage3Error(req, std::move(error));
        return;
    }

    auto data = obj["data"];
    if (data.isNull()) {
        this->handleStage3Error(req, "Malformed server response ('data' key missing or format is invalid)");
        return;
    }

    bool verified = data["verified"].asBool().unwrapOr(false);

    if (verified) {
        auto authtoken = data["authtoken"].asString().unwrapOrDefault();
        if (authtoken.empty()) {
            this->handleStage3Error(req, "Malformed server response ('authtoken' key is missing or invalid)");
            return;
        }

        req->userCommentId = data["commentId"].asInt().unwrapOrDefault();

        this->handleSuccessfulAuth(req, std::move(authtoken));
        return;
    }

    // if we did not succeed, we shall poll again after some time
    if (auto pollAfter = data["pollAfter"].asInt()) {
        this->waitAndRetryStage3(req, pollAfter.unwrap());
    } else {
        this->handleStage3Error(req, "Malformed server response ('pollAfter' key is missing or invalid)");
    }

}

void ArgonState::restartStage1(PendingRequest* preq) {
    if (preq->stage2ChosenMethod == "message") {
        preq->stage2ChosenMethod = "comment";
    } else if (preq->stage2ChosenMethod == "comment") {
        preq->stage2ChosenMethod = "message";
    }

    this->progress(preq, AuthProgress::RetryingRequest);
    preq->retrying = true;

    auto task = argon::web::restartStage1(preq->account, preq->stage2ChosenMethod, preq->forceStrong);
    preq->stage1Listener.setFilter(std::move(task));
}

void ArgonState::handleStage1Error(PendingRequest* req, std::string error) {
    req->callback(Err(std::move(error)));
    this->cleanupRequest(req);
}

void ArgonState::handleCancellation(PendingRequest* req) {
    req->callback(Err("Request was cancelled"));
    this->cleanupRequest(req);
}

void ArgonState::handleSuccessfulAuth(PendingRequest* req, std::string authtoken) {
    req->callback(Ok(authtoken));

    // asynchronously delete the message
    if (req->userCommentId != 0) {
        argon::stage2Cleanup(req);
    }

    // save authtoken in another thread, then cleanup this request
    std::thread([this, req, authtoken = std::move(authtoken)] {
        auto res = ArgonStorage::get().storeAuthToken(req, authtoken);
        if (!res) {
            log::warn("(Argon) failed to save authtoken: {}", res.unwrapErr());
        }

        this->cleanupRequest(req);
    }).detach();
}

void ArgonState::waitAndRetryStage3(PendingRequest* req, int ms) {
    // if over a minute has passed, we should just give up
    if (req->startedVerificationAt.elapsed() > Duration::fromMinutes(1)) {
        this->handleStage3Error(req, "Server did not verify the solution in a reasonable amount of time");
        return;
    }

    if (ms < 0 || ms > 60000) {
        this->handleStage3Error(req, "Server sent an invalid pollAfter value");
        return;
    }

    Duration duration = Duration::fromMillis(ms);
    log::debug("(Argon) Waiting for {} and polling again..", duration.toString());

    // Note to self: don't pass stuff in lambda captures here, it gets corrupted unlike args
    auto task = [](Duration duration, PendingRequest* req) -> web::WebTask {
        co_await sleepFor(duration);
        co_return co_await argon::web::pollStage3(req->account, req->challengeId, req->challengeSolution);
    }(duration, req);

    req->stage3Listener.setFilter(std::move(task));
}

void ArgonState::troubleshootStage2Failure(PendingRequest* req) {
    log::warn("(Argon) Stage 2 failed with method \"{}\", starting to troubleshoot..", req->stage2ChosenMethod);

    web::startMessageLimitCheck(req->account, req->account.serverUrl).listen([this, req](web::WebResponse* res) {
        auto str = res->string().unwrapOrDefault();

        if (str.empty() || !res->ok()) {
            this->handleStage2Error(req, handleGDError(res, "message limit check"));
            return;
        }

        if (str == "-1") {
            // invalid GD credentials
            this->handleStage2Error(req, "Invalid account credentials, please try to Refresh Login in account settings");
            return;
        }

        size_t msgCount = 0;
        if (str != "-2") {
            msgCount = asp::iter::split(str, '|').count();
        }

        if (msgCount == 50) {
            this->handleStage2Error(req, "Sent message limit reached, please try deleting some sent messages");
            return;
        }

        this->handleStage2Error(req, "Stage 2 failed due to unknown error, auth and message limit are OK");
    }, [](auto&&) {}, [this, req] {
        this->handleCancellation(req);
    });
}

void ArgonState::handleStage2Error(PendingRequest* req, std::string error) {
    // TODO: we do not support comment auth right now, add this later
#if 0
    // If we can, we should try another authentication method, before completely failing.
    if (!req->retrying) {
        log::warn("(Argon) Stage 2 failed with method \"{}\", retrying with a different one", req->stage2ChosenMethod);
        log::warn("(Argon) Fail reason: {}", error);

        this->restartStage1(req);
        return;
    }
#endif

    // otherwise, just fail
    req->callback(Err(std::move(error)));
    this->cleanupRequest(req);
}

void ArgonState::handleStage3Error(PendingRequest* req, std::string error) {
    // retry with a different method if we can
    if (!req->retrying) {
        log::warn("(Argon) Stage 3 failed with method \"{}\", retrying with a different one", req->stage2ChosenMethod);
        log::warn("(Argon) Fail reason: {}", error);

        this->restartStage1(req);
        return;
    }

    // otherwise, just fail
    req->callback(Err(std::move(error)));
    this->cleanupRequest(req);
}

size_t ArgonState::getNextRequestId() {
    return nextReqId++;
}

} // namespace argon