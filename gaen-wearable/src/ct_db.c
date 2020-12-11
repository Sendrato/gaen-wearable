/*
 * This file is part of the Contact Tracing / GAEN Wearable distribution
 *        https://github.com/Sendrato/gaen-wearable.
 *
 * Copyright (c) 2020 Vincent van der Locht (https://www.synchronicit.nl/)
 *                    Hessel van der Molen  (https://sendrato.com/)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/agpl-3.0.txt>.
 */

/**
 * @file
 * @brief Data storage of TEKs and RPIs.
 */

#include <kernel.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <drivers/flash.h>

#include <device.h>

#include <logging/log.h>

#include <settings/settings.h>

#include "ct.h"
#include "ct_db.h"


LOG_MODULE_REGISTER(ct_db, LOG_LEVEL_INF);

// Numer of TEK keys to be stored locally
// >> when external flash is loaded, this buffer is filled with the
//     "CT_DB_TEK_CNT_LOCAL" last / most recent TEK's from the flash.
#define CT_DB_TEK_CNT_LOCAL 14

// Number of RPI's to be stored locally
// >> when external flash is available, RPI's are pushed from local>>external
// >> when external flash is loaded, NO data is loaded from flash to this buffer.
#define CT_DB_RPI_CNT_LOCAL 512

// NOTE: ival are first member in TEK and RPI structure to ease lookup in flash.
// >>       DO NOT CHANGE ORDER OF IVAL IN STRUCT
// >>    lookup mechanism depends on this property!

// size = 4+16 = 20 bytes
typedef struct {
    uint32_t ival;
    uint8_t tek[TEK_SIZE];
} db_tek_t;

// size = 4+4+16+4+1+padding+1+padding = 32 bytes
typedef struct {
    uint32_t ival_first; // initial ival at which RPI is observed
    uint32_t ival_last;  // last ival at which RPI was observerd
    uint8_t rpi[RPI_SIZE]; // 16 bytes
    uint8_t aem[AEM_SIZE]; // 4 bytes
    int8_t rssi;
    uint8_t cnt;
} db_rpi_t;

//Use external flash?
#define CT_FLASH_NODE DT_INST(0, jedec_spi_nor)
#if DT_NODE_HAS_STATUS(CT_FLASH_NODE, okay)
#define DB_USE_EXTERNAL_FLASH
#endif

// Layout of single sector of external flash
// ==> a sector consists of 4094 bytes.
// -    4 bytes ival    (total:    4 bytes) - 1x starting interval of all RPI's
//                                                  and TEK in sector
// -   20 bytes tek     (total:   24 bytes) - 1x active TEK at this interval
// - 4064 bytes rpi     (total: 4075 bytes) - 127x observered RPI's
// -    8 bytes padding (total: 4096 bytes)

// A new sector is allocated/started iff:
// - TEK updates. All local/received RPI data is first flushed,
//                  followed by new allocation of sector
// - RPI count exceeds sector sector size. New sector is started with
//                  current ival and current active TEK

// RPI's are pushed from local to flash, when:
// - TEK is updated => flushing of all RPI's
// - RPI.start_interval + CT_DB_IVAL_DIFF_OLD < interval.now()
#define CT_DB_IVAL_DIFF_OLD 2

// Representation of "empty" bytes
#define CT_DB_EMPTY (0xFF)

// Local list of TEKs
static db_tek_t _db_tek_list[CT_DB_TEK_CNT_LOCAL];
static uint16_t _db_tek_idx;
static uint16_t _db_tek_cnt;

// Local database of RPI's
static db_rpi_t _db_rpi_list[CT_DB_RPI_CNT_LOCAL];
static uint16_t _db_rpi_idx;
static uint16_t _db_rpi_cnt;

// Current active interval on which DB works.
static uint32_t _db_ival = 0;

// Circular-buffer index calculations.
// > assumes that: "skip" < "array-size"
// > i = current index
// > s = indices to be skipped/moved
// > a = maximum array size
#define IDX_SKIP_NEXT(i,s,a)   (((i)+(s)) % (a))
#define IDX_SKIP_PREV(i,s,a)   ((((i)+(a)) - (s)) % (a))

