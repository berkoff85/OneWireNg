/*
 * Copyright (c) 2019 Piotr Stolarz
 * OneWireNg: Ardiono 1-wire service library
 *
 * Distributed under the 2-clause BSD License (the License)
 * see accompanying file LICENSE for details.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#ifndef __OWNG__
#define __OWNG__

#include <stdint.h>
#include <stddef.h>
#include "config.h"

#ifndef UNUSED
# define UNUSED(x) ((void)(x))
#endif

/**
 * 1-wire service interface specification.
 *
 * The class relies on virtual functions provided by derivative class to
 * perform platform specific operations. The platform specific class shall
 * provide at least:
 * - @sa reset() - reset cycle transmission,
 * - @sa touchBit() - 1-wire touch (write and read are touch derived).
 *
 * and optionally:
 * - @sa powerBus() - if parasite powering is supported.
 */
class OneWireNg
{
public:
    /**
     * 1-wire 64-bit id; little-endian
     *
     * Id[0]: device family id,
     * Id[1..6]: unique s/n,
     * Id[7]: CRC-8.
     */
    typedef uint8_t Id[8];

    typedef enum
    {
        /** Success */
        EC_SUCCESS = 0,
        /** Search process finished - no more slave devices available */
        EC_DONE = EC_SUCCESS,
        /** Search process in progress - more slave devices available */
        EC_MORE,
        /** No slave devices detected on the bus */
        EC_NO_DEVS,
        /** 1-wire bus error */
        EC_BUS_ERROR,
        /** CRC error */
        EC_CRC_ERROR,
        /** Service is not supported by the platform */
        EC_UNSUPPORED
    } ErrorCode;

    /**
     * Transmit reset cycle on the 1-wire bus.
     *
     * @return @c true: detected device(s) connected to the bus (presence pulse
     *     observed after the reset cycle), @c false: no devices on the bus.
     */
    virtual bool reset() = 0;

    /**
     * Bit touch.
     *
     * 1-wire bus touching depends on a touched bit as follows:
     * 0: Writes 0 on the bus. There is no bus sampling (the function returns 0),
     * 1: Writes 1 on the bus and samples for response (returned by the
     *    function). The write-1 cycle is equivalent for reading cycle, except
     *    the writing cycle doesn't sample the bus for a response.
     */
    virtual int touchBit(int bit) = 0;

    /**
     * Byte touch with least significant bit transmitted first.
     * @return touching result.
     */
    virtual uint8_t touchByte(uint8_t byte);

    /**
     * Array of bytes touch.
     * Result is passed back in the same buffer.
     */
    virtual void touchBytes(uint8_t *bytes, size_t len) {
        for (size_t i=0; i < len; i++)
            bytes[i] = touchByte(bytes[i]);
    }

    /**
     * Bit write.
     */
    virtual void writeBit(int bit) {
        touchBit(bit);
    }

    /**
     * Byte write with least significant bit transmitted first.
     */
    virtual void writeByte(uint8_t byte) {
        touchByte(byte);
    }

    /**
     * Array of bytes write.
     */
    virtual void writeBytes(const uint8_t *bytes, size_t len) {
        for (size_t i=0; i < len; i++)
            touchByte(bytes[i]);
    }

    /**
     * Bit read.
     */
    virtual int readBit() {
        return touchBit(1);
    }

    /**
     * Byte read with least significant bit transmitted first.
     * @return reading result.
     */
    virtual uint8_t readByte() {
        return touchByte(0xff);
    }

    /**
     * Array of bytes read.
     */
    virtual void readBytes(uint8_t *bytes, size_t len) {
        for (size_t i=0; i < len; i++)
            bytes[i] = touchByte(0xff);
    }

    /**
     * Perform single search step to recognize slave devices connected to
     * the bus. Before calling this routine @sa searchReset() must be called
     * to initialize the search context.
     *
     * @param id In case of success will be filled with an id of the slave
     *     device detected in this step.
     * @param alarm If @c true - search for devices only with alarm state set,
     *     @c false - search for all devices.
     *
     * @return Error codes:
     *     - @sa EC_MORE: Success; more devices available by subsequent calls
     *         of this routine. @id is written with slave id.
     *     - @sa EC_DONE: Success; no more devices available. @id is written
     *         with slave id.
     *     - @sa EC_NO_DEVS: No devices on the bus (@c id is zeroed).
     *     - @sa EC_BUS_ERROR: Bus error.
     *     - @sa EC_CRC_ERROR: CRC error.
     */
    virtual ErrorCode search(Id& id, bool alarm = false);

