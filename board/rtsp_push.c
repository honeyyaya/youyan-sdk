/*
 * 板上的轻量推流。从本机 RTSP 拉一路，再用 TCP 原样推到另一台 RTSP 服务器。
 * 不重新编码，不经过代理。给国内能直连的 MediaMTX 用。
 *
 *   ./rtsp_push rtsp://127.0.0.1:554/live.h265 rtsp://publish:密码@服务器:8554/cam
 *
 * 断了会隔 3 秒再连。目标地址要写成 rtsp://用户:密码@主机:端口/路径。
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MAX_TRACKS 4
#define HDR_CAP 8192
#define SDP_CAP 4096
#define PKT_CAP (256 * 1024)

struct url {
    char host[128];
    char path[256];
    char user[64];
    char pass[64];
    char base[320];
    int port;
};

struct conn {
    int fd;
    int cseq;
    int nc;
    int has_auth;
    int basic;
    int qop;
    char session[80];
    char realm[96];
    char nonce[96];
    char opaque[96];
    char user[64];
    char pass[64];
    unsigned char in[8192];
    int in_len;
    int in_pos;
};

struct track {
    char src[320];
    char dst[320];
    int src_ch;
    int dst_ch;
};

static const uint32_t md5_k[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static const int md5_s[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

struct md5 {
    uint32_t s[4];
    uint64_t bits;
    unsigned char buf[64];
    int n;
};

static uint32_t rol(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

static void md5_block(uint32_t st[4], const unsigned char *b)
{
    uint32_t m[16];
    uint32_t a, b1, c, d;
    int i;

    for (i = 0; i < 16; i++) {
        m[i] = (uint32_t)b[i * 4] | ((uint32_t)b[i * 4 + 1] << 8) |
               ((uint32_t)b[i * 4 + 2] << 16) | ((uint32_t)b[i * 4 + 3] << 24);
    }
    a = st[0];
    b1 = st[1];
    c = st[2];
    d = st[3];
    for (i = 0; i < 64; i++) {
        uint32_t f, g, t;
        if (i < 16) {
            f = (b1 & c) | ((~b1) & d);
            g = (uint32_t)i;
        } else if (i < 32) {
            f = (d & b1) | ((~d) & c);
            g = (uint32_t)((5 * i + 1) % 16);
        } else if (i < 48) {
            f = b1 ^ c ^ d;
            g = (uint32_t)((3 * i + 5) % 16);
        } else {
            f = c ^ (b1 | (~d));
            g = (uint32_t)((7 * i) % 16);
        }
        t = d;
        d = c;
        c = b1;
        b1 = b1 + rol(a + f + md5_k[i] + m[g], md5_s[i]);
        a = t;
    }
    st[0] += a;
    st[1] += b1;
    st[2] += c;
    st[3] += d;
}

static void md5_init(struct md5 *m)
{
    m->s[0] = 0x67452301;
    m->s[1] = 0xefcdab89;
    m->s[2] = 0x98badcfe;
    m->s[3] = 0x10325476;
    m->bits = 0;
    m->n = 0;
}

static void md5_update(struct md5 *m, const void *data, size_t len)
{
    const unsigned char *p = data;
    m->bits += (uint64_t)len * 8;
    while (len > 0) {
        int k = 64 - m->n;
        if ((size_t)k > len) {
            k = (int)len;
        }
        memcpy(m->buf + m->n, p, (size_t)k);
        m->n += k;
        p += k;
        len -= (size_t)k;
        if (m->n == 64) {
            md5_block(m->s, m->buf);
            m->n = 0;
        }
    }
}

static void md5_hex(struct md5 *m, char out[33])
{
    unsigned char bits[8];
    unsigned char dig[16];
    uint64_t n = m->bits;
    int i;

    m->buf[m->n++] = 0x80;
    if (m->n > 56) {
        while (m->n < 64) {
            m->buf[m->n++] = 0;
        }
        md5_block(m->s, m->buf);
        m->n = 0;
    }
    while (m->n < 56) {
        m->buf[m->n++] = 0;
    }
    for (i = 0; i < 8; i++) {
        bits[i] = (unsigned char)(n & 0xff);
        n >>= 8;
    }
    memcpy(m->buf + 56, bits, 8);
    md5_block(m->s, m->buf);
    for (i = 0; i < 4; i++) {
        dig[i * 4] = (unsigned char)(m->s[i] & 0xff);
        dig[i * 4 + 1] = (unsigned char)((m->s[i] >> 8) & 0xff);
        dig[i * 4 + 2] = (unsigned char)((m->s[i] >> 16) & 0xff);
        dig[i * 4 + 3] = (unsigned char)((m->s[i] >> 24) & 0xff);
    }
    for (i = 0; i < 16; i++) {
        sprintf(out + i * 2, "%02x", dig[i]);
    }
}

static void md5_str(const char *text, char out[33])
{
    struct md5 m;
    md5_init(&m);
    md5_update(&m, text, strlen(text));
    md5_hex(&m, out);
}

static int parse_url(const char *text, struct url *u)
{
    const char *p;
    const char *at;
    const char *slash;
    const char *colon;
    char hostport[160];

    memset(u, 0, sizeof(*u));
    if (strncmp(text, "rtsp://", 7) != 0) {
        return -1;
    }
    p = text + 7;
    slash = strchr(p, '/');
    at = NULL;
    if (slash) {
        const char *s;
        for (s = p; s < slash; s++) {
            if (*s == '@') {
                at = s;
            }
        }
    } else {
        at = strrchr(p, '@');
    }
    if (at) {
        const char *user = p;
        const char *sep = memchr(user, ':', (size_t)(at - user));
        if (!sep) {
            return -1;
        }
        if (sep - user >= (int)sizeof(u->user) || at - (sep + 1) >= (int)sizeof(u->pass)) {
            return -1;
        }
        memcpy(u->user, user, (size_t)(sep - user));
        memcpy(u->pass, sep + 1, (size_t)(at - (sep + 1)));
        p = at + 1;
    }
    if (slash) {
        if (slash - p >= (int)sizeof(hostport)) {
            return -1;
        }
        memcpy(hostport, p, (size_t)(slash - p));
        hostport[slash - p] = 0;
        snprintf(u->path, sizeof(u->path), "%s", slash);
    } else {
        snprintf(hostport, sizeof(hostport), "%s", p);
        snprintf(u->path, sizeof(u->path), "/");
    }
    colon = strrchr(hostport, ':');
    if (colon) {
        if (colon - hostport >= (int)sizeof(u->host)) {
            return -1;
        }
        memcpy(u->host, hostport, (size_t)(colon - hostport));
        u->port = atoi(colon + 1);
    } else {
        snprintf(u->host, sizeof(u->host), "%s", hostport);
        u->port = 554;
    }
    if (u->host[0] == 0 || u->port <= 0) {
        return -1;
    }
    snprintf(u->base, sizeof(u->base), "rtsp://%s:%d%s", u->host, u->port, u->path);
    return 0;
}

static void note(const char *s)
{
    if (s) {
        write(STDERR_FILENO, s, strlen(s));
    }
    write(STDERR_FILENO, "\n", 1);
}

static int tcp_connect(const char *host, int port)
{
    struct sockaddr_in addr;
    struct timeval tv;
    fd_set wfds;
    socklen_t elen;
    int fd;
    int flags;
    int err;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        return -1;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    flags = fcntl(fd, F_GETFL, 0);
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    if (select(fd + 1, NULL, &wfds, NULL, &tv) <= 0) {
        close(fd);
        return -1;
    }
    err = 0;
    elen = sizeof(err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) < 0 || err != 0) {
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, flags);
    tv.tv_sec = 15;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return fd;
}

static int send_all(int fd, const void *data, int len)
{
    const char *p = data;
    while (len > 0) {
        int n = (int)send(fd, p, (size_t)len, 0);
        if (n <= 0) {
            return -1;
        }
        p += n;
        len -= n;
    }
    return 0;
}

static int fill_in(struct conn *c)
{
    int n;
    if (c->in_pos > 0) {
        memmove(c->in, c->in + c->in_pos, (size_t)(c->in_len - c->in_pos));
        c->in_len -= c->in_pos;
        c->in_pos = 0;
    }
    if (c->in_len >= (int)sizeof(c->in)) {
        return 0;
    }
    n = (int)recv(c->fd, c->in + c->in_len, sizeof(c->in) - (size_t)c->in_len, 0);
    if (n <= 0) {
        return -1;
    }
    c->in_len += n;
    return n;
}

static int take(struct conn *c, unsigned char *dst, int n)
{
    int got = 0;
    while (got < n) {
        int have = c->in_len - c->in_pos;
        if (have > 0) {
            int k = n - got;
            if (k > have) {
                k = have;
            }
            memcpy(dst + got, c->in + c->in_pos, (size_t)k);
            c->in_pos += k;
            got += k;
        } else {
            int r = (int)recv(c->fd, dst + got, (size_t)(n - got), 0);
            if (r <= 0) {
                return -1;
            }
            got += r;
        }
    }
    return 0;
}

static int peek_byte(struct conn *c, unsigned char *ch)
{
    if (c->in_pos >= c->in_len && fill_in(c) < 0) {
        return -1;
    }
    *ch = c->in[c->in_pos];
    return 0;
}

static int header_value(const char *msg, const char *key, char *out, int cap)
{
    size_t klen = strlen(key);
    const char *p = msg;
    while (*p) {
        if (strncasecmp(p, key, klen) == 0 && p[klen] == ':') {
            int n = 0;
            p += klen + 1;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            while (*p && *p != '\r' && *p != '\n' && n + 1 < cap) {
                out[n++] = *p++;
            }
            out[n] = 0;
            return 0;
        }
        p = strchr(p, '\n');
        if (!p) {
            break;
        }
        p++;
    }
    return -1;
}

static int content_length(const char *msg)
{
    char v[32];
    if (header_value(msg, "Content-Length", v, sizeof(v)) < 0) {
        return 0;
    }
    return atoi(v);
}

static int status_code(const char *msg)
{
    const char *p = strchr(msg, ' ');
    if (!p) {
        return 0;
    }
    return atoi(p + 1);
}

static void quoted_param(const char *hdr, const char *key, char *out, int cap)
{
    char pat[32];
    const char *p;
    int n = 0;
    snprintf(pat, sizeof(pat), "%s=\"", key);
    p = strstr(hdr, pat);
    out[0] = 0;
    if (!p) {
        return;
    }
    p += strlen(pat);
    while (*p && *p != '"' && n + 1 < cap) {
        out[n++] = *p++;
    }
    out[n] = 0;
}

static void b64_encode(const unsigned char *in, int len, char *out)
{
    static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i;
    int n = 0;

    for (i = 0; i < len; i += 3) {
        unsigned int v = ((unsigned int)in[i]) << 16;
        if (i + 1 < len) {
            v |= ((unsigned int)in[i + 1]) << 8;
        }
        if (i + 2 < len) {
            v |= (unsigned int)in[i + 2];
        }
        out[n++] = tab[(v >> 18) & 63];
        out[n++] = tab[(v >> 12) & 63];
        out[n++] = (i + 1 < len) ? tab[(v >> 6) & 63] : '=';
        out[n++] = (i + 2 < len) ? tab[v & 63] : '=';
    }
    out[n] = 0;
}

static void read_auth(struct conn *c, const char *msg)
{
    char hdr[512];
    if (header_value(msg, "WWW-Authenticate", hdr, sizeof(hdr)) < 0) {
        return;
    }
    if (strncasecmp(hdr, "Basic", 5) == 0) {
        c->basic = 1;
        c->has_auth = 1;
        return;
    }
    quoted_param(hdr, "realm", c->realm, sizeof(c->realm));
    quoted_param(hdr, "nonce", c->nonce, sizeof(c->nonce));
    quoted_param(hdr, "opaque", c->opaque, sizeof(c->opaque));
    c->qop = strstr(hdr, "qop=") != NULL;
    c->has_auth = c->nonce[0] != 0;
}

static void build_auth(struct conn *c, const char *method, const char *uri, char *out, int cap)
{
    char ha1_in[256];
    char ha2_in[384];
    char resp_in[512];
    char ha1[33];
    char ha2[33];
    char resp[33];
    char cnonce[17];
    struct md5 m1;
    struct md5 m2;
    struct md5 m3;

    if (c->basic) {
        char raw[160];
        char enc[256];
        int n = snprintf(raw, sizeof(raw), "%s:%s", c->user, c->pass);
        if (n < 0 || n >= (int)sizeof(raw)) {
            out[0] = 0;
            return;
        }
        b64_encode((const unsigned char *)raw, n, enc);
        snprintf(out, cap, "Authorization: Basic %s\r\n", enc);
        return;
    }
    c->nc++;
    snprintf(cnonce, sizeof(cnonce), "%08x%08x", (unsigned)time(NULL), (unsigned)c->nc);
    snprintf(ha1_in, sizeof(ha1_in), "%s:%s:%s", c->user, c->realm, c->pass);
    snprintf(ha2_in, sizeof(ha2_in), "%s:%s", method, uri);
    md5_init(&m1);
    md5_update(&m1, ha1_in, strlen(ha1_in));
    md5_hex(&m1, ha1);
    md5_init(&m2);
    md5_update(&m2, ha2_in, strlen(ha2_in));
    md5_hex(&m2, ha2);
    if (c->qop) {
        snprintf(resp_in, sizeof(resp_in), "%s:%s:%08d:%s:auth:%s", ha1, c->nonce, c->nc, cnonce, ha2);
    } else {
        snprintf(resp_in, sizeof(resp_in), "%s:%s:%s", ha1, c->nonce, ha2);
    }
    md5_init(&m3);
    md5_update(&m3, resp_in, strlen(resp_in));
    md5_hex(&m3, resp);
    if (c->qop) {
        snprintf(out, cap,
                 "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", "
                 "response=\"%s\", algorithm=MD5, qop=auth, nc=%08d, cnonce=\"%s\"%s%s%s\r\n",
                 c->user, c->realm, c->nonce, uri, resp, c->nc, cnonce,
                 c->opaque[0] ? ", opaque=\"" : "",
                 c->opaque[0] ? c->opaque : "",
                 c->opaque[0] ? "\"" : "");
    } else {
        snprintf(out, cap,
                 "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", "
                 "response=\"%s\", algorithm=MD5%s%s%s\r\n",
                 c->user, c->realm, c->nonce, uri, resp,
                 c->opaque[0] ? ", opaque=\"" : "",
                 c->opaque[0] ? c->opaque : "",
                 c->opaque[0] ? "\"" : "");
    }
}

static int read_rtsp(struct conn *c, char *msg, int cap)
{
    int len = 0;
    int body;

    while (len + 1 < cap) {
        unsigned char ch;
        if (take(c, &ch, 1) < 0) {
            return -1;
        }
        msg[len++] = (char)ch;
        if (len >= 4 && memcmp(msg + len - 4, "\r\n\r\n", 4) == 0) {
            break;
        }
    }
    msg[len] = 0;
    body = content_length(msg);
    if (body < 0 || len + body >= cap) {
        return -1;
    }
    if (body > 0 && take(c, (unsigned char *)msg + len, body) < 0) {
        return -1;
    }
    len += body;
    msg[len] = 0;
    return len;
}

static int rtsp_call(struct conn *c, const char *method, const char *uri, const char *extra,
                     const char *body, char *resp, int resp_cap)
{
    char req[HDR_CAP + SDP_CAP];
    char auth[640];
    int attempt;

    for (attempt = 0; attempt < 2; attempt++) {
        int n;
        int code;
        auth[0] = 0;
        if (c->has_auth && c->user[0]) {
            build_auth(c, method, uri, auth, sizeof(auth));
        }
        n = snprintf(req, sizeof(req),
                     "%s %s RTSP/1.0\r\nCSeq: %d\r\nUser-Agent: rtsp_push\r\n%s%s%s%s%s\r\n%s",
                     method, uri, ++c->cseq,
                     c->session[0] ? "Session: " : "",
                     c->session[0] ? c->session : "",
                     c->session[0] ? "\r\n" : "",
                     auth,
                     extra ? extra : "",
                     body ? body : "");
        if (n < 0 || n >= (int)sizeof(req)) {
            return -1;
        }
        if (send_all(c->fd, req, n) < 0) {
            note("send failed");
            return -1;
        }
        if (read_rtsp(c, resp, resp_cap) < 0) {
            fprintf(stderr, "%s 没有回应 errno %d\n", method, errno);
            return -1;
        }
        code = status_code(resp);
        if (code == 401 && attempt == 0) {
            read_auth(c, resp);
            if (!c->has_auth) {
                note("401 without challenge");
                return -1;
            }
            continue;
        }
        if (code < 200 || code >= 300) {
            char shown[180];
            int k;
            int n = 0;
            fprintf(stderr, "%s -> %d\n", method, code);
            for (k = 0; resp[k] && n < (int)sizeof(shown) - 1; k++) {
                char ch = resp[k];
                if (ch == '\r' || ch == '\n') {
                    ch = '|';
                }
                shown[n++] = ch;
            }
            shown[n] = 0;
            note(shown);
            return -1;
        }
        {
            char sess[80];
            char *semi;
            if (header_value(resp, "Session", sess, sizeof(sess)) == 0) {
                semi = strchr(sess, ';');
                if (semi) {
                    *semi = 0;
                }
                snprintf(c->session, sizeof(c->session), "%s", sess);
            }
        }
        return 0;
    }
    return -1;
}

static int parse_interleaved(const char *msg)
{
    const char *p = strstr(msg, "interleaved=");
    if (!p) {
        return 0;
    }
    return atoi(p + 12);
}

static int collect_tracks(const char *sdp, const struct url *src, const struct url *dst, struct track *tracks)
{
    const char *p = sdp;
    int after_m = 0;
    int n = 0;

    while (*p && n < MAX_TRACKS) {
        const char *eol = strstr(p, "\r\n");
        int len = eol ? (int)(eol - p) : (int)strlen(p);
        if (len >= 2 && p[0] == 'm' && p[1] == '=') {
            after_m = 1;
        } else if (after_m && len > 10 && strncmp(p, "a=control:", 10) == 0) {
            char ctrl[320];
            int cl = len - 10;
            if (cl >= (int)sizeof(ctrl)) {
                cl = (int)sizeof(ctrl) - 1;
            }
            memcpy(ctrl, p + 10, (size_t)cl);
            ctrl[cl] = 0;
            if (strncmp(ctrl, "rtsp://", 7) == 0) {
                snprintf(tracks[n].src, sizeof(tracks[n].src), "%s", ctrl);
            } else if (ctrl[0] == '/') {
                snprintf(tracks[n].src, sizeof(tracks[n].src), "rtsp://%s:%d%s", src->host, src->port, ctrl);
            } else {
                snprintf(tracks[n].src, sizeof(tracks[n].src), "%s/%s", src->base, ctrl);
            }
            if (strncmp(ctrl, src->base, strlen(src->base)) == 0) {
                snprintf(tracks[n].dst, sizeof(tracks[n].dst), "%s%s", dst->base, ctrl + strlen(src->base));
            } else if (strncmp(ctrl, "rtsp://", 7) == 0) {
                const char *rel = strrchr(ctrl, '/');
                snprintf(tracks[n].dst, sizeof(tracks[n].dst), "%s%s", dst->base, rel ? rel : "");
            } else if (ctrl[0] == '/') {
                snprintf(tracks[n].dst, sizeof(tracks[n].dst), "rtsp://%s:%d%s", dst->host, dst->port, ctrl);
            } else {
                snprintf(tracks[n].dst, sizeof(tracks[n].dst), "%s/%s", dst->base, ctrl);
            }
            tracks[n].src_ch = n * 2;
            tracks[n].dst_ch = n * 2;
            n++;
            after_m = 0;
        }
        if (!eol) {
            break;
        }
        p = eol + 2;
    }
    return n;
}

static void rewrite_sdp(char *sdp, const char *src_base, const char *dst_base)
{
    char out[SDP_CAP];
    const char *p = sdp;
    size_t sl = strlen(src_base);
    size_t dl = strlen(dst_base);
    int n = 0;

    while (*p && n + 1 < (int)sizeof(out)) {
        if (strncmp(p, src_base, sl) == 0) {
            if (n + (int)dl >= (int)sizeof(out)) {
                break;
            }
            memcpy(out + n, dst_base, dl);
            n += (int)dl;
            p += sl;
        } else {
            out[n++] = *p++;
        }
    }
    out[n] = 0;
    snprintf(sdp, SDP_CAP, "%s", out);
}

static int map_channel(struct track *tracks, int ntracks, int ch)
{
    int i;
    for (i = 0; i < ntracks; i++) {
        if (ch == tracks[i].src_ch) {
            return tracks[i].dst_ch;
        }
        if (ch == tracks[i].src_ch + 1) {
            return tracks[i].dst_ch + 1;
        }
    }
    return ch;
}

static int reply_ok(struct conn *c, const char *req)
{
    char cseq[32];
    char resp[256];
    int n;
    if (header_value(req, "CSeq", cseq, sizeof(cseq)) < 0) {
        snprintf(cseq, sizeof(cseq), "1");
    }
    n = snprintf(resp, sizeof(resp), "RTSP/1.0 200 OK\r\nCSeq: %s\r\nSession: %s\r\n\r\n", cseq, c->session);
    return send_all(c->fd, resp, n);
}

static int relay(struct conn *src, struct conn *dst, struct track *tracks, int ntracks)
{
    unsigned char *pkt = malloc(PKT_CAP);
    int count = 0;
    if (!pkt) {
        return -1;
    }
    for (;;) {
        unsigned char kind;
        fd_set rfds;
        struct timeval tv;
        int src_ready = src->in_pos < src->in_len;
        int dst_ready = dst->in_pos < dst->in_len;
        int maxfd = src->fd > dst->fd ? src->fd : dst->fd;

        if (!src_ready && !dst_ready) {
            FD_ZERO(&rfds);
            FD_SET(src->fd, &rfds);
            FD_SET(dst->fd, &rfds);
            tv.tv_sec = 15;
            tv.tv_usec = 0;
            if (select(maxfd + 1, &rfds, NULL, NULL, &tv) <= 0) {
                free(pkt);
                return -1;
            }
            src_ready = FD_ISSET(src->fd, &rfds);
            dst_ready = FD_ISSET(dst->fd, &rfds);
        }
        if (src_ready) {
            if (peek_byte(src, &kind) < 0) {
                free(pkt);
                return -1;
            }
            if (kind == '$') {
                unsigned char hdr[4];
                int len;
                int ch;
                if (take(src, hdr, 4) < 0) {
                    free(pkt);
                    return -1;
                }
                len = (hdr[2] << 8) | hdr[3];
                if (len < 0 || len > PKT_CAP) {
                    free(pkt);
                    return -1;
                }
                if (len > 0 && take(src, pkt, len) < 0) {
                    free(pkt);
                    return -1;
                }
                ch = map_channel(tracks, ntracks, hdr[1]);
                hdr[1] = (unsigned char)ch;
                if (send_all(dst->fd, hdr, 4) < 0 || (len > 0 && send_all(dst->fd, pkt, len) < 0)) {
                    free(pkt);
                    return -1;
                }
                if (++count % 120 == 0) {
                    fprintf(stderr, "fwd %d\n", count);
                }
            } else {
                char msg[HDR_CAP];
                if (read_rtsp(src, msg, sizeof(msg)) < 0) {
                    free(pkt);
                    return -1;
                }
            }
        }
        if (dst_ready) {
            if (peek_byte(dst, &kind) < 0) {
                free(pkt);
                return -1;
            }
            if (kind == '$') {
                unsigned char hdr[4];
                int len;
                if (take(dst, hdr, 4) < 0) {
                    free(pkt);
                    return -1;
                }
                len = (hdr[2] << 8) | hdr[3];
                if (len < 0 || len > PKT_CAP || (len > 0 && take(dst, pkt, len) < 0)) {
                    free(pkt);
                    return -1;
                }
            } else {
                char msg[HDR_CAP];
                if (read_rtsp(dst, msg, sizeof(msg)) < 0) {
                    free(pkt);
                    return -1;
                }
                if (strncmp(msg, "RTSP/", 5) != 0 && reply_ok(dst, msg) < 0) {
                    free(pkt);
                    return -1;
                }
            }
        }
    }
}

static void conn_init(struct conn *c, int fd, const struct url *u)
{
    memset(c, 0, sizeof(*c));
    c->fd = fd;
    snprintf(c->user, sizeof(c->user), "%s", u->user);
    snprintf(c->pass, sizeof(c->pass), "%s", u->pass);
}

static int run_once(const char *src_text, const char *dst_text)
{
    struct url src_url;
    struct url dst_url;
    struct conn src;
    struct conn dst;
    struct track tracks[MAX_TRACKS];
    char resp[HDR_CAP];
    char sdp[SDP_CAP];
    char extra[128];
    int ntracks;
    int i;
    int src_fd = -1;
    int dst_fd = -1;
    const char *body;

    if (parse_url(src_text, &src_url) < 0 || parse_url(dst_text, &dst_url) < 0) {
        fprintf(stderr, "地址要写成 rtsp://主机:端口/路径\n");
        return -1;
    }
    note("begin");
    src_fd = tcp_connect(src_url.host, src_url.port);
    if (src_fd < 0) {
        fprintf(stderr, "连不上源 %s:%d\n", src_url.host, src_url.port);
        note("src connect failed");
        return -1;
    }
    note("src connected");
    conn_init(&src, src_fd, &src_url);
    if (rtsp_call(&src, "OPTIONS", src_url.base, NULL, NULL, resp, sizeof(resp)) < 0 ||
        rtsp_call(&src, "DESCRIBE", src_url.base, "Accept: application/sdp\r\n", NULL, resp, sizeof(resp)) < 0) {
        close(src_fd);
        return -1;
    }
    body = strstr(resp, "\r\n\r\n");
    if (!body) {
        note("describe has no header end");
        close(src_fd);
        return -1;
    }
    note("describe ok");
    snprintf(sdp, sizeof(sdp), "%s", body + 4);
    ntracks = collect_tracks(sdp, &src_url, &dst_url, tracks);
    if (ntracks <= 0) {
        fprintf(stderr, "SDP 里没有轨道\n");
        close(src_fd);
        return -1;
    }
    for (i = 0; i < ntracks; i++) {
        snprintf(extra, sizeof(extra), "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n", i * 2, i * 2 + 1);
        if (rtsp_call(&src, "SETUP", tracks[i].src, extra, NULL, resp, sizeof(resp)) < 0) {
            close(src_fd);
            return -1;
        }
        tracks[i].src_ch = parse_interleaved(resp);
    }
    note("src setup done");
    dst_fd = tcp_connect(dst_url.host, dst_url.port);
    if (dst_fd < 0) {
        fprintf(stderr, "连不上目标 %s:%d\n", dst_url.host, dst_url.port);
        close(src_fd);
        return -1;
    }
    conn_init(&dst, dst_fd, &dst_url);
    rewrite_sdp(sdp, src_url.base, dst_url.base);
    snprintf(extra, sizeof(extra), "Content-Type: application/sdp\r\nContent-Length: %d\r\n", (int)strlen(sdp));
    if (rtsp_call(&dst, "OPTIONS", dst_url.base, NULL, NULL, resp, sizeof(resp)) < 0 ||
        rtsp_call(&dst, "ANNOUNCE", dst_url.base, extra, sdp, resp, sizeof(resp)) < 0) {
        close(src_fd);
        close(dst_fd);
        return -1;
    }
    for (i = 0; i < ntracks; i++) {
        snprintf(extra, sizeof(extra), "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d;mode=record\r\n",
                 i * 2, i * 2 + 1);
        if (rtsp_call(&dst, "SETUP", tracks[i].dst, extra, NULL, resp, sizeof(resp)) < 0) {
            note(tracks[i].dst);
            close(src_fd);
            close(dst_fd);
            return -1;
        }
        tracks[i].dst_ch = parse_interleaved(resp);
    }
    if (rtsp_call(&dst, "RECORD", dst_url.base, "Range: npt=0.000-\r\n", NULL, resp, sizeof(resp)) < 0 ||
        rtsp_call(&src, "PLAY", src_url.base, "Range: npt=0.000-\r\n", NULL, resp, sizeof(resp)) < 0) {
        close(src_fd);
        close(dst_fd);
        return -1;
    }
    fprintf(stderr, "pushing %s -> %s:%d%s\n", src_url.path, dst_url.host, dst_url.port, dst_url.path);
    if (relay(&src, &dst, tracks, ntracks) < 0) {
        fprintf(stderr, "stream stopped\n");
    }
    close(src_fd);
    close(dst_fd);
    return -1;
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (argc == 2 && strcmp(argv[1], "--md5") == 0) {
        char hex[33];
        md5_str("", hex);
        printf("%s\n", hex);
        md5_str("abc", hex);
        printf("%s\n", hex);
        return 0;
    }
    if (argc != 3) {
        fprintf(stderr, "用法: %s rtsp://源/live.h265 rtsp://用户:密码@服务器:8554/cam\n", argv[0]);
        return 1;
    }
    for (;;) {
        run_once(argv[1], argv[2]);
        sleep(3);
    }
}