#define IDX_NEXT(i,a)   IDX_SKIP_NEXT(i,1,a)
#define IDX_PREV(i,a)   IDX_SKIP_PREV(i,1,a)



#ifdef DB_USE_EXTERNAL_FLASH

#define CT_FLASH_NODE DT_INST(0, jedec_spi_nor)
#if DT_NODE_HAS_STATUS(CT_FLASH_NODE, okay)
#define CT_FLASH_SPI_BUS       DT_BUS_LABEL(CT_FLASH_NODE)
#define CT_FLASH_LABEL         DT_LABEL(CT_FLASH_NODE)
#define CT_FLASH_DEVICE        DT_LABEL(CT_FLASH_NODE)
#else
#warning Unsupported flash driver
#define CT_FLASH_SPI_BUS       ""
#define CT_FLASH_LABEL         ""
#define CT_FLASH_DEVICE        ""
#endif

#define CT_FLASH_SECTOR_SIZE   (4096)
#define CT_FLASH_SECTOR_COUNT  (256)
//#define CT_FLASH_SECTOR_COUNT  (16)
#define CT_FLASH_MEMORY_SIZE   (CT_FLASH_SECTOR_SIZE*CT_FLASH_SECTOR_COUNT)

// Read full RPI when loading data from flash.
// 0: only ival is read and checked (fast)
// 1: full RPI is read and printed (slow)
#define CT_FLASH_LOAD_RPI_FULL 0



const static struct device *_db_flash_dev  = NULL;
static const uint32_t _db_ival_empty = -1; //0xFFFFFFFF
static const uint16_t _db_cnt_empty  = -1; //0xFFFF

typedef struct {
    uint32_t ival;
    uint16_t cnt;
} db_flash_toc_page_t;

// Structure / Table of Contents, representing data in flash.
static db_flash_toc_page_t _db_flash_toc[CT_FLASH_SECTOR_COUNT];
// Number of RPI's in flash
static uint32_t _db_flash_rpi_cnt       = 0;

// Index of sector in which we push data
static uint32_t _db_flash_sector_idx    = 0;
// addr-offset in sector in which we write data
static uint32_t _db_flash_sector_offset = 0;


int ct_db_flash_init(void)
{
    LOG_INF(CT_FLASH_LABEL " SPI flash");
    LOG_INF("==========================");
    LOG_INF("Bus: " CT_FLASH_SPI_BUS );
    LOG_INF("Dev: " CT_FLASH_DEVICE );

    _db_flash_dev = device_get_binding(CT_FLASH_DEVICE);

    if (!_db_flash_dev) {
        LOG_ERR("Flash driver %s was not found!\n", CT_FLASH_DEVICE);
        return -ENODEV;
    }

    return 0;
}

int ct_db_flash_clear(void) {
    flash_write_protection_set(_db_flash_dev, false);
    int err = flash_erase(_db_flash_dev, 0x0, CT_FLASH_MEMORY_SIZE);
    if (err != 0) {
        LOG_ERR("Flash erase failed! %d\n", err);
        return err;
    }
    return 0;
}

