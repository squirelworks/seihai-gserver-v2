#!/usr/bin/env python3
# Seihai project, 2026-11-21: see CHANGES-SEIHAI.md.
"""Ephemeral OpenSSL oracle for ConnectorCipher; destination must be outside source."""
import argparse
import os
from pathlib import Path
import subprocess


def run(*args, data=None):
    return subprocess.run(['openssl', *map(str, args)], input=data, check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    parser.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    root = args.directory.resolve()
    source = Path(__file__).resolve().parents[2]
    if root == source or source in root.parents:
        parser.error('ephemeral private keys must stay outside the source checkout')
    plain = bytes(range(1, 17))
    if args.verify:
        actual = run('pkeyutl', '-decrypt', '-inkey', root/'server.pem',
                     '-pkeyopt', 'rsa_padding_mode:pkcs1', '-in', root/'server-output.rsa')
        if actual != plain:
            raise SystemExit('independent OpenSSL decrypt mismatch')
        print('Independent OpenSSL decrypt passed')
        return
    root.mkdir(mode=0o700, parents=True, exist_ok=False)
    run('genrsa', '-traditional', '-out', root/'server.pem', '2048')
    os.chmod(root/'server.pem', 0o600)
    run('rsa', '-in', root/'server.pem', '-pubout', '-out', root/'server.pub.pem')
    def encrypt(name, encoded, padding):
        result = run('pkeyutl', '-encrypt', '-pubin', '-inkey', root/'server.pub.pem',
                     '-pkeyopt', 'rsa_padding_mode:'+padding, data=encoded)
        (root/name).write_bytes(result)
    encrypt('client-key.rsa', plain, 'pkcs1')
    encrypt('wrong-type.rsa', b'\0\1'+b'\xff'*237+b'\0'+plain, 'none')
    encrypt('short-padding.rsa', b'\0\2'+b'X'*7+b'\0'+b'X'*246, 'none')
    encrypt('missing-delimiter.rsa', b'\0\2'+b'X'*254, 'none')
    print('Ephemeral RSA fixtures ready outside source')


if __name__ == '__main__':
    main()
