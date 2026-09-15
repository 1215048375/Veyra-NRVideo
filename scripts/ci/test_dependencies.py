"""Offline tests using synthetic headers; no SDK or secret is committed."""
import base64
import gzip
import hashlib
import json
import unittest
from unittest.mock import patch

import dependencies as deps


class SecretTests(unittest.TestCase):
    def setUp(self):
        self.headers = {'one.h': b'synthetic header one', 'two.h': b'synthetic header two'}
        hashes = {name: hashlib.sha256(data).hexdigest() for name, data in self.headers.items()}
        self.lock = patch.dict(deps.LOCK, {'nvof': {'headers': hashes}})
        self.lock.start()
        self.addCleanup(self.lock.stop)
        self.secret = self.encode(self.headers)

    @staticmethod
    def encode(headers):
        raw = json.dumps({name: base64.b64encode(data).decode() for name, data in headers.items()}).encode()
        return base64.b64encode(gzip.compress(raw, mtime=0)).decode()

    def test_original_and_editor_whitespace(self):
        for value in (self.secret, '\ufeff' + self.secret,
                      ' \r\n' + '\r\n\t '.join(self.secret[i:i+16] for i in range(0, len(self.secret), 16)) + '\n'):
            with self.subTest(format_length=len(value)):
                self.assertEqual(deps.decode_headers(value), self.headers)

    def test_trailing_content_and_duplicate_are_not_silently_discarded(self):
        # Both cases previously surfaced only the opaque padding exception.
        for value in (self.secret + 'EXTRA', self.secret * 2, '"' + self.secret + '"', self.secret[:-1]):
            with self.subTest(length=len(value)):
                with self.assertRaises(ValueError) as error:
                    deps.decode_headers(value)
                self.assertNotIn(self.secret, str(error.exception))

    def test_changed_header_rejected_after_whitespace_normalization(self):
        value = self.encode(dict(self.headers, **{'one.h': b'changed'}))
        with self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
            deps.decode_headers('\n' + value + '\n')

    def test_extra_header_rejected(self):
        with self.assertRaisesRegex(ValueError, 'exactly the two'):
            deps.decode_headers(self.encode(dict(self.headers, **{'extra.h': b'extra'})))

    def test_encoded_and_decompressed_size_limits(self):
        for value in (' ' * 48001, base64.b64encode(gzip.compress(b'x' * 100001)).decode()):
            with self.assertRaises(ValueError):
                deps.decode_headers(value)


if __name__ == '__main__':
    unittest.main()
