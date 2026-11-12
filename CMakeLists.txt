cmake_minimum_required(VERSION 3.16)
# Version changes are explicit; builds and tags keep 0.1 until the next release is agreed.
project(openfirepower VERSION 0.1 LANGUAGES C)
if(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
  set(OFP_TVOS ON)
  enable_language(OBJC)
  set(OFP_BUILD_SERVER OFF CACHE BOOL "Build the dedicated server" FORCE)
  set(OFP_BUILD_TESTS OFF CACHE BOOL "Build desktop tests" FORCE)
  set(OFP_WITH_CURL OFF CACHE BOOL "Use the platform HTTPS implementation" FORCE)
  add_compile_definitions(OFP_TVOS=1)
endif()
add_compile_definitions(OFP_VERSION="${PROJECT_VERSION}")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(OFP_BUILD_SDL2   "Build the game (the SDL2 frontend); OFF builds just the server and tools" ON)
option(OFP_BUILD_SERVER "Build the dedicated server ofp_server and the ofp_match tool" ON)
option(OFP_BUILD_TESTS  "Build the tests"        ON)
option(OFP_SERVER_SQLITE "Statistics and leaderboards in the server (needs SQLite 3)" ON)

# How many players one machine may split its screen between. The modern
# platforms (macOS, Linux, Windows) take four; the planned AmigaOS 3.x
# port is meant to be built with 2 and to add network play instead
# (see docs/PORTING.md).
set(OFP_MAX_LOCAL_PLAYERS 4 CACHE STRING "Most players sharing one screen (1-4)")
set_property(CACHE OFP_MAX_LOCAL_PLAYERS PROPERTY STRINGS 1 2 3 4)
# Tanks in one game: every player plus the computer opponent.
set(OFP_MAX_TANKS 10 CACHE STRING "Most tanks in one game (local + network players + computer)")

if(MSVC)
  set(OFP_WARN /W4)
else()
  set(OFP_WARN -Wall -Wextra -Wshadow -Wpedantic)
endif()

# ---- core: pure C99, no OS, no floats -------------------------------------
# -DOFP_FUZZ=ON: everything with address and undefined-behaviour checks (the fuzzers, the network tests;
# CI's sanitizers job). Set before any target, so all of them get it.
option(OFP_FUZZ "build with sanitizers for fuzz testing" OFF)
if(OFP_FUZZ)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g -O1)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(ofp_core STATIC
  src/core/fixed.c
  src/core/world.c
  src/core/worldgen.c
  src/core/game.c
  src/core/ai.c
  src/core/mapfile.c
  src/core/match.c)
target_include_directories(ofp_core PUBLIC src)
target_compile_options(ofp_core PRIVATE ${OFP_WARN})
target_compile_definitions(ofp_core PUBLIC OFP_MAX_TANKS=${OFP_MAX_TANKS})

