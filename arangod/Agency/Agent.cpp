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

#include "Agent.h"

#include <chrono>

using namespace arangodb::velocypack;
namespace arangodb {
namespace consensus {

Agent::Agent () : Thread ("Agent"), _stopping(false) {}

Agent::Agent (config_t const& config) : _config(config), Thread ("Agent") {
  //readPersistence(); // Read persistence (log )
  _constituent.configure(this);
  _state.configure(_config.size());
}

id_t Agent::id() const { return _config.id;}

Agent::~Agent () {}

void Agent::start() {
  _constituent.start();
}

term_t Agent::term () const {
  return _constituent.term();
}

query_t Agent::requestVote(term_t t, id_t id, index_t lastLogIndex,
                           index_t lastLogTerm) {
  Builder builder;
  builder.add("term", Value(term()));
  builder.add("voteGranted", Value(
                _constituent.vote(id, t, lastLogIndex, lastLogTerm)));
  builder.close();
	return std::make_shared<Builder>(builder);
}

Config<double> const& Agent::config () const {
  return _config;
}

void Agent::print (arangodb::LoggerStream& logger) const {
  //logger << _config;
}

void Agent::report(status_t status) {
  _status = status;
}

id_t Agent::leaderID () const {
  return _constituent.leaderID();
}

arangodb::LoggerStream& operator<< (arangodb::LoggerStream& l, Agent const& a) {
  a.print(l);
  return l;
}


bool waitFor(std::vector<index_t>& unconfirmed) {
  while (true) {
    CONDITION_LOCKER(guard, _cv);
    // Shutting down
    if (_stopping) {      
      return false;
    }
    // Remove any unconfirmed which is confirmed
    for (size_t i = 0; i < unconfirmed.size(); ++i) { 
      if (auto found = find (_unconfirmed.begin(), _unconfirmed.end(),
                             unconfirmed[i])) {
        unconfirmed.erase(found);
      }
    }
    // none left? 
    if (unconfirmed.size() ==0) {
      return true;        
    }
  }
  // We should never get here
  TRI_ASSERT(false);
}

append_entries_t Agent::appendEntries (
  term_t term, id_t leaderId, index_t prevLogIndex, term_t prevLogTerm,
  index_t leadersLastCommitIndex, query_t const& query) {

  if (term < this->term()) { // Reply false if term < currentTerm (§5.1)
    LOG(WARN) << "Term of entry to be appended smaller than my own term (§5.1)";
    return append_entries_t(false,this->term());
  }
  
  if (!_state.findit(prevLogIndex, prevLogTerm)) { // Find entry at pli with plt
    LOG(WARN) << "No entry in logs at index " << prevLogIndex
              << " and term " prevLogTerm; 
    return append_entries_t(false,this->term());
  }

  _state.log(query, index_t idx, term, leaderId, _config.size());          // Append all new entries
  _read_db.apply(query); // once we become leader we create a new spear head
  _last_commit_index = leadersLastCommitIndex;
  
}

append_entries_t Agent::appendEntriesRPC (
  id_t slave_id, collect_ret_t const& entries) {
    
	std::vector<ClusterCommResult> result;

  // RPC path
  std::stringstream path;
  path << "/_api/agency_priv/appendEntries?term=" << _term << "&leaderId="
       << id() << "&prevLogIndex=" << entries.prev_log_index << "&prevLogTerm="
       << entries.prev_log_term << "&leaderCommitId=" << commitId;

  // Headers
	std::unique_ptr<std::map<std::string, std::string>> headerFields =
	  std::make_unique<std::map<std::string, std::string> >();

  // Body
  Builder builder;
  builder.add("term", Value(term()));
  builder.add("voteGranted", Value(
                _constituent.vote(id, t, lastLogIndex, lastLogTerm)));
  builder.close();
  
  arangodb::ClusterComm::instance()->asyncRequest
    ("1", 1, _config.end_points[slave_id],
     rest::HttpRequest::HTTP_REQUEST_GET,
     path.str(), std::make_shared<std::string>(body), headerFields,
     std::make_shared<arangodb::ClusterCommCallback>(_agent_callbacks),
     1.0, true);
}

//query_ret_t
write_ret_t Agent::write (query_t const& query)  {
  if (_constituent.leading()) {             // We are leading
    if (_spear_head.apply(query)) {         // We could apply to spear head? 
      std::vector<index_t> indices =        //    otherwise through
        _state.log (query, term(), id(), _config.size()); // Append to my own log
    } else {
      throw QUERY_NOT_APPLICABLE;
    }
  } else {                          // We redirect
    return query_ret_t(false,_constituent.leaderID());
  }
}

read_ret_t Agent::read (query_t const& query) const {
  if (_constituent.leading()) {     // We are leading
    return _state.read (query);
  } else {                          // We redirect
    return read_ret_t(false,_constituent.leaderID());
  }
}

void State::run() {
  while (!_stopping) {
    auto dur = std::chrono::system_clock::now();
    std::vector<std::vector<index_t>> work(_config.size());

    // Collect all unacknowledged
    for (size_t i = 0; i < _size() ++i) {
      if (i != id()) {
        work[i] = _state.collectUnAcked(i);
      }}

    // (re-)attempt RPCs
    for (size_t j = 0; j < _setup.size(); ++j) {
      if (j != id() && work[j].size()) {
        appendEntriesRPC(j, work[j]);
      }}

    // catch up read db
    catchUpReadDB();
    
    // We were too fast?
    if (dur = std::chrono::system_clock::now() - dur < _poll_interval) {
      std::this_thread::sleep_for (_poll_interval - dur);
    }
  }
}

bool State::operator(id_t, index_t) (ClusterCommResult* ccr) {
  CONDITION_LOCKER(guard, _cv);
  _state.confirm(id_t, index_t);
  guard.broadcast();
}


void shutdown() {
  // wake up all blocked rest handlers
  _stopping = true;
  CONDITION_LOCKER(guard, _cv);
  guard.broadcast();
}


}}
