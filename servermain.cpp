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

void async_send_calc_assignment(int sockfd, Client *target,
                                struct sockaddr_in clientAddr,
                                socklen_t clientAddrLen) {
  calcProtocol assignment;
  assignment.type = htons(1);
  assignment.major_version = htons(1);
  assignment.minor_version = htons(0);
  assignment.arith = htonl(rand() % 8 + 1);
  assignment.id = htonl(generate_id());

  if (assignment.arith <= 4) {
    assignment.inValue1 = randomInt();
    assignment.inValue2 = randomInt();
  } else {
    assignment.flValue1 = randomFloat();
    assignment.flValue2 = randomFloat();
  }

  target->assignment = assignment;

  if (sendto(sockfd, &assignment, sizeof(calcProtocol), 0,
             (struct sockaddr *)&clientAddr, clientAddrLen) == -1) {
    perror("talker: sendto");
  }
}

void async_send_calc_result(int sockfd, struct sockaddr_in clientAddr,
                            socklen_t clientAddrLen) {
  calcMessage result;
  result.message = htonl(1);
  result.major_version = htons(1);
  result.minor_version = htons(0);
  result.protocol = htons(17);
  result.type = htons(1);

  if (sendto(sockfd, &result, sizeof(result), 0, (struct sockaddr *)&clientAddr,
             clientAddrLen) == -1) {
    perror("talker: sendto");
  }
}

void async_reject_client(int sockfd, struct sockaddr_in clientAddr,
                         socklen_t clientAddrLen) {
  calcMessage result;
  result.message = htonl(2);
  result.major_version = htons(1);
  result.minor_version = htons(0);
  result.protocol = htons(17);
  result.type = htons(2);

  if (sendto(sockfd, &result, sizeof(calcMessage), 0,
             (struct sockaddr *)&clientAddr, clientAddrLen) == -1) {
    perror("talker: sendto");
  }
}

void handle_invalid_message(int sockfd, struct sockaddr_in clientAddr,
                            socklen_t clientAddrLen) {
  std::thread t(async_reject_client, sockfd, clientAddr, clientAddrLen);
  t.detach();
}

void handle_new_client(int sockfd, struct sockaddr_in clientAddr,
                       socklen_t clientAddrLen, char *buffer) {
  // FIXME: Should I release it?
  calcMessage *calcMsg = reinterpret_cast<calcMessage *>(buffer);

  calcMsg->type = ntohs(calcMsg->type);
  calcMsg->message = ntohs(calcMsg->message);
  calcMsg->major_version = ntohs(calcMsg->major_version);
  calcMsg->minor_version = ntohs(calcMsg->minor_version);
  calcMsg->protocol = ntohs(calcMsg->protocol);

  if (calcMsg->protocol == 17 && calcMsg->major_version == 1 &&
      calcMsg->minor_version == 0 && calcMsg->type == 22) {
    // binary protocol
    std::string clientKey = std::to_string(clientAddr.sin_addr.s_addr) + ":" +
                            std::to_string(clientAddr.sin_port);

    if (clients.find(clientKey) == clients.end()) {

      printf("New client from %s\n", clientKey.c_str());
      Client client;
      client.ip = std::to_string(clientAddr.sin_addr.s_addr);
      client.port = clientAddr.sin_port;
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

void handle_calc_response(int sockfd, struct sockaddr_in clientAddr,
                          socklen_t clientAddrLen, char *buffer) {

  std::string clientKey = std::to_string(clientAddr.sin_addr.s_addr) + ":" +
                          std::to_string(clientAddr.sin_port);
  if (clients.find(clientKey) == clients.end())
    return;

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
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    char buffer[MAX_BUFFER_SIZE] = {};

    int bytesReceived =
        recvfrom(server_socket, buffer, MAX_BUFFER_SIZE, 0,
                 (struct sockaddr *)&clientAddr, &clientAddrLen);

    if (bytesReceived < 0) {
      std::cerr << "Error receiving data" << std::endl;
      continue;
    } else if (bytesReceived == sizeof(calcProtocol)) {
      handle_calc_response(server_socket, clientAddr, clientAddrLen, buffer);
      memset(buffer, 0, sizeof(buffer));
    } else if (bytesReceived == sizeof(calcMessage)) {
      handle_new_client(server_socket, clientAddr, clientAddrLen, buffer);
      memset(buffer, 0, sizeof(buffer));
    } else {
      handle_invalid_message(server_socket, clientAddr, clientAddrLen);
    }
  }

  close(server_socket);

  return 0;
}
