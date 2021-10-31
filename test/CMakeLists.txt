add_executable(client client.cc)
install(TARGETS client)

add_executable(server server.cc)
install(TARGETS server)

add_executable(pollnet_client pollnet_client.cc)
install(TARGETS pollnet_client)