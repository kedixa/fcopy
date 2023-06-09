#ifndef FCOPY_MESSAGE_H
#define FCOPY_MESSAGE_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

#include "common.h"
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
public:
    static constexpr uint16_t MAGIC         = 0xF1FAU;
    static constexpr uint16_t VERSION       = 1U;
    static constexpr uint16_t HEADER_SIZE   = 12U;

public:
    explicit MessageBase(Command cmd = Command::UNKNOWN, int16_t error = 0);

    MessageBase(MessageBase &&) = default;
    MessageBase &operator= (MessageBase &&) = default;

    virtual ~MessageBase() { }

    Command get_command() const { return static_cast<Command>(command); }
    void set_error(int16_t error) { this->error = error; }
    int16_t get_error() const { return error; }

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

    std::string body;
};

class CreateFileReq : public MessageBase {
public:
    CreateFileReq() : MessageBase(Command::CREATE_FILE_REQ) { }

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
    CreateFileResp() : MessageBase(Command::CREATE_FILE_RESP) { }

protected:
    int decode_body() noexcept override;
    int encode_body(struct iovec vectors[], int max) noexcept override;

public:
    std::string file_token;
};

class SendFileReq : public MessageBase {
public:
    SendFileReq() : MessageBase(Command::SEND_FILE_REQ) { }
    SendFileReq(SendFileReq &&);
    SendFileReq &operator= (SendFileReq &&) noexcept;

    void set_content(std::string content) {
        content_copy = std::move(content);
        content_view = content_copy;
        local_view = true;
    }

    void set_content_view(std::string_view content) {
        content_view = content;
        content_copy.clear();
        local_view = false;
    }

    std::string_view get_content_view() const {
        return content_view;
    }

protected:
    int decode_body() noexcept override;
    int encode_body(struct iovec vectors[], int max) noexcept override;

private:
    void move_content_view(SendFileReq &&that) noexcept;

public:
    uint16_t max_chain_len = 0;
    uint16_t compress_type = 0;
    uint32_t origin_size   = 0;
    uint32_t crc32         = 0;
    uint64_t offset        = 0;
    std::string file_token;

private:
    // local_view means the data in `content_view` is stored in `content_copy`
    std::string_view content_view;
    bool local_view = false;
    std::string content_copy;
};

class SendFileResp : public MessageBase {
public:
    SendFileResp() : MessageBase(Command::SEND_FILE_RESP) { }
};

class CloseFileReq : public MessageBase {
public:
    CloseFileReq() : MessageBase(Command::CLOSE_FILE_REQ) { }

protected:
    int decode_body() noexcept override;
    int encode_body(struct iovec vectors[], int max) noexcept override;

public:
    std::string file_token;
};

class CloseFileResp : public MessageBase {
public:
    CloseFileResp() : MessageBase(Command::CLOSE_FILE_RESP) { }
};

class DeleteFileReq : public MessageBase {
public:
    DeleteFileReq() : MessageBase(Command::DELETE_FILE_REQ) { }

protected:
    int decode_body() noexcept override;
    int encode_body(struct iovec vectors[], int max) noexcept override;

public:
    std::string file_token;
};

class DeleteFileResp : public MessageBase {
public:
    DeleteFileResp() : MessageBase(Command::DELETE_FILE_RESP) { }
};

class SetChainReq : public MessageBase {
public:
    SetChainReq() : MessageBase(Command::SET_CHAIN_REQ) { }

protected:
    int decode_body() noexcept override;
    int encode_body(struct iovec vectors[], int max) noexcept override;

public:
    std::string file_token;
    std::vector<ChainTarget> targets;
};

class SetChainResp : public MessageBase {
public:
    SetChainResp() : MessageBase(Command::SET_CHAIN_RESP) { }
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

    MessageBase *get_message_pointer() {
        return message.get();
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
