#include <bit>
#include <utility>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <type_traits>

#include "message.h"

static bool create_message(std::unique_ptr<MessageBase> &ptr, Command cmd) {
    switch (cmd) {
    case Command::CREATE_FILE_REQ:  ptr.reset(new CreateFileReq());     break;
    case Command::SEND_FILE_REQ:    ptr.reset(new SendFileReq());       break;
    case Command::CLOSE_FILE_REQ:   ptr.reset(new CloseFileReq());      break;
    case Command::DELETE_FILE_REQ:  ptr.reset(new DeleteFileReq());     break;
    case Command::SET_CHAIN_REQ:    ptr.reset(new SetChainReq());       break;

    case Command::CREATE_FILE_RESP: ptr.reset(new CreateFileResp());    break;
    case Command::SEND_FILE_RESP:   ptr.reset(new SendFileResp());      break;
    case Command::CLOSE_FILE_RESP:  ptr.reset(new CloseFileResp());     break;
    case Command::DELETE_FILE_RESP: ptr.reset(new DeleteFileResp());    break;
    case Command::SET_CHAIN_RESP:   ptr.reset(new SetChainResp());      break;

    default:
        return false;
    }

    return true;
}

template<typename T>
    requires std::is_unsigned_v<T>
static T byte_swap(T n) {
    static_assert(std::is_same_v<T, uint8_t> ||
                  std::is_same_v<T, uint16_t> ||
                  std::is_same_v<T, uint32_t> ||
                  std::is_same_v<T, uint64_t>);

    if constexpr (std::endian::native == std::endian::little) {
        constexpr std::size_t size = sizeof(T);
        T ret = 0;
        for (std::size_t i = 0; i < size; i++) {
            ret <<= 8;
            ret |= (n & 0xFF);
            n >>= 8;
        }
        n = ret;
    }

    return n;
}

template<typename T>
    requires std::is_signed_v<T>
static T byte_swap(T n) {
    using type = std::make_unsigned_t<T>;
    auto t = static_cast<type>(n);
    return static_cast<T>(byte_swap(t));
}

template<typename T>
static void append_int(std::string &s, T n) {
    n = byte_swap(n);
    s.append(reinterpret_cast<char *>(&n), sizeof(n));
}

static void append_string(std::string &s, const std::string &o) {
    uint32_t n = o.size();
    append_int(s, n);
    s.append(o);
}

template<typename T>
static int decode_int(const std::string &s, std::size_t &pos, T &n) {
    if (pos + sizeof(n) > s.size())
        return -1;

    std::memcpy(&n, s.data() + pos, sizeof(n));
    pos += sizeof(n);
    n = byte_swap(n);

    return 0;
}

static int decode_string(const std::string &s, std::size_t &pos, std::string &o) {
    uint32_t n;
    int ret;

    ret = decode_int(s, pos, n);
    if (ret < 0)
        return ret;

    if (pos + n > s.size())
        return -1;

    o.assign(s, pos, n);
    pos += n;

    return 0;
}

#define FAIL_IF(x) do { int ret = (x); if (ret < 0) return ret; } while(0)

MessageBase::MessageBase(Command cmd, int16_t error) {
    this->magic     = MAGIC;
    this->version   = VERSION;
    this->command   = static_cast<uint16_t>(cmd);
    this->error     = error;
    this->body_len  = 0;
    this->data_len  = 0;
    this->data_pos  = 0;
}

bool MessageBase::set_data(const std::string_view &d) {
    void *buf;
    data_pos = d.size();
    data_len = d.size();
    data.reset();

    if (d.empty()) {
        data_view = std::string_view();
    }
    else {
        buf = std::aligned_alloc(FCOPY_CHUNK_BASE, d.size());
        if (!buf)
            return false;

        std::memcpy(buf, d.data(), d.size());
        data.reset(reinterpret_cast<char *>(buf));
        data_view = std::string_view(data.get(), d.size());
    }

    return true;
}

