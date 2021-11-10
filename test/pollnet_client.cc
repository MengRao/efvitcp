#include <bits/stdc++.h>
#include "efvitcp/EfviTcpClient.h"
using namespace std;

struct Packet
{
  int64_t ts = 0;
  int64_t val = 0;
};

volatile bool running = true;

void my_handler(int s) {
  running = false;
}

inline int64_t getns() {
  timespec ts;
  ::clock_gettime(CLOCK_REALTIME, &ts);
  return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

int main(int argc, const char** argv) {
  struct sigaction sigIntHandler;
  sigIntHandler.sa_handler = my_handler;
  sigemptyset(&sigIntHandler.sa_mask);
  sigIntHandler.sa_flags = 0;
  sigaction(SIGINT, &sigIntHandler, NULL);
  sigaction(SIGTERM, &sigIntHandler, NULL);
  sigaction(SIGPIPE, &sigIntHandler, NULL);
  if (argc < 4) {
    cout << "usage: " << argv[0] << " interface server_ip server_port" << endl;
    return 1;
  }
  const char* interface = argv[1];
  const char* server_ip = argv[2];
  uint16_t server_port = atoi(argv[3]);

  EfviTcpClient<> client;
  if (!client.connect(interface, server_ip, server_port)) {
    cout << client.getLastError() << endl;
    return 1;
  }

  Packet pack;
  while (running) {
    auto now = getns();
    // send new pack once per sec
    if (now - pack.ts >= 1000000000) {
      pack.val++;
      pack.ts = now;
      client.writeNonblock((const uint8_t*)&pack, sizeof(pack));
    }
    client.read([](const uint8_t* data, uint32_t size) {
      auto now = getns();
      while (size >= sizeof(Packet)) {
        const Packet& recv_pack = *(const Packet*)data;
        auto lat = now - recv_pack.ts;
        cout << "recv val: " << recv_pack.val << " latency: " << lat << endl;
        data += sizeof(Packet);
        size -= sizeof(Packet);
      }
      return size;
    });
    if (!client.isConnected()) {
      cout << "connection closed: " << client.getLastError() << endl;
      break;
    }
  }
  return 0;
}