    /**
     * Reset 1-wire search state.
     */
    virtual void searchReset();

    /**
     * In case there is only one slave connected to the 1-wire bus the routine
     * enables read its id (without performing the whole searching process) by:
     * - Send the reset pulse.
     * - If presence pulse indicates some slave(s) present on the bus, send
     *   "Read ROM" command (0x33).
     * - Read followed 8 bytes constituting the id.
     * - Check CRC of the read id. In case of CRC failure there is probably
     *   more than one slave on the bus causing garbage (logical AND) of the
     *   received data.
     *
     * @return Error codes:
     *     - @sa EC_SUCCESS - Success, the result written to @c id.
     *     - @sa EC_NO_DEVS - No devices on the bus.
     *     - @sa EC_CRC_ERROR - Probably more than one slave on the bus.
     */
    ErrorCode readSingleId(Id &id)
    {
        if (!reset()) return EC_NO_DEVS;
        writeByte(CMD_READ_ROM);
        readBytes(&id[0], sizeof(Id));
        return (checkCrcId(id) ? EC_SUCCESS : EC_CRC_ERROR);
    }

    /**
     * Address single slave device by:
     * - Send the reset pulse.
     * - If presence pulse indicates some slave(s) present on the bus, send
     *   "Match ROM" command (0x55) followed by the slave id.
     *
     * After calling this routine subsequent data send over the bus will be
     * received by the selected slave until the next reset pulse.
     */
    void addressSingle(const Id& id)
    {
        if (reset()) {
            writeByte(CMD_MATCH_ROM);
            writeBytes(&id[0], sizeof(Id));
        }
    }

    /**
     * Address all the slave devices connected to the bus by:
     * - Send the reset pulse.
     * - If presence pulse indicates some slave(s) present on the bus, send
     *   "Skip ROM" command (0xCC).
     *
     * After calling this routine subsequent data send over the bus will be
     * received by all connected slaves until the next reset pulse.
     */
    void addressAll() {
        if (reset()) writeByte(CMD_SKIP_ROM);
    }

    /**
     * Power the 1-wire bus via direct connection a voltage source to the bus.
     * The function enables to leverage parasite powering of slave devices
     * required to perform energy consuming operation, where energy provided
     * by resistor-pulled-up data wire is not sufficient.
     *
     * The power is provided until the next activity on the bus (reset, read,
     * write, touch) or the routine is called with @c on set to @c false.
     *
     * @param on If @true - power the bus, @false - depower the bus.
     *
     * @return Error codes: @sa EC_UNSUPPORED: service is unsupported by the
     *     platform, otherwise @sa EC_SUCCESS.
     */
    virtual ErrorCode powerBus(bool on) {
        UNUSED(on);
        return EC_UNSUPPORED;
    }

    /**
     * Compute Maxim/Dallas CRC-8.
     * Polynomial used: x^8 + x^5 + x^4 + 1
     */
    static uint8_t crc8(const void *in, size_t len);

    /**
     * Check CRC for a given @c id.
     * @return @c false for CRC mismatch, @c true otherwise.
     */
    static bool checkCrcId(const Id& id)
    {
        uint8_t crc = crc8(&id[0], sizeof(Id)-1);
        return (crc == id[sizeof(Id)-1]);
    }

    virtual ~OneWireNg() {}

    const static uint8_t CMD_READ_ROM        = 0x33;
    const static uint8_t CMD_MATCH_ROM       = 0x55;
    const static uint8_t CMD_SKIP_ROM        = 0xCC;

    const static uint8_t CMD_SEARCH_ROM      = 0xF0;
    const static uint8_t CMD_SEARCH_ROM_COND = 0xEC;

protected:
   /**
    * This class is intended to be inherited by specialized classes.
    */
    OneWireNg() {
        searchReset();
    }

private:
    /* search related support routines */
    int setDiscrepancy(size_t n);
    bool updateDiscrepancy();
    ErrorCode transmitSearchTriplet(size_t n, Id& id, size_t& dscrCnt);

    Id _msk;   /** discrepancy mask (56-bit are valid) */
    Id _dscr;  /** discrepancy state (56-bit are valid) */

#ifdef __TEST__
friend class OneWireNg_Test;
#endif
};

#endif /* __OWNG__ */