/*
 * protocol.h
 *
 * Client header file
 * Definitions, constants and structures for the client
 */

#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#include <stdint.h>


#define SERVER_PORT        56700   // Porta di default del server
#define BUFFER_SIZE        512     // Buffer per parsing e messaggi
#define CITY_NAME_MAX_LEN  64      // Lunghezza massima nome città

#define STATUS_SUCCESS            0
#define STATUS_CITY_UNAVAILABLE   1
#define STATUS_INVALID_REQUEST    2

#define TYPE_TEMPERATURE  't'
#define TYPE_HUMIDITY     'h'
#define TYPE_WIND         'w'
#define TYPE_PRESSURE     'p'

typedef struct {
    char type;                       // 't','h','w','p'
    char city[CITY_NAME_MAX_LEN];    // Nome città
} weather_request_t;

typedef struct {
    unsigned int status;  // 0,1,2
    char type;            // eco del tipo richiesto
    float value;          // valore meteo
} weather_response_t;

#endif /* PROTOCOL_H_ */
