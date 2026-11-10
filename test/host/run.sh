#!/usr/bin/env sh
set -eu

cc -std=c11 -Wall -Wextra -Werror   -Imain/core   -Imain/link   test/host/test_core.c   main/core/config_model.c   main/link/wearable_protocol.c   -o /tmp/playmaker_wearable_core_tests

/tmp/playmaker_wearable_core_tests

cc -std=c11 -Wall -Wextra -Werror -Itest/host/stubs -Imain/core -Imain/link -Imain/input -Imain/hardware -Imain/power test/host/test_buttons.c main/core/config_model.c -o /tmp/playmaker_buttons_tests
/tmp/playmaker_buttons_tests
cc -std=c11 -Wall -Wextra -Werror -Itest/host/stubs -Imain/core -Imain/link test/host/test_wifi_link.c main/link/wearable_protocol.c main/core/config_model.c -o /tmp/playmaker_wifi_link_tests
/tmp/playmaker_wifi_link_tests
