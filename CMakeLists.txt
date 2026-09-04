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
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libfreetype.a
        # poppler is built with DCT support, so it pulls in jpeg_std_error &
        # co for JPEG images embedded in PDFs. Neither solver provides them
        # (gpac is configured --use-jpeg=no), so the filter has to carry its
        # own copy, exactly like libfreetype.a just above.
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libjpeg.a
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
