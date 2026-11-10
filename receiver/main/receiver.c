#include "receiver.h"
#include "usb_protocol.h"
#include "wifi_network.h"
#include "playmaker_discovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "driver/usb_serial_jtag.h"
#include "esp_http_client.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "nvs.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

typedef struct {char id[13],ip[16];int64_t seen;} candidate_t;
typedef struct {char line[RX_USB_LINE_MAX];uint32_t epoch;} output_t;
typedef struct {rx_message_t message;uint32_t epoch;} ack_request_t;
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_output,s_acks;
static bool s_lan,s_ready,s_hello,s_rebooting,s_pending,s_have_score;
static uint32_t s_epoch;
static int64_t s_pc_seen,s_link_seen,s_deadline;
static char s_id[13],s_boot[9],s_selected[13],s_ip[16],s_client[33];
static candidate_t s_candidates[8];
static wearable_action_packet_t s_action;
static wearable_match_state_t s_score;

static int64_t now(void){return esp_timer_get_time();}
static cJSON *object(const char *type)
{
    cJSON *j=cJSON_CreateObject();cJSON_AddNumberToObject(j,"v",1);cJSON_AddStringToObject(j,"type",type);return j;
}
static bool pc_ready(void)
{
    xSemaphoreTake(s_lock,portMAX_DELAY);
    if(s_ready&&(!usb_serial_jtag_is_connected()||now()-s_pc_seen>=3000000)){
        s_ready=s_hello=s_pending=false;s_epoch++;
    }
    bool ready=s_ready&&!s_rebooting;
    xSemaphoreGive(s_lock);return ready;
}
static void emit_epoch(cJSON *j,uint32_t epoch)
{
    output_t output={0};
    xSemaphoreTake(s_lock,portMAX_DELAY);output.epoch=epoch==UINT32_MAX?s_epoch:epoch;xSemaphoreGive(s_lock);
    if(j&&cJSON_PrintPreallocated(j,output.line,sizeof(output.line)-1,false)){
        size_t len=strlen(output.line);output.line[len]='\n';output.line[len+1]=0;
        if(xQueueSend(s_output,&output,0)!=pdTRUE){
            xSemaphoreTake(s_lock,portMAX_DELAY);s_ready=s_hello=s_pending=false;s_epoch++;xSemaphoreGive(s_lock);
        }
    }
    cJSON_Delete(j);
}
static void emit(cJSON *j){emit_epoch(j,UINT32_MAX);}
static void error(const char *code)
{
    cJSON *j=object("error");cJSON_AddStringToObject(j,"code",code);cJSON_AddStringToObject(j,"message",code);emit(j);
}
static void identity(cJSON *j)
{
    cJSON_AddStringToObject(j,"receiver_id",s_id);cJSON_AddStringToObject(j,"boot_id",s_boot);
    cJSON_AddStringToObject(j,"mode",s_lan?"lan":"direct");
}
static void ids(cJSON *j,const char *wearable,uint32_t session,uint32_t sequence)
{
    cJSON_AddStringToObject(j,"wearable_id",wearable);cJSON_AddNumberToObject(j,"session_id",session);cJSON_AddNumberToObject(j,"sequence",sequence);
}
static cJSON *status(void)
{
    bool ready=pc_ready();wifi_network_status_t wifi;wifi_network_status(&wifi);
    cJSON *j=object("status");identity(j);
    cJSON_AddBoolToObject(j,"wifi_connected",s_lan?wifi.connected:true);
    if(s_lan&&!wifi.connected)cJSON_AddNullToObject(j,"ip");else cJSON_AddStringToObject(j,"ip",s_lan?wifi.ip:"192.168.8.1");
    cJSON_AddBoolToObject(j,"pc_ready",ready);
    xSemaphoreTake(s_lock,portMAX_DELAY);
    cJSON_AddBoolToObject(j,"wearable_connected",s_link_seen>0&&now()-s_link_seen<3000000);
    if(s_selected[0])cJSON_AddStringToObject(j,"wearable_id",s_selected);else cJSON_AddNullToObject(j,"wearable_id");
    if(s_ip[0])cJSON_AddStringToObject(j,"wearable_ip",s_ip);else cJSON_AddNullToObject(j,"wearable_ip");
    if(s_have_score)cJSON_AddItemToObject(j,"last_confirmed_score",rx_score_json(&s_score));else cJSON_AddNullToObject(j,"last_confirmed_score");
    xSemaphoreGive(s_lock);return j;
}
static void usb_tx(void *arg)
{
    (void)arg;output_t output;
    for(;;){
        if(xQueueReceive(s_output,&output,portMAX_DELAY)!=pdTRUE)continue;
        xSemaphoreTake(s_lock,portMAX_DELAY);bool valid=output.epoch==s_epoch;xSemaphoreGive(s_lock);
        if(!valid||!usb_serial_jtag_is_connected())continue;
        size_t length=strlen(output.line),offset=0;int64_t deadline=now()+300000;
        while(offset<length&&now()<deadline){
            int n=usb_serial_jtag_write_bytes(output.line+offset,length-offset,pdMS_TO_TICKS(20));
            if(n>0)offset+=n;
        }
        if(offset<length){
            /* Terminate a partial frame before the next handshake response. */
            usb_serial_jtag_write_bytes("\n",1,0);
            xSemaphoreTake(s_lock,portMAX_DELAY);s_ready=s_hello=s_pending=false;s_epoch++;xSemaphoreGive(s_lock);
        }
    }
}
static void usb_message(const char *line)
{
    rx_message_t msg;const char *why;
    if(!rx_message_parse(line,&msg,&why)){error(why);return;}
    if(msg.type==RX_HELLO){
        xSemaphoreTake(s_lock,portMAX_DELAY);s_epoch++;s_hello=true;s_ready=s_pending=false;s_pc_seen=now();xSemaphoreGive(s_lock);
        cJSON *j=object("hello");identity(j);cJSON_AddNumberToObject(j,"max_attempts",3);cJSON_AddNumberToObject(j,"ack_timeout_ms",1000);emit(j);return;
    }
    if(msg.type==RX_GET_STATUS){
        if(pc_ready()){xSemaphoreTake(s_lock,portMAX_DELAY);s_pc_seen=now();xSemaphoreGive(s_lock);}
        emit(status());return;
    }
    if(msg.type==RX_READY){
        xSemaphoreTake(s_lock,portMAX_DELAY);bool hello=s_hello;
        if(hello){s_ready=true;s_pc_seen=now();}xSemaphoreGive(s_lock);
        if(!hello)error("NOT_READY");else emit(status());return;
    }
    if(!pc_ready()){error("NOT_READY");return;}
    xSemaphoreTake(s_lock,portMAX_DELAY);s_pc_seen=now();uint32_t epoch=s_epoch;xSemaphoreGive(s_lock);
    if(msg.type==RX_PING){cJSON *j=object("pong");cJSON_AddNumberToObject(j,"nonce",msg.nonce);emit(j);return;}
    if(msg.type==RX_ACK){
        ack_request_t request={.message=msg,.epoch=epoch};
        if(xQueueSend(s_acks,&request,0)!=pdTRUE)error("LINK_UNAVAILABLE");
    }
}
static void usb_rx(void *arg)
{
    (void)arg;char line[RX_USB_LINE_MAX];size_t used=0;bool discard=false;
    for(;;){
        uint8_t bytes[128];int n=usb_serial_jtag_read_bytes(bytes,sizeof(bytes),pdMS_TO_TICKS(50));
        pc_ready();
        for(int i=0;i<n;i++){
            if(bytes[i]=='\n'){
                if(discard)error("LINE_TOO_LONG");
                else{if(used&&line[used-1]=='\r')used--;line[used]=0;if(used)usb_message(line);}
                used=0;discard=false;
            }else if(bytes[i]==0){discard=true;}
            else if(!discard){if(used>=sizeof(line)-1)discard=true;else line[used++]=bytes[i];}
        }
        if(!usb_serial_jtag_is_connected()){used=0;discard=false;}
    }
}

