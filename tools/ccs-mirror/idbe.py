"""The icon of a title, made here instead of found.

The Download Management fetches the icon of a title before it takes the
title at all, so a title whose icon Nintendo dropped fails with 105-1017
and nothing is downloaded. The format is written down (see the README), so
an icon that the console accepts can be made for any title ID.

The AES of the format is 128 bit CBC. It is written out below instead of
taken from a library so that the mirror keeps running on a machine with
nothing but python3 on it, the way its README sets one up. The S-box is
generated rather than typed out, which is one way of not getting it wrong,
and the whole thing is checked against openssl by the test next to it.
"""

import hashlib
import struct

# nn_idbe.rpl carries these in one 0x50 byte block: the IV, then four keys
# the byte at 0x1 of a file picks between.
IDBE_IV = bytes.fromhex("a46987ae47d82bb4fa8abc0450285fa4")
IDBE_KEYS = (
    bytes.fromhex("4ab9a40e146975a84bb1b4f3ecefc47b"),
    bytes.fromhex("90a0bb1e0e864ae87d13a6a03d28c9b8"),
    bytes.fromhex("ffbb57c14e98ec6975b384fcf40786b5"),
    bytes.fromhex("80923799b41f36a6a75fb8b48c95f66f"),
)

# The decrypted icon: the hash of everything behind it, the title ID, the
# magic, sixteen blocks of names and then the picture.
IDBE_TID = 0x20
IDBE_VERSION = 0x28
IDBE_MAGIC = 0x40
IDBE_STRINGS = 0x50
IDBE_STRING_BLOCKS = 16
IDBE_STRING_SIZE = 0x200
IDBE_TGA = 0x2050
# 18 bytes of header, 128 * 128 pixels of four bytes, 26 bytes of footer.
IDBE_TGA_SIZE = 0x1002C
IDBE_SIZE = 0x12080

ICON_SIDE = 128


def _build_sbox():
    sbox = [0] * 256
    p = q = 1
    while True:
        p = (p ^ ((p << 1) & 0xFF) ^ (0x1B if p & 0x80 else 0)) & 0xFF
        q ^= (q << 1) & 0xFF
        q ^= (q << 2) & 0xFF
        q ^= (q << 4) & 0xFF
        if q & 0x80:
            q ^= 0x09
        q &= 0xFF
        x = q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4))
        sbox[p] = (x ^ 0x63) & 0xFF
        if p == 1:
            break

    sbox[0] = 0x63
    return bytes(sbox)


SBOX = _build_sbox()
RCON = (0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36)


def _xtime(a):
    a <<= 1
    return (a ^ 0x1B) & 0xFF if a & 0x100 else a


