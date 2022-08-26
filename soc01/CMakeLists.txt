set(socket_srcs  "bgs_socket_cmd.c"
			    "bgs_socket_util.c")

idf_component_register(SRCS "${socket_srcs}"
                       INCLUDE_DIRS "."
					   ${project_dir}/main
					   PRIV_REQUIRES nvs_flash
                       REQUIRES driver)
  