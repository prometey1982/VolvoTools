/*
 * This is the Drew Technologies CarDAQ+ header file.
 * The CarDAQ+ supports the full J2534-1 v0404 API, plus
 * some extensions. The extensions are marked as follows:
 *    "-2" = Feature from the J2534-2 Specification
 *    "v2" = Backwards-compatiable feature from J2534-1 v0202 API
 *    "DT" = DrewTech-specific feature, not yet standardized
 *
 * This file is free to use as long as this header stays intact.
 * Author: Mark Wine
 */

#ifndef __J2534_H
#define __J2534_H


/**************************/
/* ProtocolID definitions */
/**************************/
enum Protocol_ID
{
	J1850VPW     = 0x1,
	J1850PWM     = 0x2,
	ISO9141      = 0x3,
	ISO14230     = 0x4,
	CAN          = 0x5,
	ISO15765     = 0x6,
	SCI_A_ENGINE = 0x7,
	SCI_A_TRANS  = 0x8,
	SCI_B_ENGINE = 0x9,
	SCI_B_TRANS  = 0xA,
	CAN_XON_XOFF = 0x10000001
};

// J2534-2 Pin Switched ProtocolIDs
#define J1850VPW_PS			/*-2*/			0x8000 // Not supported
#define J1850PWM_PS			/*-2*/			0x8001 // Not supported
#define ISO9141_PS			/*-2*/			0x8002 // Not supported
#define ISO14230_PS			/*-2*/			0x8003 // Not supported
#define CAN_PS				/*-2*/			0x8004
#define ISO15765_PS			/*-2*/			0x8005
#define J2610_PS			/*-2*/			0x8006 // Not supported
#define SW_ISO15765_PS		/*-2*/			0x8007
#define SW_CAN_PS			/*-2*/			0x8008
#define GM_UART_PS			/*-2*/			0x8009 // Not supported (YET)
#define CAN_XON_XOFF_PS			/*-2*/			0x800A // Not supported (YET)
#define ANALOG_IN_1			/*-2*/			0x800B
#define ANALOG_IN_2			/*-2*/			0x800C // Not supported
#define ANALOG_IN_3			/*-2*/			0x800D // Only 1st analog 
#define ANALOG_IN_4			/*-2*/			0x800E // subsystem is supported
#define ANALOG_IN_5			/*-2*/			0x800F // on CarDAQ2534 and CarDAQ+
#define ANALOG_IN_6			/*-2*/			0x8010
#define ANALOG_IN_7			/*-2*/			0x8011
#define ANALOG_IN_8			/*-2*/			0x8012
#define ANALOG_IN_9			/*-2*/			0x8013
#define ANALOG_IN_10			/*-2*/			0x8014
#define ANALOG_IN_11			/*-2*/			0x8015
#define ANALOG_IN_12			/*-2*/			0x8016
#define ANALOG_IN_13			/*-2*/			0x8017
#define ANALOG_IN_14			/*-2*/			0x8018
#define ANALOG_IN_15			/*-2*/			0x8019
#define ANALOG_IN_16			/*-2*/			0x801A
#define ANALOG_IN_17			/*-2*/			0x801B
#define ANALOG_IN_18			/*-2*/			0x801C
#define ANALOG_IN_19			/*-2*/			0x801D
#define ANALOG_IN_20			/*-2*/			0x801E
#define ANALOG_IN_21			/*-2*/			0x801F
#define ANALOG_IN_22			/*-2*/			0x8020
#define ANALOG_IN_23			/*-2*/			0x8021
#define ANALOG_IN_24			/*-2*/			0x8022
#define ANALOG_IN_25			/*-2*/			0x8023
#define ANALOG_IN_26			/*-2*/			0x8024
#define ANALOG_IN_27			/*-2*/			0x8025
#define ANALOG_IN_28			/*-2*/			0x8026
#define ANALOG_IN_29			/*-2*/			0x8027
#define ANALOG_IN_30			/*-2*/			0x8028
#define ANALOG_IN_31			/*-2*/			0x8029
#define ANALOG_IN_32			/*-2*/			0x802A

#define LIN_PS				/*DT*/			0x10000000
#define J1708_PS			/*DT*/			0x10000001


