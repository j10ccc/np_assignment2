#include <arpa/inet.h>
#include <iostream>
#include <netdb.h>
#include <thread>
#include <unistd.h>

#include "calcLib.h"
#include "protocol.h"

#define MAX_BUFFER_SIZE 1024
#define TIMEOUT 10

struct Client {
  std::string ip;
  int port;
  calcProtocol assignment;
  std::chrono::steady_clock::time_point last_seen;
};

std::unordered_map<std::string, Client> clients;
int server_socket;

int seed = 0;
int generate_id() { return seed++; }

std::tuple<std::string, int> get_ip_port(sockaddr clientAddr) {
  std::tuple<std::string, int> t;

  if (clientAddr.sa_family == AF_INET6) {
    char ip[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&clientAddr)->sin6_addr, ip,
              INET6_ADDRSTRLEN);

    std::get<0>(t) = ip;
    std::get<1>(t) = ntohs(((struct sockaddr_in *)&clientAddr)->sin_port);
  } else {
    std::get<0>(t) = inet_ntoa(((struct sockaddr_in *)&clientAddr)->sin_addr);
    std::get<1>(t) = ntohs(((struct sockaddr_in *)&clientAddr)->sin_port);
  }

  // std::cout << "IP: " << std::get<0>(t) << ", port: " << std::get<1>(t) <<
  // std::endl;
  return t;
}

std::string create_client_key(std::tuple<std::string, int> ip_port) {
  std::string s = std::get<0>(ip_port);
  s += std::to_string(std::get<1>(ip_port));

  return s;
}

void async_send_calc_assignment(int sockfd, Client *target,
                                struct sockaddr clientAddr,
                                socklen_t clientAddrLen) {

  int assignment_id = generate_id();
  calcProtocol assignment;
  assignment.type = htons(1);
  assignment.major_version = htons(1);
  assignment.minor_version = htons(0);
  int arith = rand() % 8 + 1;
  assignment.arith = htonl(arith);
  assignment.id = htonl(assignment_id);

  if (arith <= 4) {
    assignment.inValue1 = htonl(randomInt());
    assignment.inValue2 = htonl(randomInt());
  } else {
    assignment.flValue1 = randomFloat();
    assignment.flValue2 = randomFloat();
  }

  target->assignment = assignment;

  if (sendto(sockfd, &assignment, sizeof(calcProtocol), 0, &clientAddr,
             clientAddrLen) == -1) {
    perror("talker: sendto");
  }

  sleep(TIMEOUT);

  auto ip_port = get_ip_port(clientAddr);
  auto key = create_client_key(ip_port);
  if (clients.find(key) != clients.end()) {
    std::cout << "[INFO] Assignment#" << assignment_id << " timeout"
              << std::endl;
    clients.erase(key);
  }
}

void async_send_calc_result(int sockfd, struct sockaddr clientAddr,
                            socklen_t clientAddrLen) {
  calcMessage result;
  result.message = htonl(1);
  result.major_version = htons(1);
  result.minor_version = htons(0);
  result.protocol = htons(17);
  result.type = htons(1);

  if (sendto(sockfd, &result, sizeof(result), 0, &clientAddr, clientAddrLen) ==
      -1) {
    perror("talker: sendto");
  }

  auto ip_port = get_ip_port(clientAddr);
  auto key = create_client_key(ip_port);
  clients.erase(key);
}

void async_reject_client(int sockfd, struct sockaddr clientAddr,
                         socklen_t clientAddrLen) {
  calcMessage result;
  result.message = htonl(2);
  result.major_version = htons(1);
  result.minor_version = htons(0);
  result.protocol = htons(17);
  result.type = htons(2);

  if (sendto(sockfd, &result, sizeof(calcMessage), 0, &clientAddr,
             clientAddrLen) == -1) {
    perror("talker: sendto");
  }
}

void handle_invalid_message(int sockfd, struct sockaddr clientAddr,
                            socklen_t clientAddrLen) {
  std::thread t(async_reject_client, sockfd, clientAddr, clientAddrLen);
  t.detach();
}

