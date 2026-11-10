#include "playmaker_discovery.h"
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_mac.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void playmaker_device_id(char out[13])
{
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_BT));
    snprintf(out, 13, "%02x%02x%02x%02x%02x%02x", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}
static void discovery_task(void *arg)
{
    (void)arg;
    char id[13]; playmaker_device_id(id);
    for (;;) {
        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0) { vTaskDelay(pdMS_TO_TICKS(3000)); continue; }
        struct sockaddr_in local = {.sin_family=AF_INET,.sin_port=htons(PLAYMAKER_DISCOVERY_PORT),.sin_addr.s_addr=INADDR_ANY};
        if (bind(sock, (struct sockaddr *)&local, sizeof(local))) { close(sock); vTaskDelay(pdMS_TO_TICKS(3000)); continue; }
        for (;;) {
            char bytes[PLAYMAKER_DISCOVERY_MAX+1];
            struct sockaddr_in peer; socklen_t len=sizeof(peer);
            int n=recvfrom(sock,bytes,sizeof(bytes),0,(struct sockaddr *)&peer,&len);
            if(n<0)break;
            if(n==0||n>PLAYMAKER_DISCOVERY_MAX)continue;
            bytes[n]=0;
            cJSON *j=cJSON_ParseWithLengthOpts(bytes,n+1,NULL,true);
            cJSON *v=cJSON_GetObjectItemCaseSensitive(j,"v"), *type=cJSON_GetObjectItemCaseSensitive(j,"type");
            cJSON *service=cJSON_GetObjectItemCaseSensitive(j,"service"), *nonce=cJSON_GetObjectItemCaseSensitive(j,"nonce");
            if(cJSON_IsNumber(v)&&v->valuedouble==1&&cJSON_IsString(type)&&!strcmp(type->valuestring,"discover")&&
               cJSON_IsString(service)&&!strcmp(service->valuestring,"playmaker-wearable")&&cJSON_IsNumber(nonce)&&
               nonce->valuedouble>=0&&nonce->valuedouble<=UINT32_MAX&&nonce->valuedouble==(uint32_t)nonce->valuedouble) {
                char response[220];
                int size=snprintf(response,sizeof(response),"{\"v\":1,\"type\":\"discovery\",\"service\":\"playmaker-wearable\",\"wearable_id\":\"%s\",\"http_port\":80,\"nonce\":%lu}",id,(unsigned long)(uint32_t)nonce->valuedouble);
                sendto(sock,response,size,0,(struct sockaddr *)&peer,len);
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            cJSON_Delete(j);
        }
        close(sock);
    }
}
esp_err_t playmaker_discovery_start(void)
{
    return xTaskCreate(discovery_task,"discovery",4096,NULL,3,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
