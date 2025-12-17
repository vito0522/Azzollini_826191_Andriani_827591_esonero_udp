/*
 * main.c
 *
 * UDP Client - Weather Service (portable: Windows, Linux, macOS)
 */
#include <stdint.h>
#if defined(_WIN32) || defined(WIN32)
  typedef int socklen_t;
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #ifndef NO_ERROR
  #define NO_ERROR 0
  #endif
#else
  #include <unistd.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <netdb.h>
  #define closesocket close
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protocol.h"


 void clearwinsock(void) {
  #if defined WIN32
      WSACleanup();
  #endif
  }

void errorHandler(const char *msg) {
      printf("ERRORE: %s\n", msg);
  }

float net_to_float(uint32_t net) {
      uint32_t tmp = ntohl(net);
      float f;
      memcpy(&f, &tmp, sizeof(float));
      return f;
  }


int serialize_request(weather_request_t *req, char *buf) {
      int offset = 0;
      memcpy(buf + offset, &req->type, sizeof(char));
      offset += sizeof(char);
      memcpy(buf + offset, req->city, CITY_NAME_MAX_LEN);
      offset += CITY_NAME_MAX_LEN;
      return offset;
  }


void deserialize_response(char *buf, weather_response_t *res) {
      int offset = 0;
      uint32_t net_status;
      memcpy(&net_status, buf + offset, sizeof(uint32_t));
      res->status = ntohl(net_status);
      offset += sizeof(uint32_t);

      memcpy(&res->type, buf + offset, sizeof(char));
      offset += sizeof(char);

      uint32_t net_val;
      memcpy(&net_val, buf + offset, sizeof(float));
      res->value = net_to_float(net_val);
  }


 void usage(void) {
      printf("Uso: client-project [-s server] [-p porta] -r \"tipo città\"\n");
      printf("  -s server : nome host o IP (default: 127.0.0.1)\n");
      printf("  -p porta  : porta del server (default: %d)\n", SERVER_PORT);
      printf("  -r req    : stringa di richiesta, ad es. \"t Bari\" oppure \"p Roma\"\n");
      exit(1);
  }

  int main(int argc, char *argv[]) {
  #if defined WIN32
      WSADATA wsa_data;
      int result = WSAStartup(MAKEWORD(2,2), &wsa_data);
      if (result != NO_ERROR) {
          errorHandler("Errore a WSAStartup()");
          return 1;
      }
  #endif

      char server_host[256] = "127.0.0.1";
      int server_port = SERVER_PORT;
      char request_str[BUFFER_SIZE] = {0};

      if (argc < 3) usage();
      int have_r = 0;
      for (int i = 1; i < argc; i++) {
          if (strcmp(argv[i], "-s") == 0) {
              if (++i >= argc) usage();
              strncpy(server_host, argv[i], sizeof(server_host)-1);
              server_host[sizeof(server_host)-1] = '\0';
          } else if (strcmp(argv[i], "-p") == 0) {
              if (++i >= argc) usage();
              server_port = atoi(argv[i]);
              if (server_port <= 0 || server_port > 65535) {
                  printf("porta non valida : %s\n", argv[i]);
                  usage();
              }
          } else if (strcmp(argv[i], "-r") == 0) {
              if (++i >= argc) usage();
              strncpy(request_str, argv[i], sizeof(request_str)-1);
              request_str[sizeof(request_str)-1] = '\0';
              have_r = 1;
          } else {
              usage();
          }
      }
      if (!have_r) usage();

      weather_request_t request;
      memset(&request, 0, sizeof(request));

      if (request_str[0] == '\0') {
          printf("argomento -r non valido (vuoto)\n");
          usage();
      }
      request.type = request_str[0];

      const char *city_start = request_str + 1;
      while (*city_start == ' ') city_start++;
      snprintf(request.city, CITY_NAME_MAX_LEN, "%s", city_start);


      int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (sock < 0) {
          errorHandler("Creazione socket fallita");
          clearwinsock();
          return 1;
      }

      struct sockaddr_in server_addr;
      memset(&server_addr, 0, sizeof(server_addr));
      server_addr.sin_family = AF_INET;
      server_addr.sin_port = htons(server_port);

      unsigned long ip = inet_addr(server_host);
      char resolved_ip[64] = {0};
      char resolved_host[256] = {0};

      if (ip != INADDR_NONE) {
          server_addr.sin_addr.s_addr = ip;
          snprintf(resolved_ip, sizeof(resolved_ip), "%s", server_host);
          struct hostent *rev = gethostbyaddr((char*)&ip, sizeof(ip), AF_INET);
          if (rev && rev->h_name) strncpy(resolved_host, rev->h_name, sizeof(resolved_host)-1);
          else strncpy(resolved_host, server_host, sizeof(resolved_host)-1);
      } else {
          struct hostent *host = gethostbyname(server_host);
          if (host == NULL || host->h_addr_list == NULL || host->h_addr_list[0] == NULL) {
              errorHandler("Risoluzione dell'hostname fallita");
              closesocket(sock);
              clearwinsock();
              return 1;
          }
          memcpy(&server_addr.sin_addr.s_addr, host->h_addr_list[0], host->h_length);
          snprintf(resolved_ip, sizeof(resolved_ip), "%s",
                   inet_ntoa(*(struct in_addr*)host->h_addr_list[0]));

          snprintf(resolved_host, sizeof(resolved_host), "%s", server_host);
      }

      char req_buf[1 + CITY_NAME_MAX_LEN];
      int req_len = serialize_request(&request, req_buf);

      if (sendto(sock, req_buf, req_len, 0, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
          errorHandler("Invio fallito");
          closesocket(sock);
          clearwinsock();
          return 1;
      }

      char rsp_buf[sizeof(uint32_t) + sizeof(char) + sizeof(float)];
      socklen_t addr_len = sizeof(server_addr);
      int n = recvfrom(sock, rsp_buf, sizeof(rsp_buf), 0, (struct sockaddr*)&server_addr, &addr_len);
      if (n <= 0) {
          errorHandler("recvfrom fallita");
          closesocket(sock);
          clearwinsock();
          return 1;
      }

      weather_response_t response;
      deserialize_response(rsp_buf, &response);

      printf("Ricevuto risultato dal server %s (ip %s). ",
             resolved_host[0] ? resolved_host : server_host,
             resolved_ip[0] ? resolved_ip : "unknown");

      if (response.status == STATUS_SUCCESS) {
          char city_output[CITY_NAME_MAX_LEN];
          strncpy(city_output, request.city, CITY_NAME_MAX_LEN);
          city_output[CITY_NAME_MAX_LEN - 1] = '\0';
          city_output[0] = toupper(city_output[0]);
          for (int i = 1; city_output[i] != '\0'; i++) {
              city_output[i] = tolower(city_output[i]);
          }

          if (response.type == TYPE_TEMPERATURE)
              printf("%s: Temperatura = %.1f°C\n", city_output, response.value);
          else if (response.type == TYPE_HUMIDITY)
              printf("%s: Umidità = %.1f%%\n", city_output, response.value);
          else if (response.type == TYPE_WIND)
              printf("%s: Vento = %.1f km/h\n", city_output, response.value);
          else if (response.type == TYPE_PRESSURE)
              printf("%s: Pressione = %.1f hPa\n", city_output, response.value);
          else
              printf("Tipo sconosciuto nella risposta\n");

      } else if (response.status == STATUS_CITY_UNAVAILABLE) {
          printf("Città non disponibile\n");
      } else if (response.status == STATUS_INVALID_REQUEST) {
          printf("Richiesta non valida\n");
      }

  #if defined WIN32
      closesocket(sock);
  #else
      close(sock);
  #endif
      clearwinsock();
      return 0;
  }
