idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_matter button)

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-deprecated-declarations")


