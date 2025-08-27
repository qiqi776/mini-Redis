# CMake generated Testfile for 
# Source directory: /app
# Build directory: /app/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[BufferTest]=] "/app/build/test_buffer")
set_tests_properties([=[BufferTest]=] PROPERTIES  _BACKTRACE_TRIPLES "/app/CMakeLists.txt;118;add_test;/app/CMakeLists.txt;0;")
add_test([=[AofTest]=] "/app/build/test_aof")
set_tests_properties([=[AofTest]=] PROPERTIES  _BACKTRACE_TRIPLES "/app/CMakeLists.txt;123;add_test;/app/CMakeLists.txt;0;")
add_test([=[TimerTest]=] "/app/build/test_timer")
set_tests_properties([=[TimerTest]=] PROPERTIES  _BACKTRACE_TRIPLES "/app/CMakeLists.txt;128;add_test;/app/CMakeLists.txt;0;")
add_test([=[TimerComprehensiveTest]=] "/app/build/test_timer_comprehensive")
set_tests_properties([=[TimerComprehensiveTest]=] PROPERTIES  _BACKTRACE_TRIPLES "/app/CMakeLists.txt;133;add_test;/app/CMakeLists.txt;0;")
add_test([=[AofSyncTest]=] "/app/build/test_aof_sync")
set_tests_properties([=[AofSyncTest]=] PROPERTIES  _BACKTRACE_TRIPLES "/app/CMakeLists.txt;138;add_test;/app/CMakeLists.txt;0;")
add_test([=[AofPerformanceTest]=] "/app/build/test_aof_performance")
set_tests_properties([=[AofPerformanceTest]=] PROPERTIES  _BACKTRACE_TRIPLES "/app/CMakeLists.txt;143;add_test;/app/CMakeLists.txt;0;")
add_test([=[KeyExpirationTest]=] "/app/build/test_key_expiration")
set_tests_properties([=[KeyExpirationTest]=] PROPERTIES  _BACKTRACE_TRIPLES "/app/CMakeLists.txt;148;add_test;/app/CMakeLists.txt;0;")
add_test([=[TransactionTest]=] "/app/build/test_transaction")
set_tests_properties([=[TransactionTest]=] PROPERTIES  _BACKTRACE_TRIPLES "/app/CMakeLists.txt;153;add_test;/app/CMakeLists.txt;0;")
add_test([=[IntegrationTest]=] "/app/build/test_integration")
set_tests_properties([=[IntegrationTest]=] PROPERTIES  _BACKTRACE_TRIPLES "/app/CMakeLists.txt;158;add_test;/app/CMakeLists.txt;0;")
