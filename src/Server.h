/*
 Mining Pool Agent

 Copyright (C) 2016  BTC.COM

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef SERVER_H_
#define SERVER_H_

#include "Common.h"
#include "Utils.h"

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>

#include <bitset>
#include <map>
#include <set>

#include "utilities_js.hpp"

#define CMD_MAGIC_NUMBER      0x7Fu

#define CMD_REGISTER_WORKER   0x00u             // send msg
#define CMD_SUBMIT_SHARE      0x01u             // send msg
#define CMD_SUBMIT_SHARE_WITH_TIME  0x02u       // send msg
#define CMD_MINING_SET_DIFF   0x03u             // recv msg


class StratumSession;
class StratumServer;
class UpStratumClient;


//////////////////////////////// StratumError ////////////////////////////////
class StratumError {
public:
  enum {
    NO_ERROR        = 0,

    UNKNOWN         = 20,
    JOB_NOT_FOUND   = 21,
    DUPLICATE_SHARE = 22,
    LOW_DIFFICULTY  = 23,
    UNAUTHORIZED    = 24,
    NOT_SUBSCRIBED  = 25,

    ILLEGAL_METHOD   = 26,
    ILLEGAL_PARARMS  = 27,
    IP_BANNED        = 28,
    INVALID_USERNAME = 29,
    INTERNAL_ERROR   = 30,
    TIME_TOO_OLD     = 31,
    TIME_TOO_NEW     = 32
  };
  static const char * toString(int err);
};


//////////////////////////////// SessionIDManager //////////////////////////////
#define MAX_SESSION_ID   0xFFFFu   // 65535 = 2^16 - 1

class SessionIDManager {
  std::bitset<MAX_SESSION_ID + 1> sessionIds_;
  int32_t count_;

public:
  SessionIDManager();

  bool ifFull();
  uint16_t allocSessionId();
  void freeSessionId(const uint16_t sessionId);
};


/////////////////////////////////// StratumServer //////////////////////////////
class StratumServer {
  static const int8_t kUpSessionCount_ = 5;
  bool running_;

  vector<string>   upPoolHost_;
  vector<uint16_t> upPoolPort_;
  string upPoolUserName_;

  // up stream connnections
  vector<UpStratumClient *> upSessions_;
  vector<int32_t> upSessionCount_;
  struct event *upEvTimer_;

  // down stream connections
  vector<StratumSession *> downSessions_;

  // libevent2
  struct event_base *base_;
  struct event *signal_event_;
  struct evconnlistener *listener_;
  struct sockaddr_in listenAddr_;

  void checkUpSessions();
  void waitUtilAllUpSessionsAvailable();

public:
  SessionIDManager sessionIDManager_;


public:
  StratumServer(const uint16_t listenPort, const string upPoolUserName);
  ~StratumServer();

  UpStratumClient *createUpSession(const int8_t idx);

  void addUpPool(const char *host, const uint16_t port);

  void addDownConnection   (StratumSession *conn);
  void removeDownConnection(StratumSession *conn);

  static void listenerCallback(struct evconnlistener *listener,
                               evutil_socket_t fd,
                               struct sockaddr* saddr,
                               int socklen, void *ptr);
  static void downReadCallback (struct bufferevent *, void *ptr);
  static void downEventCallback(struct bufferevent *, short, void *ptr);

  void addUpConnection   (UpStratumClient *conn);
  void removeUpConnection(UpStratumClient *conn);

  static void upReadCallback (struct bufferevent *, void *ptr);
  static void upEventCallback(struct bufferevent *, short, void *ptr);

  static void upWatcherCallback(evutil_socket_t fd, short events, void *ptr);
  static void upSesssionCheckCallback(evutil_socket_t fd, short events, void *ptr);

  void sendMiningNotifyToAll(const int8_t idx, const char *p1,
                             size_t p1Len, const char *p2);
  void sendMiningNotify(StratumSession *downSession);
  void sendDefaultMiningDifficulty(StratumSession *downSession);
  void sendMiningDifficulty(UpStratumClient *upconn,
                            uint16_t sessionId, uint32_t diff);

  void submitShare(JsonNode &jparams, StratumSession *downSession);

  int8_t findUpSessionIdx();

  void registerWorker(StratumSession *downSession, uint16_t sessionId,
                      const char *minerAgent, const string &workerName);

  bool setup();
  void run();
  void stop();
};


///////////////////////////////// UpStratumClient //////////////////////////////
class UpStratumClient {
  struct bufferevent *bev_;
  uint64_t extraNonce2_;
  string userName_;

  bool handleMessage();
  void handleStratumMessage(const string &line);
  bool handleExMessage(struct evbuffer *inBuf);

public:
  enum State {
    INIT          = 0,
    CONNECTED     = 1,
    SUBSCRIBED    = 2,
    AUTHENTICATED = 3
  };
  State state_;
  int8_t idx_;
  StratumServer *server_;

  uint32_t poolDefaultDiff_;
  uint32_t extraNonce1_;  // session ID

  string   latestMiningNotifyStr_;
  // latest two job Id & time, use to check if send nTime
  uint8_t  latestJobId_[2];
  uint32_t latestJobGbtTime_[2];

public:
  UpStratumClient(const int8_t idx,
                  struct event_base *base, const string &userName,
                  StratumServer *server);
  ~UpStratumClient();

  bool connect(struct sockaddr_in &sin);

  void recvData();
  void sendData(const char *data, size_t len);
  inline void sendData(const string &str) {
    sendData(str.data(), str.size());
  }

  void sendMiningNotify(const string &line);

  // means auth success and got at least stratum job
  bool isAvailable();

  void submitShare();
  void submitWorkerInfo();
};


////////////////////////////////// StratumSession //////////////////////////////
class StratumSession {
  // mining state
  enum State {
    CONNECTED     = 0,
    SUBSCRIBED    = 1,
    AUTHENTICATED = 2
  };

  //----------------------
  static const int32_t kExtraNonce2Size_ = 4;
  State state_;
  char *minerAgent_;

  void setReadTimeout(const int32_t timeout);

  void handleStratumMessage(const string &line);

  void handleRequest(const string &idStr, const string &method,
                     JsonNode &jparams);
  void handleRequest_Subscribe(const string &idStr, const JsonNode &jparams);
  void handleRequest_Authorize(const string &idStr, const JsonNode &jparams);
  void handleRequest_Submit   (const string &idStr, JsonNode &jparams);

  void responseError(const string &idStr, int code);
  void responseTrue(const string &idStr);

public:
  int8_t  upSessionIdx_;
  uint16_t sessionId_;
  struct bufferevent *bev_;
  StratumServer *server_;


public:
  StratumSession(const int8_t upSessionIdx, const uint16_t sessionId,
                 struct bufferevent *bev, StratumServer *server);
  ~StratumSession();

  void recvData();
  void sendData(const char *data, size_t len);
  inline void sendData(const string &str) {
    sendData(str.data(), str.size());
  }
};

#endif
