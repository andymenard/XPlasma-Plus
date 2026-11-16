#!/usr/bin/python3
"""Update only the XPlasma-managed SDDM greeter settings."""
import argparse
import configparser
import os
from pathlib import Path

CONFIG = Path('/etc/sddm.conf.d/90-xp-welcome.conf')

def update(text, percent, keyboard):
    if not 50 <= percent <= 300:
        raise ValueError('Scale must be between 50 and 300 percent.')
    config = configparser.RawConfigParser()
    config.optionxform = str
    config.read_string(text)
    if config.get('Theme', 'Current', fallback='') != 'xp-welcome':
        raise ValueError('Install the XPlasma Welcome login screen first.')
    if not config.has_section('General'):
        config.add_section('General')
    values = [v for v in config.get('General', 'GreeterEnvironment', fallback='').split(',')
              if v.strip() and v.strip().split('=', 1)[0] not in ('QT_SCALE_FACTOR', 'QT_IM_MODULE')]
    values += [f'QT_SCALE_FACTOR={percent / 100:g}',
               'QT_IM_MODULE=' + ('qtvirtualkeyboard' if keyboard else 'compose')]
    config.set('General', 'GreeterEnvironment', ','.join(values))
    config.set('General', 'InputMethod', 'qtvirtualkeyboard' if keyboard else 'compose')
    import io
    output = io.StringIO()
    config.write(output, space_around_delimiters=False)
    return output.getvalue()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--scale', type=int, required=True)
    parser.add_argument('--keyboard', choices=['on', 'off'], default='off')
    args = parser.parse_args()
    if os.geteuid() != 0:
        parser.error('Administrator authentication is required.')
    result = update(CONFIG.read_text(), args.scale, args.keyboard == 'on')
    # Keep the first settings so the local adjustment can be undone.
    backup = CONFIG.with_suffix('.conf.before-properties')
    if not backup.exists():
        backup.write_bytes(CONFIG.read_bytes())
        backup.chmod(0o644)
    temporary = CONFIG.with_suffix('.conf.new')
    temporary.write_text(result)
    temporary.chmod(0o644)
    temporary.replace(CONFIG)

if __name__ == '__main__':
    main()
