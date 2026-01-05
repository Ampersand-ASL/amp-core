# Ampersand Project
# Copyright (C) 2025, Bruce MacKinnon KC1FSZ
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# ( at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
cmake_minimum_required(VERSION 3.13)
project(ampersand C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 23)
add_compile_options(-fstack-protector-all -Wall -Wpedantic -g -fstack-usage -Wstack-usage=8192)

# ----- USB (local) Parrot ----------------------------------------------------

add_executable(main-local-parrot
  src/demos/main-local-parrot.cpp
  src/EventLoop.cpp
  src/Message.cpp
  src/Line.cpp
  src/LineUsb.cpp
  src/Transcoder_G711_ULAW.cpp
  src/Transcoder_SLIN_48K.cpp
  src/Transcoder_SLIN.cpp
  src/Transcoder_SLIN_16K.cpp
  src/BridgeIn.cpp
  src/BridgeOut.cpp
  src/IAX2Util.cpp
  src/Resampler.cpp
  kc1fsz-tools-cpp/src/Common.cpp
  kc1fsz-tools-cpp/src/NetUtils.cpp
  kc1fsz-tools-cpp/src/DTMFDetector2.cpp
  kc1fsz-tools-cpp/src/StdPollTimer.cpp
  kc1fsz-tools-cpp/src/linux/StdClock.cpp
  itu-g711-codec/src/codec.cpp
  itu-g711-codec/src/Plc.cpp
  cmsis-dsp-mock/src/main.cpp
  sound-map/src/sound-map.c
)

target_include_directories(main-local-parrot PRIVATE src)
target_include_directories(main-local-parrot PRIVATE include)
target_include_directories(main-local-parrot PRIVATE kc1fsz-tools-cpp/include)
target_include_directories(main-local-parrot PRIVATE itu-g711-codec/src)
target_include_directories(main-local-parrot PRIVATE cmsis-dsp-mock/include)
target_include_directories(main-local-parrot PRIVATE sound-map/include)

target_link_libraries(main-local-parrot -lasound -lusb-1.0)

# ----- tone-test-1

add_executable(tone-test-1
  src/tests/tone-test-1.cpp
  src/Message.cpp
  src/Line.cpp
  src/LineUsb.cpp
  kc1fsz-tools-cpp/src/Common.cpp
  kc1fsz-tools-cpp/src/DTMFDetector2.cpp
  kc1fsz-tools-cpp/src/StdPollTimer.cpp
  kc1fsz-tools-cpp/src/linux/StdClock.cpp
  itu-g711-codec/src/codec.cpp
  cmsis-dsp-mock/src/main.cpp
)

target_include_directories(tone-test-1 PRIVATE src)
target_include_directories(tone-test-1 PRIVATE kc1fsz-tools-cpp/include)
target_include_directories(tone-test-1 PRIVATE itu-g711-codec/src)
target_include_directories(tone-test-1 PRIVATE cmsis-dsp-mock/include)
target_link_libraries(tone-test-1 -lasound)

# ----- tone-test-2

add_executable(tone-test-2
  src/tests/tone-test-2.cpp
  kc1fsz-tools-cpp/src/Common.cpp
  kc1fsz-tools-cpp/src/linux/StdClock.cpp
)

target_include_directories(tone-test-2 PRIVATE src)
target_include_directories(tone-test-2 PRIVATE kc1fsz-tools-cpp/include)
target_link_libraries(tone-test-2 -lasound)

# ----- mixer-test-1

add_executable(mixer-test-1
  src/tests/mixer-test-1.cpp
  kc1fsz-tools-cpp/src/Common.cpp
  kc1fsz-tools-cpp/src/linux/StdClock.cpp
)

target_include_directories(mixer-test-1 PRIVATE src)
target_include_directories(mixer-test-1 PRIVATE kc1fsz-tools-cpp/include)
target_link_libraries(mixer-test-1 -lasound)

# ----- unit-test

add_executable(unit-test
  src/tests/unit-test.cpp
  src/Message.cpp
  src/Line.cpp
  src/IAX2FrameFull.cpp
  src/Resampler.cpp
  #src/Transcoder_G711_ULAW.cpp
  #src/NodeParrot.cpp
  #src/BridgeIn.cpp
  #src/BridgeOut.cpp
  #src/Transcoder_G711_ULAW.cpp
  #src/Transcoder_SLIN_48K.cpp
  kc1fsz-tools-cpp/src/Common.cpp
  kc1fsz-tools-cpp/src/NetUtils.cpp
  kc1fsz-tools-cpp/src/DTMFDetector2.cpp
  kc1fsz-tools-cpp/src/StdPollTimer.cpp
  kc1fsz-tools-cpp/src/linux/StdClock.cpp
  itu-g711-codec/src/codec.cpp
  itu-g711-codec/src/Plc.cpp
  cmsis-dsp-mock/src/main.cpp
  #alsa-mock/src/asoundlib.c
)

