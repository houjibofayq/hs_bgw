idf_component_register(SRCS "bgs_main.c"
							"bgs_mqtt.c"
							"ixe_blufi.c"
							"ixe_params.c"
							"ixe_blufi_command.c"
							"ixe_blufi_security.c"
							"bgs_ble.c"
							"bgs_ble_utils.c"
							"ixe_ble_ota.c"
                    INCLUDE_DIRS "."
					EMBED_TXTFILES ca_cert.pem
					               mqtt_ca_cert.pem)