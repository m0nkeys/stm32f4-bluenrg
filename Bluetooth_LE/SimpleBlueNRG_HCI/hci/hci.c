/******************** (C) COPYRIGHT 2013 STMicroelectronics ********************
* File Name          : bluenrg_hci.h
* Author             : AMS - HEA&RF BU
* Version            : V1.0.0
* Date               : 4-Oct-2013
* Description        : Function for managing HCI interface. Implementation of
*                      standard HCI commands.
********************************************************************************
* THE PRESENT FIRMWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
* WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE TIME.
* AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY DIRECT,
* INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING FROM THE
* CONTENT OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE CODING
* INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
*******************************************************************************/

/**
  ******************************************************************************
  * @file    hci.c
  * @author  AMS/HESA Application Team
  * @brief   Function for managing HCI interface.
  ******************************************************************************
  * @copy
  *
  * THE PRESENT FIRMWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
  * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
  * TIME. AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY
  * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
  * FROM THE CONTENT OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
  * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
  *
  * <h2><center>&copy; COPYRIGHT 2013 STMicroelectronics</center></h2>
  */

#include <string.h>

#include "stm32f401_discovery.h"
#include "hal_types.h"
#include "ble_status.h"
#include "hal.h"
#include "hci_internal.h"
#include "gp_timer.h"

#if BLE_CONFIG_DBG_ENABLE
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

#define HCI_LOG_ON 0
#define HCI_READ_PACKET_NUM_MAX 		 (5)

#define MIN(a,b)            ((a) < (b) )? (a) : (b)
#define MAX(a,b)            ((a) > (b) )? (a) : (b)

#define Disable_SPI_IRQ()	HAL_NVIC_DisableIRQ(BLUENRG_IRQ_EXTI_IRQn);
#define Enable_SPI_IRQ()	HAL_NVIC_EnableIRQ(BLUENRG_IRQ_EXTI_IRQn);
#define Clear_SPI_EXTI_Flag()	__HAL_GPIO_EXTI_CLEAR_FLAG(BLUENRG_IRQ_EXTI_IRQn);

static void enqueue_packet(tHciDataPacket * hciReadPacket);

tListNode hciReadPktPool;
tListNode hciReadPktRxQueue;
/* pool of hci read packets */
static tHciDataPacket     hciReadPacketBuffer[HCI_READ_PACKET_NUM_MAX];

static uint8_t *hci_buffer = NULL;
static volatile uint16_t hci_pckt_len;

volatile uint8_t readPacketListFull=FALSE;

void HCI_Init(void)
{
    uint8_t index;

    /* Initialize list heads of ready and free hci data packet queues */
    list_init_head (&hciReadPktPool);
    list_init_head (&hciReadPktRxQueue);

    /* Initialize the queue of free hci data packets */
    for (index = 0; index < HCI_READ_PACKET_NUM_MAX; index++)
    {
        list_insert_tail(&hciReadPktPool, (tListNode *)&hciReadPacketBuffer[index]);
    }
}

static volatile hci_packet_complete_callback packet_complete_callback = NULL;

static void hci_set_packet_complete_callback(hci_packet_complete_callback cb)
{
	packet_complete_callback = cb;
}

