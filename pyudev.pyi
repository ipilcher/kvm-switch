# SPDX-FileCopyrightText: 2026 Ian Pilcher <arequipeno@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later


import collections.abc
import typing


class Device:

    @property
    def device_type(self) -> str | None:
        ...

    @typing.overload
    def get(self, attribute: str) -> str | None:
        ...

    @typing.overload
    def get[T](self, attribute: str, default: T) -> str | T:
        ...


class Enumerator:

    def __iter__(self) -> collections.abc.Iterator[Device]:
        ...


class Context:

    def __init__(self) -> None:
        ...
    def list_devices(self, **kwargs: str) -> Enumerator:
        ...


class Monitor:

    @classmethod
    def from_netlink(cls, context: Context, source: str = "udev") -> Monitor:
        ...

    def filter_by(self, subsystem: str, device_type: str | None = None) -> None:
        ...

    def start(self) -> None:
        ...

    def __iter__(self) -> collections.abc.Iterator[tuple[str, Device]]:
        ...
