cmake_minimum_required(VERSION 3.7)
project(Filters)

include(filters.cmake)

add_definitions(-fpic)

SET(LIBPOPPLER_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/dec_pdf.cpp
)

SET(LIBPOPPLER_LIB
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libpoppler-cpp.a
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libpoppler.a
)

SET(LIBPOPPLER_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/poppler-cpp
)

add_filter(libpoppler
        "${LIBPOPPLER_SRC}"
        "${LIBPOPPLER_LIB}"
        []
        ""
        "${LIBPOPPLER_INC}"
        ""
        "1")

target_compile_features(libpoppler_1 PRIVATE cxx_std_20)
