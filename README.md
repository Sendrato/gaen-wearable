#  Contact Tracing / GAEN Wearable

This repo contains an open source solution for the Google-Apple Exposure
Notification (GAEN) solution, used in many countries. It provides an
implementation for the basic mechanism of TEK and RPI generation and
maintenance. A fully functional solution requires an additional Bluetooth
Central device running the BLE API (specified below) to download the TEK and
RPI values and to compute the exposure risk.  

## Setup

1. Follow step 1 to 3.1 of of the [Zephyr Getting Starting Guide](https://docs.zephyrproject.org/latest/getting_started/index.html) (installing dependencies and west)
2. Get the GAEN-wearable source code
```
mkdir ~/gaen-wearable
cd ~/gaen-wearable
west init -m https://github.com/Sendrato/gaen-wearable.git --mr main
west update
```
3. Build & flash Zephyr test program (replace XX with target board)
```
west build -b XX zephyr/samples/hello_world/
west flash
```
4. Build & flash GAEN-wearable (replace XX with target board)
```
west build -b XX gaen-wearable/gaen-wearable
west flash
```

## User feedback

| Feedback                  | Explanation              |
| ------------------------- | ------------------------ |
| 1x red blink + 1x haptic  | The internal clock of the wearable is not yet synchronised. |
| 1x blue blink + 2x haptic | The wearable is out of memory. Please sync data. |
| continuous green blink + 5x haptic | EN Config-mode is active |
| 1x yellow blink | The wearable is active  |
| 2x yellow blink | The wearable has detected other RPI keys |

To put the wearable in the EN-Config mode (or return to EN-mode) the user needs
to perform a long-press on the available button.

## Time / CTS

The GAEN stack has a huge dependency on the definition of time. As such it is
required for the wearable to synchronise its internal clock on a regular basis
with the clock of a central device.

To ease this synchronisation the wearable is equipped with BLE CTS.

Setting (and reading) the proper time can be done by writing (or reading) data
from CTS (current-time-service) with UUID 0x2A2B. This service represents time
since the 1-1-1900 (not epoch!). Its byte structure is defined as Little Endian
with the following layout: `YYYYMMDDHHmmSS`
- YYYY = years after 1900. So, 2020 = 120 years = `0x0078` = `7800`.
- MM = month, 0..11
- DD = day-of-the-month, 1..31
- HH = hours, 0..23
- mm = minutes, 0..59
- SS = seconds, 0..59

For example: May, 5th 2021, 00:00:00, is translated into the following
hexadecimal byte stream : `79 00 04 05 00 00 00`.
This should result in an UTC of `1620172800`.

## EN-Config : Authentication

The EN-Config application (enc) allows an user to adjust settings and to offload
data from the wearable via a BLE-API. To put the wearable in this mode the user
needs to long-press the button on the wearable. The wearable will flash green
when EN-Config is active. The BLE Central device is now able to connect to the
wearable.

When connecting, the BLE Central device needs to authenticate the wearable.
If the firmware of the wearable has been configured with a fixed password,
the requested password is: `123456`.

## EN-Config : Services

| Type            | function              | UUID                                   |
| --------------- | --------------------- | -------------------------------------- |
| Primary Service | -                     | `b3c04e98-82b5-4587-84b6-6179a66a079f` |
| Characteristic  | cmd (write)           | `b3c04e99-82b5-4587-84b6-6179a66a079f` |
| Characteristic  | cmd-response (notify) | `b3c04e9a-82b5-4587-84b6-6179a66a079f` |
| Characteristic  | RPI (read)            | `b3c04e9b-82b5-4587-84b6-6179a66a079f` |
| Characteristic  | TEK (read)            | `b3c04e9c-82b5-4587-84b6-6179a66a079f` |

All services require authentication

## EN-Config : BLE API

| Command            | Code | Payload    | Description                     |
| ------------------ | ---- | ---------- | ------------------------------- |
| `PING`             | 0x00 | no payload | test communication              |
| `CLEAR_DB_ALL`     | 0x01 | no payload | clear local and external memory |
| `CLEAR_DB_RPI`     | 0x02 | no payload | clear local RPI buffer          |
| `CLEAR_DB_TEK`     | 0x03 | no payload | clear local TEK buffer          |
| `SET_RPI_IDX`      | 0x04 | 2 bytes, unsigned | set index to start reading `RPI (read)` |
| `GET_RPI_IDX`      | 0x05 | no payload on request, "SET_RPI_IDX" on response |  |
| `SET_TEK_IDX`      | 0x06 | 2 bytes, unsigned | set index to start reading `TEK (read)` |
| `GET_TEK_IDX`      | 0x07 | no payload on request, "SET_TEK_IDX" on response |  |
| `SET_ADV_PERIOD`   | 0x10 | 4 bytes, unsigned | advertising period in milliseconds |
| `GET_ADV_PERIOD`   | 0x11 | no payload on request, "SET_ADV_PERIOD" on response | |
| `SET_SCAN_PERIOD`  | 0x12 | 4 bytes, unsigned | scan period in milliseconds |
| `GET_SCAN_PERIOD`  | 0x13 | no payload on request, "SET_SCAN_PERIOD" on response | |
| `SET_ADV_IVAL_MIN` | 0x14 | 2 bytes, unsigned | minimal advertisement interval in 0.625 milliseconds |
| `GET_ADV_IVAL_MIN` | 0x15 | no payload on request, "SET_ADV_IVAL_MIN" on response | |
| `SET_ADV_IVAL_MAX` | 0x16 | 2 bytes, unsigned | maximum advertisement interval in 0.625 milliseconds |
| `GET_ADV_IVAL_MAX` | 0x17 | no payload in request, "SET_ADV_IVAL_MAX" on response | |
| `SET_TEK_IVAL`     | 0x20 | 4 bytes, unsigned | GAEN TEK rolling interval |
| `GET_TEK_IVAL`     | 0x21 | no payload in request, "SET_TEK_IVAL" on response | |
| `SET_TEK_PERIOD`   | 0x22 | 4 bytes, unsigned | GAEN TEK rolling period |
| `GET_TEK_PERIOD`   | 0x23 | no payload in request, "SET_TEK_PERIOD" on response | |
| `SET_DEVICENAME`   | 0x30 | 10 bytes, unsigned | 10-character custom (BT) device name |
| `GET_DEVICENAME`   | 0x31 | no payload in request, "SET_DEVICENAME" on response | |

Bytes are in Little Endian.

Response bitmask for CMD:
- 0x80 : CMD_OK
- 0x40 : CMD_INVALID

Byte format of a CMD:
- Byte[0]   = CMD
- Byte[1..] = VALUE / Payload

To send commands:
1. Authenticate
2. Subscribe to `cmd-response` characteristic
3. Write to `cmd` characteristic

When a CMD is written to `cmd`, a response is sent over `cmd-response`,
This response contains CMD, the value sent/stored and a status flag masked over
CMD (`cmd | flag`) indicating the succes of the CMD.
- a "set" operation responds with either:
  - the value stored in memory and a `CMD_OK`, if the sent value is ok.
  - the value sent with the `CMD` and a `CMD_INVALID`, if the the sent value
    can't be processed or is invalid.
- a "get" operation responds with either:
  - the value stored in memory and a `CMD_OK`, if the requested value could
    be retrieved.
  - no value and `CMD_INVALID`, if the requested value could not be retrieved.

## EN-Config : TEK and RPI readout

A read of a BLE characteristic is limited to 512 bytes. As it is likely that a
wearable needs to transfer more bytes, the total dataset is split into chunks
of which each chunk is retrieved upon performing a `read` of the characteristic.

A chunk contains a subset of items (RPI's or TEK's with metadata) which are
identified by the index at which they are stored in the wearable. The lower the
index, the older the item. At index 0, you can find the oldest item.

Setting the (start) index of the read is done by sending command `SET_xx_IDX`.
The default index is 0. Upon each read of the characteristic, the internal
pointers are updated automatically so you do not have to control the set/read
index manually throughout the readout. When all items have been read, the
internal pointers are reset to the default index of 0.

Each chunk contains a 6-byte header, followed by the data.
- Byte[0..1] : 2 bytes, the wearable-index of the first item in this chunk.
- Byte[2..3] : 2 bytes, the number of items in this chunk.
- Byte[4..5] : 2 bytes, the remaining number of items to be read.

An example:

1. Lets assume there are 50 items in the wearable-database.
2. We have a fresh connection and did not set `SET_xx_IDX`.
3. We do a readout.

The response may look like: `[0x00 0x00 0x12 0x00 0x30 0x00 ... <data> ... ]`
- `0x00 0x00` = `0x0000` = Index of first item in this chunk is 0.
- `0x12 0x00` = `0x0012` = There are 18 items is this chunk.
- `0x20 0x00` = `0x0020` = 32 items remaining.

Since there are items remaining, we do a second readout.
Note that we do not adjust `SET_xx_IDX`.

The second readout : `[0x12 0x00 0x12 0x00 0x0E 0x00 ... <data> ...]`
- `0x12 0x00` = `0x0012` = Index of first item in this chunk is 18.
- `0x12 0x00` = `0x0012` = There are 18 items is this chunk.
- `0x0E 0x00` = `0x000E` = 14 items remaining.

The index in the second readout is automatically increased since we already
read the first 18 items in the initial readout. With 14 items remaining, a
final readout is required.

Performing a third readout: `[0x24 0x00 0x0E 0x00 0x00 0x00 ... <data> ...]`
- `0x24 0x00` = `0x0024` = Index of first item in this chunk is 36.
- `0x0E 0x00` = `0x000E` = There are 14 items is this chunk.
- `0x00 0x00` = `0x0000` = No remaining items. All has been read.

At this point all data is downloaded, but we can still request data. A fourth
readout in this example would provide the same output as the initial call.

### Data format RPI

An single RPI structure / item consists of 26 bytes.

```
u8  rpi[RPI_SIZE]; // 16 bytes RPI
u8  aem[AEM_SIZE]; // 4 bytes AEM
u32 ival_last;     // final interval-number at which the RPI was observed
i8  rssi;          // average RSSI value over all observations
u8  cnt;           // number of observations
```

### Data format TEK

An single TEK structure / item consists of 20 bytes.

```
u8  tek[TEK_SIZE]; // 16 bytes TEK
u32 ival;          // Starting interval of TEK
```
