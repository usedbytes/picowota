#!/usr/bin/env python3
# Derived from pad_checksum in the Pico SDK, which carries the following
# LICENSE.txt:
# Copyright 2020 (c) 2020 Raspberry Pi (Trading) Ltd.
# 
# Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
# following conditions are met:
# 
# 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
#    disclaimer.
# 
# 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
#    disclaimer in the documentation and/or other materials provided with the distribution.
# 
# 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products
#    derived from this software without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
# INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import argparse
import binascii
import struct
import sys

def any_int(x):
    try:
        return int(x, 0)
    except:
        raise argparse.ArgumentTypeError("expected an integer, not '{!r}'".format(x))

parser = argparse.ArgumentParser()
parser.add_argument("ifile", help="Input application binary (binary)")
parser.add_argument("ofile", help="Output header file (binary)")
parser.add_argument("-a", "--addr", help="Load address of the application image",
                    type=any_int, default=0x10004000)
parser.add_argument("-m", "--map", help="Map file to scan for application image section")
parser.add_argument("-s", "--section", help="Section name to look for in map file",
                    default=".app_bin")
args = parser.parse_args()

try:
    idata = open(args.ifile, "rb").read()
except:
    sys.exit("Could not open input file '{}'".format(args.ifile))

if args.map:
    vtor = None
    with open(args.map) as mapfile:
        for line in mapfile:
            if line.startswith(args.section):
                parts = line.split()
                if len(parts)>1:
                    vtor = int(parts[1],0)
                    print('found address 0x%x for %s in %s'%(vtor,args.section,args.map))
                    break

    if vtor is None:
        sys.exit("Could not find section {} in {}".format(args.section,args.map))
else:
    vtor = args.addr

size = len(idata)
crc = binascii.crc32(idata)

odata = vtor.to_bytes(4, byteorder='little') + size.to_bytes(4, byteorder='little') + crc.to_bytes(4, byteorder='little')

try:
    with open(args.ofile, "wb") as ofile:
        ofile.write(odata)
except:
    sys.exit("Could not open output file '{}'".format(args.ofile))