typedef struct {uint8_t data[1536];size_t size;unsigned attempt,remaining;bool overflow;} http_result_t;
static esp_err_t http_event(esp_http_client_event_t *event)
{
    http_result_t *r=event->user_data;
    if(event->event_id==HTTP_EVENT_ON_DATA){
        if(event->data_len<0||r->size+event->data_len>sizeof(r->data)-1)r->overflow=true;
        else{memcpy(r->data+r->size,event->data,event->data_len);r->size+=event->data_len;r->data[r->size]=0;}
    }else if(event->event_id==HTTP_EVENT_ON_HEADER){
        if(!strcasecmp(event->header_key,"X-Playmaker-Attempt"))r->attempt=strtoul(event->header_value,NULL,10);
        if(!strcasecmp(event->header_key,"X-Playmaker-Remaining-Ms"))r->remaining=strtoul(event->header_value,NULL,10);
    }
    return ESP_OK;
}
static int request(const char *ip,const char *path,bool post,const uint8_t *body,size_t len,http_result_t *result)
{
    char url[96];snprintf(url,sizeof(url),"http://%s%s",ip,path);memset(result,0,sizeof(*result));
    esp_http_client_config_t cfg={.url=url,.timeout_ms=500,.event_handler=http_event,.user_data=result,.disable_auto_redirect=true};
    esp_http_client_handle_t client=esp_http_client_init(&cfg);if(!client)return -1;
    esp_http_client_set_header(client,"X-Playmaker-Client",s_client);
    if(post){esp_http_client_set_method(client,HTTP_METHOD_POST);esp_http_client_set_header(client,"Content-Type","application/octet-stream");esp_http_client_set_post_field(client,(const char *)(body?body:(const uint8_t *)""),len);}
    esp_err_t err=esp_http_client_perform(client);int code=err==ESP_OK&&!result->overflow?esp_http_client_get_status_code(client):-1;
    esp_http_client_cleanup(client);return code;
}
static void ack_result(const rx_message_t *m,const char *result,uint32_t epoch)
{
    cJSON *j=object("ack_result");ids(j,m->wearable_id,m->ack.session_id,m->ack.sequence);cJSON_AddStringToObject(j,"result",result);emit_epoch(j,epoch);
}
static void bridge_task(void *arg)
{
    (void)arg;char active_ip[16]="",active_id[13]="";bool claimed=false;uint32_t epoch=0;int64_t last_check=0,last_status=0;
    http_result_t result;
    for(;;){
        bool ready=pc_ready();char ip[16],id[13];uint32_t current_epoch;
        xSemaphoreTake(s_lock,portMAX_DELAY);strlcpy(ip,s_ip,sizeof(ip));strlcpy(id,s_selected,sizeof(id));current_epoch=s_epoch;xSemaphoreGive(s_lock);
        if(!ready||!ip[0]){claimed=false;vTaskDelay(pdMS_TO_TICKS(50));continue;}
        if(epoch!=current_epoch||strcmp(active_ip,ip)||strcmp(active_id,id)){
            epoch=current_epoch;strlcpy(active_ip,ip,sizeof(active_ip));strlcpy(active_id,id,sizeof(active_id));claimed=false;last_check=0;
        }
        if(!last_check||now()-last_check>=10000000){
            int code=request(ip,"/api/network",false,NULL,0,&result);
            cJSON *j=code==200?cJSON_Parse((char *)result.data):NULL;
            const cJSON *device=cJSON_GetObjectItemCaseSensitive(j,"device_id");
            bool valid=cJSON_IsString(device)&&!strcmp(device->valuestring,id);cJSON_Delete(j);
            if(!valid){claimed=false;vTaskDelay(pdMS_TO_TICKS(1000));continue;}
            last_check=now();
        }
        if(!claimed){
            int code=request(ip,"/api/session",true,NULL,0,&result);
            if(code!=204){if(code==409)error("WEARABLE_BUSY");vTaskDelay(pdMS_TO_TICKS(1000));continue;}
            claimed=true;
        }
        if(!pc_ready())continue;
        ack_request_t ack;
        while(xQueueReceive(s_acks,&ack,0)==pdTRUE){
            if(ack.epoch!=epoch||!pc_ready())continue;
            xSemaphoreTake(s_lock,portMAX_DELAY);
            bool valid=s_pending&&!strcmp(ack.message.wearable_id,id)&&ack.message.ack.session_id==s_action.session_id&&
                ack.message.ack.sequence==s_action.sequence&&now()<s_deadline;
            xSemaphoreGive(s_lock);
            if(!valid){ack_result(&ack.message,"expired",epoch);continue;}
            uint8_t bytes[WEARABLE_ACK_PACKET_SIZE];wearable_encode_ack(&ack.message.ack,bytes,sizeof(bytes));
            int code=request(ip,"/api/ack",true,bytes,sizeof(bytes),&result);
            if(code==204){
                xSemaphoreTake(s_lock,portMAX_DELAY);
                if(epoch==s_epoch){
                    if(ack.message.ack.status==WEARABLE_ACK_OK){s_score=ack.message.ack.state;s_have_score=true;}
                    if(ack.message.ack.status!=WEARABLE_ACK_TEMPORARY_ERROR)s_pending=false;
                    s_link_seen=now();
                }
                xSemaphoreGive(s_lock);
            }
            ack_result(&ack.message,code==204?"forwarded":code==410?"expired":"link_error",epoch);
            if(code==409)claimed=false;
        }
        if(!pc_ready())continue;
        int64_t started=now();int code=request(ip,"/api/action",false,NULL,0,&result);
        if(code==200||code==204){xSemaphoreTake(s_lock,portMAX_DELAY);if(epoch==s_epoch)s_link_seen=now();xSemaphoreGive(s_lock);}
        if(code==409)claimed=false;
        if(code==200){
            wearable_action_packet_t action;
            if(result.attempt<1||result.attempt>3||result.remaining==0||result.remaining>3000||
               !wearable_decode_action(result.data,result.size,&action)){error("UNKNOWN_ACTION");}
            else if(pc_ready()&&now()<started+(int64_t)result.remaining*1000){
                xSemaphoreTake(s_lock,portMAX_DELAY);
                if(epoch==s_epoch){s_pending=true;s_action=action;s_deadline=started+(int64_t)result.remaining*1000;}
                xSemaphoreGive(s_lock);
                cJSON *j=object("action");ids(j,id,action.session_id,action.sequence);cJSON_AddNumberToObject(j,"action",action.action);
                cJSON_AddStringToObject(j,"command",rx_command(action.action));cJSON_AddNumberToObject(j,"attempt",result.attempt);emit_epoch(j,epoch);
            }
        }
        if(now()-last_status>=2000000){emit(status());last_status=now();}
        /* ACKs wake the bridge immediately rather than waiting for the poll interval. */
        for(unsigned i=0;i<15;i++){if(uxQueueMessagesWaiting(s_acks))break;vTaskDelay(pdMS_TO_TICKS(10));}
    }
}

