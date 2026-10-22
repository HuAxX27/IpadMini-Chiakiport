set(bridge_dir ${CMAKE_CURRENT_LIST_DIR}/../..)

idf_component_register(
	SRCS
		"main.c"
		"bridge_config.c"
		"bridge_console.c"
		"bridge_net.c"
		"bridge_platform.c"
		"${bridge_dir}/protocol/ck_pad_codec.c"
		"${bridge_dir}/core/ck_bridge_map.c"
		"${bridge_dir}/core/ck_bridge_pace.c"
	INCLUDE_DIRS "." "${bridge_dir}/protocol" "${bridge_dir}/core"
	REQUIRES bluepad32 btstack console esp_event esp_netif esp_timer esp_wifi lwip nvs_flash)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
