#ifndef FCOPY_MESSAGE_H
#define FCOPY_MESSAGE_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

#include "common/structures.h"
#include "workflow/ProtocolMessage.h"

// chunk_size should be multiple of FCOPY_CHUNK_BASE
constexpr std::size_t FCOPY_CHUNK_BASE = 8192UL;

enum class Command : uint16_t {
    UNKNOWN             = 0x0000,

    CREATE_FILE_REQ     = 0x0001,
    SEND_FILE_REQ       = 0x0002,
    CLOSE_FILE_REQ      = 0x0003,
    DELETE_FILE_REQ     = 0x0004,

    SET_CHAIN_REQ       = 0x0011,

    CREATE_FILE_RESP    = 0x1001,
    SEND_FILE_RESP      = 0x1002,
    CLOSE_FILE_RESP     = 0x1003,
    DELETE_FILE_RESP    = 0x1004,

    SET_CHAIN_RESP      = 0x1011,
};

class MessageBase {
    struct DataDeleter {
        void operator()(void *data) { std::free(data); }
    };

public:
    static constexpr uint16_t MAGIC         = 0xF1FAU;
    static constexpr uint16_t VERSION       = 1U;
    static constexpr uint16_t HEADER_SIZE   = 16U;

public:
    explicit MessageBase(Command cmd = Command::UNKNOWN, int16_t error = 0);

    MessageBase(MessageBase &&) = default;
    MessageBase &operator= (MessageBase &&) = default;

    virtual ~MessageBase() { }

    Command get_command() const { return static_cast<Command>(command); }
    void set_error(int16_t error) { this->error = error; }
    int16_t get_error() const { return error; }

    bool set_data(const std::string_view &d);
    bool set_data_view(const std::string_view &v);

protected:
    int encode_head(std::string &head) noexcept;
    int decode_head(const std::string &head) noexcept;

    int append_body(const char *buf, size_t size) noexcept;
    virtual int decode_body() noexcept;
    virtual int encode_body(struct iovec vectors[], int max) noexcept { return 0; }

    friend class FcopyMessage;

protected:
    uint16_t magic;
    uint16_t version;
    uint16_t command;
    int16_t error;
    uint32_t body_len;
    uint32_t data_len;

    uint32_t data_pos;
    std::string body;
    std::unique_ptr<char, DataDeleter> data;
    std::string_view data_view;
};

class CreateFileReq : public MessageBase {
public:
    constexpr static Command ReqCmd = Command::CREATE_FILE_REQ;
    constexpr static Command RespCmd = Command::CREATE_FILE_RESP;
    constexpr static Command ThisCmd = Command::CREATE_FILE_REQ;

    CreateFileReq() : MessageBase(ThisCmd) { }

protected:
    int decode_body() noexcept override;
    int encode_body(struct iovec vectors[], int max) noexcept override;

public:
    uint32_t chunk_size;
    uint32_t file_perm;
    uint64_t file_size;
    std::string partition;
    std::string relative_path;
    std::string file_name;
};

class CreateFileResp : public MessageBase {
public:
    constexpr static Command ReqCmd = Command::CREATE_FILE_REQ;
    constexpr static Command RespCmd = Command::CREATE_FILE_RESP;
    constexpr static Command ThisCmd = Command::CREATE_FILE_RESP;

    CreateFileResp() : MessageBase(ThisCmd) { }

protected:
    int decode_body() noexcept override;
    int encode_body(struct iovec vectors[], int max) noexcept override;

public:
    std::string file_token;
};

class SendFileReq : public MessageBase {
public:
    constexpr static Command ReqCmd = Command::SEND_FILE_REQ;
    constexpr static Command RespCmd = Command::SEND_FILE_RESP;
    constexpr static Command ThisCmd = Command::SEND_FILE_REQ;

    SendFileReq() : MessageBase(ThisCmd) { }

    bool set_content(const std::string &content) {
        std::string_view v(content.data(), content.size());
        return set_data(v);
    }

    bool set_content(const std::string_view &content) {
        return set_data(content);
    }

    bool set_content(const char *s, std::size_t size) {
        return set_content(std::string_view(s, size));
    }

    bool set_content_view(const std::string_view &content) {
        return set_data_view(content);
    }

    bool set_content_view(const char *s, std::size_t size) {
        return set_content_view(std::string_view(s, size));
    }

    std::string_view get_content_view() const {
        return data_view;
    }

protected:
    int decode_body() noexcept override;
    int encode_body(struct iovec vectors[], int max) noexcept override;

public:
    uint16_t max_chain_len = 0;
    uint16_t compress_type = 0;
    uint32_t origin_size   = 0;
    uint32_t crc32         = 0;
    uint64_t offset        = 0;
    std::string file_token;
};