int ct_db_flash_load(void) {
    int err;

    uint32_t ival;
    uint32_t addr;
    db_flash_toc_page_t *toc_page;
    uint32_t sector;
    uint32_t sector_addr;

    // Setting up TOC and local buffers is done in several steps:
    // 1.0) Find sector in flash containing newest data
    //      => sector to which we write is the sector following this one.
    // 2.1) Setup TOC (counting number of RPI's in each sector)
    // 2.2) Copy last "TEK_ROLLING_PERIOD" number of TEKS from flash to buffer.

    // Scan flash find which sector contains the "newest" data / highest ival.
    uint32_t target_sector = 0;
    uint32_t target_ival   = 0;
    for (sector = 0; sector<CT_FLASH_SECTOR_COUNT; sector++) {
        //Read ival from sector.
        ival = 0;
        addr = sector*CT_FLASH_SECTOR_SIZE;
        err  = flash_read(_db_flash_dev, addr, (uint8_t*)&ival, sizeof(ival));
        if (err != 0) {
            LOG_ERR("Flash read failed! %d [IVAL]\n", err);
            return err;
        }
        //printk("Flash: ival %d @ 0x%08x\n", ival, addr);

        // We need to find the sector with the highest ival.
        if ((ival != _db_ival_empty) && (ival >= target_ival)){
            target_sector = sector;
            target_ival   = ival;
        }
    }

    // Clear TOC, local TEK and local RPI
    memset(_db_flash_toc, CT_DB_EMPTY, sizeof(_db_flash_toc));
    _db_flash_rpi_cnt = 0;
    ct_db_tek_clear();
    ct_db_rpi_clear();

    // Load TOC & external TEK
    // => starting at "newest sector", working backwards, scanning sectors and
    //     retrieving data until all sectors are loaded or when an empty
    //     sector is encounterd.
    for (uint32_t offset = 0; offset<CT_FLASH_SECTOR_COUNT; offset++) {
        sector      = IDX_SKIP_PREV(target_sector,offset,CT_FLASH_SECTOR_COUNT);
        sector_addr = sector*CT_FLASH_SECTOR_SIZE;
        addr        = sector_addr;

        // Read ival from sector.
        ival = 0;
        err  = flash_read(_db_flash_dev, addr, (uint8_t*)&ival, sizeof(ival));
        if (err != 0) {
            LOG_ERR("Flash read failed! %d [IVAL]\n", err);
            return err;
        }

        // When no valid ival is found, we are done : sector is empty.
        if (ival == _db_ival_empty)
            break;

        // Initialize corresponding toc-page
        toc_page       = &_db_flash_toc[sector];
        toc_page->ival = ival;
        toc_page->cnt  = 0;

        // Offset read-address with "ival" number,
        addr += sizeof(ival);

        // Fetch TEK from flash
        // ==> Copy when there is still space available in our local-buffer
        // ==> Only the last "CT_DB_TEK_CNT_LOCAL" TEK's should be copied.
        if (_db_tek_cnt < CT_DB_TEK_CNT_LOCAL) {
            db_tek_t tek;
            err  = flash_read(_db_flash_dev, addr, (uint8_t*)&tek, sizeof(tek));
            if (err != 0) {
                LOG_ERR("Flash read failed! %d [TEK]\n", err);
                return err;
            }

            // TEK not yet added? ==> add TEK!
            // > shift "prev" because we add from new...old!
            // > after "flash_load", new TEK will be insterted at idx=0!
            // > 'ival' is used to validate as this should be unique to the TEK
            if (_db_tek_list[_db_tek_idx].ival != tek.ival) {
                _db_tek_cnt++;
                _db_tek_idx = IDX_PREV(_db_tek_idx, CT_DB_TEK_CNT_LOCAL);
                memcpy(&_db_tek_list[_db_tek_idx], &tek, sizeof(tek) );
            }
        }

        // Offset address with "tek"
        addr += sizeof(db_tek_t);

        // Count RPI's
        do {
#if defined(CT_FLASH_LOAD_RPI_FULL) && (CT_FLASH_LOAD_RPI_FULL==1)
            db_rpi_t rpi;
            err  = flash_read(_db_flash_dev, addr, (uint8_t*) &rpi, sizeof(rpi));
#else
            ival = 0;
            err  = flash_read(_db_flash_dev, addr, (uint8_t*) &ival, sizeof(ival));
#endif
            if (err != 0) {
                LOG_ERR("Flash read failed! %d [RPI]\n", err);
                return err;
            }

#if defined(CT_FLASH_LOAD_RPI_FULL) && (CT_FLASH_LOAD_RPI_FULL==1)
            ival = rpi.ival_first;
#endif
            // No more RPI's
            if (ival == _db_ival_empty)
                break;

#if defined(CT_FLASH_LOAD_RPI_FULL) && (CT_FLASH_LOAD_RPI_FULL==1)
            LOG_DBG(" >> [%04d] addr:%06x - ival:%010d",
                            _db_flash_rpi_cnt, addr, ival);
            LOG_HEXDUMP_DBG((uint8_t*)&rpi.rpi, RPI_SIZE, "RPI");
#endif

            // Foud valid RPI
            toc_page->cnt++;
            _db_flash_rpi_cnt++;

            // Point to next RPI
            addr += sizeof(db_rpi_t);

        // Stop when there isn't valid data or when we reached end of sector.
        } while ( (ival != _db_ival_empty) &&
                    (addr < (sector_addr + CT_FLASH_SECTOR_SIZE)));
    }

    //New TEK's should be added at idx=0
    _db_tek_idx = 0;

    // New data should be pushed to the sector following the sector with the
    //      highest ival
    _db_flash_sector_idx    = IDX_NEXT(target_sector, CT_FLASH_SECTOR_COUNT);
    _db_flash_sector_offset = 0; //write at first addr.
#if 0
    // Print TOC
    for (sector = 0; sector<CT_FLASH_SECTOR_COUNT; sector++) {
        toc_page = &_db_flash_toc[sector];
        printk("ToC[%04d] addr:0x%06x ival:%010d rpi-cnt:%03d %s\n",
            sector, sector*CT_FLASH_SECTOR_SIZE, toc_page->ival, toc_page->cnt,
                ((sector==_db_flash_sector_idx) ? "<< target" : ""));
    }
#endif

    // Print TEK
    LOG_DBG("Flash: %d RPI's found\n", _db_flash_rpi_cnt);
    LOG_DBG("Flash: %d TEK's found\n", _db_tek_cnt);
    for (uint16_t n=0; n<_db_tek_cnt; n++) {

        uint32_t n_idx;
        //idx of first element
        n_idx = IDX_SKIP_PREV(_db_tek_idx, _db_tek_cnt, CT_DB_TEK_CNT_LOCAL);
        //idx of n'th first element
        n_idx = IDX_SKIP_NEXT(n_idx, n, CT_DB_TEK_CNT_LOCAL);

        LOG_DBG("TEK[%04d] ival:%010d", n_idx, _db_tek_list[n_idx].ival);
        LOG_HEXDUMP_DBG((uint8_t*)&_db_tek_list[n_idx].tek, TEK_SIZE, "");
    }

    return 0;
}

