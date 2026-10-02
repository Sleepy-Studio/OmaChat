#!/usr/bin/env python3
"""Independent host libopus reference for Android interoperability tests.
Uses the installed desktop library, not Android JNI or the vendored build.
"""
import base64
import ctypes as c
import ctypes.util
import math

RATE, SAMPLES = 48000, 960

def library():
    lib = c.CDLL(ctypes.util.find_library('opus'))
    lib.opus_encoder_create.argtypes = [c.c_int, c.c_int, c.c_int, c.POINTER(c.c_int)]
    lib.opus_encoder_create.restype = c.c_void_p
    lib.opus_decoder_create.argtypes = [c.c_int, c.c_int, c.POINTER(c.c_int)]
    lib.opus_decoder_create.restype = c.c_void_p
    lib.opus_encode_float.argtypes = [c.c_void_p, c.POINTER(c.c_float), c.c_int, c.POINTER(c.c_ubyte), c.c_int]
    lib.opus_decode_float.argtypes = [c.c_void_p, c.POINTER(c.c_ubyte), c.c_int, c.POINTER(c.c_float), c.c_int, c.c_int]
    lib.opus_encoder_destroy.argtypes = [c.c_void_p]
    lib.opus_decoder_destroy.argtypes = [c.c_void_p]
    return lib

def reference_packet():
    lib, error = library(), c.c_int()
    encoder = lib.opus_encoder_create(RATE, 1, 2048, c.byref(error))
    assert encoder and error.value == 0
    try:
        pcm = (c.c_float * SAMPLES)(*[.3 * math.sin(2 * math.pi * 440 * i / RATE) for i in range(SAMPLES)])
        out = (c.c_ubyte * 1275)()
        n = lib.opus_encode_float(encoder, pcm, SAMPLES, out, 1275)
        assert 0 < n <= 1275
        return bytes(out[:n])
    finally:
        lib.opus_encoder_destroy(encoder)

def verify_android_packet(encoded):
    packet = base64.b64decode(encoded, validate=True)
    assert 0 < len(packet) <= 1275
    lib, error = library(), c.c_int()
    decoder = lib.opus_decoder_create(RATE, 1, c.byref(error))
    assert decoder and error.value == 0
    try:
        out = (c.c_float * SAMPLES)()
        raw = (c.c_ubyte * len(packet)).from_buffer_copy(packet)
        assert lib.opus_decode_float(decoder, raw, len(packet), out, SAMPLES, 0) == SAMPLES
        # Encoder delay is permitted. Energy + dominant frequency verifies PCM meaning.
        energy = sum(x*x for x in out) / SAMPLES
        assert .005 < energy < .2, energy
        def spectral(f):
            return abs(sum(x * complex(math.cos(2*math.pi*f*i/RATE), math.sin(2*math.pi*f*i/RATE)) for i, x in enumerate(out)))
        assert spectral(440) > 3 * max(spectral(200), spectral(1000), spectral(2000))
    finally:
        lib.opus_decoder_destroy(decoder)