void HCI_Input(tHciDataPacket * hciReadPacket)
{
    uint8_t byte;
    hci_acl_hdr *acl_hdr;

	static hci_state state = WAITING_TYPE;

	tHalUint16 collected_payload_len = 0;
	tHalUint16 payload_len;

    hci_buffer = hciReadPacket->dataBuff;

    if(state == WAITING_TYPE)
        hci_pckt_len = 0;

    while(hci_pckt_len < HCI_PACKET_SIZE){

        byte = hci_buffer[hci_pckt_len++];

        if(state == WAITING_TYPE){
            /* Only ACL Data and Events packets are accepted. */
            if(byte == HCI_EVENT_PKT){
                 state = WAITING_EVENT_CODE;
            }
//            else if(byte == HCI_ACLDATA_PKT){
//                state = WAITING_HANDLE;
//            }
            else{
                /* Incorrect type. Reset state machine. */
                state = WAITING_TYPE;
                break;
            }
        }
        else if(state == WAITING_EVENT_CODE)
            state = WAITING_PARAM_LEN;
        else if(state == WAITING_HANDLE)
            state = WAITING_HANDLE_FLAG;
        else if(state == WAITING_HANDLE_FLAG)
            state = WAITING_DATA_LEN1;
        else if(state == WAITING_DATA_LEN1)
            state = WAITING_DATA_LEN2;

        else if(state == WAITING_DATA_LEN2){
            acl_hdr = (void *)&hci_buffer[HCI_HDR_SIZE];
            payload_len = acl_hdr->dlen;
            collected_payload_len = 0;
            state = WAITING_PAYLOAD;
        }
        else if(state == WAITING_PARAM_LEN){
             payload_len = byte;
             collected_payload_len = 0;
             state = WAITING_PAYLOAD;
        }
        else if(state == WAITING_PAYLOAD){
            collected_payload_len += 1;
            if(collected_payload_len >= payload_len){
                /* Reset state machine. */
                state = WAITING_TYPE;
                enqueue_packet(hciReadPacket);

                if(packet_complete_callback){
                  uint16_t len = hci_pckt_len;
                  packet_complete_callback(hci_buffer, len);
                }
                break;
            }
        }
    }
}

void enqueue_packet(tHciDataPacket * hciReadPacket)
{
    hci_uart_pckt *hci_pckt = (void*)hciReadPacket->dataBuff;
    hci_event_pckt *event_pckt = (void*)hci_pckt->data;

    // Do not enqueue Command Complete or Command Status events

    if((hci_pckt->type != HCI_EVENT_PKT) ||
       event_pckt->evt == EVT_CMD_COMPLETE ||
           event_pckt->evt == EVT_CMD_STATUS){
        // Insert the packet back into the pool.
        list_insert_tail(&hciReadPktPool, (tListNode *)hciReadPacket);
    }
    else {
        // Insert the packet into the queue of events to be processed.
        list_insert_tail(&hciReadPktRxQueue, (tListNode *)hciReadPacket);
    }
}

void HCI_Process(void)
{
    uint8_t data_len;
    uint8_t buffer[HCI_PACKET_SIZE];
    tHciDataPacket * hciReadPacket = NULL;

    Disable_SPI_IRQ();
    tHalBool list_empty = list_is_empty(&hciReadPktRxQueue);
    /* process any pending events read */
    while(list_empty == FALSE)
    {
        list_remove_head (&hciReadPktRxQueue, (tListNode **)&hciReadPacket);
        Enable_SPI_IRQ();
        HCI_Event_CB(hciReadPacket->dataBuff);
        Disable_SPI_IRQ();
        list_insert_tail(&hciReadPktPool, (tListNode *)hciReadPacket);
        list_empty = list_is_empty(&hciReadPktRxQueue);
    }
    if (readPacketListFull) {
      while(BlueNRG_DataPresent()) {
        data_len = BlueNRG_SPI_Read_All(buffer, HCI_PACKET_SIZE);
        if(data_len > 0)
          HCI_Event_CB(buffer);
      }
      readPacketListFull = FALSE;
    }

    Enable_SPI_IRQ();
}

void HCI_Isr(void)
{
  tHciDataPacket * hciReadPacket = NULL;
  uint8_t data_len;

  Clear_SPI_EXTI_Flag();
  while (BlueNRG_DataPresent()) {
    if (list_is_empty (&hciReadPktPool) == FALSE){

      /* enqueueing a packet for read */
      list_remove_head (&hciReadPktPool, (tListNode **)&hciReadPacket);

      data_len = BlueNRG_SPI_Read_All(hciReadPacket->dataBuff,HCI_PACKET_SIZE);
      if(data_len > 0){
	HCI_Input(hciReadPacket);
	// Packet will be inserted to te correct queue by
      }
      else {
	// Insert the packet back into the pool.
	list_insert_head(&hciReadPktPool, (tListNode *)hciReadPacket);
      }

    }
    else{
      // HCI Read Packet Pool is empty, wait for a free packet.
      readPacketListFull = TRUE;
      Clear_SPI_EXTI_Flag();
      return;
    }

    Clear_SPI_EXTI_Flag();
  }
}

