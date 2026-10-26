# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Qt6 REQUIRED COMPONENTS Test Network)

add_executable(test-app test_app.cpp)
target_link_libraries(test-app PRIVATE stdc++ xplasma-app xplasma::archive Qt6::Test Qt6::Network)
add_test(NAME test-app COMMAND test-app)

# The Start menu's QML, drawn offscreen from generated look data.
find_package(Qt6 COMPONENTS Quick)
if(Qt6Quick_FOUND)
  add_executable(test-startmenu test_startmenu.cpp)
  target_link_libraries(test-startmenu PRIVATE stdc++ xplasma-app Qt6::Quick Qt6::Widgets Qt6::Test)
  add_test(NAME test-startmenu COMMAND test-startmenu)
endif()

add_executable(test-preview test_preview.cpp ${PROJECT_SOURCE_DIR}/src/app/editor/preview.cpp ${PROJECT_SOURCE_DIR}/src/app/editor/jobs.cpp)
target_link_libraries(test-preview PRIVATE stdc++ xplasma-app Qt6::Widgets Qt6::Test)
add_test(NAME test-preview COMMAND test-preview)
set_tests_properties(test-preview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_STYLE_OVERRIDE=Fusion")

# Manual GPU check (run in a graphical session; not the software CI renderer).
if(Qt6Quick_FOUND)
  add_executable(live-tint-probe ../manual/live_tint_probe.cpp)
  target_link_libraries(live-tint-probe PRIVATE xplasma-app Qt6::Quick Qt6::Widgets Qt6::Test)
endif()
add_executable(live-tint-install ../manual/live_tint_install.cpp)
target_link_libraries(live-tint-install PRIVATE xplasma-app Qt6::Widgets)
if(Qt6Quick_FOUND)
  add_executable(live-surface-probe ../manual/live_surface_probe.cpp)
  target_link_libraries(live-surface-probe PRIVATE xplasma-app Qt6::Quick Qt6::Widgets Qt6::Test)
endif()

# Isolated baked-color cache and decoration preparation timings (no desktop writes).
add_executable(theme-performance-probe ../manual/theme_performance_probe.cpp)
target_link_libraries(theme-performance-probe PRIVATE xplasma-app Qt6::Widgets)

add_executable(test-cachepreferences test_cachepreferences.cpp ${PROJECT_SOURCE_DIR}/src/app/editor/cachepreferences.cpp)
target_link_libraries(test-cachepreferences PRIVATE stdc++ xplasma-app Qt6::Widgets Qt6::Test)
add_test(NAME test-cachepreferences COMMAND test-cachepreferences)
set_tests_properties(test-cachepreferences PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_STYLE_OVERRIDE=Fusion")

add_executable(check-widget-patches check_widget_patches.cpp)
target_link_libraries(check-widget-patches PRIVATE stdc++ xplasma-app Qt6::Core)
