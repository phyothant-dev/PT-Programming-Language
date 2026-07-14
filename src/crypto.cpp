#include "crypto.h"
#include <cstring>
#include <cstdio>
#include <random>

static const unsigned char base64Table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string cryptoBase64Encode(const std::string& input) {
  std::string out;
  out.reserve(((input.size() + 2) / 3) * 4);
  unsigned char bytes3[3];
  unsigned char bytes4[4];
  int i = 0, j = 0;
  int len = (int)input.size();
  while (len--) {
    bytes3[i++] = (unsigned char)input[j++];
    if (i == 3) {
      bytes4[0] = (bytes3[0] & 0xfc) >> 2;
      bytes4[1] = ((bytes3[0] & 0x03) << 4) | ((bytes3[1] & 0xf0) >> 4);
      bytes4[2] = ((bytes3[1] & 0x0f) << 2) | ((bytes3[2] & 0xc0) >> 6);
      bytes4[3] = bytes3[2] & 0x3f;
      for (i = 0; i < 4; i++) out += (char)base64Table[bytes4[i]];
      i = 0;
    }
  }
  if (i) {
    for (j = i; j < 3; j++) bytes3[j] = '\0';
    bytes4[0] = (bytes3[0] & 0xfc) >> 2;
    bytes4[1] = ((bytes3[0] & 0x03) << 4) | ((bytes3[1] & 0xf0) >> 4);
    bytes4[2] = ((bytes3[1] & 0x0f) << 2) | ((bytes3[2] & 0xc0) >> 6);
    for (j = 0; j < i + 1; j++) out += (char)base64Table[bytes4[j]];
    while (i++ < 3) out += '=';
  }
  return out;
}

std::string cryptoBase64Decode(const std::string& input) {
  static const int decodeTable[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
  };
  std::string out;
  out.reserve(input.size() * 3 / 4);
  int val = 0, bits = -8;
  for (char c : input) {
    if (c == '=') break;
    unsigned char uc = (unsigned char)c;
    int d = decodeTable[uc];
    if (d < 0) continue;
    val = (val << 6) | d;
    bits += 6;
    if (bits >= 0) {
      out += (char)((val >> bits) & 0xff);
      bits -= 8;
    }
  }
  return out;
}

struct SHA256State {
  uint32_t h[8];
  uint64_t totalLen;
  unsigned char buf[64];
  size_t bufLen;
};

static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static const uint32_t K[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256Transform(SHA256State& s, const unsigned char* block) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++)
    w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) | ((uint32_t)block[i*4+2] << 8) | block[i*4+3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
    uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
    w[i] = w[i-16] + s0 + w[i-7] + s1;
  }
  uint32_t a=s.h[0], b=s.h[1], c=s.h[2], d=s.h[3], e=s.h[4], f=s.h[5], g=s.h[6], h=s.h[7];
  for (int i = 0; i < 64; i++) {
    uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t temp1 = h + S1 + ch + K[i] + w[i];
    uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = S0 + maj;
    h = g; g = f; f = e; e = d + temp1;
    d = c; c = b; b = a; a = temp1 + temp2;
  }
  s.h[0] += a; s.h[1] += b; s.h[2] += c; s.h[3] += d;
  s.h[4] += e; s.h[5] += f; s.h[6] += g; s.h[7] += h;
}

static void sha256Init(SHA256State& s) {
  s.h[0]=0x6a09e667; s.h[1]=0xbb67ae85; s.h[2]=0x3c6ef372; s.h[3]=0xa54ff53a;
  s.h[4]=0x510e527f; s.h[5]=0x9b05688c; s.h[6]=0x1f83d9ab; s.h[7]=0x5be0cd19;
  s.totalLen = 0; s.bufLen = 0;
}

static void sha256Update(SHA256State& s, const unsigned char* data, size_t len) {
  size_t i = 0;
  s.totalLen += len;
  if (s.bufLen) {
    while (i < len && s.bufLen < 64) s.buf[s.bufLen++] = data[i++];
    if (s.bufLen == 64) { sha256Transform(s, s.buf); s.bufLen = 0; }
  }
  while (i + 64 <= len) { sha256Transform(s, data + i); i += 64; }
  while (i < len) s.buf[s.bufLen++] = data[i++];
}

static std::string sha256Final(SHA256State& s) {
  uint64_t totalBits = s.totalLen * 8;
  s.buf[s.bufLen++] = 0x80;
  if (s.bufLen > 56) { while (s.bufLen < 64) s.buf[s.bufLen++] = 0; sha256Transform(s, s.buf); s.bufLen = 0; }
  while (s.bufLen < 56) s.buf[s.bufLen++] = 0;
  for (int i = 7; i >= 0; i--) s.buf[s.bufLen++] = (unsigned char)(totalBits >> (i * 8));
  sha256Transform(s, s.buf);
  char hex[65];
  for (int i = 0; i < 8; i++) snprintf(hex + i*8, 9, "%08x", s.h[i]);
  return std::string(hex, 64);
}