void hci_write(void* data1, void* data2, uint32_t n_bytes1, uint32_t n_bytes2)
{
#if  HCI_LOG_ON
    PRINTF("HCI <- ");
    for(int i=0; i < n_bytes1; i++)
        PRINTF("%02X ", *((uint8_t*)data1 + i));
    for(int i=0; i < n_bytes2; i++)
        PRINTF("%02X ", *((uint8_t*)data2 + i));
    PRINTF("\n");
#endif

    int i = 10;
    while (--i) {
        if (BlueNRG_SPI_Write(data1, data2, n_bytes1, n_bytes2) == 0)
            break;
        HAL_Delay(10);
    }
    if (0 == i)
        ; /* winfred TODO */
}

int hci_send_cmd(uint16_t ogf, uint16_t ocf, uint8_t plen, void *param)
{
	hci_command_hdr hc;

	hc.opcode = htobs(cmd_opcode_pack(ogf, ocf));
	hc.plen= plen;

	uint8_t header[HCI_HDR_SIZE + HCI_COMMAND_HDR_SIZE];
	header[0] = HCI_COMMAND_PKT;
	memcpy(header+1, &hc, sizeof(hc));

	hci_write(header, param, sizeof(header), plen);

	return 0;
}

static tHalBool new_packet;

void new_hci_event(void *pckt, tHalUint16 len)
{
	Disable_SPI_IRQ(); /* Must be re-enabled after packet processing. */

	new_packet = TRUE;
}

/* 'to' is timeout in system clock ticks.  */
int hci_send_req(struct hci_request *r)
{
	tHalUint8 *ptr;
	tHalUint16 opcode = htobs(cmd_opcode_pack(r->ogf, r->ocf));
	hci_event_pckt *event_pckt;
	hci_uart_pckt *hci_hdr;
	int try;

	new_packet = FALSE;
	hci_set_packet_complete_callback(new_hci_event);
	if (hci_send_cmd(r->ogf, r->ocf, r->clen, r->cparam) < 0)
		goto failed;

	try = 10;
	while (try--) {
		evt_cmd_complete *cc;
		evt_cmd_status *cs;
		evt_le_meta_event *me;
		int len;
		int to = 12;

		while (to--) {
			if (new_packet)
				break;
			if (1 == to)
				goto failed;
			HAL_Delay(50);
		}

		hci_hdr = (void *)hci_buffer;
		if(hci_hdr->type != HCI_EVENT_PKT){
			new_packet = FALSE;
			Enable_SPI_IRQ();
			continue;
		}

		event_pckt = (void *) (hci_hdr->data);

		ptr = hci_buffer + (1 + HCI_EVENT_HDR_SIZE);
		len = hci_pckt_len - (1 + HCI_EVENT_HDR_SIZE);

		switch (event_pckt->evt) {

		case EVT_CMD_STATUS:
			cs = (void *) ptr;

			if (cs->opcode != opcode)
				break;

			if (r->event != EVT_CMD_STATUS) {
				if (cs->status) {
					goto failed;
				}
				break;
			}

			r->rlen = MIN(len, r->rlen);
			memcpy(r->rparam, ptr, r->rlen);
			goto done;

		case EVT_CMD_COMPLETE:
			cc = (void *) ptr;

			if (cc->opcode != opcode)
				break;

			ptr += EVT_CMD_COMPLETE_SIZE;
			len -= EVT_CMD_COMPLETE_SIZE;

			r->rlen = MIN(len, r->rlen);
			memcpy(r->rparam, ptr, r->rlen);
			goto done;

		case EVT_LE_META_EVENT:
			me = (void *) ptr;

			if (me->subevent != r->event)
				break;

			len -= 1;
			r->rlen = MIN(len, r->rlen);
			memcpy(r->rparam, me->data, r->rlen);
			goto done;

        case EVT_HARDWARE_ERROR:
            goto failed;

		default:
            break; // In the meantime there could be other events from the controller.
		}

		new_packet = FALSE;
		Enable_SPI_IRQ();

	}

failed:
	hci_set_packet_complete_callback(NULL);
	Enable_SPI_IRQ();
	return -1;

done:
	hci_set_packet_complete_callback(NULL);
	Enable_SPI_IRQ();
	return 0;
}

