#ifndef _IXE_COMMAND
#define _IXE_COMMAND


enum ixe_frame_main_type{
	IXE_CMD_TYPE_START = 0,
	IXE_QUERY_CMD,
	IXE_QUERY_RESP,
	IXE_SET_CMD,
	IXE_SET_ACK,
	IXE_CMD_TYPE__END
};

enum ixe_frame_query_type{
	IXE_QUERY_START = 0,
	IXE_QUERY_STATUS,
	IXE_QUERY_AP,
	IXE_QUERY_TIME,
	IXE_QUERY_BLE_KEY,
	IXE_QUERY_FIRMWARE,
	IXE_QUERY_END
};

enum ixe_frame_set_type{
	IXE_SET_CMD_START = 0,
	IXE_SET_WIFI_OnOff,
	IXE_RESET_PARAM,
	IXE_SET_OTA,
	IXE_SET_TIME,
	IXE_SET_ZONE,
	IXE_SET_RALLBACK,
	IXE_RESET_PROGRAME = 0x11,
	IXE_SET_BLE_NAME = 0x12,
	IXE_SET_BLE_KEY = 0x13,
	IXE_SET_BLE_OTA = 0x14,
	IXE_SET_TYPE__END
};



void ixe_recv_command_handler(uint8_t *recv_data,uint32_t recv_len,uint8_t *send_buf,uint8_t *send_len);



#endif
