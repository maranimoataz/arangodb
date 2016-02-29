
////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2016 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Kaveh Vahedipour
////////////////////////////////////////////////////////////////////////////////

#include "RestServer/ArangoServer.h"
#include "Rest/HttpRequest.h"
#include "Rest/Version.h"
#include "RestAgencyHandler.h"

#include "Agency/Agent.h"

#include <velocypack/Builder.h>
#include <velocypack/velocypack-aliases.h>

#include "Basics/Logger.h"

using namespace arangodb;

using namespace arangodb::basics;
using namespace arangodb::rest;
using namespace arangodb::consensus;

////////////////////////////////////////////////////////////////////////////////
/// @brief ArangoDB server
////////////////////////////////////////////////////////////////////////////////

extern ArangoServer* ArangoInstance;

RestAgencyHandler::RestAgencyHandler(HttpRequest* request, Agent* agent)
    : RestBaseHandler(request), _agent(agent) {
}

bool RestAgencyHandler::isDirect() const { return false; }

inline HttpHandler::status_t RestAgencyHandler::reportErrorEmptyRequest () {
  LOG(WARN) << "Empty request to public agency interface.";
  generateError(HttpResponse::NOT_FOUND,404);
  return HttpHandler::status_t(HANDLER_DONE);
}

inline HttpHandler::status_t RestAgencyHandler::reportTooManySuffices () {
  LOG(WARN) << "Too many suffixes. Agency public interface takes one path.";
  generateError(HttpResponse::NOT_FOUND,404);
  return HttpHandler::status_t(HANDLER_DONE);
}

inline HttpHandler::status_t RestAgencyHandler::unknownMethod () {
  LOG(WARN) << "Too many suffixes. Agency public interface takes one path.";
  generateError(HttpResponse::NOT_FOUND,404);
  return HttpHandler::status_t(HANDLER_DONE);
}

inline HttpHandler::status_t RestAgencyHandler::redirect (id_t leader_id) {
  LOG(WARN) << "Redirecting request to " << leader_id;
  generateError(HttpResponse::NOT_FOUND,404);
  return HttpHandler::status_t(HANDLER_DONE);
}

inline HttpHandler::status_t RestAgencyHandler::handleReadWrite () {
  bool accepted;
  typedef arangodb::velocypack::Options opts_t;
  opts_t opts;
  if (_request->suffix()[0] == "write") { 
    write_ret_t ret = _agent->write (
      _request->toVelocyPack(std::make_shared<opts_t>(opts)));
    accepted = ret.accepted; 
    _agent->waitFor (ret.indices); // Wait for confirmation
  } else {
    ret = _agent->read(
      _request->toVelocyPack(std::make_shared<opts_t>(opts)));
    accepted = ret.accepted; 
  }
  if (accepted) { // We accepted the request
    ret.result->close();
    generateResult(ret.result->slice());
  } else {            // We redirect the request
    _response->setHeader("Location", _agent->config().endpoints[ret.redirect]);
    generateError(HttpResponse::TEMPORARY_REDIRECT,307);
  }
  return HttpHandler::status_t(HANDLER_DONE);
}

HttpHandler::status_t RestAgencyHandler::execute() {
  try {
    if (_request->suffix().size() == 0) {         // Empty request
      return reportErrorEmptyRequest();
    } else if (_request->suffix().size() > 1) {   // path size >= 2
      return reportTooManySuffices();
    } else {
    	if (_request->suffix()[0] == "write" ||
          _request->suffix()[0] ==  "read") { // write to / read from agency
        return handleReadWrite();
    	} else {
        return reportUnknownMethod();
    	}
    }
  } catch (...) {
    // Ignore this error
  }
  return HttpHandler::status_t(HANDLER_DONE);
}