static bool save_selected(const char *id)
{
    nvs_handle_t h;if(nvs_open("receiver",NVS_READWRITE,&h)!=ESP_OK)return false;
    esp_err_t err=nvs_set_str(h,"wearable",id);if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);return err==ESP_OK;
}
static void broadcast(int sock,const char *key,const char *probe)
{
    esp_netif_t *netif=esp_netif_get_handle_from_ifkey(key);esp_netif_ip_info_t info;
    if(!netif||esp_netif_get_ip_info(netif,&info)!=ESP_OK||!info.ip.addr)return;
    struct sockaddr_in peer={.sin_family=AF_INET,.sin_port=htons(PLAYMAKER_DISCOVERY_PORT)};
    peer.sin_addr.s_addr=info.ip.addr|~info.netmask.addr;
    sendto(sock,probe,strlen(probe),0,(struct sockaddr *)&peer,sizeof(peer));
}
static void discovery_task(void *arg)
{
    (void)arg;
    for(;;){
        int sock=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if(sock<0){vTaskDelay(pdMS_TO_TICKS(3000));continue;}
        int yes=1;setsockopt(sock,SOL_SOCKET,SO_BROADCAST,&yes,sizeof(yes));
        struct timeval timeout={.tv_usec=100000};setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
        uint32_t nonce=esp_random();char probe[140];
        snprintf(probe,sizeof(probe),"{\"v\":1,\"type\":\"discover\",\"service\":\"playmaker-wearable\",\"nonce\":%lu}",(unsigned long)nonce);
        broadcast(sock,"WIFI_AP_DEF",probe);if(s_lan)broadcast(sock,"WIFI_STA_DEF",probe);
        int64_t until=now()+800000;
        while(now()<until){
            char bytes[PLAYMAKER_DISCOVERY_MAX+1];struct sockaddr_in peer;socklen_t plen=sizeof(peer);
            int n=recvfrom(sock,bytes,sizeof(bytes),0,(struct sockaddr *)&peer,&plen);
            if(n<=0||n>PLAYMAKER_DISCOVERY_MAX)continue;
            bytes[n]=0;
            cJSON *j=cJSON_ParseWithLengthOpts(bytes,n+1,NULL,true);uint32_t v,received,port;
            const cJSON *type=cJSON_GetObjectItemCaseSensitive(j,"type"),*service=cJSON_GetObjectItemCaseSensitive(j,"service"),*id=cJSON_GetObjectItemCaseSensitive(j,"wearable_id");
            bool valid=rx_json_uint(j,"v",1,&v)&&v==1&&rx_json_uint(j,"nonce",UINT32_MAX,&received)&&received==nonce&&
                rx_json_uint(j,"http_port",65535,&port)&&port==80&&cJSON_IsString(type)&&!strcmp(type->valuestring,"discovery")&&
                cJSON_IsString(service)&&!strcmp(service->valuestring,"playmaker-wearable")&&cJSON_IsString(id)&&rx_id_valid(id->valuestring);
            if(valid){
                char ip[16];inet_ntop(AF_INET,&peer.sin_addr,ip,sizeof(ip));
                xSemaphoreTake(s_lock,portMAX_DELAY);
                int slot=-1;
                for(unsigned i=0;i<8;i++)if(!strcmp(s_candidates[i].id,id->valuestring)){slot=i;break;}
                if(slot<0)for(unsigned i=0;i<8;i++)if(!s_candidates[i].id[0]||now()-s_candidates[i].seen>15000000){slot=i;break;}
                if(slot>=0){strlcpy(s_candidates[slot].id,id->valuestring,13);strlcpy(s_candidates[slot].ip,ip,16);s_candidates[slot].seen=now();}
                if(!strcmp(s_selected,id->valuestring))strlcpy(s_ip,ip,sizeof(s_ip));
                xSemaphoreGive(s_lock);
            }
            cJSON_Delete(j);
        }
        close(sock);
        xSemaphoreTake(s_lock,portMAX_DELAY);
        char only[13]="",only_ip[16]="";unsigned count=0;
        for(unsigned i=0;i<8;i++)if(s_candidates[i].id[0]&&now()-s_candidates[i].seen<15000000){count++;strlcpy(only,s_candidates[i].id,13);strlcpy(only_ip,s_candidates[i].ip,16);}
        bool choose=!s_selected[0]&&count==1;
        xSemaphoreGive(s_lock);
        if(choose&&save_selected(only)){
            xSemaphoreTake(s_lock,portMAX_DELAY);
            if(!s_selected[0]){strlcpy(s_selected,only,sizeof(s_selected));strlcpy(s_ip,only_ip,sizeof(s_ip));}
            xSemaphoreGive(s_lock);
        }
        vTaskDelay(pdMS_TO_TICKS(2200));
    }
}

