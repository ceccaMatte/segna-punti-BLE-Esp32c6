#include <assert.h>
#include "../../main/link/wifi_link.c"
int64_t host_now_us;
static unsigned failures,acks;
static void failed(wearable_action_t a,uint32_t seq,void *ctx) {(void)a;(void)seq;(void)ctx;failures++;}
static void confirmed(wearable_action_t a,const wearable_ack_packet_t *ack,void *ctx) {(void)a;(void)ack;(void)ctx;acks++;}
static const char *id="0123456789abcdef0123456789abcdef";
static httpd_req_t poll_at(int64_t time)
{
    host_now_us=time;expire_head();
    httpd_req_t req={.id=id};action_get(&req);return req;
}
static httpd_req_t ack(uint32_t sequence)
{
    uint8_t bytes[20]={6,2,0,0};
    bytes[4]=123;bytes[8]=(uint8_t)sequence;
    httpd_req_t req={.id=id,.body=bytes,.content_len=20};
    ack_post(&req);return req;
}
int main(void)
{
    assert(wifi_link_start(confirmed,failed,NULL)==ESP_OK);
    httpd_req_t req={.id=id};session_post(&req);
    assert(wifi_link_send_action(WEARABLE_ACTION_POINT_A));
    for(unsigned attempt=0;attempt<3;attempt++){
        req=poll_at(attempt*ACTION_ACK_WAIT_US);
        assert(req.response_len==12 && req.response[6]==1);
        req=poll_at(attempt*ACTION_ACK_WAIT_US+150000);assert(req.response_len==0);
    }
    req=poll_at(3000000);assert(req.response_len==0 && failures==1 && s_count==0);
    req=ack(1);assert(!strncmp(req.status,"410",3));
    assert(wifi_link_send_action(WEARABLE_ACTION_POINT_A));
    req=poll_at(3100000);assert(req.response_len==12 && req.response[6]==2);
    req=ack(2);assert(!strncmp(req.status,"204",3) && acks==1 && s_count==0);
    req=ack(2);assert(!strncmp(req.status,"204",3) && acks==1);
    assert(wifi_link_send_action(WEARABLE_ACTION_POINT_A));
    assert(wifi_link_send_action(WEARABLE_ACTION_POINT_B));
    req=poll_at(6200000);assert(failures==2 && req.response_len==12 && req.response[6]==4);
    req=ack(4);assert(acks==2 && s_count==0);
    puts("Wi-Fi HTTP queue: exactly 3 deliveries, expiry, late/duplicate ACK and next command OK");
}