/*************/
/* IOCTL IDs */
/*************/
#define GET_CONFIG						0x01
#define SET_CONFIG						0x02
#define READ_VBATT						0x03
#define FIVE_BAUD_INIT						0x04
#define FAST_INIT						0x05
// unused							0x06
#define CLEAR_TX_BUFFER						0x07
#define CLEAR_RX_BUFFER						0x08
#define CLEAR_PERIODIC_MSGS					0x09
#define CLEAR_MSG_FILTERS					0x0A
#define CLEAR_FUNCT_MSG_LOOKUP_TABLE				0x0B
#define ADD_TO_FUNCT_MSG_LOOKUP_TABLE				0x0C
#define DELETE_FROM_FUNCT_MSG_LOOKUP_TABLE			0x0D
#define READ_PROG_VOLTAGE					0x0E
// J2534-2 SW_CAN
#define SW_CAN_HS			/*-2*/			0x8000
#define SW_CAN_NS			/*-2*/			0x8001
// J2534-2 Device Config Parameters
#define GET_DEVICE_CONFIG	/*-2*/			0x8008
#define SET_DEVICE_CONFIG	/*-2*/			0x8009

// DT CarDAQ2534 Ioctl values to read most recent analog sample (Flushes all samples in queue)
#define READ_CH1_VOLTAGE		/*DT*/			0x10000
#define READ_CH2_VOLTAGE		/*DT*/			0x10001
#define READ_CH3_VOLTAGE		/*DT*/			0x10002
#define READ_CH4_VOLTAGE		/*DT*/			0x10003
#define READ_CH5_VOLTAGE		/*DT*/			0x10004
#define READ_CH6_VOLTAGE		/*DT*/			0x10005
// DT CarDAQ2534 Ioctl for reading block of data
#define READ_ANALOG_CH1			/*DT*/			0x10010
#define READ_ANALOG_CH2			/*DT*/			0x10011
#define READ_ANALOG_CH3			/*DT*/			0x10012
#define READ_ANALOG_CH4			/*DT*/			0x10013
#define READ_ANALOG_CH5			/*DT*/			0x10014
#define READ_ANALOG_CH6			/*DT*/			0x10015
#define READ_TIMESTAMP			/*DT*/			0x10100
#define DT_COMMIT_CHANGES		/*DT*/			0x30000
#define GET_DEVICE_BYTE_ARRAY	/*DT*/			0x30001 // input = SBYTE_ARRAY, output = define
#define SET_DEVICE_BYTE_ARRAY	/*DT*/			0x30002 // input = SBYTE_ARRAY, output = define 
#define DT_IOCTL_VVSTATS		/*DT*/			0x20000000
#define DT_READ_DIO			    /*DT*/			0x20000001 // AVIT only
#define DT_WRITE_DIO			/*DT*/			0x20000002 // AVIT only
#define DT_ANALOG_IGNORE_CAL	/*DT*/			0x20000003 // AVIT only

// Volvo DiCE
#define CAN_XON_XOFF_FILTER						0x20000001
#define CAN_XON_XOFF_FILTER_ACTIVE				0x20000002



/*******************************/
/* Configuration Parameter IDs */
/*******************************/
#define DATA_RATE						0x01
// unused							0x02
#define LOOPBACK						0x03
#define NODE_ADDRESS						0x04
#define NETWORK_LINE						0x05
#define P1_MIN							0x06 // Don't use
#define P1_MAX							0x07
#define P2_MIN							0x08 // Don't use
#define P2_MAX							0x09 // Don't use
#define P3_MIN							0x0A
#define P3_MAX							0x0B // Don't use
#define P4_MIN							0x0C
#define P4_MAX							0x0D // Don't use
// See W0 = 0x19
#define W1							0x0E
#define W2							0x0F
#define W3							0x10
#define W4							0x11
#define W5							0x12
#define TIDLE							0x13
#define TINIL							0x14
#define TWUP							0x15
#define PARITY							0x16
#define BIT_SAMPLE_POINT					0x17
#define SYNC_JUMP_WIDTH						0x18
#define W0							0x19
#define T1_MAX							0x1A
#define T2_MAX							0x1B
// See T3_MAX							0x24
#define T4_MAX							0x1C
#define T5_MAX							0x1D
#define ISO15765_BS						0x1E
#define ISO15765_STMIN						0x1F
#define DATA_BITS						0x20
#define FIVE_BAUD_MOD						0x21
#define BS_TX							0x22
#define STMIN_TX						0x23
#define T3_MAX							0x24
#define ISO15765_WFT_MAX					0x25
#define ISO15765_SIMULTANEOUS		/*DT*/			0x10000000
#define DT_ISO15765_PAD_BYTE            /*DT*/                  0x10000001
#define DT_FILTER_FREQ			/*DT*/			0x10000002 // AVIT only
#define DT_PARAM_RX_BUFFER_SIZE		/*DT*/			0x10000003
#define DT_PARAM_TX_BUFFER_SIZE		/*DT*/			0x10000004

