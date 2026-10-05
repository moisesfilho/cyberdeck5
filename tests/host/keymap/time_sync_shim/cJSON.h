#pragma once
#include <stddef.h>
typedef struct cJSON {
    char *valuestring;
    int object;
    int string;
    int number;
    double valuedouble;
    int valueint;
    char *timezone;
    char *time_zone;
    char *utc_datetime;
    int numeric[6];
} cJSON;
cJSON *cJSON_ParseWithLength(const char *, size_t);
int cJSON_IsObject(const cJSON *); int cJSON_IsString(const cJSON *); int cJSON_IsNumber(const cJSON *);
const cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *, const char *);
void cJSON_Delete(cJSON *);