def _expand_key(key):
    words = [list(key[i * 4:(i + 1) * 4]) for i in range(4)]
    for i in range(4, 44):
        word = list(words[i - 1])
        if i % 4 == 0:
            word = word[1:] + word[:1]
            word = [SBOX[b] for b in word]
            word[0] ^= RCON[i // 4 - 1]

        words.append([a ^ b for a, b in zip(words[i - 4], word)])

    return [bytes(b for word in words[r * 4:(r + 1) * 4] for b in word) for r in range(11)]


def _encrypt_block(round_keys, block):
    state = [b ^ k for b, k in zip(block, round_keys[0])]
    for rnd in range(1, 11):
        state = [SBOX[b] for b in state]
        # ShiftRows: the state is column major, row r moves left by r.
        state = [state[(i + (i % 4) * 4) % 16] for i in range(16)]
        if rnd != 10:
            mixed = []
            for c in range(4):
                col = state[c * 4:(c + 1) * 4]
                t = col[0] ^ col[1] ^ col[2] ^ col[3]
                mixed += [col[i] ^ t ^ _xtime(col[i] ^ col[(i + 1) % 4]) for i in range(4)]

            state = mixed

        state = [b ^ k for b, k in zip(state, round_keys[rnd])]

    return bytes(state)


def aes128_cbc_encrypt(key, iv, data):
    """CBC without padding: the caller brings a multiple of the block size."""
    if len(data) % 16:
        raise ValueError("data is not a multiple of the AES block size")

    round_keys = _expand_key(key)
    out = bytearray()
    previous = iv
    for off in range(0, len(data), 16):
        block = bytes(a ^ b for a, b in zip(data[off:off + 16], previous))
        previous = _encrypt_block(round_keys, block)
        out += previous

    return bytes(out)


def make_tga(side=ICON_SIDE):
    """The picture of the icon: a plain grey square, uncompressed."""
    header = bytes((0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0)) + struct.pack("<HHBB", side, side, 32, 8)
    # Uncompressed BGRA, opaque.
    pixels = bytes((0x40, 0x40, 0x40, 0xFF)) * (side * side)
    footer = bytes(8) + b"TRUEVISION-XFILE." + bytes(1)
    return header + pixels + footer


def _put_string(buf, off, text, size):
    # The names are UTF-16 big endian, cut to the room they have and padded
    # with the zeros the buffer already holds.
    encoded = text.encode("utf-16-be")[:size - 2]
    buf[off:off + len(encoded)] = encoded


def make_plain(tid, name=None, version=0):
    """The icon before it is encrypted, which is what the console reads."""
    if name is None:
        name = "%016X" % tid

    plain = bytearray(IDBE_SIZE)
    struct.pack_into(">Q", plain, IDBE_TID, tid)
    struct.pack_into(">I", plain, IDBE_VERSION, version)
    # The magic the format carries, four bytes of 0xC0.
    plain[IDBE_MAGIC:IDBE_MAGIC + 4] = b"\xC0\xC0\xC0\xC0"

    for block in range(IDBE_STRING_BLOCKS):
        off = IDBE_STRINGS + (block * IDBE_STRING_SIZE)
        _put_string(plain, off, name, 0x80)
        _put_string(plain, off + 0x80, name, 0x100)
        _put_string(plain, off + 0x180, "NUSspli", 0x80)

    tga = make_tga()
    if len(tga) != IDBE_TGA_SIZE:
        raise ValueError("the picture is %#x bytes, not %#x" % (len(tga), IDBE_TGA_SIZE))

    plain[IDBE_TGA:IDBE_TGA + IDBE_TGA_SIZE] = tga

    # The hash covers everything behind itself and is the first thing in.
    plain[0:0x20] = hashlib.sha256(bytes(plain[0x20:])).digest()
    return bytes(plain)


def make_idbe(tid, name=None, version=0, key_index=0):
    """A whole .idbe for a title, encrypted the way the console reads it."""
    plain = make_plain(tid, name, version)
    return bytes((0, key_index)) + aes128_cbc_encrypt(IDBE_KEYS[key_index], IDBE_IV, plain)


def _selftest():
    """Enough to see that the AES and the layout are what they should be."""
    failures = 0

    def check(label, got, want):
        nonlocal failures
        if got != want:
            failures += 1
            print("FAIL %s: %r != %r" % (label, got, want))
        else:
            print("ok   %s" % label)

    check("S-box is a permutation", len(set(SBOX)), 256)
    check("S-box of 0x53", SBOX[0x53], 0xED)
    # The single block of FIPS-197 appendix B, which CBC with a zero IV is.
    check("FIPS-197 vector",
          aes128_cbc_encrypt(bytes.fromhex("000102030405060708090a0b0c0d0e0f"), bytes(16),
                             bytes.fromhex("00112233445566778899aabbccddeeff")).hex(),
          "69c4e0d86a7b0430d8cdb78070b4c55a")

    tid = 0x000500001010EC00
    plain = make_plain(tid, "Test Title")
    check("size", len(plain), IDBE_SIZE)
    check("hash covers the rest", plain[:0x20], hashlib.sha256(plain[0x20:]).digest())
    check("title ID", struct.unpack_from(">Q", plain, IDBE_TID)[0], tid)
    check("magic", plain[IDBE_MAGIC:IDBE_MAGIC + 4], b"\xC0\xC0\xC0\xC0")
    check("name", plain[IDBE_STRINGS:IDBE_STRINGS + 20].decode("utf-16-be"), "Test Title")
    check("picture ends where the format says", IDBE_TGA + IDBE_TGA_SIZE, 0x1207C)
    check("picture is a TGA", plain[IDBE_TGA + 2], 2)
    check("the whole file encrypts", len(make_idbe(tid)), 2 + IDBE_SIZE)

    print("all ok" if failures == 0 else "%d failed" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(_selftest())