bool MessageBase::set_data_view(const std::string_view &d) {
    data_pos = d.size();
    data_len = d.size();
    data.reset();
    data_view = d;
    return true;
}

int MessageBase::encode_head(std::string &head) noexcept {
    head.reserve(HEADER_SIZE);

    append_int(head, magic);
    append_int(head, version);
    append_int(head, command);
    append_int(head, error);
    append_int(head, body_len);
    append_int(head, data_len);

    if (head.size() != HEADER_SIZE)
        return -1;

    return 1;
}

int MessageBase::decode_head(const std::string &head) noexcept {
    if (head.size() != HEADER_SIZE)
        return -1;

    std::size_t pos = 0;

    decode_int(head, pos, magic);
    decode_int(head, pos, version);
    decode_int(head, pos, command);
    decode_int(head, pos, error);
    decode_int(head, pos, body_len);
    decode_int(head, pos, data_len);

    if (pos != head.size() || magic != MAGIC || version != VERSION)
        return -1;

    return 0;
}

int MessageBase::append_body(const char *buf, size_t size) noexcept {
    std::size_t n;

    if (body.size() < body_len) {
        if (body.capacity() < body_len)
            body.reserve(body_len);

        n = std::min(size, body_len - body.size());
        body.append(buf, n);
        buf += n;
        size -= n;

        if (body.size() < body_len)
            return 0;
    }

    if (data_pos < data_len) {
        char *p = data.get();

        if (!p) {
            p = static_cast<char *>(std::aligned_alloc(FCOPY_CHUNK_BASE, data_len));
            data.reset(p);

            if (!p)
                return -1;
        }

        n = std::min<std::size_t>(size, data_len - data_pos);
        std::memcpy(p + data_pos, buf, n);
        data_pos += n;

        if (data_pos < data_len)
            return 0;

        data_view = std::string_view(data.get(), data_len);
    }

    return decode_body();
}

int MessageBase::decode_body() noexcept {
    if (!body.empty()) {
        errno = EBADMSG;
        return -1;
    }

    return 1;
}

int CreateFileReq::decode_body() noexcept {
    std::size_t pos = 0;
    FAIL_IF(decode_int(body, pos, chunk_size));
    FAIL_IF(decode_int(body, pos, file_perm));
    FAIL_IF(decode_int(body, pos, file_size));
    FAIL_IF(decode_string(body, pos, partition));
    FAIL_IF(decode_string(body, pos, relative_path));
    FAIL_IF(decode_string(body, pos, file_name));

    return (pos == body.size()) ? 1 : -1;
}

int CreateFileReq::encode_body(struct iovec vectors[], int max) noexcept {
    append_int(body, chunk_size);
    append_int(body, file_perm);
    append_int(body, file_size);
    append_string(body, partition);
    append_string(body, relative_path);
    append_string(body, file_name);

    vectors->iov_base = body.data();
    vectors->iov_len = body.size();

    return 1;
}

int CreateFileResp::decode_body() noexcept {
    std::size_t pos = 0;
    FAIL_IF(decode_string(body, pos, file_token));

    return (pos == body.size()) ? 1 : -1;
}

int CreateFileResp::encode_body(struct iovec vectors[], int max) noexcept {
    append_string(body, file_token);

    vectors->iov_base = body.data();
    vectors->iov_len = body.size();

    return 1;
}

int SendFileReq::decode_body() noexcept {
    std::size_t pos = 0;

    FAIL_IF(decode_int(body, pos, max_chain_len));
    FAIL_IF(decode_int(body, pos, compress_type));
    FAIL_IF(decode_int(body, pos, origin_size));
    FAIL_IF(decode_int(body, pos, crc32));
    FAIL_IF(decode_int(body, pos, offset));
    FAIL_IF(decode_string(body, pos, file_token));

    return (pos == body.size()) ? 1 : -1;
}

