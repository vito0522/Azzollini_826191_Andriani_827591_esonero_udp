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

// --- Utility ---
static void clearwinsock(void) {
#if defined(_WIN32) || defined(WIN32)
    WSACleanup();
#endif
}

static void errorHandler(const char *msg) {
    printf("ERRORE: %s\n", msg);
}

static float net_to_float(uint32_t net) {
    uint32_t tmp = ntohl(net);
    float f;
    memcpy(&f, &tmp, sizeof(float));
    return f;
}

// --- Serializzazione richiesta ---
static int serialize_request(const weather_request_t *req, char *buf) {
    int offset = 0;
    memcpy(buf + offset, &req->type, sizeof(char));
    offset += sizeof(char);
    memcpy(buf + offset, req->city, CITY_NAME_MAX_LEN);
    offset += CITY_NAME_MAX_LEN;
    return offset;
}

// --- Deserializzazione risposta ---
static void deserialize_response(const char *buf, weather_response_t *res) {
    int offset = 0;
    uint32_t net_status;
    memcpy(&net_status, buf + offset, sizeof(uint32_t));
    res->status = ntohl(net_status);
    offset += sizeof(uint32_t);

    memcpy(&res->type, buf + offset, sizeof(char));
    offset += sizeof(char);

    uint32_t net_val;
    memcpy(&net_val, buf + offset, sizeof(uint32_t));
    res->value = net_to_float(net_val);
}

// --- Parsing della stringa -r "type city" con regole traccia ---
static int parse_request_string(const char *request_str, char *out_type, char *out_city) {
    if (!request_str || !out_type || !out_city) return 0;


    for (const char *t = request_str; *t; ++t) {
        if (*t == '\t') return 0;
    }


    const char *p = request_str;
    while (*p == ' ') p++;


    const char *q = p;
    while (*q && *q != ' ') q++;
    size_t first_len = (size_t)(q - p);
    if (first_len != 1) return 0;

    *out_type = *p;


    while (*q == ' ') q++;


    size_t city_len = strnlen(q, CITY_NAME_MAX_LEN - 1);
    if (city_len > CITY_NAME_MAX_LEN - 1) return 0;
    memcpy(out_city, q, city_len);
    out_city[city_len] = '\0';
    return 1;
}

// --- Formattazione citt�: prima lettera maiuscola, resto minuscolo ---
static void format_city_title(const char *in, char *out, size_t outlen) {
    size_t n = strnlen(in, outlen - 1);
    memcpy(out, in, n);
    out[n] = '\0';
    if (n > 0) {
        out[0] = (char)toupper((unsigned char)out[0]);
        for (size_t i = 1; i < n; i++) {
            out[i] = (char)tolower((unsigned char)out[i]);
        }
    }
}

// --- Usage ---
static void usage(void) {
    printf("Uso: client-project [-s server] [-p porta] -r \"tipo citt�\"\n");
    printf("  -s server : nome host o IP (default: localhost)\n");
    printf("  -p porta  : porta del server (default: %d)\n", SERVER_PORT);
    printf("  -r req    : stringa di richiesta, ad es. \"t Bari\" oppure \"p Roma\"\n");
    exit(1);
}

int main(int argc, char *argv[]) {
#if defined(_WIN32) || defined(WIN32)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        printf("error at WSAStartup\n");
        return EXIT_FAILURE;
    }
#endif


    int sock;
    struct sockaddr_in servAddr, fromAddr;
    socklen_t fromSize;
    char req_buf[1 + CITY_NAME_MAX_LEN];
    char rsp_buf[sizeof(uint32_t) + sizeof(char) + sizeof(float)];
    int req_len;


    char server_host[256] = "localhost"; // default conforme traccia
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
    if (!parse_request_string(request_str, &request.type, request.city)) {
        printf("Richiesta non valida\n");
#if defined(_WIN32) || defined(WIN32)
        WSACleanup();
#endif
        return EXIT_FAILURE;
    }


    req_len = serialize_request(&request, req_buf);


    sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        errorHandler("socket() failed");
#if defined(_WIN32) || defined(WIN32)
        WSACleanup();
