/*
MIT License

Copyright (c) 2021 Meng Rao <raomeng1@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#pragma once
#include "TcpClient.h"

// This is a wrapper class for pollnet interface
// Check https://github.com/MengRao/pollnet for details
template<uint32_t RecvBufSize = 4096>
class EfviTcpClient
{
  struct Conf
  {
    static const uint32_t ConnSendBufCnt = 1024;
    static const bool SendBuf1K = true;
    static const uint32_t ConnRecvBufSize = RecvBufSize;
    static const uint32_t MaxConnCnt = 1;
    static const uint32_t MaxTimeWaitConnCnt = 1;
    static const uint32_t RecvBufCnt = 512;
    static const uint32_t SynRetries = 3;
    static const uint32_t TcpRetries = 10;
    static const uint32_t DelayedAckMS = 10;
    static const uint32_t MinRtoMS = 100;
    static const uint32_t MaxRtoMS = 30 * 1000;
    static const bool WindowScaleOption = false;
    static const bool TimestampOption = false;
    static const int CongestionControlAlgo = 0; // 0: no cwnd, 1: new reno, 2: cubic
    static const uint32_t UserTimerCnt = 0;
    using UserData = char;
  };
  using TcpClient = efvitcp::TcpClient<Conf>;
  using TcpConn = typename TcpClient::Conn;

public:
  const char* getLastError() { return err; };

  bool isConnected() { return pc && !pc->isClosed(); }

  bool getPeername(struct sockaddr_in& addr) {
    if (!isConnected()) return false;
    pc->getPeername(addr);
    return true;
  }

  bool connect(const char* interface, const char* server_ip, uint16_t server_port) {
    pc = nullptr;
    err = nullptr;
    if ((err = client.init(interface))) return false;
    if ((err = client.connect(server_ip, server_port))) return false;
    while (!pc && !err) {
      read([&](const uint8_t* data, uint32_t size) { return size; });
    };
    return pc;
  }

  void close(const char* reason) {
    err = reason;
    client.close();
  }

  // blocking write is not supported currently
  bool writeNonblock(const void* data, uint32_t size, bool more = false) {
    if (pc->send(data, size, more) != size) {
      pc->close();
      err = "send buffer full";
      return false;
    }
    return true;
  }

  template<typename Handler>
  bool read(Handler handler) {
    struct TmpHandler
    {
      TmpHandler(Handler& h_, TcpConn*& pc_, const char*& err_)
        : handler(h_)
        , pc(pc_)
        , err(err_)
        , got_data(false) {}
      void onConnectionRefused() { err = "connection refused"; }
      void onConnectionReset(TcpConn& conn) { err = "connection reset"; }
      void onConnectionTimeout(TcpConn& conn) { err = "connection timeout"; }
      void onConnectionClosed(TcpConn& conn) { err = "connection closed"; }
      void onFin(TcpConn& conn, uint8_t* data, uint32_t size) { err = "connection closed"; }
      void onMoreSendable(TcpConn& conn) {}
      void onUserTimeout(TcpConn& conn, uint32_t timer_id) {}
      void onConnectionEstablished(TcpConn& conn) { pc = &conn; }
      inline uint32_t onData(TcpConn& conn, const uint8_t* data, uint32_t size) {
        got_data = true;
        return handler(data, size);
      }

      Handler& handler;
      TcpConn*& pc;
      const char*& err;
      bool got_data;
    } tmp_handler(handler, pc, err);

    client.poll(tmp_handler);
    return tmp_handler.got_data;
  }

  TcpClient client;
  TcpConn* pc = nullptr;
  const char* err = nullptr;
};