extern const unsigned char index_start[] asm("_binary_index_html_start"),index_end[] asm("_binary_index_html_end");
extern const unsigned char script_start[] asm("_binary_app_js_start"),script_end[] asm("_binary_app_js_end");
extern const unsigned char css_start[] asm("_binary_brand_css_start"),css_end[] asm("_binary_brand_css_end");
static esp_err_t asset(httpd_req_t *req,const char *type,const unsigned char *start,const unsigned char *end)
{size_t n=end-start;while(n&&!start[n-1])n--;httpd_resp_set_type(req,type);httpd_resp_set_hdr(req,"Cache-Control","no-store");return httpd_resp_send(req,(const char *)start,n);}
static esp_err_t root(httpd_req_t *req){return asset(req,"text/html; charset=utf-8",index_start,index_end);}
static esp_err_t script(httpd_req_t *req){return asset(req,"application/javascript; charset=utf-8",script_start,script_end);}
static esp_err_t css(httpd_req_t *req){return asset(req,"text/css; charset=utf-8",css_start,css_end);}
static esp_err_t receiver_get(httpd_req_t *req)
{
    cJSON *j=status(),*list=cJSON_AddArrayToObject(j,"wearables");
    xSemaphoreTake(s_lock,portMAX_DELAY);
    for(unsigned i=0;i<8;i++)if(s_candidates[i].id[0]&&now()-s_candidates[i].seen<15000000){
        cJSON *entry=cJSON_CreateObject();cJSON_AddStringToObject(entry,"id",s_candidates[i].id);cJSON_AddStringToObject(entry,"ip",s_candidates[i].ip);cJSON_AddItemToArray(list,entry);
    }
    xSemaphoreGive(s_lock);
    char *body=cJSON_PrintUnformatted(j);cJSON_Delete(j);
    if(!body)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Memoria insufficiente");
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    esp_err_t err=httpd_resp_sendstr(req,body);cJSON_free(body);return err;
}
static void reboot_task(void *arg){(void)arg;vTaskDelay(pdMS_TO_TICKS(1000));esp_restart();}
static esp_err_t receiver_post(httpd_req_t *req)
{
    char bytes[160];if(req->content_len<=0||req->content_len>=sizeof(bytes))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Richiesta non valida");
    int used=0;while(used<req->content_len){int n=httpd_req_recv(req,bytes+used,req->content_len-used);if(n<=0)return ESP_FAIL;used+=n;}bytes[used]=0;
    cJSON *j=cJSON_ParseWithLengthOpts(bytes,used+1,NULL,true);
    const cJSON *mode=cJSON_GetObjectItemCaseSensitive(j,"mode"),*id=cJSON_GetObjectItemCaseSensitive(j,"wearable_id");
    bool mode_valid=cJSON_IsString(mode)&&(!strcmp(mode->valuestring,"direct")||!strcmp(mode->valuestring,"lan"));
    bool id_valid=cJSON_IsString(id)&&rx_id_valid(id->valuestring);
    if(!cJSON_IsObject(j)||(!mode&&!id)||(mode&&!mode_valid)||(id&&!id_valid)){cJSON_Delete(j);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Modalita o wearable non validi");}
    bool reboot=mode_valid&&(s_lan!=!strcmp(mode->valuestring,"lan"));
    nvs_handle_t h;esp_err_t err=nvs_open("receiver",NVS_READWRITE,&h);
    if(err==ESP_OK){
        if(mode_valid)err=nvs_set_u8(h,"lan",!strcmp(mode->valuestring,"lan"));
        if(err==ESP_OK&&id_valid)err=nvs_set_str(h,"wearable",id->valuestring);
        if(err==ESP_OK)err=nvs_commit(h);
        nvs_close(h);
    }
    if(err!=ESP_OK){cJSON_Delete(j);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Salvataggio fallito");}
    if(id_valid){xSemaphoreTake(s_lock,portMAX_DELAY);strlcpy(s_selected,id->valuestring,13);s_ip[0]=0;s_pending=s_have_score=false;s_link_seen=0;s_epoch++;s_hello=s_ready=false;xSemaphoreGive(s_lock);}
    cJSON_Delete(j);
    if(reboot){
        xSemaphoreTake(s_lock,portMAX_DELAY);s_rebooting=true;xSemaphoreGive(s_lock);
        if(xTaskCreate(reboot_task,"reboot",2048,NULL,2,NULL)!=pdPASS)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Riavvia manualmente il ricevitore");
    }
    return httpd_resp_sendstr(req,reboot?"Modalita salvata. Riavvio: ricollegati al Wi-Fi del ricevitore.":"Wearable salvato. Riapri la sessione USB sul PC.");
}

esp_err_t receiver_start(bool lan_mode)
{
    s_lan=lan_mode;s_lock=xSemaphoreCreateMutex();s_output=xQueueCreate(8,sizeof(output_t));s_acks=xQueueCreate(4,sizeof(ack_request_t));
    if(!s_lock||!s_output||!s_acks)return ESP_ERR_NO_MEM;
    playmaker_device_id(s_id);snprintf(s_boot,sizeof(s_boot),"%08lx",(unsigned long)esp_random());
    for(unsigned i=0;i<4;i++)snprintf(s_client+i*8,9,"%08lx",(unsigned long)esp_random());
    nvs_handle_t h;if(nvs_open("receiver",NVS_READONLY,&h)==ESP_OK){size_t size=sizeof(s_selected);if(nvs_get_str(h,"wearable",s_selected,&size)!=ESP_OK||!rx_id_valid(s_selected))s_selected[0]=0;nvs_close(h);}
    usb_serial_jtag_driver_config_t usb={.tx_buffer_size=2048,.rx_buffer_size=2048};
    esp_err_t err=usb_serial_jtag_driver_install(&usb);if(err!=ESP_OK)return err;
    if(xTaskCreate(usb_tx,"usb_tx",4096,NULL,5,NULL)!=pdPASS||xTaskCreate(usb_rx,"usb_rx",6144,NULL,6,NULL)!=pdPASS||
       xTaskCreate(discovery_task,"rx_discovery",6144,NULL,3,NULL)!=pdPASS||xTaskCreate(bridge_task,"bridge",8192,NULL,4,NULL)!=pdPASS)return ESP_ERR_NO_MEM;
    return ESP_OK;
}
esp_err_t receiver_register(httpd_handle_t server)
{
    const httpd_uri_t routes[]={
        {.uri="/",.method=HTTP_GET,.handler=root},{.uri="/app.js",.method=HTTP_GET,.handler=script},
        {.uri="/brand.css",.method=HTTP_GET,.handler=css},{.uri="/api/receiver",.method=HTTP_GET,.handler=receiver_get},
        {.uri="/api/receiver",.method=HTTP_POST,.handler=receiver_post},
    };
    for(unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);i++){esp_err_t err=httpd_register_uri_handler(server,&routes[i]);if(err!=ESP_OK)return err;}
    return ESP_OK;
}