void handle_new_client(int sockfd, struct sockaddr clientAddr,
                       socklen_t clientAddrLen, char *buffer) {
  // FIXME: Should I release it?
  calcMessage *calcMsg = reinterpret_cast<calcMessage *>(buffer);

  // TODO: delete directly transform
  calcMsg->type = ntohs(calcMsg->type);
  calcMsg->message = ntohs(calcMsg->message);
  calcMsg->major_version = ntohs(calcMsg->major_version);
  calcMsg->minor_version = ntohs(calcMsg->minor_version);
  calcMsg->protocol = ntohs(calcMsg->protocol);

  if (calcMsg->protocol == 17 && calcMsg->major_version == 1 &&
      calcMsg->minor_version == 0 && calcMsg->type == 22) {
    // binary protocol
    auto ip_port = get_ip_port(clientAddr);
    std::string clientKey = create_client_key(ip_port);

    if (clients.find(clientKey) == clients.end()) {
      Client client;
      client.ip = std::get<0>(ip_port);
      client.port = std::get<1>(ip_port);

      std::cout << "[INFO] New client from " << client.ip << " " << client.port
                << std::endl;
      client.last_seen = std::chrono::steady_clock::now();
      clients[clientKey] = client;
      std::thread t(async_send_calc_assignment, sockfd, &clients[clientKey],
                    clientAddr, clientAddrLen);
      t.detach();
    }
  } else {
    handle_invalid_message(sockfd, clientAddr, clientAddrLen);
  }
}

void handle_calc_response(int sockfd, struct sockaddr clientAddr,
                          socklen_t clientAddrLen, char *buffer) {

  auto ip_port = get_ip_port(clientAddr);
  std::string clientKey = create_client_key(ip_port);

  if (clients.find(clientKey) == clients.end()) {
    std::thread t(async_reject_client, sockfd, clientAddr, clientAddrLen);
    t.detach();
    return;
  }

  calcProtocol *response = reinterpret_cast<calcProtocol *>(buffer);

  if (ntohs(response->major_version) == 1 &&
      ntohs(response->minor_version) == 0) {
    const Client *client = &clients[clientKey];

    // printf("[id compare] %d:%d\n", client->assignment.id, response->id);
    if (client->assignment.id == response->id) {
      std::thread t(async_send_calc_result, sockfd, clientAddr, clientAddrLen);
      t.detach();
    }
  } else {
    handle_invalid_message(sockfd, clientAddr, clientAddrLen);
  }
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " <IP:port>" << std::endl;
    return 1;
  }

  const char *addr = argv[1];
  const char *port = argv[2];

  struct addrinfo hints, *res, *p;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  int server_socket;

  if (getaddrinfo(addr, port, &hints, &res) != 0) {
    std::cerr << "getaddrinfo error" << std::endl;
    return -1;
  }

  for (p = res; p != NULL; p = p->ai_next) {
    if ((server_socket =
             socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      std::cerr << "socket error" << std::endl;
      continue;
    }

    if (bind(server_socket, p->ai_addr, p->ai_addrlen) == -1) {
      std::cerr << "Bind error" << std::endl;
      close(server_socket);
      continue;
    }

    break;
  }

  if (p == NULL) {
    std::cerr << "Failed to bind" << std::endl;
    return 1;
  }

  bool use_ipv6 = res->ai_family == AF_INET6;
  printf("Server started at port %s, support %s\n", port,
         use_ipv6 ? "ipv6" : "ipv4");
  freeaddrinfo(res);

  while (true) {
    struct sockaddr clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    char buffer[MAX_BUFFER_SIZE] = {};

    int bytesReceived = recvfrom(server_socket, buffer, MAX_BUFFER_SIZE, 0,
                                 &clientAddr, &clientAddrLen);

    clientAddr.sa_family = use_ipv6 ? AF_INET6 : AF_INET;

    if (bytesReceived < 0) {
      std::cerr << "Error receiving data" << std::endl;
    } else if (bytesReceived == sizeof(calcProtocol)) {
      handle_calc_response(server_socket, clientAddr, clientAddrLen, buffer);
    } else if (bytesReceived == sizeof(calcMessage)) {
      handle_new_client(server_socket, clientAddr, clientAddrLen, buffer);
    } else {
      handle_invalid_message(server_socket, clientAddr, clientAddrLen);
    }
  }

  close(server_socket);

  return 0;
}
