#include <assert.h>
#include <string.h>
#include "config_model.h"
#include "../../main/input/button_manager.c"
#include "action_retry.h"

int host_levels[64];
static wearable_token_t events[64];
static unsigned count;
static uint32_t clock_ms;
void sleep_manager_note_button_activity(void) {}
static void capture(wearable_token_t token, void *ctx) { (void)ctx; events[count++]=token; }
static void advance(unsigned duration)
{
    for(unsigned i=0;i<duration;i+=5){clock_ms+=5;process_buttons_once(clock_ms);}
}
static void reset(void)
{
    for(unsigned i=0;i<64;i++)host_levels[i]=1;
    wearable_config_t cfg;wearable_config_set_defaults(&cfg);
    assert(button_manager_start(&cfg,capture,NULL)==ESP_OK);
    clock_ms=0;count=0;
}
static void click(unsigned button)
{
    host_levels[s_pins[button]]=0;advance(70);
    host_levels[s_pins[button]]=1;advance(45);
}
static void check_clicks(unsigned button)
{
    reset();
    for(unsigned i=0;i<5;i++)click(button);
    assert(count==5);
    for(unsigned i=0;i<5;i++)assert(events[i]==wearable_token(button,WEARABLE_PRIMITIVE_CLICK));
}
static void test_central(void)
{
    reset();click(WEARABLE_BUTTON_MOMENT);assert(count==0);advance(300);
    assert(count==1 && events[0]==wearable_token(WEARABLE_BUTTON_MOMENT,WEARABLE_PRIMITIVE_CLICK));
    reset();click(WEARABLE_BUTTON_MOMENT);click(WEARABLE_BUTTON_MOMENT);
    assert(count==1 && events[0]==wearable_token(WEARABLE_BUTTON_MOMENT,WEARABLE_PRIMITIVE_DOUBLE));
    advance(400);assert(count==1);
    reset();for(unsigned i=0;i<5;i++)click(WEARABLE_BUTTON_MOMENT);advance(300);
    assert(count==3);
    assert(wearable_token_primitive(events[0])==WEARABLE_PRIMITIVE_DOUBLE);
    assert(wearable_token_primitive(events[1])==WEARABLE_PRIMITIVE_DOUBLE);
    assert(wearable_token_primitive(events[2])==WEARABLE_PRIMITIVE_CLICK);
}
static void test_retry(void)
{
    action_retry_t retry;action_retry_start(&retry,0);
    for(unsigned i=0;i<3;i++){
        int64_t now=i*ACTION_ACK_WAIT_US;
        assert(action_retry_ready(&retry,now));action_retry_sent(&retry,now);
        assert(!action_retry_ready(&retry,now+100));
    }
    assert(!action_retry_ready(&retry,3*ACTION_ACK_WAIT_US));
    assert(action_retry_expired(&retry,3*ACTION_ACK_WAIT_US));
    action_retry_start(&retry,0);
    assert(action_retry_expired(&retry,3*ACTION_ACK_WAIT_US));
}
int main(void)
{
    check_clicks(WEARABLE_BUTTON_A);check_clicks(WEARABLE_BUTTON_B);check_clicks(WEARABLE_BUTTON_UNDO);
    reset();click(WEARABLE_BUTTON_B);click(WEARABLE_BUTTON_A);click(WEARABLE_BUTTON_UNDO);assert(count==3);
    reset();host_levels[BOARD_GPIO_BUTTON_A]=0;advance(600);
    assert(count==1 && wearable_token_primitive(events[0])==WEARABLE_PRIMITIVE_LONG);
    host_levels[BOARD_GPIO_BUTTON_A]=1;advance(40);
    assert(count==2 && wearable_token_primitive(events[1])==WEARABLE_PRIMITIVE_RELEASE);
    test_central();test_retry();puts("button trains, long, central double and 3-attempt expiry: OK");
}
