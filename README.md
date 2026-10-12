<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# kvm-switch

Copyright 2026 Ian Pilcher <<arequipeno@gmail.com>>

## Overview

This is a simple user service that reacts when my KVM switch changes from one
system to another.  It currently works only with KDE Plasma.

* When the keyboard, displays, mouse, etc., are switched **away**, it locks the
  desktop.
* When they are switched **back**, it wakes the displays up.

A USB device **downstream** of the KVM is used to detect changes.  Note that the
device must not be connected to the dedicated keyboard or mouse port on the KVM,
because the KVM maintains "virtual" keyboard and mouse devices that are never
disconnected.

The USB vendor and model IDs are set at the top of `kvm-switch.py`:

```python
DOWNSTREAM_USB_VENDOR: typing.Final = '1a40'
DOWNSTREAM_USB_MODEL: typing.Final = '0101'
```

## Installation

* `cp kvm-switch.py ~/.local/bin/`
* `cp kvm-switch.service ~/.config/systemd/user/`
* `systemctl --user daemon-reload`
* `systemctl --user enable kvm-switch.service --now`

## Logging

When running in a terminal, **kvm-switch** logs to `stderr` at `DEBUG` level.
Otherwise (e.g. when run under **systemd**), it logs to the system log
(**journald**) at `INFO` level.  Both the destination and the level can be
overridden with environment variables:

* `KVM_SWITCH_LOG_DEST`: `stderr` or `syslog`
* `KVM_SWITCH_LOG_LEVEL`: `DEBUG`, `INFO`, `WARNING`, `ERROR`, or `CRITICAL`

For example, to force **kvm-switch** to log to the system log at `DEBUG` level,
add the following to the `[Service]` section of `kvm-switch.service`:

```ini
Environment=KVM_SWITCH_LOG_DEST=syslog
Environment=KVM_SWITCH_LOG_LEVEL=DEBUG
```

