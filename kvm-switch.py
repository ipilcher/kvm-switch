#!/usr/bin/python3
# SPDX-FileCopyrightText: 2026 Ian Pilcher <arequipeno@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

import contextlib
import logging
import logging.handlers
import os
import sys
import typing

import pyudev
import sdbus

DOWNSTREAM_USB_VENDOR: typing.Final = '1a40'
DOWNSTREAM_USB_MODEL: typing.Final = '0101'

hub_devpath: str | None = None
logger: typing.Final = logging.getLogger('kvm-switch')

def setup_logging() -> None:
    if sys.stderr.isatty():
        dest, level = 'stderr', logging.DEBUG
    else:
        dest, level = 'syslog', logging.INFO
    env_dest = os.environ.get('KVM_SWITCH_LOG_DEST', '').strip().lower()
    if env_dest in ('stderr', 'syslog'):
        dest = env_dest
    env_level_name = os.environ.get('KVM_SWITCH_LOG_LEVEL', '').strip().upper()
    if env_level_name:
        env_level = logging.getLevelNamesMapping().get(env_level_name)
        if env_level:
            level = env_level
    logger.setLevel(level)
    if dest == 'stderr':
        handler: logging.Handler = logging.StreamHandler(stream=sys.stderr)
        handler.setFormatter(
            logging.Formatter('%(asctime)s %(levelname)s %(message)s')
        )
    else:
        handler = logging.handlers.SysLogHandler(address='/dev/log')
        handler.ident = 'kvm-switch'
    logger.addHandler(handler)
    if env_dest and dest != env_dest:
        logger.warning("Ignoring invalid KVM_SWITCH_LOG_DEST: '%s'", env_dest)
    if env_level_name and not env_level:
        logger.warning(
            "Ignoring invalid KVM_SWITCH_LOG_LEVEL: '%s'", env_level_name
        )

def is_new_hub(device: pyudev.Device) -> bool:
    return (
        hub_devpath is None
        and device.device_type == 'usb_device'
        and device.get('ID_VENDOR_ID') == DOWNSTREAM_USB_VENDOR
        and device.get('ID_MODEL_ID') == DOWNSTREAM_USB_MODEL
    )

class KdePowerManagement(
    sdbus.DbusInterfaceCommon,
    interface_name='org.kde.Solid.PowerManagement'
):
    @sdbus.dbus_method(method_name='wakeup')
    def wakeup(self) -> None:
        raise NotImplementedError

def wake_screens() -> None:
    with contextlib.closing(sdbus.sd_bus_open_user()) as bus:
        kde_power = KdePowerManagement(
            'org.kde.Solid.PowerManagement',
            '/org/kde/Solid/PowerManagement',
            bus
        )
        kde_power.wakeup()

class ScreenSaver(
    sdbus.DbusInterfaceCommon,
    interface_name='org.freedesktop.ScreenSaver'
):
    @sdbus.dbus_method(method_name='Lock')
    def lock(self) -> None:
        raise NotImplementedError

def lock_screens() -> None:
    with contextlib.closing(sdbus.sd_bus_open_user()) as bus:
        screensaver = ScreenSaver(
            'org.freedesktop.ScreenSaver','/ScreenSaver', bus
        )
        screensaver.lock()

def main() -> None:
    global hub_devpath
    setup_logging()
    context: typing.Final = pyudev.Context()
    monitor: typing.Final = pyudev.Monitor.from_netlink(context)
    monitor.filter_by(subsystem='usb', device_type='usb_device')
    # Start listening for events; they'll queue up until we process them
    monitor.start()
    # Check existing devices
    for device in context.list_devices(subsystem='usb'):
        if is_new_hub(device):
            hub_devpath = device.get('DEVPATH')
            logger.debug('Found existing downstream device at %s', hub_devpath)
            break
    else:
        logger.debug('Existing downstream device not found')
    logger.info('Waiting for downstream device events')
    # Process events; is_new_hub() guards against "adding" an existing hub
    for action, device in monitor:
        if action == 'add' and is_new_hub(device):
            hub_devpath = device.get('DEVPATH')
            logger.debug('Found new downstream device at %s', hub_devpath)
            logger.info('KVM switched to this system; waking screens')
            wake_screens()
        elif action == 'remove' and device.get('DEVPATH') == hub_devpath:
            logger.debug('Downstream device at %s was removed', hub_devpath)
            logger.info('KVM switch to another system; locking screens')
            hub_devpath = None
            lock_screens()
        else:
            logger.debug(
                'Ignoring USB %s event for %s', action, device.get('DEVPATH')
            )

if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        pass
