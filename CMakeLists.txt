# Segnapunti padel su Waveshare ESP32-C6-LCD-1.47
#
# La logica del gioco sta in main/ e si prova sul computer con
# test\run_tests.ps1, senza scheda collegata.
#
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(padel_scoreboard)