int ct_db_flash_tek(db_tek_t* tek)
{
    int err;
    uint32_t addr;
    db_flash_toc_page_t *toc_page;

    //determine address of next sector
    // => Only start new sector if data has been written in current.
    if (_db_flash_sector_offset != 0)
        _db_flash_sector_idx = IDX_NEXT(_db_flash_sector_idx,CT_FLASH_SECTOR_COUNT);

    // TOC page containing corresponding data.
    toc_page = &_db_flash_toc[_db_flash_sector_idx];

    // Write of a new TEK should always be aligned to the start of a sector.
    _db_flash_sector_offset = 0;
    addr = _db_flash_sector_idx*CT_FLASH_SECTOR_SIZE + _db_flash_sector_offset;

    // Erase sector to enable us to write data.
    // ==> a "write" can only write "0" !
    flash_write_protection_set(_db_flash_dev, false);
    err = flash_erase(_db_flash_dev, addr, CT_FLASH_SECTOR_SIZE);
    if (err != 0) {
        LOG_ERR("Flash erase failed! %d\n", err);
        return err;
    }

    // Erase succesfull, so we need to clear ival & RPI's from TOC
    // 1) Decrease RPI-count if TOC-page contains data.
    if (toc_page->cnt != _db_cnt_empty) {
        _db_flash_rpi_cnt -= toc_page->cnt;
    }
    // 2) Clear TOC-page
    memset(toc_page, CT_DB_EMPTY, sizeof(db_flash_toc_page_t));

    // Write current ival ==> always at start of sector!
    flash_write_protection_set(_db_flash_dev, false);
    err = flash_write(_db_flash_dev, addr, (uint8_t*)&_db_ival, sizeof(uint32_t));
    if (err != 0) {
        LOG_ERR("Flash write (ival) failed! %d\n", err);
        //reset offset so new sector will be written upon next db-update
        _db_flash_sector_offset = 0;
        return err;
    }

    _db_flash_sector_offset += sizeof(uint32_t);
    addr = _db_flash_sector_idx*CT_FLASH_SECTOR_SIZE + _db_flash_sector_offset;

    // Write tek
    flash_write_protection_set(_db_flash_dev, false);
    err = flash_write(_db_flash_dev, addr, (uint8_t*)tek, sizeof(db_tek_t));
    if (err != 0) {
        LOG_ERR("Flash write (tek) failed! %d\n", err);
        //reset offset so this sector will be re-written upon next db-update
        _db_flash_sector_offset = 0;
        return err;
    }

    //printf("Flash write succes! 0x%06x : %d [tek]\n", addr, tek->ival);
    _db_flash_sector_offset += sizeof(db_tek_t);

    //ival and TEK written succesfully to Flash, update TOC
    toc_page->ival = _db_ival;
    toc_page->cnt  = 0;

    return 0;
}

