#ifndef _LINUX_IF_USERLINK
#define _LINUX_IF_USERLINK

struct ul_ifru_data {
    u_int8_t command;
    u_int8_t version;
    u_int16_t max_mtu;
};

struct ul_fs_header_base {
    u_int8_t version;
    u_int8_t reserv1;
    u_int16_t protocol;
    u_int32_t reserv2;
};

#define	UL_IOCGET	0
#define	UL_IOCSET	1
#define	UL_V_NONE	0
#define	UL_V_BASE	1

#endif /* _LINUX_IF_USERLINK */