int hci_reset()
{
  	struct hci_request rq;
	tHalUint8 status;

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_HOST_CTL;
	rq.ocf = OCF_RESET;
	rq.rparam = &status;
	rq.rlen = 1;

	if (hci_send_req(&rq) < 0)
		return -1;

	if (status) {
		return -1;
	}

	return 0;
}

int hci_disconnect(uint16_t	handle, uint8_t reason)
{
    struct hci_request rq;
	disconnect_cp cp;
	uint8_t status;

	cp.handle = handle;
	cp.reason = reason;

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LINK_CTL;
	rq.ocf = OCF_DISCONNECT;
    rq.cparam = &cp;
	rq.clen = DISCONNECT_CP_SIZE;
    rq.event = EVT_CMD_STATUS;
	rq.rparam = &status;
	rq.rlen = 1;

	if (hci_send_req(&rq) < 0)
		return -1;

	if (status) {
		return -1;
	}

	return 0;
}

int hci_le_read_local_version(uint8_t *hci_version, uint16_t *hci_revision, uint8_t *lmp_pal_version,
			      uint16_t *manufacturer_name, uint16_t *lmp_pal_subversion)
{
	struct hci_request rq;
	read_local_version_rp resp;

	memset(&resp, 0, sizeof(resp));

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_INFO_PARAM;
	rq.ocf = OCF_READ_LOCAL_VERSION;
	rq.cparam = NULL;
	rq.clen = 0;
	rq.rparam = &resp;
	rq.rlen = READ_LOCAL_VERSION_RP_SIZE;

	if (hci_send_req(&rq) < 0)
		return -1;

	if (resp.status) {
		return -1;
	}


	*hci_version = resp.hci_version;
	*hci_revision =  btohs(resp.hci_revision);
	*lmp_pal_version = resp.lmp_pal_version;
	*manufacturer_name = btohs(resp.manufacturer_name);
	*lmp_pal_subversion = btohs(resp.lmp_pal_subversion);

	return 0;
}

int hci_le_read_buffer_size(uint16_t *pkt_len, uint8_t *max_pkt)
{
	struct hci_request rq;
	le_read_buffer_size_rp resp;

	memset(&resp, 0, sizeof(resp));

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_READ_BUFFER_SIZE;
	rq.cparam = NULL;
	rq.clen = 0;
	rq.rparam = &resp;
	rq.rlen = LE_READ_BUFFER_SIZE_RP_SIZE;

	if (hci_send_req(&rq) < 0)
		return -1;

	if (resp.status) {
		return -1;
	}

	*pkt_len = resp.pkt_len;
	*max_pkt = resp.max_pkt;

	return 0;
}

int hci_le_set_advertising_parameters(uint16_t min_interval, uint16_t max_interval, uint8_t advtype,
		uint8_t own_bdaddr_type, uint8_t direct_bdaddr_type, tBDAddr direct_bdaddr, uint8_t chan_map,
		uint8_t filter)
{
	struct hci_request rq;
	le_set_adv_parameters_cp adv_cp;
	uint8_t status;

	memset(&adv_cp, 0, sizeof(adv_cp));
	adv_cp.min_interval = min_interval;
	adv_cp.max_interval = max_interval;
	adv_cp.advtype = advtype;
	adv_cp.own_bdaddr_type = own_bdaddr_type;
	adv_cp.direct_bdaddr_type = direct_bdaddr_type;
    memcpy(adv_cp.direct_bdaddr, direct_bdaddr, sizeof(adv_cp.direct_bdaddr));
	adv_cp.chan_map = chan_map;
	adv_cp.filter = filter;

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_SET_ADV_PARAMETERS;
	rq.cparam = &adv_cp;
	rq.clen = LE_SET_ADV_PARAMETERS_CP_SIZE;
	rq.rparam = &status;
	rq.rlen = 1;

	if (hci_send_req(&rq) < 0)
		return -1;

	if (status) {
		return -1;
	}

	return 0;
}

