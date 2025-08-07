#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>
#include <chrono>

constexpr int SOURCE_PORT = 33333;
constexpr int DEST_PORT = 44444;
constexpr int HEADER_LEN = 8;
constexpr int MAX_BODY = 65535;
constexpr uint8_t CTMP_MAGIC = 0xCC;

std::vector<int> dest_clients;
std::mutex dest_lock;
std::atomic<bool> run_flag{true};

bool get_ctmp_message(int sock, std::vector<uint8_t> &msg)
{
  uint8_t header[HEADER_LEN];
  ssize_t n = recv(sock, header, HEADER_LEN, MSG_WAITALL);
  if (n != HEADER_LEN)
    return false;

  if (header[0] != CTMP_MAGIC || header[1] != 0x00)
    return false;

  if (std::memcmp(header + 4, "\x00\x00\x00\x00", 4) != 0)
    return false;

  uint16_t length = ntohs(*reinterpret_cast<uint16_t *>(header + 2));
  if (length > MAX_BODY)
    return false;

  msg.resize(HEADER_LEN + length);
  std::memcpy(msg.data(), header, HEADER_LEN);

  n = recv(sock, msg.data() + HEADER_LEN, length, MSG_WAITALL);
  return n == length;
}

void handle_source(int source_sock)
{
  std::vector<uint8_t> message;
  while (run_flag.load())
  {
    if (!get_ctmp_message(source_sock, message))
    {
      std::cerr << "[!] Source disconnected or sent invalid message.\n";
      break;
    }

    std::lock_guard<std::mutex> lock(dest_lock);
    for (auto it = dest_clients.begin(); it != dest_clients.end();)
    {
      int client_fd = *it;
      ssize_t sent = send(client_fd, message.data(), message.size(), MSG_NOSIGNAL);
      if (sent != static_cast<ssize_t>(message.size()))
      {
        close(client_fd);
        it = dest_clients.erase(it);
      }
      else
      {
        ++it;
      }
    }
  }

  close(source_sock);
  std::cerr << "[*] Source disconnected. Waiting for new source...\n";
}

void handle_dest(int client_sock)
{
  {
    std::lock_guard<std::mutex> lock(dest_lock);
    dest_clients.push_back(client_sock);
  }

  // Keep connection open until client disconnects
  char tmp;
  while (recv(client_sock, &tmp, 1, MSG_PEEK | MSG_DONTWAIT) != 0 && run_flag.load())
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  close(client_sock);
  {
    std::lock_guard<std::mutex> lock(dest_lock);
    dest_clients.erase(std::remove(dest_clients.begin(), dest_clients.end(), client_sock), dest_clients.end());
  }
}

int create_listener(int port)
{
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
  {
    perror("socket");
    exit(1);
  }

  int reuse = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(sockfd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
  {
    perror("bind");
    exit(1);
  }

  if (listen(sockfd, SOMAXCONN) < 0)
  {
    perror("listen");
    exit(1);
  }

  return sockfd;
}

int main()
{
  int source_listener = create_listener(SOURCE_PORT);
  int dest_listener = create_listener(DEST_PORT);

  std::thread([source_listener]
              {
    while (run_flag.load()) {
        int source_sock = accept(source_listener, nullptr, nullptr);
        if (source_sock >= 0) {
            std::cout << "[*] Source client connected.\n";
            std::thread(handle_source, source_sock).detach();
        }
    } })
      .detach();

  while (run_flag.load())
  {
    int client_sock = accept(dest_listener, nullptr, nullptr);
    if (client_sock >= 0)
    {
      std::cout << "[*] Destination client connected.\n";
      std::thread(handle_dest, client_sock).detach();
    }
  }

  close(source_listener);
  close(dest_listener);
  return 0;
}
