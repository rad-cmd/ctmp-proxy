#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>
#include <cstdlib>

constexpr int SOURCE_PORT = 33333;
constexpr int DEST_PORT = 44444;
constexpr int HEADER_LENGTH = 8;
constexpr int MAX_PAYLOAD = 65535;
constexpr uint8_t MAGIC_BYTE = 0xCC;

// Shared list of connected destinations
std::vector<int> clients;
std::mutex clients_mutex;
std::atomic<bool> running{true};

// Calculate 16-bit checksum
static uint16_t calculateChecksum(const std::vector<uint8_t> &data)
{
  uint32_t sum = 0;
  size_t n = data.size();
  for (size_t i = 0; i + 1 < n; i += 2)
  {
    sum += (uint16_t(data[i]) << 8) | data[i + 1];
    if (sum > 0xFFFF)
      sum = (sum & 0xFFFF) + 1;
  }
  if (n & 1)
  {
    sum += uint16_t(data[n - 1]) << 8;
    if (sum > 0xFFFF)
      sum = (sum & 0xFFFF) + 1;
  }
  return uint16_t(~sum) & 0xFFFF;
}

// Read and validate one CTMP message from sock
// Returns false on error or invalid packet
bool readCtmpMessage(int sock, std::vector<uint8_t> &out)
{
  uint8_t header_buf[HEADER_LENGTH];
  if (recv(sock, header_buf, HEADER_LENGTH, MSG_WAITALL) != HEADER_LENGTH)
    return false;

  if (header_buf[0] != MAGIC_BYTE)
    return false; // wrong magic

  uint8_t options = header_buf[1];
  uint16_t payloadLen = ntohs(*reinterpret_cast<uint16_t *>(header_buf + 2));
  uint16_t checksum = ntohs(*reinterpret_cast<uint16_t *>(header_buf + 4));

  if (payloadLen > MAX_PAYLOAD)
    return false; // too big

  // padding bytes must be zero
  if (header_buf[6] != 0 || header_buf[7] != 0)
    return false;

  // read payload
  out.resize(HEADER_LENGTH + payloadLen);
  std::memcpy(out.data(), header_buf, HEADER_LENGTH);
  if (recv(sock, out.data() + HEADER_LENGTH, payloadLen, MSG_WAITALL) != payloadLen)
    return false;

  // if sensitive (OPTIONS bit 1 = 0x40), verify checksum
  if (options & 0x40)
  {
    std::vector<uint8_t> temp = out;
    temp[4] = temp[5] = MAGIC_BYTE; // set checksum bytes to 0xCC for calc
    if (calculateChecksum(temp) != checksum)
    {
      std::cerr << "[!] dropping packet: checksum mismatch\n";
      return false;
    }
  }

  return true;
}

// Relay loop: read from source socket, broadcast to all destinations
void sourceHandler(int sourceSock)
{
  std::vector<uint8_t> packet;
  while (running.load())
  {
    if (!readCtmpMessage(sourceSock, packet))
      break;

    std::lock_guard<std::mutex> lk(clients_mutex);
    for (auto it = clients.begin(); it != clients.end();)
    {
      if (send(*it, packet.data(), packet.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(packet.size()))
      {
        close(*it);
        it = clients.erase(it);
      }
      else
      {
        ++it;
      }
    }
  }
  close(sourceSock);
}

// Keep a destination open until it disconnects
void destHandler(int clientSock)
{
  {
    std::lock_guard<std::mutex> lk(clients_mutex);
    clients.push_back(clientSock);
  }

  char peek;
  while (running.load() &&
         recv(clientSock, &peek, 1, MSG_PEEK | MSG_DONTWAIT) != 0)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  close(clientSock);
  std::lock_guard<std::mutex> lk(clients_mutex);
  clients.erase(
      std::remove(clients.begin(), clients.end(), clientSock),
      clients.end());
}

// Create and bind a listening socket on given port
static int makeListener(int port)
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
  int srcListener = makeListener(SOURCE_PORT);
  int dstListener = makeListener(DEST_PORT);

  // Accept the single source in a separate thread
  std::thread([&]()
              {
        while (running.load()) {
            int s = accept(srcListener, nullptr, nullptr);
            if (s >= 0)
                std::thread(sourceHandler, s).detach();
        } })
      .detach();

  // Main loop accepts multiple destinations
  while (running.load())
  {
    int c = accept(dstListener, nullptr, nullptr);
    if (c >= 0)
      std::thread(destHandler, c).detach();
  }

  close(srcListener);
  close(dstListener);
  return 0;
}
