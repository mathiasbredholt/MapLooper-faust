idf_component_register(SRCS "main.cpp" "${CMAKE_CURRENT_BINARY_DIR}/Faust/Faust.cpp"
                       INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(OUTPUT Faust/Faust.h Faust/Faust.cpp
     COMMAND faust2esp32 -es8388 -lib ${COMPONENT_DIR}/Faust.dsp && unzip ${CMAKE_CURRENT_BINARY_DIR}/Faust.zip
     DEPENDS ${COMPONENT_DIR}/Faust.dsp
     COMMENT "Compiling faust..."
     VERBATIM)