#endif
        return EXIT_FAILURE;
    }


    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(server_port);

    char resolved_ip[64] = {0};
    char resolved_host[256] = {0};


    unsigned long ip4 = inet_addr(server_host);
    if (ip4 != INADDR_NONE) {
        servAddr.sin_addr.s_addr = ip4;
        snprintf(resolved_ip, sizeof(resolved_ip), "%s", server_host);
#if defined(_WIN32) || defined(WIN32)
        struct hostent *rev = gethostbyaddr((const char*)&ip4, sizeof(ip4), AF_INET);
#else
        struct in_addr ina; memcpy(&ina, &ip4, sizeof(ip4));
        struct hostent *rev = gethostbyaddr(&ina, sizeof(ina), AF_INET);
#endif
        if (rev && rev->h_name) {
            strncpy(resolved_host, rev->h_name, sizeof(resolved_host)-1);
        } else {

            if (strcmp(server_host, "127.0.0.1") == 0)
                strncpy(resolved_host, "localhost", sizeof(resolved_host)-1);
            else
                strncpy(resolved_host, server_host, sizeof(resolved_host)-1);
        }
        resolved_host[sizeof(resolved_host)-1] = '\0';
    } else {

        struct hostent *he = gethostbyname(server_host);
        if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
            errorHandler("Risoluzione dell'hostname fallita");
#if defined(_WIN32) || defined(WIN32)
            closesocket(sock);
            WSACleanup();
#else
            close(sock);
#endif
            return EXIT_FAILURE;
        }
        memcpy(&servAddr.sin_addr.s_addr, he->h_addr_list[0], he->h_length);
        const char *ipstr = inet_ntoa(*(struct in_addr*)he->h_addr_list[0]);
        if (!ipstr) ipstr = "unknown";
        strncpy(resolved_ip, ipstr, sizeof(resolved_ip)-1);
        resolved_ip[sizeof(resolved_ip)-1] = '\0';

        strncpy(resolved_host, server_host, sizeof(resolved_host)-1);
        resolved_host[sizeof(resolved_host)-1] = '\0';
    }


    if (sendto(sock, req_buf, req_len, 0, (struct sockaddr*)&servAddr, sizeof(servAddr)) != req_len) {
        errorHandler("sendto() failed");
#if defined(_WIN32) || defined(WIN32)
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return EXIT_FAILURE;
    }


    fromSize = (socklen_t)sizeof(fromAddr);
    int rsp_len = recvfrom(sock, rsp_buf, sizeof(rsp_buf), 0, (struct sockaddr*)&fromAddr, &fromSize);
    if (rsp_len <= 0) {
        errorHandler("recvfrom() failed");
#if defined(_WIN32) || defined(WIN32)
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return EXIT_FAILURE;
    }

    // Controllo provenienza (IP)
    if (servAddr.sin_addr.s_addr != fromAddr.sin_addr.s_addr) {
        fprintf(stderr, "Error: received packet from unknown source.\n");
#if defined(_WIN32) || defined(WIN32)
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return EXIT_FAILURE;
    }

    // Deserializza risposta
    weather_response_t response;
    deserialize_response(rsp_buf, &response);

    // Capitalizza citt�
    char city_output[CITY_NAME_MAX_LEN];
    format_city_title(request.city, city_output, sizeof(city_output));

    // Output conforme alla traccia
    printf("Ricevuto risultato dal server %s (ip %s). ", resolved_host, resolved_ip[0] ? resolved_ip : "unknown");

    if (response.status == STATUS_SUCCESS) {
        if (response.type == TYPE_TEMPERATURE)
            printf("%s: Temperatura = %.1f�C\n", city_output, response.value);
        else if (response.type == TYPE_HUMIDITY)
            printf("%s: Umidit� = %.1f%%\n", city_output, response.value);
        else if (response.type == TYPE_WIND)
            printf("%s: Vento = %.1f km/h\n", city_output, response.value);
        else if (response.type == TYPE_PRESSURE)
            printf("%s: Pressione = %.1f hPa\n", city_output, response.value);
        else
            printf("Tipo sconosciuto nella risposta\n");
    } else if (response.status == STATUS_CITY_UNAVAILABLE) {
        printf("Citt� non disponibile\n");
    } else if (response.status == STATUS_INVALID_REQUEST) {
        printf("Richiesta non valida\n");
    } else {
        // qualunque altro status non previsto
        printf("Richiesta non valida\n");
    }

    // Chiusura
#if defined(_WIN32) || defined(WIN32)
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
    return EXIT_SUCCESS;
}