class SendFileResp : public MessageBase {
public:
    constexpr static Command ReqCmd = Command::SEND_FILE_REQ;
    constexpr static Command RespCmd = Command::SEND_FILE_RESP;
    constexpr static Command ThisCmd = Command::SEND_FILE_RESP;

    SendFileResp() : MessageBase(ThisCmd) { }

/*
// TODO support later
protected:
    int decode_body() noexcept override;
    int encode_body(struct iovec vectors[], int max) noexcept override;

public:
    // send file error may occur anywhere in the send chain,
    // this string indicates which target the error occurred on.
    std::string error_from;
*/
};

class CloseFileReq : public MessageBase {
public:
    constexpr static Command ReqCmd = Command::CLOSE_FILE_REQ;
    constexpr static Command RespCmd = Command::CLOSE_FILE_RESP;
    constexpr static Command ThisCmd = Command::CLOSE_FILE_REQ;

    CloseFileReq() : MessageBase(ThisCmd) { }

protected:
    int decode_body() noexcept override;
    int encode_body(struct iovec vectors[], int max) noexcept override;

public:
    uint8_t wait_close {0};
    std::string file_token;
};

class CloseFileResp : public MessageBase {
public:
    constexpr static Command ReqCmd = Command::CLOSE_FILE_REQ;
    constexpr static Command RespCmd = Command::CLOSE_FILE_RESP;
    constexpr static Command ThisCmd = Command::CLOSE_FILE_RESP;

    CloseFileResp() : MessageBase(ThisCmd) { }
};

class DeleteFileReq : public MessageBase {
public:
    constexpr static Command ReqCmd = Command::DELETE_FILE_REQ;
    constexpr static Command RespCmd = Command::DELETE_FILE_RESP;
    constexpr static Command ThisCmd = Command::DELETE_FILE_REQ;

    DeleteFileReq() : MessageBase(ThisCmd) { }

protected:
    int decode_body() noexcept override;
    int encode_body(struct iovec vectors[], int max) noexcept override;

public:
    std::string file_token;
};

class DeleteFileResp : public MessageBase {
public:
    constexpr static Command ReqCmd = Command::DELETE_FILE_REQ;
    constexpr static Command RespCmd = Command::DELETE_FILE_RESP;
    constexpr static Command ThisCmd = Command::DELETE_FILE_RESP;

    DeleteFileResp() : MessageBase(ThisCmd) { }
};

class SetChainReq : public MessageBase {
public:
    constexpr static Command ReqCmd = Command::SET_CHAIN_REQ;
    constexpr static Command RespCmd = Command::SET_CHAIN_RESP;
    constexpr static Command ThisCmd = Command::SET_CHAIN_REQ;

    SetChainReq() : MessageBase(ThisCmd) { }

protected:
    int decode_body() noexcept override;
    int encode_body(struct iovec vectors[], int max) noexcept override;

public:
    std::string file_token;
    std::vector<ChainTarget> targets;
};

class SetChainResp : public MessageBase {
public:
    constexpr static Command ReqCmd = Command::SET_CHAIN_REQ;
    constexpr static Command RespCmd = Command::SET_CHAIN_RESP;
    constexpr static Command ThisCmd = Command::SET_CHAIN_RESP;

    SetChainResp() : MessageBase(ThisCmd) { }
};

class FcopyMessage : public protocol::ProtocolMessage {
public:
    FcopyMessage() { }

    FcopyMessage(FcopyMessage &&that) = default;
    FcopyMessage &operator= (FcopyMessage &&that) = default;

    virtual ~FcopyMessage() = default;

    Command get_command() const {
        if (message)
            return message->get_command();
        return Command::UNKNOWN;
    }

    int get_error() const {
        if (message)
            return message->get_error();
        return 0;
    }

    MessageBase *get_message_pointer() {
        return message.get();
    }

    template<typename MessageType>
    bool move_message(MessageType &m) {
        if (get_command() == MessageType::ThisCmd) {
            auto *ptr = static_cast<MessageType *>(message.get());
            m = std::move(*ptr);
            message.reset();
            return true;
        }
        return false;
    }

    template<typename M>
    void set_message(M &&m) {
        message = std::make_unique<M>(std::move(m));
    }

private:
    virtual int encode(struct iovec vectors[], int max);
    virtual int append(const void *buf, size_t size);

protected:
    std::string head;
    std::unique_ptr<MessageBase> message;
};

using FcopyRequest = FcopyMessage;
using FcopyResponse = FcopyMessage;

#endif // FCOPY_MESSAGE_H
