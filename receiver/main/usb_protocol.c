#include "usb_protocol.h"
#include <string.h>

bool rx_id_valid(const char *id)
{
    if(!id||strlen(id)!=12)return false;
    for(unsigned i=0;i<12;i++)if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f')))return false;
    return true;
}
bool rx_json_uint(const cJSON *object,const char *key,uint32_t max,uint32_t *out)
{
    const cJSON *n=cJSON_GetObjectItemCaseSensitive(object,key);
    if(!cJSON_IsNumber(n)||!(n->valuedouble>=0)||n->valuedouble>max||n->valuedouble!=(uint32_t)n->valuedouble)return false;
    *out=(uint32_t)n->valuedouble;return true;
}
/* Reject duplicate keys so two consumers cannot disagree about an ACK's identity. */
static bool unique_object(const cJSON *j)
{
    if(!cJSON_IsObject(j))return false;
    for(const cJSON *a=j->child;a;a=a->next){
        for(const cJSON *b=a->next;b;b=b->next)if(!strcmp(a->string,b->string))return false;
        if(cJSON_IsObject(a)&&!unique_object(a))return false;
    }
    return true;
}
bool rx_message_parse(const char *line,rx_message_t *out,const char **error)
{
    *error="INVALID_MESSAGE";
    size_t len=strlen(line);
    if(len>=RX_USB_LINE_MAX){*error="LINE_TOO_LONG";return false;}
    if(strstr(line,"\\u0000"))return false;
    cJSON *j=cJSON_ParseWithLengthOpts(line,len+1,NULL,true);
    bool ok=false;uint32_t n;
    if(!unique_object(j))goto done;
    if(!rx_json_uint(j,"v",UINT32_MAX,&n))goto done;
    if(n!=1){*error="UNSUPPORTED_VERSION";goto done;}
    const cJSON *type=cJSON_GetObjectItemCaseSensitive(j,"type");
    if(!cJSON_IsString(type))goto done;
    memset(out,0,sizeof(*out));
    if(!strcmp(type->valuestring,"hello"))out->type=RX_HELLO;
    else if(!strcmp(type->valuestring,"ready"))out->type=RX_READY;
    else if(!strcmp(type->valuestring,"get_status"))out->type=RX_GET_STATUS;
    else if(!strcmp(type->valuestring,"ping")){
        out->type=RX_PING;if(!rx_json_uint(j,"nonce",UINT32_MAX,&out->nonce))goto done;
    }else if(!strcmp(type->valuestring,"ack")){
        out->type=RX_ACK;
        const cJSON *id=cJSON_GetObjectItemCaseSensitive(j,"wearable_id");
        if(!cJSON_IsString(id)||!rx_id_valid(id->valuestring))goto done;
        memcpy(out->wearable_id,id->valuestring,13);
        if(!rx_json_uint(j,"session_id",UINT32_MAX,&out->ack.session_id)||!out->ack.session_id||
           !rx_json_uint(j,"sequence",UINT32_MAX,&out->ack.sequence)||!out->ack.sequence)goto done;
        const cJSON *status=cJSON_GetObjectItemCaseSensitive(j,"status");
        if(!cJSON_IsString(status))goto done;
        if(!strcmp(status->valuestring,"ok"))out->ack.status=WEARABLE_ACK_OK;
        else if(!strcmp(status->valuestring,"rejected"))out->ack.status=WEARABLE_ACK_REJECTED;
        else if(!strcmp(status->valuestring,"temporary_error"))out->ack.status=WEARABLE_ACK_TEMPORARY_ERROR;
        else goto done;
        if(!rx_json_uint(j,"transition_flags",7,&n))goto done;
        out->ack.transition_flags=n;
        if(out->ack.status!=WEARABLE_ACK_OK&&n)goto done;
        const cJSON *score=cJSON_GetObjectItemCaseSensitive(j,"score");
        if(!unique_object(score)||!rx_json_uint(score,"revision",65535,&n))goto done;
        out->ack.state.revision=n;
#define FIELD(name) if(!rx_json_uint(score,#name,255,&n))goto done;out->ack.state.name=n
        FIELD(points_a);FIELD(points_b);FIELD(games_a);FIELD(games_b);FIELD(sets_a);FIELD(sets_b);
#undef FIELD
    }else{*error="UNKNOWN_COMMAND";goto done;}
    ok=true;
done:
    cJSON_Delete(j);return ok;
}
const char *rx_command(wearable_action_t action)
{
    static const char *commands[]={".A",".B",".MOMENTO",".UNDO",".VAR",".AVANTI_START",".INDIETRO_START",".STOP"};
    return action<=WEARABLE_ACTION_STOP?commands[action]:NULL;
}
cJSON *rx_score_json(const wearable_match_state_t *s)
{
    cJSON *j=cJSON_CreateObject();
#define FIELD(name) cJSON_AddNumberToObject(j,#name,s->name)
    FIELD(revision);FIELD(points_a);FIELD(points_b);FIELD(games_a);FIELD(games_b);FIELD(sets_a);FIELD(sets_b);
#undef FIELD
    return j;
}
