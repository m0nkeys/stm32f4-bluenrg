/******************** (C) COPYRIGHT 2012 STMicroelectronics ********************
* File Name          : gatt_server.h
* Author             : AMS - HEA&RF BU
* Version            : V1.0.0
* Date               : 19-July-2012
* Description        : Header file for BlueNRG's GATT server layer.
********************************************************************************
* THE PRESENT FIRMWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
* WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE TIME.
* AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY DIRECT,
* INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING FROM THE
* CONTENT OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE CODING
* INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
*******************************************************************************/

#ifndef __GATT_SERVER_H__
#define __GATT_SERVER_H__

#include "compiler.h"
#include "ble_status.h"

/**
 * UUID table
 */
#define PRIMARY_SERVICE_UUID                       (0x2800)
#define SECONDARY_SERVICE_UUID                     (0x2801)
#define INCLUDE_SERVICE_UUID                       (0x2802)
#define CHARACTERISTIC_UUID                        (0x2803)
#define CHAR_EXTENDED_PROP_DESC_UUID               (0x2900)
#define CHAR_USER_DESC_UUID                        (0x2901)
#define CHAR_CLIENT_CONFIG_DESC_UUID               (0x2902)
#define CHAR_SERVER_CONFIG_DESC_UUID               (0x2903)
#define CHAR_FORMAT_DESC_UUID                      (0x2904)
#define CHAR_AGGR_FMT_DESC_UUID                    (0x2905)
#define GATT_SERVICE_UUID                          (0x1801)
#define GAP_SERVICE_UUID                           (0x1800)
#define SERVICE_CHANGED_UUID                       (0x2A05)

/******************************************************************************
 * Types
 *****************************************************************************/

/** 
 * Access permissions 
 * for an attribute
 */
typedef tHalUint8 tAttrAccessFlags;
#define ATTR_NO_ACCESS                             (0x00)
#define ATTR_ACCESS_READ_ONLY                      (0x01) 
#define ATTR_ACCESS_WRITE_REQ_ONLY                 (0x02)
#define ATTR_ACCESS_READ_WRITE                     (0x03)
#define ATTR_ACCESS_WRITE_WITHOUT_RESPONSE         (0x04)
#define ATTR_ACCESS_SIGNED_WRITE_ALLOWED           (0x08)

/**
 * Allows all write procedures
 */
#define ATTR_ACCESS_WRITE_ANY                      (0x0E)

/**
 * Characteristic properties.
 */
#define CHAR_PROP_BROADCAST 					(0x01)
#define CHAR_PROP_READ							(0x02)
#define CHAR_PROP_WRITE_WITHOUT_RESP			(0x04)
#define CHAR_PROP_WRITE			                (0x08)
#define CHAR_PROP_NOTIFY			            (0x10)
#define CHAR_PROP_INDICATE			            (0x20)
#define CHAR_PROP_SIGNED_WRITE                  (0x40)
#define CHAR_PROP_EXT           	            (0x80)

/** 
 * Security permissions
 * for an attribute
 */
typedef tHalUint8 tAttrSecurityFlags;
#define ATTR_PERMISSION_NONE                       (0x00)
#define ATTR_PERMISSION_AUTHEN_READ                (0x01)
#define ATTR_PERMISSION_AUTHOR_READ                (0x02)
#define ATTR_PERMISSION_ENCRY_READ                 (0x04)
#define ATTR_PERMISSION_AUTHEN_WRITE               (0x08)
#define ATTR_PERMISSION_AUTHOR_WRITE               (0x10)
#define ATTR_PERMISSION_ENCRY_WRITE                (0x20)

/** 
 * Type of UUID 
 * (16 bit or 128 bit)
 */
typedef tHalUint8 tUuidType;
#define UUID_TYPE_16                               (0x01)
#define UUID_TYPE_128                              (0x02)

/**
 * Type of service
 * (primary or secondary)
 */
typedef tHalUint8 tServiceType;
#define PRIMARY_SERVICE                            (0x01)
#define SECONDARY_SERVICE                          (0x02)

/** 
 * Type of event generated by 
 * Gatt server
 */
typedef tHalUint8 tGattServerEvent;
#define GATT_SERVER_ATTR_WRITE                     (0x01)
#define GATT_INTIMATE_AND_WAIT_FOR_APPL_AUTH       (0x02)
#define GATT_INTIMATE_APPL_WHEN_READ_N_WAIT        (0x04)
#define GATT_SERVER_ATTR_READ_WRITE                GATT_SERVER_ATTR_WRITE|GATT_INTIMATE_APPL_WHEN_READ_N_WAIT


/**
 * Min encryption key size
 */
#define MIN_ENCRY_KEY_SIZE                (7)

/**
 * Max encryption key size
 */
#define MAX_ENCRY_KEY_SIZE                (0x10)


typedef __packed struct _charactFormat {
    tHalUint8 format;
    tHalInt8 exp;
    tHalUint16 unit;
    tHalUint8 name_space;
    tHalUint16 desc;
} PACKED charactFormat;

#define FORMAT_UINT8         0x04
#define FORMAT_UINT16        0x06
#define FORMAT_SINT16        0x0E
#define FORMAT_SINT24        0x0F


#define UNIT_UNITLESS        0x2700
#define UNIT_TEMP_CELSIUS    0x272F
#define UNIT_PRESSURE_BAR    0x2780


/*
 * Default MTU size
 */
#define ATT_DEFAULT_MTU       (23)


#endif /* __GATT_SERVER_H__ */
