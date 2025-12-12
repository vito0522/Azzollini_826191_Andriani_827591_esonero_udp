/*
 * main.c
 *
 * UDP Server - Weather Service (portable: Windows, Linux, macOS)
 */
#include <stdint.h>
#if defined(_WIN32) || defined(WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #ifndef NO_ERROR
  #define NO_ERROR 0
  #endif
  typedef int socklen_t;
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
#include <time.h>
#include "protocol.h"

void clearwinsock(void) {
#if defined(_WIN32) || defined(WIN32)
    WSACleanup();
#endif
}

void to_lowercase(char *str) {
    for (int i = 0; str[i] != '\0'; i++) {
        str[i] = (char)tolower((unsigned char)str[i]);
    }
}

int is_alpha_space_string(const char *s) {
    for (int i = 0; s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\t') return 0;
        if (!(isalpha(c) || c == ' ')) return 0;
    }
    return 1;
}

int is_city_supported(const char *city) {
    static const char *SUPPORTED_CITIES[] = {
        "bari","roma","milano","napoli","torino",
        "palermo","genova","bologna","firenze","venezia"
    };
    char lower_city[CITY_NAME_MAX_LEN];
    strncpy(lower_city, city, CITY_NAME_MAX_LEN - 1);
    lower_city[CITY_NAME_MAX_LEN - 1] = '\0';
    to_lowercase(lower_city);
    for (int i = 0; i < 10; i++) {
        if (strcmp(lower_city, SUPPORTED_CITIES[i]) == 0) return 1;
    }
    return 0;
}

float get_temperature(void) { return -10.0f + (rand() / (float)RAND_MAX) * 50.0f; }
float get_humidity(void)    { return  20.0f + (rand() / (float)RAND_MAX) * 80.0f; }
float get_wind(void)        { return   0.0f + (rand() / (float)RAND_MAX) * 100.0f; }
float get_pressure(void)    { return 950.0f + (rand() / (float)RAND_MAX) * 100.0f; }

uint32_t float_to_net(float f) {
    uint32_t tmp;
    memcpy(&tmp, &f, sizeof(float));
    return htonl(tmp);
}

void deserialize_request(const char *buf, weather_request_t *req) {
    int offset = 0;
    memcpy(&req->type, buf + offset, sizeof(char));
    offset += sizeof(char);
    memcpy(req->city, buf + offset, CITY_NAME_MAX_LEN);
    req->city[CITY_NAME_MAX_LEN - 1] = '\0';
}

int serialize_response(const weather_response_t *res, char *buf) {
    int offset = 0;
    uint32_t net_status = htonl(res->status);
    memcpy(buf + offset, &net_status, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    memcpy(buf + offset, &res->type, sizeof(char));
    offset += sizeof(char);

    uint32_t v32 = float_to_net(res->value);
    memcpy(buf + offset, &v32, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    return offset;
}

int main() {
#if defined(_WIN32) || defined(WIN32)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        printf("Errore a WSAStartup()\n");
        return EXIT_FAILURE;
    }
#endif

    int sock;
    struct sockaddr_in srvAddr, cliAddr;
    socklen_t cliLen;
    char req_buf[1 + CITY_NAME_MAX_LEN];
    char rsp_buf[sizeof(uint32_t) + sizeof(char) + sizeof(float)];
    int recvSize;

    if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("socket() failed");
        clearwinsock();
        return EXIT_FAILURE;
    }

    memset(&srvAddr, 0, sizeof(srvAddr));
    srvAddr.sin_family = AF_INET;
    srvAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    srvAddr.sin_port = htons(SERVER_PORT);

    if (bind(sock, (struct sockaddr*)&srvAddr, sizeof(srvAddr)) < 0) {
        perror("bind() failed");
        closesocket(sock);
        clearwinsock();
        return EXIT_FAILURE;
    }

    printf("Server in ascolto sulla porta %d...\n", SERVER_PORT);
    srand((unsigned)time(NULL));

    while (1) {
        cliLen = sizeof(cliAddr);
        recvSize = recvfrom(sock, req_buf, sizeof(req_buf), 0,
                            (struct sockaddr*)&cliAddr, &cliLen);
        if (recvSize <= 0) continue;

        weather_request_t request;
        memset(&request, 0, sizeof(request));
        deserialize_request(req_buf, &request);

        char hostbuf[256];
        struct hostent *rev = gethostbyaddr(&cliAddr.sin_addr, sizeof(cliAddr.sin_addr), AF_INET);
        if (rev && rev->h_name) {
            strncpy(hostbuf, rev->h_name, sizeof(hostbuf)-1);
        } else {
            strncpy(hostbuf, inet_ntoa(cliAddr.sin_addr), sizeof(hostbuf)-1);
        }
        hostbuf[sizeof(hostbuf)-1] = '\0';

        printf("Richiesta ricevuta da %s (ip %s): type='%c', city='%s'\n",
               hostbuf, inet_ntoa(cliAddr.sin_addr), request.type, request.city);

        weather_response_t response;
        response.status = STATUS_SUCCESS;
        response.type   = request.type;
        response.value  = 0.0f;

        if (request.type != TYPE_TEMPERATURE &&
            request.type != TYPE_HUMIDITY &&
            request.type != TYPE_WIND &&
            request.type != TYPE_PRESSURE) {
            response.status = STATUS_INVALID_REQUEST;
            response.type   = '\0';
        }
        else if (!is_alpha_space_string(request.city)) {
            response.status = STATUS_INVALID_REQUEST;
            response.type   = '\0';
        }
        else if (!is_city_supported(request.city)) {
            response.status = STATUS_CITY_UNAVAILABLE;
            response.type   = '\0';
        }
        else {
            switch (request.type) {
                case TYPE_TEMPERATURE: response.value = get_temperature(); break;
                case TYPE_HUMIDITY:    response.value = get_humidity();    break;
                case TYPE_WIND:        response.value = get_wind();        break;
                case TYPE_PRESSURE:    response.value = get_pressure();    break;
            }
        }

        int rsp_len = serialize_response(&response, rsp_buf);
        if (sendto(sock, rsp_buf, rsp_len, 0,
                   (struct sockaddr*)&cliAddr, cliLen) != rsp_len) {
            perror("sendto() failed");
        }
    }

#if defined(_WIN32) || defined(WIN32)
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
    return 0;
}
