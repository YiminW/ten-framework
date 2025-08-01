#
# Copyright © 2025 Agora
# This file is part of TEN Framework, an open source project.
# Licensed under the Apache License, Version 2.0, with certain conditions.
# Refer to the "LICENSE" file in the root directory for more information.
#
import asyncio
from ten_runtime import (
    AsyncExtension,
    AsyncTenEnv,
    Cmd,
    LogLevel,
    CmdResult,
)


class DefaultExtension(AsyncExtension):
    def __init__(self, name: str) -> None:
        super().__init__(name)
        self.name = name

    async def on_configure(self, ten_env: AsyncTenEnv) -> None:
        await asyncio.sleep(0.5)

    async def on_init(self, ten_env: AsyncTenEnv) -> None:
        await asyncio.sleep(0.5)

    async def on_start(self, ten_env: AsyncTenEnv) -> None:
        await asyncio.sleep(0.5)
        ten_env.log(LogLevel.DEBUG, "on_start")

    async def on_deinit(self, ten_env: AsyncTenEnv) -> None:
        await asyncio.sleep(0.5)
        await asyncio.sleep(1)

    async def on_cmd(self, ten_env: AsyncTenEnv, cmd: Cmd) -> None:
        cmd_json, _ = cmd.get_property_to_json()
        ten_env.log(LogLevel.DEBUG, f"on_cmd: {cmd_json}")

        # Mock async operation, e.g. network, file I/O.
        await asyncio.sleep(0.5)

        # Send a new command to other extensions and wait for the result. The
        # result will be returned to the original sender.
        new_cmd = Cmd.create("hello")
        cmd_result, _ = await ten_env.send_cmd(new_cmd)
        assert cmd_result is not None

        cmd_result_json, _ = cmd_result.get_property_to_json()

        new_result = CmdResult.create(cmd_result.get_status_code(), cmd)
        new_result.set_property_from_json(None, cmd_result_json)

        await ten_env.return_result(new_result)

    async def on_stop(self, ten_env: AsyncTenEnv) -> None:
        ten_env.log(LogLevel.DEBUG, "on_stop")

        await asyncio.sleep(0.5)