target_include_directories(unit-test PRIVATE src)
target_include_directories(unit-test PRIVATE include)
target_include_directories(unit-test PRIVATE kc1fsz-tools-cpp/include)
target_include_directories(unit-test PRIVATE itu-g711-codec/src)
target_include_directories(unit-test PRIVATE cmsis-dsp-mock/include)
target_include_directories(unit-test PRIVATE alsa-mock/include)
target_include_directories(unit-test PRIVATE hid-mock/include)
target_link_libraries(unit-test -lws2_32)

# ----- Protocol Test

add_executable(protocol-test
  src/tests/protocol-test-1.cpp
  src/EventLoop.cpp
  src/Message.cpp
  src/RegisterTask.cpp
  src/Line.cpp
  src/LineIAX2.cpp
  src/IAX2FrameFull.cpp
  src/IAX2Util.cpp
  src/RetransmissionBufferStd.cpp
  kc1fsz-tools-cpp/src/Common.cpp
  kc1fsz-tools-cpp/src/NetUtils.cpp
  kc1fsz-tools-cpp/src/MicroDNS.cpp
  kc1fsz-tools-cpp/src/linux/StdClock.cpp
  kc1fsz-tools-cpp/src/md5/md5c.c
  kc1fsz-tools-cpp/src/StdPollTimer.cpp
)

target_include_directories(protocol-test PRIVATE src)
target_include_directories(protocol-test PRIVATE include)
target_include_directories(protocol-test PRIVATE kc1fsz-tools-cpp/include)
target_include_directories(protocol-test PRIVATE itu-g711-codec/src)
target_include_directories(protocol-test PRIVATE cmsis-dsp-mock/include)
target_include_directories(protocol-test PRIVATE alsa-mock/include)
target_include_directories(protocol-test PRIVATE hid-mock/include)
target_include_directories(protocol-test PRIVATE amp-server/sw/include)
target_link_libraries(protocol-test -lcurl)

# ---- DTMF Test ------

add_executable(dtmf-test-1
  src/tests/dtmf-test-1.cpp
  kc1fsz-tools-cpp/src/DTMFDetector2.cpp
  kc1fsz-tools-cpp/src/Common.cpp
)

target_include_directories(dtmf-test-1 PRIVATE src)
target_include_directories(dtmf-test-1 PRIVATE kc1fsz-tools-cpp/include)

# ----- RegisterTask Test

add_executable(register-test
  src/tests/register-test-1.cpp
  src/RegisterTask.cpp
  kc1fsz-tools-cpp/src/Common.cpp
  kc1fsz-tools-cpp/src/linux/StdClock.cpp
)

target_include_directories(register-test PRIVATE src)
target_include_directories(register-test PRIVATE kc1fsz-tools-cpp/include)
target_include_directories(register-test PRIVATE amp-server/sw/include)
target_link_libraries(register-test -lcurl)

# ----- stats-test --------------------------------------------------------

add_executable(stats-test
  src/tests/stats-test-1.cpp
  src/StatsTask.cpp
  src/EventLoop.cpp
  kc1fsz-tools-cpp/src/Common.cpp
  kc1fsz-tools-cpp/src/StdPollTimer.cpp
  kc1fsz-tools-cpp/src/linux/StdClock.cpp
)

target_include_directories(stats-test PRIVATE src)
target_include_directories(stats-test PRIVATE kc1fsz-tools-cpp/include)
target_include_directories(stats-test PRIVATE amp-server/sw/include)
target_link_libraries(stats-test -lcurl)

# ----- manager-test ----------------------------------------------------

add_executable(manager-test
  src/tests/manager-test-1.cpp
  src/ManagerTask.cpp
  kc1fsz-tools-cpp/src/Common.cpp
  kc1fsz-tools-cpp/src/linux/StdClock.cpp
)

target_include_directories(manager-test PRIVATE src)
target_include_directories(manager-test PRIVATE kc1fsz-tools-cpp/include)
target_include_directories(manager-test PRIVATE amp-server/sw/include)

# ----- ini-test ----------------------------------------------------

#add_executable(ini-test
#  src/tests/ini-test-1.cpp
#  inih/ini.c
#)

#set_source_files_properties(inih/ini.c
#    PROPERTIES COMPILE_OPTIONS "-DINI_STOP_ON_FIRST_ERROR=1"
#)

#target_include_directories(ini-test PRIVATE src)
#target_include_directories(ini-test PRIVATE inih)

# ----- seq-test ----------------------------------------------------

add_executable(seq-test
  src/Message.cpp
  src/tests/seq-test-2.cpp
  src/tests/TestUtil.cpp
  kc1fsz-tools-cpp/src/Common.cpp
)

target_include_directories(seq-test PRIVATE src)
target_include_directories(seq-test PRIVATE include)
target_include_directories(seq-test PRIVATE kc1fsz-tools-cpp/include)
target_include_directories(seq-test PRIVATE amp-server/sw/include)

