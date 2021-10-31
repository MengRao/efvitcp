# Efvitcp
Efvitcp is a tcp library using Solarflare ef_vi interface on linux, and also a tcp multiplexing framework for both C++ client and server program.

As ef_vi is a low-level interface passing Ethernet frames between applications and the network, efvitcp implements full tcp/ip stack on its own in user space while also taking advantage of ef_vi's zero-copy buffer delivering feature.

On the user interface, efvitcp is a header-only C++ template for tcp client and server, using reactor model(like select/poll/epoll) and handling events for multiple connections in a single call. 

It opens a fast path directly between the user and the NIC, providing extremely efficient data processing performance. For example, on frame arrives the tcp payload memory could be directly handed over to the user which is written by the NIC on a DMA buffer.

## Why not use Tcpdirect?
While tcpdirect also provides a tcp implementation based on ef_vi, it suffers from the maximum PIO number limitation the solarflare NIC provides: there can be no more than 12 instances on a host(onload is sharing the same PIO resource which makes it worse). And tcpdirect performs poorly in performance when onload is also in use, which I don't know why.
These problems make tcpdirect hard to use in practice, so an alternative approach is needed to make full use of solarflare NIC's low-latency capacity for tcp applications.

## Features
* Fast - it makes good use of solarflare's low-latency features including CTPIO, it's even faster than tcpdirect especially for large segments.
* Headers only and no third-party dependences(except ef_vi lib).
* Reactor model with rich network and time events, giving finer control over a tcp connection.
* Highly configurable in compile-time.
* Non-blocking - none of the apis blocks.
* No thread created internally
* Support user timers on each connection.

## Platform
Linux and C++11 is required.

## Usage
efvitcp provides two class templates for tcp client and server: `efvitcp::TcpClient<Conf>` and `efvitcp::TcpServer<Conf>`, both taking a template argument used for configuration in compile time(we'll cover it later). 

Take client for example, an instance can be declared as below:
```c++
struct Conf {
...
};
using TcpClient = efvitcp::TcpClient<Conf>;
using TcpConn = TcpClient::Conn;

TcpClient client;
```
At first we need to call `init(interface)` for initialization:
```c++
const char* err = client.init("p1p1");
if (err) {
  cout << "init failed: " << err << endl;
}
else {
  cout << "init ok" << endl;
}
```
To create a tcp connection we call `connect(server_ip, server_ip[, local_port=0])`:
```c++
if((err = client.connect("192.168.20.15", 1234)) {
  cout << "connect failed: " << err << endl;
}
else {
  cout << "connect ok" << endl;
}
```
Note that `connect()` not returning an error doesn't mean the connection is established: it just indicate that a Syn is sent out without error(as it's non-blocking), we still need to wait for the 3-way handshake to complete. As efvitcp won't create a thread internally, we need to `poll()` it in the user thread, and as `poll()` is also non-blocking, we need to call it **repetitively**. 

`poll()` is the core of muliplexing in the lib, all sorts of events will be triggers by this single call including whether or not the `connect()` is successful. How can user retrive the events from `poll()`? A class with event handler functions(not virtual) need to be defined and an instance be provided as the argument of `poll()`, and it'll call those user defined callback functions when events arrive. For completing a connection establishment, three events could be triggered:

```c++
struct MyClient {
  void onConnectionRefused() { 
    err = "onConnectionRefused"; 
  }
  void onConnectionTimeout(TcpConn& conn) { 
    err = "onConnectionTimeout"; 
  }
  void onConnectionEstablished(TcpConn& conn) { 
    pc = &conn;
  }
  ...
  
  const char* err = nullptr;
  TcpConn* pc = nullptr;
} my_client;

while(!my_client.err && !my_client.pc){
  client.poll(handler);
}
if(my_client.err) {
  cout << my_client.err << endl;
}
else {
  cout << "connection established" << endl;
}
```
Once connection is established on the callback `onConnectionEstablished`, we can save the reference to the `TcpConn` parameter, because all operations on an established connection will be called on this object.

