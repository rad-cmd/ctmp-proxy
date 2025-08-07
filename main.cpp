#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>
#include <cstdlib>

constexpr int SOURCE_PORT = 33333;
constexpr int DEST_PORT = 44444;
constexpr int HEADER_LEN = 8;
constexpr int MAX_BODY = 65535;
constexpr uint8_t MAGIC = 0xCC;

// Global state for shutdown
std::atomic<bool> running{true};
int srcListenerFd = -1;
int dstListenerFd = -1;

// Protects destClients
std::vector<int> destClients;
std::mutex destLock;

// Signal handler: stop running and close listeners
void handleSignal(int)
{
  running.store(false);
  if (srcListenerFd >= 0)
  {
    close(srcListenerFd);
    srcListenerFd = -1;
  }
  if (dstListenerFd >= 0)
  {
    close(dstListenerFd);
    dstListenerFd = -1;
  }
}

// Validate 16-bit one's-complement sum across all 16-bit words == 0xFFFF
bool validateChecksum(const std::vector<uint8_t> &buf)
{
  uint32_t sum = 0;
  size_t n = buf.size();
  // Sum all 16-bit words
  for (size_t i = 0; i + 1 < n; i += 2)
  {
    sum += (uint16_t(buf[i]) << 8) | buf[i + 1];
    if (sum > 0xFFFF)
      sum = (sum & 0xFFFF) + 1;
  }
  // If odd length, last byte + zero
  if (n & 1)
  {
    sum += uint16_t(buf[n - 1]) << 8;
    if (sum > 0xFFFF)
      sum = (sum & 0xFFFF) + 1;
  }
  // Valid if all bits set
  return sum == 0xFFFF;
}

// Read and validate one CTMP message
bool readCtmpMessage(int sock, std::vector<uint8_t> &out)
{
  uint8_t hdr[HEADER_LEN];
  if (recv(sock, hdr, HEADER_LEN, MSG_WAITALL) != HEADER_LEN)
    return false;

  if (hdr[0] != MAGIC)
    return false;
  uint8_t options = hdr[1];
  uint16_t length = ntohs(*reinterpret_cast<uint16_t *>(hdr + 2));
  // We do not pre-read the checksum here; it will be included in `out`
  if (length > MAX_BODY)
    return false;
  if (hdr[6] != 0 || hdr[7] != 0)
    return false;

  out.resize(HEADER_LEN + length);
  memcpy(out.data(), hdr, HEADER_LEN);
  if (recv(sock, out.data() + HEADER_LEN, length, MSG_WAITALL) != length)
    return false;

  bool isSensitive = (options & 0x40) != 0;
  if (isSensitive)
  {
    if (!validateChecksum(out))
    {
      std::cerr << "[!] dropping packet: checksum mismatch\n";
      return false;
    }
  }
  return true;
}

// Broadcast loop from source
void sourceHandler(int srcFd)
{
  std::vector<uint8_t> msg;
  while (running.load())
  {
    if (!readCtmpMessage(srcFd, msg))
      break;
    std::lock_guard<std::mutex> lk(destLock);
    for (auto it = destClients.begin(); it != destClients.end();)
    {
      int d = *it;
      if (send(d, msg.data(), msg.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(msg.size()))
      {
        close(d);
        it = destClients.erase(it);
      }
      else
      {
        ++it;
      }
    }
  }
  close(srcFd);
}

// Keep a destination alive until it disconnects
void destHandler(int dstFd)
{
  {
    std::lock_guard<std::mutex> lk(destLock);
    destClients.push_back(dstFd);
  }
  char buf;
  while (running.load() &&
         recv(dstFd, &buf, 1, MSG_PEEK | MSG_DONTWAIT) > 0)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  close(dstFd);
  std::lock_guard<std::mutex> lk(destLock);
  destClients.erase(
      std::remove(destClients.begin(), destClients.end(), dstFd),
      destClients.end());
}

// Create and bind a listening socket
int makeListener(int port)
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
  // Install signal handlers
  signal(SIGINT, handleSignal);
  signal(SIGTERM, handleSignal);

  // Open listeners
  srcListenerFd = makeListener(SOURCE_PORT);
  dstListenerFd = makeListener(DEST_PORT);

  // Thread for the single source
  std::thread([&]()
              {
        while (running.load()) {
            int s = accept(srcListenerFd, nullptr, nullptr);
            if (s < 0) break;
            std::thread(sourceHandler, s).detach();
        } })
      .detach();

  // Main thread accepts multiple destinations
  while (running.load())
  {
    int d = accept(dstListenerFd, nullptr, nullptr);
    if (d < 0)
      break;
    std::thread(destHandler, d).detach();
  }

  // Clean up
  if (srcListenerFd >= 0)
    close(srcListenerFd);
  if (dstListenerFd >= 0)
    close(dstListenerFd);
  return 0;
}
