#include "state.hpp"

#include "stages.hpp"
#include "storage.hpp"

#include <matjson/reflect.hpp>
#include <asp/time/sleep.hpp>

using namespace geode::prelude;
using namespace asp::time;

namespace argon {

Task<void> sleepFor(auto duration) {
    return Task<void>::run([duration](auto, auto) {
        asp::time::sleep(duration);
        return true;
    });
}

struct Stage1ResponseData {
    std::string method;
    int id;
    int challenge;
    std::string ident;
};

struct Stage3ResponseData {
    bool verified;
    std::string authtoken; // if successful, this is the authtoken
    int pollAfter; // if unsuccessful, this says how many ms to wait until polling again
};

ArgonState::ArgonState() {
    (void) this->setServerUrl("https://argon.dankmeme.dev").unwrap();

    // thread responsible for compute heavy tasks, like saving the authtoken
    workerThread.setLoopFunction([this](auto& stopToken) {
        if (auto task = workerThreadTasks.popTimeout(Duration::fromMillis(250))) {
            (*task)();
        }
    });
}

ArgonState::~ArgonState() {
    workerThread.stopAndWait();
}

Result<> ArgonState::setServerUrl(std::string url) {
    auto _lock = serverUrlMtx.lock();

    if (pendingRequests.lock()->size()) {
        return Err("Cannot change server URL while there are pending requests");
    }

    serverUrl = std::move(url);

    // Strip trailing slash
    if (!serverUrl.empty() && serverUrl.back() == '/') {
        serverUrl.pop_back();
    }

    return Ok();
}

asp::Mutex<>::Guard ArgonState::lockServerUrl() {
    return serverUrlMtx.lock();
}

std::string_view ArgonState::getServerUrl() const {
    return serverUrl;
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
        .callback = std::move(callback),
        .progressCallback = std::move(progress),
        .account = std::move(account),
        .forceStrong = forceStrong,
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
    delete req;
}

void ArgonState::processStage1Response(PendingRequest* req, web::WebResponse* response) {
    auto res = response->json();

    if (!res) {
        log::warn("(Argon) Stage 1 request failed with code {}, server did not send a JSON, dumping server response.", response->code());
        log::warn("{}", response->string().unwrapOrDefault());
        this->handleStage1Error(req, fmt::format("Unknown server error ({})", response->code()));
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

    // store server ident
    req->serverIdent = std::move(data.ident);

    // start stage 2
    this->progress(req, req->retrying ? AuthProgress::RetryingSolve : AuthProgress::SolvingChallenge);
    req->stage2ChosenMethod = data.method;
    argon::stage2Start(req, data.id, data.challenge);
}

void ArgonState::processStage2Response(PendingRequest* req, web::WebResponse* response) {
    auto res = response->string().unwrapOrDefault();
    if (res.empty()) {
        this->handleStage2Error(req, "Server did not send a response");
        return;
    }

    if (!response->ok()) {
        log::warn("(Argon) Stage 2 request failed with code {}, dumping server response.", response->code());
        log::warn("{}", res);
        this->handleStage2Error(req, fmt::format("Server responded with code {}", response->code()));
        return;
    }

    if (res == "-1") {
        this->handleStage2Error(req, "Stage 2 failed (generic error)");
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
        log::warn("(Argon) Stage 3 request failed with code {}, server did not send a JSON, dumping server response.", response->code());
        log::warn("{}", response->string().unwrapOrDefault());
        this->handleStage3Error(req, fmt::format("Unknown server error ({})", response->code()));
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

    auto datares = obj["data"].as<Stage3ResponseData>();
    if (!datares) {
        this->handleStage3Error(req, "Malformed server response ('data' key missing or format is invalid)");
        return;
    }

    auto data = std::move(datares).unwrap();

    if (data.verified) {
        this->handleSuccessfulAuth(req, std::move(data.authtoken));
        return;
    }

    // if we did not succeed, we shall poll again after some time
    this->waitAndRetryStage3(req, data.pollAfter);
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

    workerThreadTasks.push([this, req, authtoken = std::move(authtoken)] {
        auto res = ArgonStorage::get().storeAuthToken(req, authtoken);
        if (!res) {
            log::warn("(Argon) failed to save authtoken: {}", res.unwrapErr());
        }

        this->cleanupRequest(req);
    });
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

    auto task = [duration, req]() -> web::WebTask {
        co_await sleepFor(duration);
        co_return co_await argon::web::pollStage3(req->account);
    }();

    req->stage3Listener.setFilter(std::move(task));
}

void ArgonState::handleStage2Error(PendingRequest* req, std::string error) {
    // If we can, we should try another authentication method, before completely failing.
    if (!req->retrying) {
        log::warn("(Argon) Stage 2 failed with method \"{}\", retrying with a different one", req->stage2ChosenMethod);
        log::warn("(Argon) Fail reason: {}", error);

        this->restartStage1(req);
        return;
    }

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