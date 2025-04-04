#!/usr/bin/env python
import sys
from time import sleep
import argparse
from array import array

import usb.core
import usb.util

CHIPTYPE_CH573 = 0x02

CH_USB_VENDOR_ID   = 0x1a86    # VID
CH_USB_PRODUCT_ID  = 0x8010    # PID
CH_USB_EP_OUT      = 0x01      # endpoint for command transfer out
CH_USB_EP_OUT_DATA = 0x02      # endpoint for data transfer out
CH_USB_EP_IN       = 0x81      # endpoint for reply transfer in
CH_USB_EP_IN_DATA  = 0x82      # endpoint for data reply transfer in
CH_USB_PACKET_SIZE = 256       # packet size
CH_USB_TIMEOUT     = 5000      # timeout for USB operations

CH_STR_PROG_DETECT = (0x81, 0x0d, 0x01, 0x01)
CH_STR_PROG_SPEED  = (0x81, 0x0c, 0x02, 0x01, 0x02) # (0x01: 6000kHz, 0x02: 4000kHz, 0x03: 400kHz)
CH_STR_CHIP_DETECT = (0x81, 0x0d, 0x01, 0x02)
CH_STR_CHIP_SPEED  = (0x81, 0x0c, 0x02, CHIPTYPE_CH573, 0x02)
CH_STR_FLASH_PREP  = (0x81, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00) # send to addr 0x00000000
CH_STR_POLL_DEBUG  = (0x81, 0x08, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01)
CH_STR_ACK1_DEBUG  = (0x81, 0x08, 0x06, 0x05, 0x00, 0x00, 0x00, 0x00, 0x02)
CH_STR_ACK2_DEBUG  = (0x81, 0x08, 0x06, 0x04, 0x00, 0x00, 0x00, 0x04, 0x02)

device = usb.core.find(idVendor=CH_USB_VENDOR_ID, idProduct=CH_USB_PRODUCT_ID)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--flash', help='Flash .bin file. If no file is provided a blinky will be flashed')
    parser.add_argument('--dump', help='Dump memory region, use with --length to batch n bytes')
    parser.add_argument('--length', help='Number of bytes to dump')
    parser.add_argument('--terminal', help='Open debug interface terminal', action='store_true')
    parser.add_argument('--toggle-3v', help='Toggle the 3v3 line, to turn the chip off and on again', action='store_true')
    parser.add_argument('--toggle-5v', help='Toggle the 5v line, to turn the chip off and on again', action='store_true')
    parser.add_argument('--reset', help='Reset', action='store_true')
    args = parser.parse_args()

    if device is None:
        print("no programmer found")
        exit(0)

    # Get an endpoint instance
    cfg = device.get_active_configuration()
    intf = cfg[(0, 0)]

    # Claim the interface
    usb.util.claim_interface(device, intf)

    prog_init()

    if args.toggle_3v or args.toggle_5v:
        toggle_power(args.toggle_3v, args.toggle_5v)

    chip_init()

    if args.flash:
        flash(args.flash)
        reset()
        if args.terminal:
            open_terminal()
    elif args.reset:
        reset()
        if args.terminal:
            open_terminal()
    elif args.terminal:
        open_terminal()
    elif args.dump:
        dump(args.dump, args.length)
    elif not (args.toggle_3v or args.toggle_5v):
        flash()
        reset()

    # Release the interface when done
    usb.util.release_interface(device, intf)
    print('done')

flashloader = bytes.fromhex(
        ''
        )

blink_bin = bytes.fromhex(
        ''
    )

def wch_link_command(cmd):
    device.write(CH_USB_EP_OUT, cmd)
    return list( device.read(CH_USB_EP_IN, CH_USB_PACKET_SIZE, CH_USB_TIMEOUT) )

def wch_link_send_data(data):
    padding_len = CH_USB_PACKET_SIZE - (len(data) % CH_USB_PACKET_SIZE)
    data += bytes([0xff] * padding_len)
    for b in range(0, len(data), CH_USB_PACKET_SIZE):
        device.write(CH_USB_EP_OUT_DATA, data[b:b +CH_USB_PACKET_SIZE])

def toggle_power(do_3v, do_5v):
    if do_3v:
        assert wch_link_command((0x81, 0x0d, 0x01, 0x0a)) == [0x82, 0x0d, 0x01, 0x0a]
        sleep(0.1)
        assert wch_link_command((0x81, 0x0d, 0x01, 0x09)) == [0x82, 0x0d, 0x01, 0x09]
    if do_5v:
        assert wch_link_command((0x81, 0x0d, 0x01, 0x0c)) == [0x82, 0x0d, 0x01, 0x0c]
        sleep(0.1)
        assert wch_link_command((0x81, 0x0d, 0x01, 0x0b)) == [0x82, 0x0d, 0x01, 0x0b]