// J2534-2
#define CAN_MIXED_FORMAT		/*-2*/			0x8000
#define J1962_PINS			/*-2*/			0x8001
#define SW_CAN_HS_DATA_RATE		/*-2*/			0x8010
#define SW_CAN_SPEEDCHANGE_ENABLE	/*-2*/			0x8011
#define SW_CAN_RES_SWITCH		/*-2*/			0x8012
#define ACTIVE_CHANNELS			/*-2*/			0x8020
#define SAMPLE_RATE			/*-2*/			0x8021
#define SAMPLES_PER_READING		/*-2*/			0x8022
#define READINGS_PER_MSG		/*-2*/			0x8023
#define AVERAGING_METHOD		/*-2*/			0x8024
#define SAMPLE_RESOLUTION		/*-2*/			0x8025
#define INPUT_RANGE_LOW			/*-2*/			0x8026
#define INPUT_RANGE_HIGH		/*-2*/			0x8027

// old DT analogs
#define ADC_READINGS_PER_SECOND		/*DT*/			0x10000
#define ADC_READINGS_PER_SAMPLE		/*DT*/			0x20000

// Device Config parameters
#define NON_VOLATILE_STORE_1	/*-2*/			0x8032		/* use SCONFIG_LIST */
#define NON_VOLATILE_STORE_2	/*-2*/			0x8033
#define NON_VOLATILE_STORE_3	/*-2*/			0x8034
#define NON_VOLATILE_STORE_4	/*-2*/			0x8035
#define NON_VOLATILE_STORE_5	/*-2*/			0x8036
#define NON_VOLATILE_STORE_6	/*-2*/			0x8037
#define NON_VOLATILE_STORE_7	/*-2*/			0x8038
#define NON_VOLATILE_STORE_8	/*-2*/			0x8039
#define NON_VOLATILE_STORE_9	/*-2*/			0x803A
#define NON_VOLATILE_STORE_10	/*-2*/			0x803B
//DT Device Config Parameters
#define DC_NETWORK_TYPE			/*DT*/			0x30000		/* use SCONFIG_LIST CS = 0, SO = 1, CO = 2, FO = 3 */
#define DC_FIXED_IP_ADDR		/*DT*/			0x30001		/* use SCONFIG_LIST */
#define DC_NW_NET_MASK		    /*DT*/			0x30002		/* use SCONFIG_LIST */
#define DC_NW_GATEWAY		    /*DT*/			0x30003		/* use SCONFIG_LIST */
#define DC_NW_SERVER_IP_ADDR		/*DT*/		0x30004		/* use SCONFIG_LIST */
#define DC_WIRELESS_NETWORK_TYPE	/*DT*/		0x30005		/* use SCONFIG_LIST CS = 0, SO = 1, CO = 2, FO = 3 */
#define DC_WIRELESS_FIXED_IP_ADDR	/*DT*/		0x30006		/* use SCONFIG_LIST */
#define DC_WIRELESS_NET_MASK	/*DT*/  		0x30007		/* use SCONFIG_LIST */
#define DC_WIRELESS_GATEWAY	    /*DT*/	    	0x30008		/* use SCONFIG_LIST */
#define DC_WIRELESS_MODE		/*DT*/			0x30009		/* use SCONFIG_LIST Ad-Hoc = 0, Managed, Master, Repeater, Secondary, auto*/
#define DC_WIRELESS_CHANNEL		/*DT*/			0x3000A		/* use SCONFIG_LIST */
#define DC_WIRELESS_RATE		/*DT*/			0x3000B		/* use SCONFIG_LIST Auto = 0, 1M, 2M, 5M, 11M */
#define DC_FILE_SERVER_ENABLE	/*DT*/			0x3000C		/* use SCONFIG_LIST */
#define DC_TELNET_SERVER_ENABLE	/*DT*/			0x3000D		/* use SCONFIG_LIST */
#define DC_AUTO_RUN_ENABLE		/*DT*/			0x3000E		/* use SCONFIG_LIST */

