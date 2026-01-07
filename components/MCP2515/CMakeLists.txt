idf_component_register(SRCS "MCP2515.c" "spi-helper.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_event)
