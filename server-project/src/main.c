/*
 * main.c
 *
 * UDP Server - Weather Service (portable: Windows, Linux, macOS)
 */
#include <stdint.h>
#if defined WIN32
  #include <winsock.h>
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
#include <stdint.h>

static void clearwinsock(void) {
#if defined WIN32
    WSACleanup();
#endif
}

void errorHandler(const char* s) {
    printf("ERRORE: %s\n", s);
}

const int NUM_SUPPORTED_CITIES = 10;
static const char *SUPPORTED_CITIES[] = {
    "bari", "roma", "milano", "napoli", "torino",
    "palermo", "genova", "bologna", "firenze", "venezia"
};

void to_lowercase(char *str) {
    for (int i = 0; str[i] != '\0'; i++) str[i] = (char)tolower((unsigned char)str[i]);
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
    char lower_city[CITY_NAME_MAX_LEN];
    strncpy(lower_city, city, CITY_NAME_MAX_LEN);
    lower_city[CITY_NAME_MAX_LEN - 1] = '\0';
    to_lowercase(lower_city);

    for (int i = 0; i < NUM_SUPPORTED_CITIES; i++) {
        if (strcmp(lower_city, SUPPORTED_CITIES[i]) == 0) return 1;
    }
    return 0;
}
float get_temperature(void) { return -10.0f + (rand() / (float)RAND_MAX) * 50.0f; }
float get_humidity(void)    { return  20.0f + (rand() / (float)RAND_MAX) * 80.0f; }
float get_wind(void)        { return   0.0f + (rand() / (float)RAND_MAX) * 100.0f; }
float get_pressure(void)    { return 950.0f + (rand() / (float)RAND_MAX) * 100.0f; }

static uint32_t float_to_net(float f) {
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
    memcpy(buf + offset, &v32, sizeof(float));
    offset += sizeof(float);

    return offset;
}

const char* reverse_lookup_name(struct in_addr addr, char *out, size_t outlen) {
#if defined WIN32
    struct hostent *he = gethostbyaddr((const char*)&addr, sizeof(addr), AF_INET);
#else
    struct hostent *he = gethostbyaddr(&addr, sizeof(addr), AF_INET);
#endif
    if (he && he->h_name) {
        strncpy(out, he->h_name, outlen - 1);
        out[outlen - 1] = '\0';
        return out;
    }
    strncpy(out, "unknown", outlen - 1);
    out[outlen - 1] = '\0';
    return out;
}

int main(int argc, char *argv[]) {
    int port = SERVER_PORT;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            if (++i >= argc) {
                printf("Uso: server-project [-p porta]\n");
                return 1;
            }
            port = atoi(argv[i]);
            if (port <= 0 || port > 65535) {
                printf("Numero di porta non valido: %s\n", argv[i]);
                return 1;
            }
        } else {
            printf("Uso: server-project [-p porta]\n");
            return 1;
        }
    }

    srand((unsigned)time(NULL));

#if defined WIN32
    WSADATA wsa_data;
    int result = WSAStartup(MAKEWORD(2,2), &wsa_data);
    if (result != NO_ERROR) {
        errorHandler("Errore a WSAStartup()");
        return 1;
    }
#endif

    int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
#if defined WIN32
        printf("Creazione del socket fallita (WSAGetLastError=%d)\n", WSAGetLastError());
#else
        perror("Creazione del socket fallita");
#endif
        clearwinsock();
        return 1;
    }

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = inet_addr("127.0.0.1");
    srv.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
#if defined WIN32
        printf("Bind fallito (WSAGetLastError=%d)\n", WSAGetLastError());
#else
        perror("Bind fallito");
#endif
        closesocket(sock);
        clearwinsock();
        return 1;
    }

    printf("Server UDP in ascolto sulla porta %d...\n", port);

    while (1) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        char req_buf[1 + CITY_NAME_MAX_LEN];

        int n = recvfrom(sock, req_buf, sizeof(req_buf), 0, (struct sockaddr*)&cli, &cli_len);
        if (n <= 0) {
            continue;
        }

        weather_request_t request;
        memset(&request, 0, sizeof(request));
        deserialize_request(req_buf, &request);
        request.type = (char)tolower((unsigned char)request.type);
        char ip_str[64];
        snprintf(ip_str, sizeof(ip_str), "%s", inet_ntoa(cli.sin_addr));
        char host_str[256];
        reverse_lookup_name(cli.sin_addr, host_str, sizeof(host_str));

        printf("Richiesta ricevuta da %s (ip %s): type='%c', city='%s'\n",
               host_str, ip_str, request.type, request.city);

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
            response.value  = 0.0f;
        }
        else if (!is_alpha_space_string(request.city)) {
            response.status = STATUS_CITY_UNAVAILABLE;
            response.type   = '\0';
            response.value  = 0.0f;
        }
        else if (!is_city_supported(request.city)) {
            response.status = STATUS_CITY_UNAVAILABLE;
            response.type   = '\0';
            response.value  = 0.0f;
        }
        else {
            switch (request.type) {
                case TYPE_TEMPERATURE: response.value = get_temperature(); break;
                case TYPE_HUMIDITY:    response.value = get_humidity();    break;
                case TYPE_WIND:        response.value = get_wind();        break;
                case TYPE_PRESSURE:    response.value = get_pressure();    break;
            }
        }

        char rsp_buf[sizeof(uint32_t) + sizeof(char) + sizeof(float)];
        int rsp_len = serialize_response(&response, rsp_buf);

        if (sendto(sock, rsp_buf, rsp_len, 0, (struct sockaddr*)&cli, cli_len) < 0) {
#if defined WIN32
            printf("Errore nell'invio della risposta (WSAGetLastError=%d)\n", WSAGetLastError());
#else
            perror("Errore nell'invio della risposta");
#endif
        }
    }

    closesocket(sock);
    clearwinsock();
    return 0;
}