#define DCBA_WIRELESS_ESSID		/*DT*/			0x30100		/* define for GET_DEVICE_BYTE_ARRAY */
#define DCBA_NETWORK_NAME		/*DT*/			0x30101		/* define for GET_DEVICE_BYTE_ARRAY */


/*************/
/* Error IDs */
/*************/
enum J2534_ERROR_CODE {
	STATUS_NOERROR =						0x00,
	ERR_NOT_SUPPORTED =						0x01,
	ERR_INVALID_CHANNEL_ID =				0x02,
	ERR_INVALID_PROTOCOL_ID =				0x03,
	ERR_NULL_PARAMETER =					0x04,
	ERR_INVALID_IOCTL_VALUE = 				0x05,
	ERR_INVALID_FLAGS =						0x06,
	ERR_FAILED =							0x07,
	ERR_DEVICE_NOT_CONNECTED =				0x08,
	ERR_TIMEOUT =							0x09,
	ERR_INVALID_MSG =						0x0A,
	ERR_INVALID_TIME_INTERVAL =				0x0B,
	ERR_EXCEEDED_LIMIT =					0x0C,
	ERR_INVALID_MSG_ID =					0x0D,
	ERR_DEVICE_IN_USE =						0x0E,
	ERR_INVALID_IOCTL_ID =					0x0F,
	ERR_BUFFER_EMPTY =						0x10,
	ERR_BUFFER_FULL =						0x11,
	ERR_BUFFER_OVERFLOW =					0x12,
	ERR_PIN_INVALID =						0x13,
	ERR_CHANNEL_IN_USE =					0x14,
	ERR_MSG_PROTOCOL_ID =					0x15,
	ERR_INVALID_FILTER_ID =					0x16,
	ERR_NO_FLOW_CONTROL =					0x17,
	ERR_NOT_UNIQUE =						0x18,
	ERR_INVALID_BAUDRATE =					0x19,
	ERR_INVALID_DEVICE_ID =					0x1A,
	ERR_NULLPARAMETER =		/*v2*/			ERR_NULL_PARAMETER
};


/*****************************/
/* Miscellaneous definitions */
/*****************************/
#define SHORT_TO_GROUND						0xFFFFFFFE
#define VOLTAGE_OFF						0xFFFFFFFF

#define NO_PARITY						0
#define ODD_PARITY						1
#define EVEN_PARITY						2

//SWCAN
#define DISBLE_SPDCHANGE		/*-2*/			0
#define ENABLE_SPDCHANGE		/*-2*/			1
#define DISCONNECT_RESISTOR		/*-2*/			0
#define CONNECT_RESISTOR		/*-2*/			1
#define AUTO_RESISTOR			/*-2*/			2

//Mixed Mode
#define CAN_MIXED_FORMAT_OFF		/*-2*/			0
#define CAN_MIXED_FORMAT_ON		/*-2*/			1
#define CAN_MIXED_FORMAT_ALL_FRAMES	/*-2*/			2 // Not supported

// -2 Analog averaging
#define SIMPLE_AVERAGE		0x00000000 // Simple arithmetic mean
#define MAX_LIMIT_AVERAGE	0x00000001 // Choose the biggest value
#define MIN_LIMIT_AVERAGE	0x00000002 // Choose the lowest value
#define MEDIAN_AVERAGE		0x00000003

/*******************************/
/* PassThruConnect definitions */
/*******************************/
enum J2534CONNECTFLAG
{
	NONE = 0x0000,
	CAN_29BIT_ID = 0x0100,
	ISO9141_NO_CHECKSUM = 0x0200,
	CAN_ID_BOTH = 0x0800,
	ISO9141_K_LINE_ONLY = 0x1000,
	DT_ISO9141_LISTEN_L_LINE = 0x08000000,
	SNIFF_MODE = 0x10000000,                    //Drewtech only
	ISO9141_FORD_HEADER = 0x20000000,           //Drewtech only
	ISO9141_NO_CHECKSUM_DT = 0x40000000         //Drewtech only
};