int SendFileReq::encode_body(struct iovec vectors[], int max) noexcept {
    append_int(body, max_chain_len);
    append_int(body, compress_type);
    append_int(body, origin_size);
    append_int(body, crc32);
    append_int(body, offset);
    append_string(body, file_token);

    vectors[0].iov_base = body.data();
    vectors[0].iov_len = body.size();

    if (data_view.empty())
        return 1;

    vectors[1].iov_base = const_cast<char *>(data_view.data());
    vectors[1].iov_len = data_view.size();
    return 2;
}

int CloseFileReq::decode_body() noexcept {
    std::size_t pos = 0;
    FAIL_IF(decode_int(body, pos, wait_close));
    FAIL_IF(decode_string(body, pos, file_token));

    return (pos == body.size()) ? 1 : -1;
}

int CloseFileReq::encode_body(struct iovec vectors[], int max) noexcept {
    append_int(body, wait_close);
    append_string(body, file_token);

    vectors->iov_base = body.data();
    vectors->iov_len = body.size();

    return 1;
}

int DeleteFileReq::decode_body() noexcept {
    std::size_t pos = 0;
    FAIL_IF(decode_string(body, pos, file_token));

    return (pos == body.size()) ? 1 : -1;
}

int DeleteFileReq::encode_body(struct iovec vectors[], int max) noexcept {
    append_string(body, file_token);

    vectors->iov_base = body.data();
    vectors->iov_len = body.size();

    return 1;
}

int SetChainReq::decode_body() noexcept {
    std::size_t pos = 0;
    uint32_t size;
    FAIL_IF(decode_string(body, pos, file_token));
    FAIL_IF(decode_int(body, pos, size));

    for (uint32_t i = 0; i < size; i++) {
        ChainTarget t;
        FAIL_IF(decode_string(body, pos, t.host));
        FAIL_IF(decode_int(body, pos, t.port));
        FAIL_IF(decode_string(body, pos, t.file_token));
        targets.push_back(std::move(t));
    }

    return (pos == body.size()) ? 1 : -1;
}

int SetChainReq::encode_body(struct iovec vectors[], int max) noexcept {
    append_string(body, file_token);

    append_int(body, (uint32_t)targets.size());
    for (const ChainTarget &t : targets) {
        append_string(body, t.host);
        append_int(body, t.port);
        append_string(body, t.file_token);
    }

    vectors->iov_base = body.data();
    vectors->iov_len = body.size();

    return 1;
}

int FcopyMessage::encode(struct iovec vectors[], int max) {
    if (!message) {
        errno = EBADMSG;
        return -1;
    }

    int ret;
    std::size_t blen = 0;

    message->body.clear();
    ret = message->encode_body(vectors + 1, max - 1);
    if (ret < 0) {
        errno = EBADMSG;
        return ret;
    }

    for (int i = 1; i <= ret; i++)
        blen += vectors[i].iov_len;

    message->body_len = static_cast<uint32_t>(blen-message->data_len);

    head.clear();
    message->encode_head(head);

    vectors[0].iov_base = head.data();
    vectors[0].iov_len = head.size();

    return ret + 1;
}

int FcopyMessage::append(const void *buf, size_t size) {
    constexpr std::size_t HSIZE = MessageBase::HEADER_SIZE;
    auto *data = static_cast<const char *>(buf);
    std::size_t n;

    if (head.size() < HSIZE) {
        n = std::min(HSIZE - head.size(), size);
        head.append(data, n);

        data += n;
        size -= n;

        if (head.size() < HSIZE)
            return 0;

        MessageBase m;
        if (m.decode_head(head) < 0 || !create_message(this->message, m.get_command())) {
            errno = EBADMSG;
            return -1;
        }

        if (m.body_len + m.data_len + HSIZE > get_size_limit()) {
            errno = EMSGSIZE;
            return -1;
        }

        *message = std::move(m);
    }

    int ret = message->append_body(data, size);
    if (ret < 0)
        errno = EBADMSG;

    return ret;
}
