<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# kbdsync

Copyright 2026 Ian Pilcher <<arequipeno@gmail.com>>

## Overview

**kdbsync** is a simple service that resyncs the state of keyboard LEDs.  When
triggered, it creates synthetic input events (NumLock and CapsLock keypresses),
which make the system update the physical LEDs.  Creating two keypress events
for each key, syncs the LEDs without changing the system's state.

## Setup

### Device access

**kbdsync** needs permission to access `/dev/uinput`.  Rather than  running it
as `root`, create a group that allows access to the device.

Create the `uinput` group.

```
$ sudo group add -r uinput
```

Create the `udev` rule for `/dev/uinput`.

```
$ cat /etc/udev/rules.d/99-kbdsync-uinput.rules
KERNEL=="uinput", SUBSYSTEM=="misc", GROUP="uinput", MODE="0660", OPTIONS+="static_node=uinput"
```

Trigger the new rule.

```
$ sudo udevadm trigger /dev/uinput
```

### Triggering the service

**kbdsync** listens on a named pipe (FIFO), usually `/run/kbdsync/kbdsync.fifo`.
Any write to this FIFO triggers it to sync the keyboard LEDs.  (**kbdsync**
normally waits a few seconds after receiving a trigger before performing the
sync.  Any additional triggers received during this delay, or during the actual
sync, are ignored.)

Create a `udev` rule to send the trigger when a USB device **downstream** of the
KVM switch is connected.

```
$ cat /etc/udev/rules.d/99-kbdsync-trigger.rules
SUBSYSTEM=="usb", ACTION=="add", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="1a40", ATTR{idProduct}=="0101", \
	RUN+="/usr/bin/dd if=/dev/zero of=/run/kbdsync/kbdsync.fifo bs=1 count=1 oflag=nonblock conv=nocreat status=none"
```

### Install the service

Build the service executable.

```
$ gcc -O2 -Wall -Wextra -o kbdsync kbdsync.c
```

Install the executable.

```
$ sudo cp kbdsync /usr/local/bin/
```

Install the **systemd** unit file.

```
$ sudo cp kbdsync.service /etc/systemd/system/
$ sudo systemctl daemon-reload
```

### SELinux

Build the policy module.

```
$ make -f /usr/share/selinux/devel/Makefile
...
Compiling targeted kbdsync module
Creating targeted kbdsync.pp policy package
rm tmp/kbdsync.mod tmp/kbdsync.mod.fc
```

Install the policy module.

```
$ sudo semodule -i kbdsync.pp
```

Set the executable's context.

```
$ sudo restorecon /usr/local/bin/kbdsync
```

### Enable the service

This also starts it.

```
$ sudo systemctl enable kbdsync.service --now
```