/************************/
/* RxStatus definitions */
/************************/
#define TX_MSG_TYPE						    0x00000001
#define START_OF_MESSAGE					0x00000002
#define ISO15765_FIRST_FRAME	/*v2*/		0x00000002 //compat from v0202
#define RX_BREAK						    0x00000004
#define TX_DONE							    0x00000008
#define ISO15765_PADDING_ERROR				0x00000010
#define ISO15765_ADDR_TYPE					0x00000080
#define ISO15765_EXT_ADDR		/*DT*/		0x00000080 // Accidentally refered to in spec
//	CAN_29BIT_ID						    0x00000100  defined above
#define	SW_CAN_NS_RX			/*-2*/		0x00040000
#define	SW_CAN_HS_RX			/*-2*/		0x00020000
#define	SW_CAN_HV_RX			/*-2*/		0x00010000


/***********************/
/* TxFlags definitions */
/***********************/
#define ISO15765_FRAME_PAD			  	0x00000040
//      ISO15765_ADDR_TYPE			  	0x00000080  defined above
//	CAN_29BIT_ID						0x00000100  defined above
#define WAIT_P3_MIN_ONLY			  	0x00000200
#define SW_CAN_HV_TX			/*-2*/	0x00000400
#define SCI_MODE						0x00400000
#define SCI_TX_VOLTAGE				  	0x00800000
#define DT_PERIODIC_UPDATE		/*DT*/	0x10000000 //DT use in start Periodic only
#define DT_DO_NOT_USE_BITS		/*DT*/	0x43000000 // bits used by Drew Tech, set to zero


/**********************/
/* Filter definitions */
/**********************/
#define PASS_FILTER						0x00000001
#define BLOCK_FILTER						0x00000002
#define FLOW_CONTROL_FILTER					0x00000003
#define PASS_FILTER_WITH_TRIGGER	/*DT*/			0x10000005 //DT
#define BLOCK_FILTER_WITH_TRIGGER	/*DT*/			0x10000006 //DT


/*********************/
/* Message Structure */
/*********************/
typedef struct
{
	unsigned long ProtocolID;
	unsigned long RxStatus;
	unsigned long TxFlags;
	unsigned long Timestamp;
	unsigned long DataSize;
	unsigned long ExtraDataIndex;
	unsigned char Data[4128];
} PASSTHRU_MSG;


/********************/
/* IOCTL Structures */
/********************/
typedef struct
{
	unsigned long Parameter;
	unsigned long Value;
} SCONFIG;

typedef struct
{
	unsigned long NumOfParams;
	SCONFIG *ConfigPtr;
} SCONFIG_LIST;

typedef struct
{
	unsigned long NumOfBytes;
	unsigned char *BytePtr;
} SBYTE_ARRAY;


/************************/
/* Function Definitions */
/************************/
#ifdef _WIN32
#include <windows.h>
#ifndef BUILDING_DLL
// J2534 Windows Interface API defines
typedef long (CALLBACK* PTOPEN)(void *, unsigned long *);
typedef long (CALLBACK* PTCLOSE)(unsigned long);
typedef long (CALLBACK* PTCONNECT)(unsigned long, unsigned long, unsigned long, unsigned long, unsigned long *);
typedef long (CALLBACK* PTDISCONNECT)(unsigned long);
typedef long (CALLBACK* PTREADMSGS)(unsigned long, void *, unsigned long *, unsigned long);
typedef long (CALLBACK* PTWRITEMSGS)(unsigned long, void *, unsigned long *, unsigned long);
typedef long (CALLBACK* PTSTARTPERIODICMSG)(unsigned long, void *, unsigned long *, unsigned long);
typedef long (CALLBACK* PTSTOPPERIODICMSG)(unsigned long, unsigned long);
typedef long (CALLBACK* PTSTARTMSGFILTER)(unsigned long, unsigned long, void *, void *, void *, unsigned long *);
typedef long (CALLBACK* PTSTOPMSGFILTER)(unsigned long, unsigned long);
typedef long (CALLBACK* PTSETPROGRAMMINGVOLTAGE)(unsigned long, unsigned long, unsigned long);
typedef long (CALLBACK* PTREADVERSION)(unsigned long, char *, char *, char *);
typedef long (CALLBACK* PTGETLASTERROR)(char *);
typedef long (CALLBACK* PTIOCTL)(unsigned long, unsigned long, void *, void *);
// Drew Tech specific function calls
typedef long (CALLBACK* PTLOADFIRMWARE)(void);
typedef long (CALLBACK* PTRECOVERFIRMWARE)(void);
typedef long (CALLBACK* PTREADIPSETUP)(unsigned long DeviceID, char *host_name, char *ip_addr, char *subnet_mask,
                      char *gateway, char *dhcp_addr);
