#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "usb_protocol.h"

static void check(const char *line,rx_message_type_t type)
{rx_message_t m;const char *error;assert(rx_message_parse(line,&m,&error));assert(m.type==type);}
int main(void)
{
    check("{\"v\":1,\"type\":\"hello\"}",RX_HELLO);
    check("{\"v\":1,\"type\":\"ready\"}",RX_READY);
    check("{\"v\":1,\"type\":\"get_status\"}",RX_GET_STATUS);
    check("{\"v\":1,\"type\":\"ping\",\"nonce\":4294967295}",RX_PING);
    rx_message_t m;const char *error;
    const char *ack="{\"v\":1,\"type\":\"ack\",\"wearable_id\":\"ac276e45e006\",\"session_id\":4294967295,\"sequence\":42,\"status\":\"ok\",\"transition_flags\":3,\"score\":{\"revision\":65535,\"points_a\":15,\"points_b\":0,\"games_a\":0,\"games_b\":0,\"sets_a\":1,\"sets_b\":0}}";
    assert(rx_message_parse(ack,&m,&error));
    assert(m.ack.session_id==UINT32_MAX&&m.ack.sequence==42&&m.ack.state.points_a==15);
    uint8_t bytes[20];assert(wearable_encode_ack(&m.ack,bytes,sizeof(bytes))==20);
    wearable_ack_packet_t decoded;assert(wearable_decode_ack(bytes,20,&decoded));
    assert(decoded.session_id==UINT32_MAX&&decoded.state.revision==65535&&decoded.transition_flags==3);
    const char *bad[]={
        "{}","[]","{\"v\":2,\"type\":\"hello\"}","{\"v\":1,\"v\":2,\"type\":\"hello\"}",
        "{\"v\":1,\"type\":\"ping\",\"nonce\":-1}","{\"v\":1,\"type\":\"ping\",\"nonce\":1.5}",
        "{\"v\":1,\"type\":\"ping\",\"nonce\":4294967296}","{\"v\":1,\"type\":\"hello\"}garbage",
        "{\"v\":1,\"type\":\"ack\",\"status\":\"ok\"}","{\"v\":1,\"type\":\"hello\\u0000other\"}",
    };
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(!rx_message_parse(bad[i],&m,&error));
    cJSON *j=cJSON_Parse(ack);cJSON *score=cJSON_GetObjectItemCaseSensitive(j,"score");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(score,"points_a"),256);
    char *modified=cJSON_PrintUnformatted(j);assert(!rx_message_parse(modified,&m,&error));cJSON_free(modified);cJSON_Delete(j);
    char too_long[1025];memset(too_long,' ',1024);too_long[1024]=0;assert(!rx_message_parse(too_long,&m,&error));
    assert(!strcmp(rx_command(WEARABLE_ACTION_VAR),".VAR"));assert(!rx_command(WEARABLE_ACTION_ENTER_PAIRING));
    wearable_action_packet_t action={.session_id=123,.sequence=42,.action=WEARABLE_ACTION_POINT_A},roundtrip;
    uint8_t packet[12];assert(wearable_encode_action(&action,packet,12)==12);assert(wearable_decode_action(packet,12,&roundtrip));
    assert(roundtrip.sequence==42&&roundtrip.action==0);packet[10]=8;assert(!wearable_decode_action(packet,12,&roundtrip));
    puts("Receiver JSON validation and shared binary protocol: OK");
}
