set(COMPONENT_SRCS src/RTTTL.cpp)

set(COMPONENT_ADD_INCLUDEDIRS src)

set(COMPONENT_REQUIRES driver esp_timer freertos)

register_component()

