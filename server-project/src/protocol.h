#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#include <stdint.h>

#define SERVER_PORT 56700       // Porta di default da traccia
#define BUFFER_SIZE 512         // Dimensione buffer generico
#define QUEUE_SIZE 5            // Lunghezza coda connessioni pendenti
#define CITY_NAME_MAX_LEN 64    // Lunghezza massima nome citta

#define STATUS_SUCCESS 0            // Richiesta valida
#define STATUS_CITY_UNAVAILABLE 1   // Citta non supportata
#define STATUS_INVALID_REQUEST 2    // Tipo di richiesta non valido

#define TYPE_TEMPERATURE 't'
#define TYPE_HUMIDITY    'h'
#define TYPE_WIND        'w'
#define TYPE_PRESSURE    'p'

typedef struct {
    char type;                      // 't','h','w','p'
    char city[CITY_NAME_MAX_LEN];   // Nome citta
} weather_request_t;

typedef struct {
    unsigned int status;   // Codice di stato (0,1,2)
    char type;             // Eco
    float value;           // Valore meteo
} weather_response_t;

// --- Prototipi funzioni di generazione dati ---
float get_temperature(void);
float get_humidity(void);
float get_wind(void);
float get_pressure(void);


void deserialize_request(const char *buf, weather_request_t *req);
int serialize_response(const weather_response_t *res, char *buf);


int is_alpha_space_string(const char *s);
int is_city_supported(const char *city);

#endif /* server PROTOCOL_H_ */