int hci_le_set_advertising_data(uint8_t length, const uint8_t data[])
{
	struct hci_request rq;
	le_set_adv_data_cp adv_cp;
	uint8_t status;

	memset(&adv_cp, 0, sizeof(adv_cp));
	adv_cp.length = length;
	memcpy(adv_cp.data, data, MIN(31,length));

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_SET_ADV_DATA;
	rq.cparam = &adv_cp;
	rq.clen = LE_SET_ADV_DATA_CP_SIZE;
	rq.rparam = &status;
	rq.rlen = 1;

	if (hci_send_req(&rq) < 0)
		return -1;

	if (status) {
		return -1;
	}

	return 0;
}

int hci_le_set_advertise_enable(tHalUint8 enable)
{
	struct hci_request rq;
	le_set_advertise_enable_cp adv_cp;
	uint8_t status;

	memset(&adv_cp, 0, sizeof(adv_cp));
	adv_cp.enable = enable?1:0;

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_SET_ADVERTISE_ENABLE;
	rq.cparam = &adv_cp;
	rq.clen = LE_SET_ADVERTISE_ENABLE_CP_SIZE;
	rq.rparam = &status;
	rq.rlen = 1;

	if (hci_send_req(&rq) < 0)
		return -1;

	if (status) {
		return -1;
	}

	return 0;
}

int hci_le_rand(uint8_t random_number[8])
{
	struct hci_request rq;
	le_rand_rp resp;

	memset(&resp, 0, sizeof(resp));

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_RAND;
	rq.cparam = NULL;
	rq.clen = 0;
	rq.rparam = &resp;
	rq.rlen = LE_RAND_RP_SIZE;

	if (hci_send_req(&rq) < 0)
		return -1;

	if (resp.status) {
		return -1;
	}

    memcpy(random_number, resp.random, 8);

	return 0;
}

int hci_le_set_scan_resp_data(uint8_t length, const uint8_t data[])
{
	struct hci_request rq;
	le_set_scan_response_data_cp scan_resp_cp;
	uint8_t status;

	memset(&scan_resp_cp, 0, sizeof(scan_resp_cp));
	scan_resp_cp.length = length;
	memcpy(scan_resp_cp.data, data, MIN(31,length));

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_SET_SCAN_RESPONSE_DATA;
	rq.cparam = &scan_resp_cp;
	rq.clen = LE_SET_SCAN_RESPONSE_DATA_CP_SIZE;
	rq.rparam = &status;
	rq.rlen = 1;

	if (hci_send_req(&rq) < 0)
		return -1;

	if (status) {
		return -1;
	}

	return 0;
}

int hci_le_read_advertising_channel_tx_power(int8_t *tx_power_level)
{
	struct hci_request rq;
	le_read_adv_channel_tx_power_rp resp;

	memset(&resp, 0, sizeof(resp));

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_READ_ADV_CHANNEL_TX_POWER;
	rq.cparam = NULL;
	rq.clen = 0;
	rq.rparam = &resp;
	rq.rlen = LE_RAND_RP_SIZE;

	if (hci_send_req(&rq) < 0)
		return -1;

	if (resp.status) {
		return -1;
	}

	*tx_power_level = resp.level;

	return 0;
}

int hci_le_set_random_address(tBDAddr bdaddr)
{
	struct hci_request rq;
	le_set_random_address_cp set_rand_addr_cp;
	uint8_t status;

	memset(&set_rand_addr_cp, 0, sizeof(set_rand_addr_cp));
	memcpy(set_rand_addr_cp.bdaddr, bdaddr, sizeof(tBDAddr));

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_SET_RANDOM_ADDRESS;
	rq.cparam = &set_rand_addr_cp;
	rq.clen = LE_SET_RANDOM_ADDRESS_CP_SIZE;
	rq.rparam = &status;
	rq.rlen = 1;

	if (hci_send_req(&rq) < 0)
		return -1;

	if (status) {
		return -1;
	}

	return 0;
}

