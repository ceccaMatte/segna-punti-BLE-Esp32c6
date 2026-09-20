#!/usr/bin/env sh
set -eu

cc -std=c11 -Wall -Wextra -Werror   -Imain/core   -Imain/link   test/host/test_core.c   main/core/config_model.c   main/link/wearable_protocol.c   -o /tmp/playmaker_wearable_core_tests

/tmp/playmaker_wearable_core_tests