# ---- SDL2 frontend (macOS / Linux / Windows) ------------------------------
if(OFP_BUILD_SDL2)
  find_package(SDL2 REQUIRED CONFIG)

  # vendored single-file libraries (public domain), built without our warnings
  add_library(ofp_third_party STATIC third_party/stb_image_impl.c)
  target_include_directories(ofp_third_party SYSTEM PUBLIC third_party)
  # the music: tracker modules (MOD, XM, S3M, IT) through libxmp-lite (MIT, third_party/libxmp-lite)
  file(GLOB OFP_XMP_SRC third_party/libxmp-lite/src/*.c third_party/libxmp-lite/src/loaders/*.c)
  add_library(ofp_xmp STATIC ${OFP_XMP_SRC})
  target_include_directories(ofp_xmp SYSTEM PUBLIC third_party/libxmp-lite/include/libxmp-lite PRIVATE third_party/libxmp-lite/src)
  target_compile_definitions(ofp_xmp PRIVATE LIBXMP_CORE_PLAYER PUBLIC LIBXMP_STATIC)
  if(UNIX)
    target_link_libraries(ofp_xmp PRIVATE m)
  endif()

  add_library(ofp_audio_decoders STATIC third_party/stb_vorbis.c third_party/dr_mp3_impl.c)
  add_library(ofp_music STATIC src/platform/sdl2/music.c src/platform/sdl2/music_pcm.c)
  target_include_directories(ofp_music PRIVATE src)
  target_include_directories(ofp_music SYSTEM PRIVATE third_party)
  target_compile_options(ofp_music PRIVATE ${OFP_WARN})
  target_link_libraries(ofp_music PRIVATE ofp_audio_decoders ofp_xmp SDL2::SDL2)

  add_executable(openfirepower
    src/platform/sdl2/main.c
    src/platform/sdl2/render.c
    src/platform/sdl2/input.c
    src/platform/sdl2/theme.c
    src/platform/sdl2/ui.c
    src/platform/sdl2/select.c
    src/platform/sdl2/fx.c
    src/platform/sdl2/audio.c
    src/platform/sdl2/settings.c
    src/platform/sdl2/menu.c
    src/platform/sdl2/maps.c
    src/platform/sdl2/editor.c
    src/platform/sdl2/original.c
    src/platform/sdl2/netui.c
    src/platform/sdl2/osk.c
    src/platform/sdl2/gallery.c
    src/platform/sdl2/profiles.c
    src/platform/sdl2/teams.c)
  target_compile_options(openfirepower PRIVATE ${OFP_WARN})
  # where to find data/: the source tree (running from the build), or where a package puts it
  # (-DOFP_DATA_DIR=/usr/share/openfirepower); the game also looks in bin/../share/openfirepower
  set(OFP_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data" CACHE STRING "where the game finds its data")
  target_compile_definitions(openfirepower PRIVATE OFP_DATA_DIR="${OFP_DATA_DIR}"
                             OFP_MAX_LOCAL_PLAYERS=${OFP_MAX_LOCAL_PLAYERS})
  target_link_libraries(openfirepower PRIVATE ofp_third_party)
  if(TARGET SDL2::SDL2main)
    target_link_libraries(openfirepower PRIVATE SDL2::SDL2main)
  endif()
  target_link_libraries(openfirepower PRIVATE ofp_common ofp_core ofp_music SDL2::SDL2)
  if(OFP_TVOS)
    target_sources(openfirepower PRIVATE src/platform/tvos/app.m)
    set_source_files_properties(src/platform/tvos/app.m PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_options(openfirepower PRIVATE -ObjC)
    configure_file(packaging/tvos/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)
    set_target_properties(openfirepower PROPERTIES MACOSX_BUNDLE ON
      MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
      OUTPUT_NAME OpenFirePower XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY 3
      XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER org.openfirepower.OpenFirePower
      XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic)
    add_custom_command(TARGET openfirepower POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data/styles $<TARGET_BUNDLE_DIR:openfirepower>/data/styles
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data/themes $<TARGET_BUNDLE_DIR:openfirepower>/data/themes
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data/sounds $<TARGET_BUNDLE_DIR:openfirepower>/data/sounds
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data/music $<TARGET_BUNDLE_DIR:openfirepower>/data/music
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data/maps $<TARGET_BUNDLE_DIR:openfirepower>/data/maps
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/data/avatars.png ${CMAKE_CURRENT_SOURCE_DIR}/data/gamecontrollerdb.txt $<TARGET_BUNDLE_DIR:openfirepower>/data/)
  endif()
  if(UNIX AND NOT APPLE)
    set_target_properties(openfirepower PROPERTIES ENABLE_EXPORTS ON)   # (names in a crash report's backtrace)
  endif()
  if(APPLE AND OFP_MACOS_BUNDLE)                  # OpenFirePower.app: the icon, the data inside (tools/package_macos.sh)
    set(OFP_ICNS ${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/OpenFirePower.icns)
    target_sources(openfirepower PRIVATE ${OFP_ICNS})
    set_source_files_properties(${OFP_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    configure_file(packaging/macos/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)
    set_target_properties(openfirepower PROPERTIES MACOSX_BUNDLE ON MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
                          OUTPUT_NAME openfirepower)
    add_custom_command(TARGET openfirepower POST_BUILD      # the data: Contents/Resources/data (SDL_GetBasePath finds it)
      COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_BUNDLE_CONTENT_DIR:openfirepower>/Resources/data
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data/styles $<TARGET_BUNDLE_CONTENT_DIR:openfirepower>/Resources/data/styles
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data/themes $<TARGET_BUNDLE_CONTENT_DIR:openfirepower>/Resources/data/themes
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data/sounds $<TARGET_BUNDLE_CONTENT_DIR:openfirepower>/Resources/data/sounds
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data/maps $<TARGET_BUNDLE_CONTENT_DIR:openfirepower>/Resources/data/maps
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data/music $<TARGET_BUNDLE_CONTENT_DIR:openfirepower>/Resources/data/music
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/data/gamecontrollerdb.txt $<TARGET_BUNDLE_CONTENT_DIR:openfirepower>/Resources/data/
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/data/avatars.png $<TARGET_BUNDLE_CONTENT_DIR:openfirepower>/Resources/data/)   # (the original levels' layouts: shipped)
  endif()
  if(WIN32)                                       # a window, no console; the icon and version
    enable_language(RC)
    target_sources(openfirepower PRIVATE packaging/windows/openfirepower.rc)
    set_target_properties(openfirepower PROPERTIES WIN32_EXECUTABLE ON)
  endif()
  if(UNIX AND NOT APPLE)
    target_link_libraries(openfirepower PRIVATE m)
  endif()
endif()
if(OFP_BUILD_SDL2 AND OFP_BUILD_TESTS)
  # fuzzing what the game reads from the web (the server list, HTTP answers)
  add_executable(test_stick tests/test_stick.c src/platform/sdl2/profiles.c src/platform/sdl2/input.c src/platform/sdl2/osk.c
                 src/platform/sdl2/ui.c src/platform/sdl2/theme.c src/platform/sdl2/render.c src/platform/sdl2/fx.c
                 src/platform/sdl2/settings.c src/platform/sdl2/select.c src/platform/sdl2/maps.c)
  target_compile_definitions(test_stick PRIVATE OFP_DATA_DIR=\"${OFP_DATA_DIR}\" OFP_MAX_TANKS=${OFP_MAX_TANKS})
  target_link_libraries(test_stick PRIVATE ofp_common ofp_core ofp_third_party SDL2::SDL2)
  if(UNIX)
    target_link_libraries(test_stick PRIVATE m)
  endif()
  add_test(NAME stick COMMAND test_stick)
  set_tests_properties(stick PROPERTIES PASS_REGULAR_EXPRESSION "stick and d-pad: ok" TIMEOUT 60
                       ENVIRONMENT "SDL_VIDEODRIVER=dummy;HOME=${CMAKE_BINARY_DIR}/test-home-stick")
  add_executable(test_teams tests/test_teams.c src/platform/sdl2/teams.c src/platform/sdl2/profiles.c src/platform/sdl2/input.c
                 src/platform/sdl2/osk.c src/platform/sdl2/ui.c src/platform/sdl2/theme.c src/platform/sdl2/render.c
                 src/platform/sdl2/fx.c src/platform/sdl2/settings.c src/platform/sdl2/select.c src/platform/sdl2/maps.c)
  target_compile_definitions(test_teams PRIVATE OFP_DATA_DIR=\"${OFP_DATA_DIR}\" OFP_MAX_TANKS=${OFP_MAX_TANKS})
  target_link_libraries(test_teams PRIVATE ofp_common ofp_core ofp_third_party SDL2::SDL2)
  if(UNIX)
    target_link_libraries(test_teams PRIVATE m)
  endif()
  add_test(NAME teams COMMAND test_teams)
  set_tests_properties(teams PROPERTIES PASS_REGULAR_EXPRESSION "teams: ok" TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_executable(test_direct tests/test_direct.c src/platform/sdl2/menu.c src/platform/sdl2/profiles.c src/platform/sdl2/input.c
                 src/platform/sdl2/osk.c src/platform/sdl2/ui.c src/platform/sdl2/theme.c src/platform/sdl2/render.c
                 src/platform/sdl2/fx.c src/platform/sdl2/settings.c src/platform/sdl2/select.c src/platform/sdl2/maps.c
                 src/platform/sdl2/audio.c)
  target_compile_definitions(test_direct PRIVATE OFP_DATA_DIR=\"${OFP_DATA_DIR}\" OFP_MAX_TANKS=${OFP_MAX_TANKS})
  target_link_libraries(test_direct PRIVATE ofp_common ofp_core ofp_third_party ofp_music SDL2::SDL2)
  if(UNIX)
    target_link_libraries(test_direct PRIVATE m)
  endif()
  add_test(NAME direct COMMAND test_direct)
  set_tests_properties(direct PROPERTIES PASS_REGULAR_EXPRESSION "direct: ok" TIMEOUT 60
                       ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;HOME=${CMAKE_BINARY_DIR}/test-home-direct")
  add_executable(test_stats tests/test_stats.c src/platform/sdl2/profiles.c src/platform/sdl2/input.c src/platform/sdl2/osk.c
                 src/platform/sdl2/ui.c src/platform/sdl2/theme.c src/platform/sdl2/render.c src/platform/sdl2/fx.c
                 src/platform/sdl2/settings.c src/platform/sdl2/select.c src/platform/sdl2/maps.c)
  target_compile_definitions(test_stats PRIVATE OFP_DATA_DIR=\"${OFP_DATA_DIR}\" OFP_MAX_TANKS=${OFP_MAX_TANKS})
  target_link_libraries(test_stats PRIVATE ofp_common ofp_core ofp_third_party SDL2::SDL2)
  if(UNIX)
    target_link_libraries(test_stats PRIVATE m)
  endif()
  add_test(NAME stats COMMAND test_stats)
  set_tests_properties(stats PROPERTIES PASS_REGULAR_EXPRESSION "stats: ok" TIMEOUT 60
                       ENVIRONMENT "SDL_VIDEODRIVER=dummy")
  add_executable(test_editor tests/test_editor.c src/platform/sdl2/editor.c src/platform/sdl2/profiles.c src/platform/sdl2/input.c
                 src/platform/sdl2/osk.c src/platform/sdl2/ui.c src/platform/sdl2/theme.c src/platform/sdl2/render.c
                 src/platform/sdl2/fx.c src/platform/sdl2/settings.c src/platform/sdl2/select.c src/platform/sdl2/maps.c
                 src/platform/sdl2/audio.c)
  target_compile_definitions(test_editor PRIVATE OFP_DATA_DIR=\"${OFP_DATA_DIR}\" OFP_MAX_TANKS=${OFP_MAX_TANKS})
  target_link_libraries(test_editor PRIVATE ofp_common ofp_core ofp_third_party ofp_music SDL2::SDL2)
  if(UNIX)
    target_link_libraries(test_editor PRIVATE m)
  endif()
  add_test(NAME editor COMMAND test_editor)
  set_tests_properties(editor PROPERTIES PASS_REGULAR_EXPRESSION "editor: ok" TIMEOUT 60
                       ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;HOME=${CMAKE_BINARY_DIR}/test-home-editor")
  add_executable(fuzz_ui tests/fuzz_ui.c src/platform/sdl2/netui.c src/platform/sdl2/input.c src/platform/sdl2/ui.c src/platform/sdl2/theme.c
                 src/platform/sdl2/render.c src/platform/sdl2/fx.c)
  target_compile_definitions(fuzz_ui PRIVATE OFP_DATA_DIR="${OFP_DATA_DIR}" OFP_MAX_TANKS=${OFP_MAX_TANKS})
  target_link_libraries(fuzz_ui PRIVATE ofp_common ofp_core ofp_third_party SDL2::SDL2)
  if(UNIX)
    target_link_libraries(fuzz_ui PRIVATE m)
  endif()
  add_test(NAME fuzz_ui COMMAND fuzz_ui 20000)
  # a virtual game pad through the on-screen keyboard, hosting, the lobby and chat in a match
  add_executable(test_music tests/test_music.c)
  target_include_directories(test_music PRIVATE src)
  target_link_libraries(test_music PRIVATE ofp_music SDL2::SDL2)
  if(UNIX)
    target_link_libraries(test_music PRIVATE m)
  endif()
  add_test(NAME music COMMAND test_music ${CMAKE_CURRENT_SOURCE_DIR}/tests/music)
  # the Amiga's music player (src/platform/amiga/modplay.c) against libxmp: each song the same, a tenth of a second at a time
  add_executable(test_modplay tests/test_modplay.c src/platform/amiga/modplay.c)
  target_include_directories(test_modplay PRIVATE src)
  target_link_libraries(test_modplay PRIVATE ofp_xmp)
  if(UNIX)
    target_link_libraries(test_modplay PRIVATE m)
  endif()
  add_test(NAME modplay COMMAND test_modplay)
  add_test(NAME training COMMAND openfirepower --smoke-training)
  set_tests_properties(training PROPERTIES PASS_REGULAR_EXPRESSION "training ok" TIMEOUT 90
                       ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;HOME=${CMAKE_BINARY_DIR}/test-home")
  add_test(NAME signin COMMAND ${CMAKE_COMMAND} -E env HOME=${CMAKE_BINARY_DIR}/test-home-signin   # (a fresh one: no profiles)
           "OFP_TEST_KEYS=20:.,30:Return,40:@ANNA,50:Return,60:Return,90:Space,100:Return,110:@BERT,120:Return,130:Return,170:Escape"
           SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy $<TARGET_FILE:openfirepower>)
  add_test(NAME signin_fresh COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_BINARY_DIR}/test-prefs-signin/profiles.ini ${CMAKE_BINARY_DIR}/test-prefs-signin/settings.ini)
  set_tests_properties(signin_fresh PROPERTIES FIXTURES_SETUP signin_home)
  set_tests_properties(signin PROPERTIES TIMEOUT 60 FIXTURES_REQUIRED signin_home
                       PASS_REGULAR_EXPRESSION "signed in 2: ANNA \\(KEYS 1\\) BERT \\(KEYS 2\\); local game: 2 players")
  add_test(NAME pad COMMAND openfirepower --smoke-pad)
  set_tests_properties(pad PROPERTIES PASS_REGULAR_EXPRESSION "pad ok" TIMEOUT 90
                       ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;HOME=${CMAKE_BINARY_DIR}/test-home")
  foreach(test stick teams direct stats editor fuzz_ui music training signin pad)
    set(test_prefs "${CMAKE_BINARY_DIR}/test-prefs-${test}")
    file(MAKE_DIRECTORY "${test_prefs}")
    set_property(TEST ${test} APPEND PROPERTY ENVIRONMENT "OFP_TEST_PREF_PATH=${test_prefs}/")
    set_property(TEST ${test} APPEND PROPERTY ENVIRONMENT "OFP_DATA=${CMAKE_CURRENT_SOURCE_DIR}/data")
  endforeach()
endif()

if(OFP_BUILD_TESTS)
  # the files strangers hand the game (maps, replays), from every real one, mutated (longer by hand:
  # fuzz_files 200000; with -DOFP_FUZZ=ON for the sanitizers)
  add_executable(fuzz_files tests/fuzz_files.c)
  target_include_directories(fuzz_files PRIVATE src)
  target_compile_definitions(fuzz_files PRIVATE OFP_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
                             OFP_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
  target_link_libraries(fuzz_files PRIVATE ofp_common ofp_core)
  if(UNIX)
    target_link_libraries(fuzz_files PRIVATE m)
  endif()
  add_test(NAME fuzz_files COMMAND fuzz_files 1500)
  set_tests_properties(fuzz_files PROPERTIES WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 300)
endif()

# ---- tests ------------------------------------------------------------------
# replays and networking: plain C99 plus sockets, shared by the game, the
# match server and the tools
add_library(ofp_common STATIC
    src/common/fx_budget.c
    src/common/recovery.c
    src/common/achieve.c
    src/common/rules.c
    src/common/training.c
    src/common/mapcheck.c
    src/common/operation.c
    src/common/dump.c
    src/common/crash.c
    src/common/daily.c
    src/common/botnames.c
  src/common/replay.c
  src/common/net.c
  src/common/sha256.c
  src/common/http.c
  src/common/server.c
  src/common/client.c)
target_include_directories(ofp_common PUBLIC src)
target_compile_options(ofp_common PRIVATE ${OFP_WARN})
target_link_libraries(ofp_common PUBLIC ofp_core)
if(OFP_TVOS)
  target_sources(ofp_common PRIVATE src/platform/tvos/http_apple.m)
  set_source_files_properties(src/platform/tvos/http_apple.m PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  target_link_libraries(ofp_common PUBLIC "-framework Foundation")
endif()
# https for the listing (the game's server list, servers announcing): libcurl, if it's there.
# macOS has it in the SDK; Linux: libcurl-devel / libcurl4-openssl-dev. Without it, plain http.
option(OFP_MACOS_BUNDLE "macOS: build OpenFirePower.app (tools/package_macos.sh sets it)" OFF)
option(OFP_WITH_CURL "https for the server listing, through libcurl" ON)
if(WIN32)                                         # Windows: https through WinHTTP, part of Windows
  target_link_libraries(ofp_common PUBLIC winhttp)
endif()
if(OFP_WITH_CURL)
  find_package(CURL QUIET)
  if(CURL_FOUND)
    target_link_libraries(ofp_common PUBLIC CURL::libcurl)
    target_compile_definitions(ofp_common PUBLIC OFP_HAVE_CURL)
    message(STATUS "https for the listing: libcurl ${CURL_VERSION_STRING}")
  else()
    message(STATUS "https for the listing: no libcurl found, plain http only")
  endif()
endif()
if(WIN32)
  target_link_libraries(ofp_common PUBLIC ws2_32)
endif()

# the headless match runner and the dedicated server: no SDL needed, so
#   cmake -S . -B build-server -DOFP_BUILD_SDL2=OFF && cmake --build build-server
# builds only these (Linux and macOS; SQLite 3 for statistics)
if(OFP_BUILD_SERVER)
  add_executable(ofp_match src/tools/ofp_match.c)
  if(OFP_BUILD_TESTS)
    add_executable(test_maps tests/test_maps.c)        # every bundled map: loads, plays its modes
    target_link_libraries(test_maps PRIVATE ofp_core)
    file(GLOB OFP_BUNDLED_MAPS ${CMAKE_CURRENT_SOURCE_DIR}/data/maps/*.ofpmap)
    add_test(NAME maps COMMAND test_maps ${OFP_BUNDLED_MAPS})
    set_tests_properties(maps PROPERTIES PASS_REGULAR_EXPRESSION "^.*0 maps with a problem")
  endif()
  add_executable(ofp_bot src/tools/ofp_bot.c)          # a headless player, for soak tests (tools/soak.sh)
  target_compile_options(ofp_bot PRIVATE ${OFP_WARN})
  target_link_libraries(ofp_bot PRIVATE ofp_common)
  target_compile_options(ofp_match PRIVATE ${OFP_WARN})
  target_link_libraries(ofp_match PRIVATE ofp_common)
  add_executable(ofp_server src/server/main.c src/server/config.c src/server/db.c)
  if(UNIX AND NOT APPLE)
    set_target_properties(ofp_server PROPERTIES ENABLE_EXPORTS ON)       # (names in a crash report's backtrace)
  endif()
  target_compile_options(ofp_server PRIVATE ${OFP_WARN})
  target_link_libraries(ofp_server PRIVATE ofp_common)
  if(UNIX)
    target_link_libraries(ofp_server PRIVATE m)
  endif()
  if(OFP_SERVER_SQLITE)
    find_package(SQLite3)
    if(SQLite3_FOUND)
      target_compile_definitions(ofp_server PRIVATE OFP_HAVE_SQLITE=1)
      target_link_libraries(ofp_server PRIVATE SQLite::SQLite3)
    else()
      message(WARNING "SQLite 3 not found: ofp_server is built without statistics and leaderboards "
                      "(Debian/Ubuntu: apt install libsqlite3-dev; macOS has it built in)")
    endif()
  endif()
  install(TARGETS ofp_server ofp_match RUNTIME DESTINATION bin)
endif()

if(OFP_BUILD_TESTS)
  enable_testing()
  add_executable(test_local_ai tests/test_local_ai.c)
  target_link_libraries(test_local_ai PRIVATE ofp_common)
  add_test(NAME local_ai COMMAND test_local_ai)
  add_executable(test_polish tests/test_polish.c)
  target_link_libraries(test_polish PRIVATE ofp_common)
  add_test(NAME polish COMMAND test_polish)
  add_executable(test_challenges tests/test_challenges.c)
  target_link_libraries(test_challenges PRIVATE ofp_common)
  add_test(NAME challenges COMMAND test_challenges)
  add_executable(test_match_rules tests/test_match_rules.c src/server/config.c)
  target_compile_options(test_match_rules PRIVATE ${OFP_WARN})
  target_link_libraries(test_match_rules PRIVATE ofp_common)
  add_test(NAME match_rules COMMAND test_match_rules)
  add_executable(test_playability tests/test_playability.c)
  target_compile_options(test_playability PRIVATE ${OFP_WARN})
  target_link_libraries(test_playability PRIVATE ofp_common)
  add_test(NAME playability COMMAND test_playability)
  add_executable(test_net tests/test_net.c)
  target_compile_options(test_net PRIVATE ${OFP_WARN})
  target_compile_definitions(test_net PRIVATE OFP_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")   # (the maps, for the vote)
  target_link_libraries(test_net PRIVATE ofp_common)
  add_test(NAME net COMMAND test_net)
  add_executable(test_lobby tests/test_lobby.c)
  target_compile_options(test_lobby PRIVATE ${OFP_WARN})
  target_link_libraries(test_lobby PRIVATE ofp_common)
  add_test(NAME lobby COMMAND test_lobby)
  set_tests_properties(lobby PROPERTIES TIMEOUT 25)
  # the same after weeks of uptime: the millisecond clock past the middle of its range (day
  # 24.8), and ten seconds before it wraps (day 49.7)
  add_test(NAME net_uptime_25d COMMAND test_net)
  set_tests_properties(net_uptime_25d PROPERTIES ENVIRONMENT "OFP_CLOCK_OFFSET=2150000000")
  add_test(NAME net_clock_wrap COMMAND test_net)
  set_tests_properties(net_clock_wrap PROPERTIES ENVIRONMENT "OFP_CLOCK_OFFSET=4294957296")
  foreach(test net net_uptime_25d net_clock_wrap)
    set(test_dir "${CMAKE_BINARY_DIR}/test-${test}")
    file(MAKE_DIRECTORY "${test_dir}")
    set_tests_properties(${test} PROPERTIES WORKING_DIRECTORY "${test_dir}" RESOURCE_LOCK udp_suite)
  endforeach()
  # fuzzing what strangers can send (a short run here; longer by hand: fuzz_net 2000000)
  add_executable(fuzz_net tests/fuzz_net.c)
  target_link_libraries(fuzz_net PRIVATE ofp_common)
  add_test(NAME fuzz COMMAND fuzz_net 20000)
  # replays recorded by a GCC build on Linux: every other build (clang on macOS, Windows,
  # the web site's WebAssembly) has to play them back to the same checksums
  if(OFP_BUILD_SERVER)
    file(GLOB OFP_REPLAY_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/tests/replays/*.ofprep)
    foreach(rep ${OFP_REPLAY_FIXTURES})
      get_filename_component(rn ${rep} NAME_WE)
      add_test(NAME same_game_${rn} COMMAND ofp_match verify ${rep})
      set_tests_properties(same_game_${rn} PROPERTIES PASS_REGULAR_EXPRESSION "all [0-9]+ checksums match")
    endforeach()
  endif()
  # the match runner: a short computer-vs-computer match, then its replay played back
  if(OFP_BUILD_SERVER)
  add_test(NAME match_bots COMMAND ofp_match bots --seed 3 --ticks 3000 --out ${CMAKE_BINARY_DIR}/test-bots.ofprep)
  add_test(NAME match_verify COMMAND ofp_match verify ${CMAKE_BINARY_DIR}/test-bots.ofprep)
  set_tests_properties(match_verify PROPERTIES DEPENDS match_bots)
  endif()
  add_executable(test_core tests/test_core.c)
  target_compile_options(test_core PRIVATE ${OFP_WARN})
  target_link_libraries(test_core PRIVATE ofp_core)
  add_test(NAME core COMMAND test_core)
endif()

# ---- installing (for packages: cmake --install build --prefix /usr) ----------------------
include(GNUInstallDirs)
if(OFP_BUILD_SDL2 AND TARGET openfirepower)
  install(TARGETS openfirepower RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} BUNDLE DESTINATION .)
  install(DIRECTORY data/styles data/themes data/sounds data/music DESTINATION ${CMAKE_INSTALL_DATADIR}/openfirepower
          PATTERN "original" EXCLUDE PATTERN "community" EXCLUDE)
  install(DIRECTORY data/maps DESTINATION ${CMAKE_INSTALL_DATADIR}/openfirepower)   # (the original levels' layouts too)
  install(FILES data/gamecontrollerdb.txt data/avatars.png DESTINATION ${CMAKE_INSTALL_DATADIR}/openfirepower)   # (pads; profile avatars)
  if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/org.openfirepower.OpenFirePower.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/linux/org.openfirepower.OpenFirePower.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    install(FILES packaging/linux/openfirepower.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
            RENAME org.openfirepower.OpenFirePower.png)
  endif()
endif()
install(FILES server.cfg.example README.md CHANGELOG.md LICENSE CREDITS.md THIRD-PARTY-NOTICES.md DESTINATION ${CMAKE_INSTALL_DOCDIR} OPTIONAL)
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR})
option(OFP_INSTALL_SYSTEMD "install the dedicated server's systemd unit and service user" OFF)
if(OFP_INSTALL_SYSTEMD AND TARGET ofp_server)
  install(FILES packaging/systemd/ofp-server.service DESTINATION lib/systemd/system)
  install(FILES packaging/systemd/ofp-server.sysusers DESTINATION lib/sysusers.d RENAME ofp-server.conf)
  install(FILES server.cfg.example DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/ofp-server RENAME server.cfg)   # (/etc for a /usr install)
endif()