typedef long (CALLBACK* PTWRITEIPSETUP)(unsigned long DeviceID, char *host_name, char *ip_addr, char *subnet_mask,
                      char *gateway, char *dhcp_addr);
typedef long (CALLBACK* PTREADPCSETUP)(char *host_name, char *ip_addr);
typedef long (CALLBACK* PTGETPOINTER)(long vb_pointer);
typedef long (CALLBACK* PTGETNEXTCARDAQ)(char **name, unsigned long *version, char **addr);

/* PassThruGetNextCarDAQ usage:

char * pname;
unsigned long ver;
char * ipaddr;

PassThruGetNextCarDAQ(NULL,NULL,NULL); // Initialize, Do the scan

while(1) // Process the results
{
 // Get pointers to next set of results
 get_next_cardaq(&pname, &ver, &ipaddr);
 if (pname == NULL) break; // end of results
 printf("%s (%s) %02ld.%02ld.%02ld\n", pname, ipaddr, ver>>16, ver>>8 & 0xffL, ver & 0xffL);
}
//Note: The pointers are only valid right after the call.

*/

#endif // Not Building the DLL

#else
#ifdef __cplusplus
#define JTYPE extern "C" long /* Linux */
#else
#define JTYPE extern long /* Linux */
#endif

JTYPE PassThruOpen(void *pName, unsigned long *pDeviceID);
JTYPE PassThruClose(unsigned long DeviceID);
JTYPE PassThruConnect(unsigned long DeviceID, unsigned long ProtocolID, unsigned long Flags, unsigned long Baudrate, unsigned long *pChannelID);
JTYPE PassThruDisconnect(unsigned long ChannelID);
JTYPE PassThruReadMsgs(unsigned long ChannelID, PASSTHRU_MSG *pMsg, unsigned long *pNumMsgs, unsigned long Timeout);
JTYPE PassThruWriteMsgs(unsigned long ChannelID, PASSTHRU_MSG *pMsg, unsigned long *pNumMsgs, unsigned long Timeout);
JTYPE PassThruStartPeriodicMsg(unsigned long ChannelID, PASSTHRU_MSG *pMsg,
                      unsigned long *pMsgID, unsigned long TimeInterval);
JTYPE PassThruStopPeriodicMsg(unsigned long ChannelID, unsigned long MsgID);
JTYPE PassThruStartMsgFilter(unsigned long ChannelID,
                      unsigned long FilterType, PASSTHRU_MSG *pMaskMsg, PASSTHRU_MSG *pPatternMsg,
					  PASSTHRU_MSG *pFlowControlMsg, unsigned long *pMsgID);
JTYPE PassThruStopMsgFilter(unsigned long ChannelID, unsigned long MsgID);
JTYPE PassThruSetProgrammingVoltage(unsigned long DeviceID, unsigned long Pin, unsigned long Voltage);
JTYPE PassThruReadVersion(unsigned long DeviceID, char *pFirmwareVersion, char *pDllVersion, char *pApiVersion);
JTYPE PassThruGetLastError(char *pErrorDescription);
JTYPE PassThruIoctl(unsigned long ChannelID, unsigned long IoctlID,
                      void *pInput, void *pOutput);
// DrewTech Only
JTYPE PassThruLoadFirmware(void);
JTYPE PassThruRecoverFirmware(void);
JTYPE PassThruReadIPSetup(unsigned long DeviceID, char *host_name, char *ip_addr, char *subnet_mask, char *gateway, char *dhcp_addr);
JTYPE PassThruWriteIPSetup(unsigned long DeviceID, char *host_name, char *ip_addr, char *subnet_mask, char *gateway, char *dhcp_addr);
//JTYPE PassThruReadPCSetup(char *host_name, char *ip_addr);
//JTYPE PassThruGetPointer(long vb_pointer);
JTYPE PassThruGetNextCarDAQ(char **name, unsigned long *version, char **addr);

#endif


#endif /* __J2534_H */

