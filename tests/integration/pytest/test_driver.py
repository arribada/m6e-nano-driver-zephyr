# Copyright (c) 2023 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: Apache-2.0

import logging
from packaging import version

from twister_harness import Shell

logger = logging.getLogger(__name__)

def catch(func, handle=lambda e : e, *args, **kwargs):
    try:
        return func(*args, **kwargs)
    except Exception as e:
        return False

def test_shell_ping(shell: Shell):
    logger.info('send "ping" command')
    lines = shell.exec_command('test ping')
    assert 'pong' in lines, 'expected response not found'
    logger.info('response is valid')

def test_shell_version(shell: Shell):
    logger.info('send "test version" command')
    lines = shell.exec_command('test version')
    assert any([catch(lambda: version.parse(line)) for line in lines]), 'expected version not found'
    logger.info('response is valid')