int ct_db_flash_rpi(db_rpi_t* rpi)
{
    int err = 0;

    // Can we write RPI?
    // => if sector is full  ==> start new sector with TEK-write
    // => if sector is empty ==> write RPI to current sector
    if ( ((CT_FLASH_SECTOR_SIZE - _db_flash_sector_offset) < sizeof(db_rpi_t))
            || (_db_flash_sector_offset == 0) ) {
        db_tek_t tek;
        ct_db_tek_get_last(tek.tek, &tek.ival);
        err = ct_db_flash_tek(&tek);
        if (err != 0) {
            return err;
        }
    }

    // Write RPI
    flash_write_protection_set(_db_flash_dev, false);
    uint32_t addr = _db_flash_sector_idx * CT_FLASH_SECTOR_SIZE
                        + _db_flash_sector_offset;
    err = flash_write(_db_flash_dev, addr, (uint8_t*)rpi, sizeof(db_rpi_t));
    if (err != 0) {
        LOG_ERR("Flash write (rpi) failed! %d\n", err);
        return err;
    }

    //Write succesfull, so increase offset
    _db_flash_sector_offset += sizeof(db_rpi_t);

    //Update TOC
    _db_flash_toc[_db_flash_sector_idx].cnt++;
    _db_flash_rpi_cnt++;

    return err;
}

void ct_db_flash_flush(void)
{
    uint32_t idx_rpi;
    db_rpi_t *db_rpi;

    // Flush all RPI's from local buffer to flash
    for(int i = _db_rpi_cnt; i>0; i--) {
        idx_rpi = IDX_SKIP_PREV(_db_rpi_idx, i, CT_DB_RPI_CNT_LOCAL);
        db_rpi  = &_db_rpi_list[idx_rpi];

        LOG_DBG("FLUSH: [%d/%d] [%d/%d] %d..%d", i, idx_rpi, _db_rpi_cnt,
                        _db_rpi_idx, db_rpi->ival_first, db_rpi->ival_last);

        if (db_rpi->ival_first != _db_ival_empty) {
            ct_db_flash_rpi(db_rpi);
            //remove element from local databse.
            memset(db_rpi, CT_DB_EMPTY, sizeof(db_rpi_t));
            _db_rpi_cnt--;
        }
    }
}