int hci_read_bd_addr(tBDAddr bdaddr)
{
	struct hci_request rq;
	read_bd_addr_rp resp;

	memset(&resp, 0, sizeof(resp));

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_INFO_PARAM;
	rq.ocf = OCF_READ_BD_ADDR;
	rq.cparam = NULL;
	rq.clen = 0;
	rq.rparam = &resp;
	rq.rlen = READ_BD_ADDR_RP_SIZE;

	if (hci_send_req(&rq) < 0)
		return -1;

	if (resp.status) {
		return -1;
	}
	memcpy(bdaddr, resp.bdaddr, sizeof(tBDAddr));

	return 0;
}

int hci_le_create_connection(uint16_t interval,	uint16_t window, uint8_t initiator_filter, uint8_t peer_bdaddr_type,
                             const tBDAddr peer_bdaddr,	uint8_t	own_bdaddr_type, uint16_t min_interval,	uint16_t max_interval,
                             uint16_t latency,	uint16_t supervision_timeout, uint16_t min_ce_length, uint16_t max_ce_length)
{
	struct hci_request rq;
	le_create_connection_cp create_cp;
	uint8_t status;

	memset(&create_cp, 0, sizeof(create_cp));
	create_cp.interval = interval;
	create_cp.window =  window;
	create_cp.initiator_filter = initiator_filter;
	create_cp.peer_bdaddr_type = peer_bdaddr_type;
	memcpy(create_cp.peer_bdaddr, peer_bdaddr, sizeof(tBDAddr));
	create_cp.own_bdaddr_type = own_bdaddr_type;
	create_cp.min_interval=min_interval;
	create_cp.max_interval=max_interval;
	create_cp.latency = latency;
	create_cp.supervision_timeout=supervision_timeout;
	create_cp.min_ce_length=min_ce_length;
	create_cp.max_ce_length=max_ce_length;

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_CREATE_CONN;
	rq.cparam = &create_cp;
	rq.clen = LE_CREATE_CONN_CP_SIZE;
    rq.event = EVT_CMD_STATUS;
	rq.rparam = &status;
	rq.rlen = 1;

	if (hci_send_req(&rq) < 0)
		return -1;

	if (status) {
		return -1;
	}

	return 0;
}

int hci_le_encrypt(uint8_t key[16], uint8_t plaintextData[16], uint8_t encryptedData[16])
{
	struct hci_request rq;
	le_encrypt_cp params;
	le_encrypt_rp resp;

	memset(&resp, 0, sizeof(resp));

	memcpy(params.key, key, 16);
	memcpy(params.plaintext, plaintextData, 16);

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_ENCRYPT;
	rq.cparam = &params;
	rq.clen = LE_ENCRYPT_CP_SIZE;
	rq.rparam = &resp;
	rq.rlen = LE_ENCRYPT_RP_SIZE;

	if (hci_send_req(&rq) < 0){
		return -1;
	}

	if (resp.status) {
		return -1;
	}

	memcpy(encryptedData, resp.encdata, 16);

	return 0;
}

int hci_le_ltk_request_reply(uint8_t key[16])
{
	struct hci_request rq;
	le_ltk_reply_cp params;
	le_ltk_reply_rp resp;

	memset(&resp, 0, sizeof(resp));

	params.handle = 1;
	memcpy(params.key, key, 16);

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_LTK_REPLY;
	rq.cparam = &params;
	rq.clen = LE_LTK_REPLY_CP_SIZE;
	rq.rparam = &resp;
	rq.rlen = LE_LTK_REPLY_RP_SIZE;

	if (hci_send_req(&rq) < 0)
		return -1;

	if (resp.status) {
		return -1;
	}

	return 0;
}

