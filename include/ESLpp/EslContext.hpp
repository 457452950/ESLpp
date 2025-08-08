#pragma once
#ifndef ESLPP__ESL_CONTEXT_HPP
#define ESLPP__ESL_CONTEXT_HPP

#include <cassert>

#include <functional>
#include <memory>
#include <string>
#include <queue>
#include <unordered_map>
#include <utility>

namespace eslpp
{
struct BufferView
{
    uint8_t* data{};     // 缓冲区起始指针
    size_t   capacity{}; // 缓冲区总容量
};

struct EslEvent
{
    using HeadersType = std::unordered_map<std::string, std::string>;

    HeadersType headers;
    std::string body;
};

class Context
{
public:
    virtual ~Context() = default;

    virtual void OnReceiveBufferAllocate(size_t suggested_len)
    {
        std::unique_ptr<uint8_t[]> tmp = std::make_unique<uint8_t[]>(suggested_len);
        receive_buffer_size_           = suggested_len;

        if (!receive_buffer_)
        {
            receive_buffer_ = std::move(tmp);
            return;
        }

        std::memcpy(tmp.get(), receive_buffer_.get(), receive_position_);
        std::swap(tmp, receive_buffer_);
    }

    // 数据接收接口 (两种模式)
    // Reactor 模式：主动喂数据
    virtual void Feed(const uint8_t* data, size_t len)
    {
        if (!receive_buffer_ || receive_buffer_size_ < receive_position_ + len)
        {
            this->OnReceiveBufferAllocate(receive_position_ + len + 1);
        }

        std::memcpy(receive_buffer_.get() + receive_position_, data, len);
        receive_position_ += len;
    }

    // Proactor 模式：获取接收缓冲区
    virtual BufferView GetReceiveBuffer()
    {
        if (!receive_buffer_)
        {
            return BufferView{};
        }
        return BufferView{
            receive_buffer_.get() + receive_position_,
            receive_buffer_size_ - receive_position_
        };
    }

    virtual void CommitReceivedData(size_t actual_len)
    {
        receive_position_ += actual_len;
        assert(receive_position_ <= receive_buffer_size_);
    }

    // 发送接口 (统一接口)
    using SendCallback = std::function<void(const void* data, size_t len)>;

    virtual void SetSendHandler(SendCallback _handler)
    {
        send_callback_ = std::move(_handler);
    }

protected:
    void ConsumeReceivedData(size_t len)
    {
        assert(len <= receive_position_);

        std::memmove(receive_buffer_.get(), receive_buffer_.get() + len, receive_position_ - len);
        receive_position_ -= len;
    }

protected:
    std::unique_ptr<uint8_t[]> receive_buffer_;
    size_t                     receive_buffer_size_{};
    size_t                     receive_position_{};

protected:
    SendCallback send_callback_{};
};

class EslContext : public Context
{
public:
    EslContext();
    ~EslContext() override;

public:
    using OnLoggingCallback = std::function<void(EslContext*, std::string&, std::string&)>;
    void SetLoggingCallback(OnLoggingCallback&& _logging_callback);

    using OnLoggedCallback = std::function<void(EslContext*)>;
    void SetOnLoggedCallback(OnLoggedCallback&& _on_logged_callback);

    using OnEventCallback = std::function<void(std::shared_ptr<EslEvent>)>;
    void SetOnEventCallback(OnEventCallback&& _on_event_callback);

private:
    OnLoggingCallback on_logging_callback_{};
    OnLoggedCallback  on_logged_callback_{};
    OnEventCallback   on_event_callback_{};

private:
    struct ApiCallbackImpl
    {
        OnEventCallback on_callback_{};
        OnEventCallback on_bg_job_callback_{};
    };

public:
    struct ApiCallback
    {
    public:
        explicit ApiCallback(std::function<void()>&& _private_cb, ApiCallbackImpl* _on_event_callback) :
            private_cb_(std::move(_private_cb)), api_callback_(_on_event_callback)
        {
        }

        // noncopyable
        ApiCallback(const ApiCallback&)            = delete;
        ApiCallback& operator=(const ApiCallback&) = delete;

        // moveable
        ApiCallback(ApiCallback&& other) noexcept
        {
            std::swap(other.private_cb_, private_cb_);
            std::swap(other.api_callback_, api_callback_);
        }

        ApiCallback& operator=(ApiCallback&& other) noexcept
        {
            std::swap(other.private_cb_, private_cb_);
            std::swap(other.api_callback_, api_callback_);
            return *this;
        }

        ~ApiCallback()
        {
            if (private_cb_)
                private_cb_();
        }

        ApiCallback& OnCallback(OnEventCallback&& _on_event_callback)
        {
            api_callback_->on_callback_ = std::move(_on_event_callback);
            return *this;
        }

        void OnBgJobCallback(OnEventCallback&& _on_event_callback)
        {
            api_callback_->on_bg_job_callback_ = std::move(_on_event_callback);
        }

    private:
        std::function<void()> private_cb_{};
        ApiCallbackImpl*      api_callback_{nullptr};
    };

    ApiCallback Api(const std::string& cmd)
    {
        assert(!cmd.empty());
        return Send("api " + cmd);
    }

    ApiCallback BgApi(const std::string& cmd, const std::string& job_uuid = "")
    {
        assert(!cmd.empty());
        auto _cmd = "bgapi " + cmd;
        if (!job_uuid.empty())
        {
            _cmd += ("\n"
                     "Job-UUID: " + job_uuid);
        }
        return Send(_cmd);
    }

    ApiCallback Send(const std::string& cmd)
    {
        assert(!cmd.empty());
        static std::string cmd_suffix = "\n\n";
        std::string        _cmd       = cmd;

        // check is end of "\n\n"
        auto ok = std::equal(_cmd.rbegin(), _cmd.rend(), cmd_suffix.begin());
        if (!ok)
        {
            _cmd += "\n\n";
        }

        // insert into api queue
        ApiInfo entry{
            _cmd, std::make_shared<ApiCallbackImpl>()
        };

        this->api_queue_.push(entry);

        return ApiCallback{[this] { this->NextApi(); }, entry.api_callback.get()};
    }

private:
    void NextApi()
    {
        if (!current_api_.cmd.empty() || api_queue_.empty())
        {
            // has active event, next turn.
            // no api waiting, skip.
            return;
        }

        current_api_ = api_queue_.front();
        api_queue_.pop();

        assert(!current_api_.cmd.empty());
        assert(this->send_callback_);

        this->send_callback_(current_api_.cmd.c_str(), current_api_.cmd.size());
    }

    struct ApiInfo
    {
        std::string                      cmd;
        std::shared_ptr<ApiCallbackImpl> api_callback;
    };

    std::queue<ApiInfo> api_queue_;
    ApiInfo             current_api_;
};
} // namesapce eslpp

#endif // ESLPP__ESL_CONTEXT_HPP


