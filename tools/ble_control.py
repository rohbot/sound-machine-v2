#!/usr/bin/env python3
"""Control the sound machine over BLE without a Wi-Fi connection."""
import argparse
import asyncio
import json
import sys
from bleak import BleakClient, BleakScanner

SERVICE = 'd973f2e0-b19e-4b9e-a316-4bb1c0c28f70'
COMMAND = 'd973f2e1-b19e-4b9e-a316-4bb1c0c28f70'
STATE = 'd973f2e2-b19e-4b9e-a316-4bb1c0c28f70'
EVENT = 'd973f2e3-b19e-4b9e-a316-4bb1c0c28f70'


def matches(device, advertisement):
    return SERVICE in advertisement.service_uuids


async def run(args):
    if args.action == 'scan':
        devices = await BleakScanner.discover(timeout=6, return_adv=True)
        for device, advertisement in devices.values():
            if matches(device, advertisement):
                print(f'{device.address}  {advertisement.local_name or device.name or "SoundMachine"}')
        return
    if args.action == 'set':
        for command in args.values:
            raw = command.encode('ascii')
            if not 1 <= len(raw) <= 20 or command.count('=') != 1:
                raise ValueError('Use one name=value command of at most 20 ASCII bytes per setting')
    target = args.address or await BleakScanner.find_device_by_filter(matches, timeout=10)
    if not target:
        raise RuntimeError('SoundMachine not found. Move closer and disconnect other BLE clients.')
    async with BleakClient(target, timeout=15) as client:
        if args.action == 'set':
            events = asyncio.Queue()
            await client.start_notify(EVENT, lambda _, data: events.put_nowait(bytes(data).decode('ascii')))
            for command in args.values:
                await client.write_gatt_char(COMMAND, command.encode('ascii'), response=True)
                async def acknowledged():
                    while True:
                        event = await events.get()
                        if event.startswith('error:'):
                            raise RuntimeError(f'{command}: {event}')
                        if event == 'ok:' + command:
                            return
                await asyncio.wait_for(acknowledged(), timeout=6)
        state = json.loads(bytes(await client.read_gatt_char(STATE)))
        print(json.dumps(state, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', help='BLE device address (or UUID on macOS); scan to list devices')
    actions = parser.add_subparsers(dest='action', required=True)
    actions.add_parser('scan', help='Find nearby sound machines')
    actions.add_parser('status', help='Read all current sound and LED settings')
    setter = actions.add_parser('set', help='Apply settings sequentially, waiting for each acknowledgement')
    setter.add_argument('values', nargs='+', metavar='name=value')
    args = parser.parse_args()
    try:
        asyncio.run(run(args))
    except (Exception, KeyboardInterrupt) as error:
        print(f'BLE control failed: {error or type(error).__name__}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