int hci_le_ltk_request_neg_reply()
{
	struct hci_request rq;
	le_ltk_neg_reply_cp params;
	le_ltk_neg_reply_rp resp;

	memset(&resp, 0, sizeof(resp));

	params.handle = 1;

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_LTK_NEG_REPLY;
	rq.cparam = &params;
	rq.clen = LE_LTK_NEG_REPLY_CP_SIZE;
	rq.rparam = &resp;
	rq.rlen = LE_LTK_NEG_REPLY_RP_SIZE;

	if (hci_send_req(&rq) < 0)
		return -1;

	if (resp.status) {
		return -1;
	}

	return 0;
}

int hci_le_read_white_list_size(uint8_t *size)
{
	struct hci_request rq;
    le_read_white_list_size_rp resp;

	memset(&resp, 0, sizeof(resp));

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_READ_WHITE_LIST_SIZE;
	rq.rparam = &resp;
	rq.rlen = LE_READ_WHITE_LIST_SIZE_RP_SIZE;

	if (hci_send_req(&rq) < 0){
		return -1;
	}

	if (resp.status) {
		return -1;
	}

    *size = resp.size;

	return 0;
}

int hci_le_clear_white_list()
{
	struct hci_request rq;
	uint8_t status;

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_CLEAR_WHITE_LIST;
	rq.rparam = &status;
	rq.rlen = 1;

	if (hci_send_req(&rq) < 0){
		return -1;
	}

	if (status) {
		return -1;
	}

	return 0;
}

int hci_le_add_device_to_white_list(uint8_t	bdaddr_type, tBDAddr bdaddr)
{
	struct hci_request rq;
	le_add_device_to_white_list_cp params;
	uint8_t status;

	params.bdaddr_type = bdaddr_type;
	memcpy(params.bdaddr, bdaddr, 6);

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_ADD_DEVICE_TO_WHITE_LIST;
	rq.cparam = &params;
	rq.clen = LE_ADD_DEVICE_TO_WHITE_LIST_CP_SIZE;
	rq.rparam = &status;
	rq.rlen = 1;

	if (hci_send_req(&rq) < 0){
		return -1;
	}

	if (status) {
		return -1;
	}

	return 0;
}

int hci_le_remove_device_from_white_list(uint8_t bdaddr_type, tBDAddr bdaddr)
{
	struct hci_request rq;
	le_remove_device_from_white_list_cp params;
	uint8_t status;

	params.bdaddr_type = bdaddr_type;
	memcpy(params.bdaddr, bdaddr, 6);

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_REMOVE_DEVICE_FROM_WHITE_LIST;
	rq.cparam = &params;
	rq.clen = LE_REMOVE_DEVICE_FROM_WHITE_LIST_CP_SIZE;
	rq.rparam = &status;
	rq.rlen = 1;

	if (hci_send_req(&rq) < 0){
		return -1;
	}

	if (status) {
		return -1;
	}

	return 0;
}

int hci_read_transmit_power_level(uint16_t *conn_handle, uint8_t type, int8_t * tx_level)
{
    struct hci_request rq;
	read_transmit_power_level_cp params;
	read_transmit_power_level_rp resp;

	memset(&resp, 0, sizeof(resp));

	params.handle = *conn_handle;
	params.type = type;

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_HOST_CTL;
	rq.ocf = OCF_READ_TRANSMIT_POWER_LEVEL;
	rq.cparam = &params;
	rq.clen = READ_TRANSMIT_POWER_LEVEL_CP_SIZE;
	rq.rparam = &resp;
	rq.rlen = READ_TRANSMIT_POWER_LEVEL_RP_SIZE;

	if (hci_send_req(&rq) < 0){
		return -1;
	}

	if (resp.status) {
		return -1;
	}

    *conn_handle = resp.handle;
    *tx_level = resp.handle;

	return 0;
}

