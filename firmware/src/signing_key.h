#pragma once

// Public half of the release signing key. The private half is the GitHub secret SIGNING_KEY;
// releases carry an RSA-2048 / SHA-256 signature that selfupdate.cpp checks against this.
// In RAM, not PROGMEM: BearSSL reads the PEM byte by byte.
static const char kSigningKey[] = R"key(
-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAp/pqfFzLzwVbdzqcLMin
O+5PUO2WALjTk1tmGMtyB+vDwYf8NTC7uRWY38G9k2MDB91mgvzk18QEe8otFk1l
Ux+EgLVz7uxOwsT1GtozaRKjg2Ky59BLUwJIsE2V07A5vs1YPCqgeDWCDOFfEiH1
5DQSeY9RKXen8g+VNP9jzQLHLT+l0aqSnCPTPtgPhHoo113zJvftcwTmaw1iK6uy
96uqJhM88i2o2/r6jNmFNS6+NZ5maU7KtNpGqGGMXFbEWC+qt/QldGSrbOQLKoZl
F25yjc6RjW8pB6ZtnP+p3AZe6+NCTPbkJ+gSbEgeRh4FLEqfV0GHikV5l4zZr1/4
eQIDAQAB
-----END PUBLIC KEY-----
)key";
