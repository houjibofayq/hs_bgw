#ifndef _ISE_CMD_
#define _ISE_CMD_


enum ise_cmd_main{
	ISE_CMD_RECV = 5,
	ISE_CMD_RESP = 6,
	ISE_CMD_TYPE__END
};

enum ise_cmd_type{
	ISE_CMD_START = 0,
	ISE_QUERY_PAMS,
	ISE_SET_PAMS,
	ISE_QUERY_END
};

esp_err_t ise_read_params(void);

void ixe_product_comd_handler(uint8_t *recv_data,uint32_t recv_len,uint8_t *send_buf,uint8_t *send_len);


#endif
