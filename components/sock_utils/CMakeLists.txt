idf_component_register(SRCS "src/getnameinfo.c"
                            "src/ifaddrs.c"
                            "src/gai_strerror.c"
                            "src/socketpair.c"
                            "src/gethostname.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES lwip esp_netif)

# Keep the same lwIP integration as espressif/sock_utils 0.2.2.  These
# definitions make the declarations supplied by this component visible from
# lwip's public headers and prevent duplicate implementations.
idf_component_get_property(lwip lwip COMPONENT_LIB)
target_compile_definitions(${lwip} PUBLIC LWIP_SOCKET_HAS_SOCKETPAIR=1)
target_compile_definitions(${lwip} PUBLIC LWIP_NETDB_HAS_GAI_STRERROR=1)
target_link_libraries(${lwip} PUBLIC ${COMPONENT_LIB})