For sending data, two functions of `TcpConn` can be used:
```c++
// more is like MSG_MORE flag in socket, indicating there're pending sends
uint32_t send(const void* data, uint32_t size, bool more = false);
uint32_t sendv(const iovec* iov, int iovcnt);
```
Both calls return the number of bytes accepted(like socket send/writev call). User can also know previously the maxmium bytes the next send/sendv can accept by `getSendable()`.
Note that data being accepted doesn't mean it has been sent out, it(or part of it) may be buffered to be sent later due to tcp flow control or congestion control limitations. Actually we can get the immediately sendable bytes in the next send/sendv by `getImmediatelySendable()`. 

When connection send buffer is full of unacked data, no more data will be sendable, user has to wait for new data to be acked for more sendable, there is a specific event assiciated with this need:
```c++
void onMoreSendable(TcpConn& conn) {
  cout << "senable: "<< conn.getSendable() << endl;
}
```

On the receiver side, when new data is received below event will occur:
```c++
uint32_t onData(TcpConn& conn, uint8_t* data, uint32_t size) {
  while (size >= sizeof(Packet)) {
    // handle Packet...
    
    data += sizeof(Packet);
    size -= sizeof(Packet);
  }
  return size;
}
```
`onData()` callback returns the remaining size not consumed by the user, probably because only a part of application packet has been recevied and more data is needed to proceed. In this case user don't need to buffer the partial data himself but just return the remaining size. When new data is avaialble, it will be appended to the old data and handed together to the user in the next `onData()`, this allows for elegant data processing code just as above.

If user is done with sending and want to start or continue with the 4-way handshake closure, `sendFin()` can be called, this is analogous to `shutdown(fd, SHUT_WR)`. On the other hand, when a connection received a Fin(the remote side is done sending), below event will trigger:
```c++
void onFin(TcpConn& conn, uint8_t* data, uint32_t size) {}
```
where `data` and `size` are the last unconsumed data and `size` could be 0.

When the 4-way handshake is complete and the connection enters into either Closed or TimeWait status, below event will trigger:
```c++
void onConnectionClosed(TcpConn& conn){}
```

If user doesn't want to go through the elegant tcp closure procedure and wish to close the connection immediately, he can call `close()` on the `TcpConn` and a Rst will be sent out and the connection goes into Closed status. On receiving a Rst, below event will trigger:
```c++
void onConnectionReset(TcpConn& conn) {}
```
and the connection is closed. In addition, the `onConnectionTimeout(TcpConn& conn)` event can also occur on an established connection, indicating data retransmission time has exceeded some limit and the connection is automatically closed.

Note that after a connection is closed(either by active `close()` or on events `onConnectionClosed`/`onConnectionReset`/`onConnectionTimeout`) the connection reference should not be used because it could be reused for a new connection.

Lastly, efvitcp allows for user timers on a connection basis, and each connection can have multiple user timers concurrently. This is done by `void setUserTimer(uint32_t timer_id, uint32_t duration)` function on `TcpConn`, where `timer_id` is the timer type starting from 0, with different timers having different `timer_id`. `duration` is the timeout in milliseconds, currently the maximum possible duration is 65000(65 seconds). Setting a duration of 0 will delete a specific timer. When the connection is closed all users timers are deleted as well. When user timer expires below event will trigger:
```c++
void onUserTimeout(TcpConn& conn, uint32_t timer_id) {}
```

The interfaces of TcpServer template is very similar to that of TcpClient, with below differeces:
* The `connect()` function is replaced by `const char* listen(uint16_t server_port)`
* The `onConnectionRefused()` event is replaced by `bool allowNewConnection(uint32_t ip, uint16_t port_be)`, this new event occurs when a new connection is being established(in Syn-Received state) and the user can decide whether to accept it or not according to its remote ip and port.

## Configurations
to be continued...