def prog_init():
    prog_info = wch_link_command(CH_STR_PROG_DETECT)
    # print( [hex(x) for x in prog_info] )
    if prog_info[5] == 18:
        print(f'* linkE v{prog_info[3]}.{prog_info[4]} found')
    assert wch_link_command(CH_STR_PROG_SPEED) == [0x82, 0x0c, 0x01, 0x01]

def chip_init():
    assert wch_link_command(CH_STR_CHIP_DETECT)[3] == CHIPTYPE_CH573
    print('* ch573 found, set speed to 4000kHz') # might be ok on 6000kHz too
    assert wch_link_command(CH_STR_CHIP_SPEED) == [0x82, 0x0c, 0x01, 0x01]

def flash(fw = None):
    fw_bin = blink_bin
    if fw:
        fw_bin = open(fw, 'rb').read()
        print(f'* flashing {fw} (len={len(fw_bin)})')
    else:
        print('* flashing blinky example')
    assert wch_link_command(CH_STR_FLASH_PREP + tuple(len(fw_bin).to_bytes(4))) == [0x82, 0x01, 0x01, 0x01]

    assert wch_link_command((0x81, 0x02, 0x01, 0x05)) == [0x82, 0x02, 0x01, 0x05] # what's this?

    wch_link_send_data(flashloader)

    assert wch_link_command((0x81, 0x02, 0x01, 0x07)) == [0x82, 0x02, 0x01, 0x07] # what's this?
    assert wch_link_command((0x81, 0x02, 0x01, 0x02)) == [0x82, 0x02, 0x01, 0x02] # what's this?

    wch_link_send_data(fw_bin)

    assert wch_link_command((0x81, 0x02, 0x01, 0x08)) == [0x82, 0x02, 0x01, 0x08] # what's this?

def dump(address, length):
    address = int(address, 16)
    if not length:
        length = 4
    elif length[:2] == '0x':
        length = int(length, 16)
    else:
        length = int(length)
    length += (4 - (length % 4)) if length % 4 else 0
    address -= address % 4

    cmd = [0x81, 0x03, 0x08] + list(address.to_bytes(4)) + list(length.to_bytes(4))
    wch_link_command(cmd)
    assert wch_link_command((0x81, 0x02, 0x01, 0x0c)) == [0x82, 0x02, 0x01, 0x0c] # what's this?
    res = array('I', bytes( device.read(CH_USB_EP_IN_DATA, CH_USB_PACKET_SIZE, CH_USB_TIMEOUT) ))
    res.byteswap()
    print(f'{address:08x}: {[hex(x) for x in res.tobytes()]}'.replace("'",""))

def open_terminal():
    assert wch_link_command((0x81, 0x08, 0x06, 0x10, 0x80, 0x00, 0x00, 0x01, 0x02)) == [0x82, 0x08, 0x06, 0x10, 0x80, 0x00, 0x00, 0x01, 0x00]
    assert wch_link_command((0x81, 0x08, 0x06, 0x10, 0x80, 0x00, 0x00, 0x03, 0x02)) == [0x82, 0x08, 0x06, 0x10, 0x80, 0x00, 0x00, 0x03, 0x00]

    assert wch_link_command((0x81, 0x08, 0x06, 0x10, 0x80, 0x00, 0x00, 0x01, 0x02)) == [0x82, 0x08, 0x06, 0x10, 0x80, 0x00, 0x00, 0x01, 0x00]
    assert wch_link_command((0x81, 0x08, 0x06, 0x16, 0x00, 0x00, 0x07, 0x00, 0x02)) == [0x82, 0x08, 0x06, 0x16, 0x00, 0x00, 0x07, 0x00, 0x00]

    assert wch_link_command((0x81, 0x08, 0x06, 0x10, 0x40, 0x00, 0x00, 0x01, 0x02)) == [0x82, 0x08, 0x06, 0x10, 0x40, 0x00, 0x00, 0x01, 0x00]

    try:
        while True:
            res = wch_link_command(CH_STR_POLL_DEBUG)
            if res[7] == 0x85:
                print(chr(res[6]), end='')
                assert wch_link_command(CH_STR_ACK1_DEBUG) == [0x82, 0x08, 0x06, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00]
                assert wch_link_command(CH_STR_ACK2_DEBUG) == [0x82, 0x08, 0x06, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00]
    except KeyboardInterrupt:
        print()

def reset():
    assert wch_link_command((0x81, 0x0b, 0x01, 0x01)) == [0x82, 0x0b, 0x01, 0x01]
    assert wch_link_command((0x81, 0x0d, 0x01, 0xff)) == [0x82, 0x0d, 0x01, 0xff]

if __name__ == '__main__':
    main()