int ct_db_flash_rpi_get(uint16_t n, db_rpi_t *rpi)
{
    uint32_t n_idx;
    uint32_t sector = _db_flash_sector_idx;

    //get number of RPIs in databse.
    uint16_t db_cnt;
    ct_db_rpi_get_cnt(&db_cnt);

    // Instead of counting from oldest..n'th RPI, we search from newest..n'th.
    // => '+1' is added as "cnt" runs from 1..'X',
    //          while 'n' is an index running from 0..'X-1'
    n_idx  = db_cnt - (n + 1);
    // do not search in local buffer..
    n_idx -= _db_rpi_cnt;
    // Use TOC to find which sector contains the n'th RPI
    while( _db_flash_toc[sector].cnt <= n_idx ) {
        n_idx -= _db_flash_toc[sector].cnt;
        sector = IDX_PREV(sector, CT_FLASH_SECTOR_COUNT);
    }

    // address of sector
    uint32_t addr = sector*CT_FLASH_SECTOR_SIZE;
    // offset to first RPI in sector
    addr += sizeof(uint32_t) + sizeof(db_tek_t);
    // offset to n'th RPI
    // => '+1' is added as "cnt" runs from 1..'X',
    //          while 'n' is an index running from 0..'X-1'
    addr += sizeof(db_rpi_t) * (_db_flash_toc[sector].cnt - (n_idx + 1));

    // Grab RPI from memory
    int err = flash_read(_db_flash_dev, addr, (uint8_t*)rpi, sizeof(db_rpi_t));
    if (err != 0) {
        LOG_ERR("Flash read failed! %d [RPI]\n", err);
        return err;
    }

    LOG_DBG("Flash-get: n'th:%04d - addr:%06x - ival:%010d",
                    n, addr, rpi->ival_first);
    LOG_HEXDUMP_DBG((uint8_t*)&rpi->rpi, RPI_SIZE,"");

    return 0;
}

#endif /* DB_USE_EXTERNAL_FLASH */



// Allow db to provide data-management, providing the current interval.
int ct_db_tick(uint32_t ival)
{
    // no update..
    if (_db_ival == ival) {
        return 0;
    }
    _db_ival = ival;

    // DB management
#ifdef DB_USE_EXTERNAL_FLASH
    uint32_t idx_rpi;
    db_rpi_t *db_rpi;

    // Push old elements from local buffer to flash
    // >> Check RPI in buffer from old..new
    for(int i = _db_rpi_cnt; i>0; i--) {
        idx_rpi = IDX_SKIP_PREV(_db_rpi_idx, i, CT_DB_RPI_CNT_LOCAL);
        db_rpi  = &_db_rpi_list[idx_rpi];
        LOG_DBG("DB: [%d] %d..%d/%d", i, db_rpi->ival_first,
                        db_rpi->ival_last, ival);

        if (db_rpi->ival_first != 0) {
            if ((ival - db_rpi->ival_first) > CT_DB_IVAL_DIFF_OLD) {
                ct_db_flash_rpi(db_rpi);
                //remove element from local databse.
                memset(db_rpi, CT_DB_EMPTY, sizeof(db_rpi_t));
                _db_rpi_cnt--;
            } else {
                // no more outdated entries..
                break;
            }
        }
    }
#endif /* DB_USE_EXTERNAL_FLASH */

    return 0;
}



/************** TEK **************/

int ct_db_tek_clear(void)
{
    memset(_db_tek_list, CT_DB_EMPTY, sizeof(_db_tek_list));
    _db_tek_idx = 0;
    _db_tek_cnt = 0;
    return 0;
}

int ct_db_tek_add(uint8_t *tek, uint32_t ival)
{
    // Add tek..
    db_tek_t *dst = &_db_tek_list[_db_tek_idx];
    memcpy(dst->tek,tek,TEK_SIZE);
    dst->ival = ival;

#ifdef DB_USE_EXTERNAL_FLASH
    //update db management
    ct_db_tick(ival);
    //flush all old RPI's
    ct_db_flash_flush();
    //push new tek
    ct_db_flash_tek(dst);
#endif

    // update local index..
    _db_tek_idx = IDX_NEXT(_db_tek_idx, CT_DB_TEK_CNT_LOCAL);
    _db_tek_cnt++;
    if (_db_tek_cnt > CT_DB_TEK_CNT_LOCAL)
        _db_tek_cnt = CT_DB_TEK_CNT_LOCAL;

    return 0;
}

//compute number of tek's in database.
int ct_db_tek_get_cnt(uint16_t *cnt)
{
    if(!cnt)
        return -EINVAL;

    *cnt = _db_tek_cnt;
    return 0;
}


