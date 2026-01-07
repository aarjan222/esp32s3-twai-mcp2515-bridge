idf_component_register(SRCS "main.cpp" "system_monitor.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES CAN OS MCP2515)