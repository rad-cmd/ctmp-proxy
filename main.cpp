// main.cpp
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

constexpr int SOURCE_PORT = 33333;
constexpr int DEST_PORT = 44444;
constexpr int HEADER_LEN = 8;
constexpr int MAX_BODY = 65535;
constexpr uint8_t MAGIC = 0xCC;

std::atomic<bool> running{true};
int src_listener = -1;
int dst_listener = -1;

std::vector<int> sinks;
std::mutex sinks_mu;

static void handle_signal(int)
{
  running.store(false);
  if (src_listener >= 0)
  {
    close(src_listener);
    src_listener = -1;
  }
  if (dst_listener >= 0)
  {
    close(dst_listener);
    dst_listener = -1;
  }
}

// 16-bit one's-complement checksum of a byte buffer
static uint16_t compute_checksum(const std::vector<uint8_t> &b)
{
  uint32_t sum = 0;
  const size_t n = b.size();

  for (size_t i = 0; i + 1 < n; i += 2)
  {
    sum += (uint16_t(b[i]) << 8) | b[i + 1];
    if (sum > 0xFFFF)
      sum = (sum & 0xFFFF) + 1; // fold
  }
  if (n & 1)
  {
    sum += uint16_t(b[n - 1]) << 8;
    if (sum > 0xFFFF)
      sum = (sum & 0xFFFF) + 1;
  }
  return uint16_t(~sum) & 0xFFFF;
}

// Read and validate a CTMP message from sock into out
static bool read_ctmp(int sock, std::vector<uint8_t> &out)
{
  uint8_t hdr[HEADER_LEN];
  if (recv(sock, hdr, HEADER_LEN, MSG_WAITALL) != HEADER_LEN)
    return false;

  if (hdr[0] != MAGIC)
    return false;

  const uint8_t options = hdr[1];
  const uint16_t len = ntohs(*reinterpret_cast<uint16_t *>(hdr + 2));
  const uint16_t net_ck = ntohs(*reinterpret_cast<uint16_t *>(hdr + 4));

  if (len > MAX_BODY)
    return false;
  if (hdr[6] != 0 || hdr[7] != 0)
    return false; // padding must be zero

  out.resize(HEADER_LEN + len);
  std::memcpy(out.data(), hdr, HEADER_LEN);
  if (recv(sock, out.data() + HEADER_LEN, len, MSG_WAITALL) != len)
    return false;

  // If sensitive (bit 1 -> 0x40), verify checksum
  if (options & 0x40)
  {
    std::vector<uint8_t> tmp = out;
    tmp[4] = 0xCC; // per spec: set checksum field to 0xCC bytes when computing
    tmp[5] = 0xCC;
    const uint16_t calc = compute_checksum(tmp);
    if (calc != net_ck)
    {
      std::cerr << "[!] dropping packet: checksum mismatch\n";
      return false;
    }
  }
  return true;
}

static void source_loop(int fd)
{
  std::vector<uint8_t> msg;
  while (running.load())
  {
    if (!read_ctmp(fd, msg))
      break;

    std::lock_guard<std::mutex> lk(sinks_mu);
    for (auto it = sinks.begin(); it != sinks.end();)
    {
      int d = *it;
      ssize_t n = send(d, msg.data(), msg.size(), MSG_NOSIGNAL);
      if (n != static_cast<ssize_t>(msg.size()))
      {
        close(d);
        it = sinks.erase(it);
      }
      else
      {
        ++it;
      }
    }
  }
  close(fd);
}

static void sink_loop(int fd)
{
  {
    std::lock_guard<std::mutex> lk(sinks_mu);
    sinks.push_back(fd);
  }

  // Stay connected until peer closes.
  char peek;
  while (running.load() &&
         recv(fd, &peek, 1, MSG_PEEK | MSG_DONTWAIT) != 0)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  close(fd);
  std::lock_guard<std::mutex> lk(sinks_mu);
  sinks.erase(std::remove(sinks.begin(), sinks.end(), fd), sinks.end());
}

static int make_listener(int port)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
  {
    perror("socket");
    std::exit(1);
  }

  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
  {
    perror("bind");
    std::exit(1);
  }
  if (listen(fd, SOMAXCONN) < 0)
  {
    perror("listen");
    std::exit(1);
  }
  return fd;
}

int main()
{
  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);

  src_listener = make_listener(SOURCE_PORT);
  dst_listener = make_listener(DEST_PORT);

  std::thread([]
              {
    while (running.load()) {
      int s = accept(src_listener, nullptr, nullptr);
      if (s < 0) break;
      std::thread(source_loop, s).detach();
    } })
      .detach();

  while (running.load())
  {
    int d = accept(dst_listener, nullptr, nullptr);
    if (d < 0)
      break;
    std::thread(sink_loop, d).detach();
  }

  if (src_listener >= 0)
    close(src_listener);
  if (dst_listener >= 0)
    close(dst_listener);
  return 0;
}