//retrieve n'th tek from DB
int ct_db_tek_get(uint16_t n, uint8_t *tek, uint32_t *ival)
{
    if(!tek || !ival || (n>CT_DB_TEK_CNT_LOCAL))
        return -EINVAL;

    //get number of TEKs in databse.
    uint16_t cnt;
    ct_db_tek_get_cnt(&cnt);

    // No TEKs in database..
    if (cnt == 0) {
        return -EINVAL;
    }

    // the requested number is not in database.
    if (n > cnt) {
        return -EINVAL;
    }

    // get index of requested  element
    uint16_t i;
    //idx of first element
    i = IDX_SKIP_PREV(_db_tek_idx, cnt, CT_DB_TEK_CNT_LOCAL);
    //idx of n'th element
    i = IDX_SKIP_NEXT(i, n, CT_DB_TEK_CNT_LOCAL);

    db_tek_t *elm = &_db_tek_list[i];
    memcpy(tek,elm->tek,TEK_SIZE);
    *ival = elm->ival;

    return 0;
}



int ct_db_tek_get_last(uint8_t *tek, uint32_t *ival)
{
    if(!tek || !ival)
        return -EINVAL;

    //get number of TEKs in databse.
    uint16_t cnt;
    ct_db_tek_get_cnt(&cnt);

    // No TEKs in database..
    if (cnt == 0) {
        return -EINVAL;
    }

    // get index of last inserted element
    int i = IDX_PREV(_db_tek_idx,CT_DB_TEK_CNT_LOCAL);

    // fetch and copy data
    db_tek_t *last = &_db_tek_list[i];
    memcpy(tek,last->tek,TEK_SIZE);
    *ival = last->ival;

    return 0;
}

/************** RPI **************/

int ct_db_rpi_clear(void)
{
    memset(_db_rpi_list, CT_DB_EMPTY, sizeof(_db_rpi_list));
    _db_rpi_idx = 0;
    _db_rpi_cnt = 0;
    return 0;
}

int ct_db_rpi_add(uint8_t *rpi, uint8_t *aem, int8_t rssi, uint32_t ival)
{
    uint32_t i_idx;
    db_rpi_t *db_rpi;

    // check for doubles...
    // >> we check from new..old
    for(int i = 0; i<_db_rpi_cnt; i++) {
        // Adding "+1" as '_db_rpi_idx' points to memory in which we need to
        //  write the newest RPI, so '_db_rpi_idx-1' is memory containing last
        //  added RPI.
        i_idx  = IDX_SKIP_PREV(_db_rpi_idx, i+1, CT_DB_RPI_CNT_LOCAL);
        db_rpi = &_db_rpi_list[i_idx];
        LOG_DBG("DB: [%d] last %d/%d", i, db_rpi->ival_last,ival);

        // Recorded RPI is too long ago ==> add detected RPI to db
        if ((ival - db_rpi->ival_last) > CT_DB_IVAL_DIFF_OLD ) {
            break;
        }

        // RPI's match ==> update data
        // NOTE: in memory/db replacement!
        if(memcmp(db_rpi->rpi, rpi, sizeof(db_rpi->rpi)) == 0) {
            LOG_DBG("DB: old rpi (seen:%d)", db_rpi->cnt);
            int32_t rssi_sum = ((int32_t) db_rpi->rssi) * db_rpi->cnt + rssi;
            db_rpi->cnt++;
            db_rpi->rssi = rssi_sum / db_rpi->cnt;
            db_rpi->ival_last = ival;
            return 0;
        }
    }

    // To allocate new RPI we need to have space in our local buffer
    if (_db_rpi_cnt == CT_DB_RPI_CNT_LOCAL)
        return -ENOMEM;

    LOG_DBG("DB: new rpi @ %d / %d", _db_rpi_cnt, _db_rpi_idx);

    // Store data
    db_rpi = &_db_rpi_list[_db_rpi_idx];
    memcpy(db_rpi->rpi, rpi, sizeof(db_rpi->rpi));
    memcpy(db_rpi->aem, aem, sizeof(db_rpi->aem));
    db_rpi->cnt = 1;
    db_rpi->rssi = rssi;
    db_rpi->ival_first = ival;
    db_rpi->ival_last  = ival;

    // Update indices and management.
    _db_rpi_idx = IDX_NEXT(_db_rpi_idx, CT_DB_RPI_CNT_LOCAL);
    _db_rpi_cnt++;
    if (_db_rpi_cnt > CT_DB_RPI_CNT_LOCAL) {
        _db_rpi_cnt = CT_DB_RPI_CNT_LOCAL;
    }

    LOG_DBG("DB: new rpi @ %d / %d", _db_rpi_cnt, _db_rpi_idx);

    return 0;
}

