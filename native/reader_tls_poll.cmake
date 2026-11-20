# The pinned IDF async connect reuses fd_sets modified by select() on timeout.
# Short polling must rebuild those sets on every call. Stage only this public
# Espressif source in build/; the installed IDF and reference trees stay intact.
idf_component_get_property(reader_tls_lib esp-tls COMPONENT_LIB)
set(reader_tls_source "$ENV{IDF_PATH}/components/esp-tls/esp_tls.c")
file(READ "${reader_tls_source}" reader_tls_text)
set(reader_tls_needle "            /* In case of non-blocking I/O, we use the select() API")
string(FIND "${reader_tls_text}" "${reader_tls_needle}" reader_tls_match)
if(reader_tls_match LESS 0)
 message(FATAL_ERROR "IDF async TLS source changed; review the short-poll correction")
endif()
string(REPLACE "${reader_tls_needle}"
 "            FD_ZERO(&tls->rset);\n            FD_SET(tls->sockfd, &tls->rset);\n            tls->wset = tls->rset;\n${reader_tls_needle}"
 reader_tls_patched "${reader_tls_text}")
set(reader_tls_staged "${CMAKE_BINARY_DIR}/reader-tls/esp_tls.c")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/reader-tls")
file(WRITE "${reader_tls_staged}" "${reader_tls_patched}")
get_target_property(reader_tls_sources ${reader_tls_lib} SOURCES)
set(reader_tls_found FALSE)
foreach(reader_tls_entry IN LISTS reader_tls_sources)
 string(REPLACE "\\" "/" reader_tls_normal "${reader_tls_entry}")
 if(reader_tls_normal MATCHES "(^|/)esp_tls\\.c$")
  list(REMOVE_ITEM reader_tls_sources "${reader_tls_entry}")
  set(reader_tls_found TRUE)
 endif()
endforeach()
if(NOT reader_tls_found)
 message(FATAL_ERROR "Cannot identify the IDF TLS source for short polling")
endif()
set_property(TARGET ${reader_tls_lib} PROPERTY SOURCES "${reader_tls_sources};${reader_tls_staged}")
target_include_directories(${reader_tls_lib} PRIVATE "$ENV{IDF_PATH}/components/esp-tls")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${reader_tls_source}")

# The async SSL transport returns failure without publishing the TLS error,
# unlike its synchronous path. Preserve the SDK error before the client
# closes the socket so allocation and certificate failures remain identifiable.
idf_component_get_property(reader_transport_lib tcp_transport COMPONENT_LIB)
set(reader_transport_source "$ENV{IDF_PATH}/components/tcp_transport/transport_ssl.c")
file(READ "${reader_transport_source}" reader_transport_text)
set(reader_transport_needle [=[        int progress = esp_tls_conn_new_async(host, strlen(host), port, &ssl->cfg, ssl->tls);
        if (progress >= 0) {]=])
string(FIND "${reader_transport_text}" "${reader_transport_needle}" reader_transport_match)
if(reader_transport_match LESS 0)
 message(FATAL_ERROR "IDF async SSL transport source changed; review TLS error propagation")
endif()
string(REPLACE "${reader_transport_needle}" [=[        int progress = esp_tls_conn_new_async(host, strlen(host), port, &ssl->cfg, ssl->tls);
        if (progress < 0) {
            esp_tls_error_handle_t error_handle;
            if (esp_tls_get_error_handle(ssl->tls, &error_handle) == ESP_OK) {
                esp_transport_set_errors(t, error_handle);
            }
        } else {]=] reader_transport_patched "${reader_transport_text}")
set(reader_transport_staged "${CMAKE_BINARY_DIR}/reader-tls/transport_ssl.c")
file(WRITE "${reader_transport_staged}" "${reader_transport_patched}")
get_target_property(reader_transport_sources ${reader_transport_lib} SOURCES)
list(LENGTH reader_transport_sources reader_transport_source_count)
list(FILTER reader_transport_sources EXCLUDE REGEX "(^|[/\\\\])transport_ssl\\.c$")
list(LENGTH reader_transport_sources reader_transport_other_count)
math(EXPR reader_transport_replaced "${reader_transport_source_count}-${reader_transport_other_count}")
if(NOT reader_transport_replaced EQUAL 1)
 message(FATAL_ERROR "Cannot uniquely identify the IDF SSL transport source")
endif()
set_property(TARGET ${reader_transport_lib} PROPERTY SOURCES "${reader_transport_sources};${reader_transport_staged}")
target_include_directories(${reader_transport_lib} PRIVATE "$ENV{IDF_PATH}/components/tcp_transport")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${reader_transport_source}")