std::string cryptoSha256(const std::string& input) {
  SHA256State s;
  sha256Init(s);
  sha256Update(s, (const unsigned char*)input.data(), input.size());
  return sha256Final(s);
}

struct MD5State {
  uint32_t h[4];
  uint64_t totalLen;
  unsigned char buf[64];
  size_t bufLen;
};

static const uint32_t MD5_T[64] = {
  0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
  0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
  0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
  0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
  0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
  0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
  0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
  0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
};

static const int MD5_S[64] = {
  7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
  5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
  4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
  6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
};

static const int MD5_G[64] = {
  0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
  1,6,11,0,5,10,15,4,9,14,3,8,13,2,7,12,
  5,8,11,14,1,4,7,10,13,0,3,6,9,12,15,2,
  0,7,14,5,12,3,10,1,8,15,6,13,4,11,2,9
};

static void md5Transform(MD5State& s, const unsigned char* block) {
  uint32_t M[16];
  for (int i = 0; i < 16; i++)
    M[i] = (uint32_t)block[i*4] | ((uint32_t)block[i*4+1] << 8) | ((uint32_t)block[i*4+2] << 16) | ((uint32_t)block[i*4+3] << 24);
  uint32_t a = s.h[0], b = s.h[1], c = s.h[2], d = s.h[3];
  for (int i = 0; i < 64; i++) {
    uint32_t F;
    if (i < 16) F = (b & c) | (~b & d);
    else if (i < 32) F = (d & b) | (~d & c);
    else if (i < 48) F = b ^ c ^ d;
    else F = c ^ (b | ~d);
    F = F + a + MD5_T[i] + M[MD5_G[i]];
    a = d; d = c; c = b; b = b + ((F << MD5_S[i]) | (F >> (32 - MD5_S[i])));
  }
  s.h[0] += a; s.h[1] += b; s.h[2] += c; s.h[3] += d;
}

static void md5Init(MD5State& s) {
  s.h[0]=0x67452301; s.h[1]=0xefcdab89; s.h[2]=0x98badcfe; s.h[3]=0x10325476;
  s.totalLen = 0; s.bufLen = 0;
}

static void md5Update(MD5State& s, const unsigned char* data, size_t len) {
  size_t i = 0;
  s.totalLen += len;
  if (s.bufLen) {
    while (i < len && s.bufLen < 64) s.buf[s.bufLen++] = data[i++];
    if (s.bufLen == 64) { md5Transform(s, s.buf); s.bufLen = 0; }
  }
  while (i + 64 <= len) { md5Transform(s, data + i); i += 64; }
  while (i < len) s.buf[s.bufLen++] = data[i++];
}

static std::string md5Final(MD5State& s) {
  uint64_t totalBits = s.totalLen * 8;
  s.buf[s.bufLen++] = 0x80;
  if (s.bufLen > 56) { while (s.bufLen < 64) s.buf[s.bufLen++] = 0; md5Transform(s, s.buf); s.bufLen = 0; }
  while (s.bufLen < 56) s.buf[s.bufLen++] = 0;
  for (int i = 0; i < 8; i++) s.buf[s.bufLen++] = (unsigned char)(totalBits >> (i * 8));
  md5Transform(s, s.buf);
  unsigned char digest[16];
  memcpy(digest, &s.h[0], 4);
  memcpy(digest + 4, &s.h[1], 4);
  memcpy(digest + 8, &s.h[2], 4);
  memcpy(digest + 12, &s.h[3], 4);
  char hex[33];
  for (int i = 0; i < 16; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
  return std::string(hex, 32);
}

std::string cryptoMd5(const std::string& input) {
  MD5State s;
  md5Init(s);
  md5Update(s, (const unsigned char*)input.data(), input.size());
  return md5Final(s);
}

std::string cryptoUuid() {
  static std::mt19937 rng(std::random_device{}());
  static std::uniform_int_distribution<int> dist(0, 255);
  unsigned char bytes[16];
  for (int i = 0; i < 16; i++) bytes[i] = (unsigned char)dist(rng);
  bytes[6] = (bytes[6] & 0x0f) | 0x40;
  bytes[8] = (bytes[8] & 0x3f) | 0x80;
  char uuid[37];
  snprintf(uuid, sizeof(uuid), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
    bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6],bytes[7],
    bytes[8],bytes[9],bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);
  return std::string(uuid, 36);
}