//compute number of RPI's in database.
int ct_db_rpi_get_cnt(uint16_t *cnt)
{
    if(!cnt)
        return -EINVAL;

#if defined(DB_USE_EXTERNAL_FLASH)
    *cnt = _db_rpi_cnt + _db_flash_rpi_cnt;
#else
    *cnt = _db_rpi_cnt;
#endif

    return 0;
}

//retrieve n'th tek from DB
int ct_db_rpi_get(uint16_t n, uint8_t *rpi, uint8_t *aem, int8_t *rssi,
                uint8_t *cnt, uint32_t *ival_last)
{
    if(!rpi || (n>CT_DB_RPI_CNT_LOCAL))
        return -EINVAL;

    //get number of RPIs in databse.
    uint16_t db_cnt;
    ct_db_rpi_get_cnt(&db_cnt);

    // No RPIs in database..
    if (db_cnt == 0)
        return -EINVAL;

    // the requested number is not in database.
    if (n > db_cnt)
        return -EINVAL;

    uint32_t n_idx;
    uint32_t n2 = n;
    db_rpi_t elm;

#if defined(DB_USE_EXTERNAL_FLASH)
    // grab from memory?
    if (n < _db_flash_rpi_cnt) {
        ct_db_flash_rpi_get(n,&elm);

    // grab from local db.
    } else {
        // Correct 'n' with flash-cnt so it holds the index in the local buffer.
        n    -= (_db_flash_rpi_cnt);
#endif
        // get index of requested  element from local buffer
        // idx of first element
        n_idx = IDX_SKIP_PREV(_db_rpi_idx, _db_rpi_cnt, CT_DB_RPI_CNT_LOCAL);
        //idx of n'th element
        n_idx = IDX_SKIP_NEXT(n_idx, n, CT_DB_RPI_CNT_LOCAL);
        elm   = _db_rpi_list[n_idx];

        LOG_DBG("Local-get: n'th:%04d - addr:xxxxxx - ival:%010d",
                        n2, elm.ival_first);
        LOG_HEXDUMP_DBG((uint8_t*)&elm.rpi, RPI_SIZE, "");

#if defined(DB_USE_EXTERNAL_FLASH)
    }
#endif

    memcpy(rpi,elm.rpi,RPI_SIZE);
    memcpy(aem,elm.aem,AEM_SIZE);
    *rssi      = elm.rssi;
    *cnt       = elm.cnt;
    *ival_last = elm.ival_last;

    return 0;
}

/************** MAIN **************/

int ct_db_clear(void)
{
    ct_db_tek_clear();
    ct_db_rpi_clear();
#if defined(DB_USE_EXTERNAL_FLASH)
    ct_db_flash_clear();
    ct_db_flash_load();
#endif
    return 0;
}


int ct_db_init(void)
{
    int ret;

    ret = ct_db_tek_clear();
    if (ret != 0)
        return ret;

    ret = ct_db_rpi_clear();
    if (ret != 0)
        return ret;

#if defined(DB_USE_EXTERNAL_FLASH)
    ret = ct_db_flash_init();
    if (ret != 0)
        return ret;

    ret = ct_db_flash_load();
    if (ret != 0)
        return ret;
#endif

    return 0;
}