# ----- retx-test ----------------------------------------------------

add_executable(retx-test
  src/tests/retx-test.cpp
  src/tests/TestUtil.cpp
  src/RetransmissionBufferStd.cpp
  src/IAX2FrameFull.cpp
  kc1fsz-tools-cpp/src/Common.cpp
)

add_compile_options(-fstack-protector-all -Wall -Wpedantic -g)
target_include_directories(retx-test PRIVATE src)
target_include_directories(retx-test PRIVATE include)
target_include_directories(retx-test PRIVATE kc1fsz-tools-cpp/include)
target_include_directories(retx-test PRIVATE amp-server/sw/include)

# ----- hello-ed25519 ----------------------------------------------------

add_executable(hello-ed
  src/demos/hello-ed25519.cpp
  kc1fsz-tools-cpp/src/Common.cpp
  ed25519/src/add_scalar.c
  ed25519/src/ge.c
  ed25519/src/keypair.c
  ed25519/src/seed.c
  ed25519/src/sign.c
  ed25519/src/fe.c
  ed25519/src/key_exchange.c
  ed25519/src/sc.c
  ed25519/src/sha512.c
  ed25519/src/verify.c
)
target_include_directories(hello-ed PRIVATE src)
target_include_directories(hello-ed PRIVATE include)
target_include_directories(hello-ed PRIVATE kc1fsz-tools-cpp/include)
target_include_directories(hello-ed PRIVATE ed25519/src)

# ----- hello-usb ----------------------------------------------------

add_executable(hello-usb
  src/tests/hello-usb.c
)
target_include_directories(hello-usb PRIVATE src)
target_include_directories(hello-usb PRIVATE include)
target_include_directories(hello-usb PRIVATE kc1fsz-tools-cpp/include)
target_link_libraries(hello-usb -lusb-1.0)

# ----- bridge-test ----------------------------------------------------

add_executable(bridge-test
  src/tests/bridge-test-1.cpp
  src/tests/TestUtil.cpp
  src/Message.cpp
  src/Bridge.cpp
  src/BridgeCall.cpp
  src/BridgeIn.cpp
  src/BridgeOut.cpp
  src/Transcoder_G711_ULAW.cpp
  src/Transcoder_SLIN_48K.cpp
  src/Transcoder_SLIN.cpp
  src/Transcoder_SLIN_16K.cpp
  src/IAX2Util.cpp
  src/Resampler.cpp
  kc1fsz-tools-cpp/src/Common.cpp
  kc1fsz-tools-cpp/src/linux/StdClock.cpp
  itu-g711-codec/src/codec.cpp
  itu-g711-codec/src/Plc.cpp
  cmsis-dsp-mock/src/main.cpp
)

target_include_directories(bridge-test PRIVATE src)
target_include_directories(bridge-test PRIVATE include)
target_include_directories(bridge-test PRIVATE kc1fsz-tools-cpp/include)
target_include_directories(bridge-test PRIVATE amp-server/sw/include)
target_include_directories(bridge-test PRIVATE cmsis-dsp-mock/include)
target_include_directories(bridge-test PRIVATE itu-g711-codec/src)
# This program has a large stack size because of the jitter buffer
#target_link_options(bridge-test PRIVATE -Wl,--stack,4096000)

# ----- config-test ----------------------------------------------------

add_executable(config-test
  src/tests/config-test.cpp
  kc1fsz-tools-cpp/src/Common.cpp
)

target_include_directories(config-test PRIVATE src)
target_include_directories(config-test PRIVATE include)
target_include_directories(config-test PRIVATE kc1fsz-tools-cpp/include)
target_include_directories(config-test PRIVATE json/include)

# ----- signal-1 --------------------------------------------------------

add_executable(signal-1
  src/demos/signal-1.cpp
  src/SignalIn.cpp
  src/EventLoop.cpp
  src/Message.cpp
  kc1fsz-tools-cpp/src/Common.cpp
  kc1fsz-tools-cpp/src/linux/StdClock.cpp
  kc1fsz-tools-cpp/src/StdPollTimer.cpp
)

target_include_directories(signal-1 PRIVATE src)
target_include_directories(signal-1 PRIVATE include)
target_include_directories(signal-1 PRIVATE kc1fsz-tools-cpp/include)
target_include_directories(signal-1 PRIVATE json/include)

# ----- nr-log -----------------------------------------------------------

add_executable(nr-log
  src/demos/nr-log.cpp
  src/NRLog.cpp
  src/ThreadUtil.cpp
  kc1fsz-tools-cpp/src/Common.cpp
)

target_include_directories(nr-log PRIVATE src)
target_include_directories(nr-log PRIVATE include)
target_include_directories(nr-log PRIVATE kc1fsz-tools-cpp/include)
target_include_directories(nr-log PRIVATE json/include)
target_link_libraries(nr-log -lcurl)