int hci_read_rssi(uint16_t *conn_handle, int8_t * rssi)
{
    struct hci_request rq;
	read_rssi_cp params;
	read_rssi_rp resp;

	memset(&resp, 0, sizeof(resp));

	params.handle = *conn_handle;

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_STATUS_PARAM;
	rq.ocf = OCF_READ_RSSI;
	rq.cparam = &params;
	rq.clen = READ_RSSI_CP_SIZE;
	rq.rparam = &resp;
	rq.rlen = READ_RSSI_RP_SIZE;

	if (hci_send_req(&rq) < 0){
		return -1;
	}

	if (resp.status) {
		return -1;
	}

    *conn_handle = resp.handle;
    *rssi = resp.rssi;

	return 0;
}

int hci_le_read_local_supported_features(uint8_t *features)
{
	struct hci_request rq;
    le_read_local_supported_features_rp resp;

	memset(&resp, 0, sizeof(resp));

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_READ_LOCAL_SUPPORTED_FEATURES;
	rq.rparam = &resp;
	rq.rlen = LE_READ_LOCAL_SUPPORTED_FEATURES_RP_SIZE;

	if (hci_send_req(&rq) < 0){
		return -1;
	}

	if (resp.status) {
		return -1;
	}

    memcpy(features, resp.features, sizeof(resp.features));

	return 0;
}

int hci_le_read_channel_map(uint16_t conn_handle, uint8_t ch_map[5])
{
    struct hci_request rq;
	le_read_channel_map_cp params;
	le_read_channel_map_rp resp;

	memset(&resp, 0, sizeof(resp));

	params.handle = conn_handle;

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_READ_CHANNEL_MAP;
	rq.cparam = &params;
	rq.clen = LE_READ_CHANNEL_MAP_CP_SIZE;
	rq.rparam = &resp;
	rq.rlen = LE_READ_CHANNEL_MAP_RP_SIZE;

	if (hci_send_req(&rq) < 0){
		return -1;
	}

	if (resp.status) {
		return -1;
	}

    memcpy(ch_map, resp.map, 5);

	return 0;
}

int hci_le_read_supported_states(uint8_t states[8])
{
	struct hci_request rq;
    le_read_supported_states_rp resp;

	memset(&resp, 0, sizeof(resp));

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_READ_SUPPORTED_STATES;
	rq.rparam = &resp;
	rq.rlen = LE_READ_SUPPORTED_STATES_RP_SIZE;

	if (hci_send_req(&rq) < 0){
		return -1;
	}

	if (resp.status) {
		return -1;
	}

    memcpy(states, resp.states, 8);

	return 0;
}

int hci_le_receiver_test(uint8_t frequency)
{
	struct hci_request rq;
	le_receiver_test_cp params;
	uint8_t status;

	params.frequency = frequency;

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_RECEIVER_TEST;
	rq.cparam = &params;
	rq.clen = LE_RECEIVER_TEST_CP_SIZE;
	rq.rparam = &status;
	rq.rlen = 1;

	if (hci_send_req(&rq) < 0){
		return -1;
	}

	if (status) {
		return -1;
	}

	return 0;
}

int hci_le_transmitter_test(uint8_t frequency, uint8_t length, uint8_t payload)
{
	struct hci_request rq;
	le_transmitter_test_cp params;
	uint8_t status;

	params.frequency = frequency;
    params.length = length;
    params.payload = payload;

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_TRANSMITTER_TEST;
	rq.cparam = &params;
	rq.clen = LE_TRANSMITTER_TEST_CP_SIZE;
	rq.rparam = &status;
	rq.rlen = 1;

	if (hci_send_req(&rq) < 0){
		return -1;
	}

	if (status) {
		return -1;
	}

	return 0;
}

int hci_le_test_end(uint16_t *num_pkts)
{
	struct hci_request rq;
    le_test_end_rp resp;

	memset(&resp, 0, sizeof(resp));

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_TEST_END;
	rq.rparam = &resp;
	rq.rlen = LE_TEST_END_RP_SIZE;

	if (hci_send_req(&rq) < 0){
		return -1;
	}

	if (resp.status) {
		return -1;
	}

    *num_pkts = resp.num_pkts;

	return 0;
}
