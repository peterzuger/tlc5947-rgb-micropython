add_library(usermod_tlc5947 INTERFACE)

target_sources(usermod_tlc5947 INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/tlc5947/tlc5947.c
  ${CMAKE_CURRENT_LIST_DIR}/tlc5947/color.c
)

target_include_directories(usermod_tlc5947 INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}/tlc5947
)

target_link_libraries(usermod INTERFACE usermod_tlc5947)
