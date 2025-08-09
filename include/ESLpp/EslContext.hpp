#pragma once
#ifndef ESLPP__ESL_CONTEXT_HPP
#define ESLPP__ESL_CONTEXT_HPP

#include <cassert>
#include <sstream>
#include <functional>
#include <memory>
#include <string>
#include <map>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#define ESL_EVENT_HEADER_CONTENT_LENGTH "content-length"

namespace eslpp
{
class tools
{
public:
    static std::string url_decode(const std::string& str)
    {
        std::string result;
        result.reserve(str.size()); // 预分配空间提高效率

        for (size_t i = 0; i < str.size(); ++i)
        {
            char current = str[i];
            if (current == '+')
            {
                // '+' 解码为空格
                result += ' ';
            }
            else if (current == '%')
            {
                // 检查是否有足够的后续字符且为十六进制
                if (i + 2 < str.size() && std::isxdigit(static_cast<unsigned char>(str[i + 1])) &&
                    std::isxdigit(static_cast<unsigned char>(str[i + 2])))
                {
                    // 组合两个十六进制字符
                    char decoded = static_cast<char>(
                        (hex_char_to_int(str[i + 1]) << 4) |
                        hex_char_to_int(str[i + 2])
                    );
                    result += decoded;
                    i += 2; // 跳过已处理的两位十六进制字符
                }
                else
                {
                    // 无效编码保留原样
                    result += current;
                }
            }
            else
            {
                // 普通字符直接添加
                result += current;
            }
        }
        return result;
    }

    static void to_lower(std::string& s)
    {
        for (size_t i = 0; i < s.size(); ++i)
        {
            s.at(i) = static_cast<char>(tolower(s.at(i)));
        }
    }

    static void trim(std::string& str)
    {
        if (str.empty())
            return;

        size_t start = 0;
        size_t end   = str.size() - 1;

        // 定位第一个非空格字符
        while (start <= end && str.at(start) == ' ')
            ++start;

        // 定位最后一个非空格字符
        while (end >= start && str.at(end) == ' ')
            --end;

        str = (start <= end) ? str.substr(start, end - start + 1) : "";
    }

private:
    // 辅助函数：将十六进制字符转换为对应的整数值
    static int hex_char_to_int(char c)
    {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }
        else if (c >= 'a' && c <= 'f')
        {
            return 10 + (c - 'a');
        }
        else if (c >= 'A' && c <= 'F')
        {
            return 10 + (c - 'A');
        }
        return 0;
    }
};

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
    Context()          = default;
    virtual ~Context() = default;

    // noncopyable
    Context(const Context&)            = delete;
    Context& operator=(const Context&) = delete;

    virtual void OnReceiveBufferAllocate(size_t suggested_len)
    {
        std::unique_ptr<uint8_t[]> tmp = std::make_unique<uint8_t[]>(suggested_len);
        receive_buffer_size_           = suggested_len;

        if (!receive_buffer_)
        {
            receive_buffer_ = std::move(tmp);
            return;
        }

        std::memcpy(tmp.get(), receive_buffer_.get(), receive_data_size_);
        std::swap(tmp, receive_buffer_);
    }

    // 数据接收接口 (两种模式)
    // Reactor 模式：主动喂数据
    virtual void Feed(const uint8_t* data, size_t len)
    {
        if (!receive_buffer_ || receive_buffer_size_ < receive_data_size_ + len)
        {
            this->OnReceiveBufferAllocate(receive_data_size_ + len + 1);
        }

        std::memcpy(receive_buffer_.get() + receive_data_size_, data, len);
        receive_data_size_ += len;
    }

    // Proactor 模式：获取接收缓冲区
    virtual BufferView GetReceiveBuffer()
    {
        if (!receive_buffer_)
        {
            return BufferView{};
        }
        return BufferView{
            receive_buffer_.get() + receive_data_size_,
            receive_buffer_size_ - receive_data_size_
        };
    }

    virtual void CommitReceivedData(size_t actual_len)
    {
        receive_data_size_ += actual_len;
        assert(receive_data_size_ <= receive_buffer_size_);
    }

    // 发送接口 (统一接口)
    using SendCallback = std::function<void(const char* data, size_t len)>;

    virtual void SetSendHandler(SendCallback _handler)
    {
        send_callback_ = std::move(_handler);
    }

protected:
    void ConsumeReceivedData(size_t len)
    {
        assert(len <= receive_data_size_);

        std::memmove(receive_buffer_.get(), receive_buffer_.get() + len, receive_data_size_ - len);
        receive_data_size_ -= len;
    }

protected:
    std::unique_ptr<uint8_t[]> receive_buffer_;
    size_t                     receive_buffer_size_{};
    size_t                     receive_data_size_{};

protected:
    SendCallback send_callback_{};
};

enum class Error
{
    ParseError,
};

enum class LogLevel
{
    Notify,
    Debug,
    Info,
    Warn,
    Error,
};


struct SubscribeEventHelper
{
public:
    enum EventType
    {
        PLAIN,
        XML,
        JSON
    };

    static const char* to_string(EventType type)
    {
        switch (type)
        {
        case PLAIN:
            return "PLAIN";
        case XML:
            return "XML";
        case JSON:
            return "JSON";
        }
        return "";
    }

    explicit SubscribeEventHelper(EventType type) : type_(type)
    {
    }

    SubscribeEventHelper& SetEventType(EventType type)
    {
        type_ = type;
        return *this;
    }

    SubscribeEventHelper& Event(const std::string& ev)
    {
        events_.insert(ev);
        return *this;
    }

    template <typename T>
    SubscribeEventHelper& Events(T&& events)
    {
        for (auto&& it : events)
        {
            events_.insert(it);
        }
        return *this;
    }

    SubscribeEventHelper& CustomEvent(const std::string& ev)
    {
        custom_events_.insert(ev);
        return *this;
    }

    template <typename T>
    SubscribeEventHelper& CustomEvents(T&& event)
    {
        for (auto&& it : event)
        {
            custom_events_.insert(it);
        }
        return *this;
    }

    std::string GetEslCommand() const
    {
        std::stringstream ss;
        ss << "event " << to_string(type_);
        for (auto&& it : events_)
        {
            ss << " " << it;
        }
        if (!custom_events_.empty())
        {
            ss << " CUSTOM";
            for (auto&& it : custom_events_)
            {
                ss << " " << it;
            }
        }
        return ss.str();
    }

private:
    EventType                       type_{PLAIN};
    std::unordered_set<std::string> events_;
    std::unordered_set<std::string> custom_events_;
};

class EslContext : public Context
{
public:
    EslContext()
    {
        current_event_ = std::make_shared<EslEvent>();
    }

    ~EslContext() override
    {
    }

    // noncopyable

public:
    using OnLoggingCallback = std::function<void(EslContext*, std::string&, std::string&)>;

    void SetLoggingCallback(OnLoggingCallback&& _logging_callback)
    {
        on_logging_callback_ = std::move(_logging_callback);
    }

    using OnLoggedCallback = std::function<void(EslContext*)>;

    void SetOnLoggedCallback(OnLoggedCallback&& _on_logged_callback)
    {
        on_logged_callback_ = std::move(_on_logged_callback);
    }

    using OnEventCallback = std::function<void(EslContext*, std::shared_ptr<EslEvent>)>;

    void SetOnEventCallback(OnEventCallback&& _on_event_callback)
    {
        on_event_callback_ = std::move(_on_event_callback);
    }

    using OnErrorCallback = std::function<void(EslContext*, Error err)>;

    void SetOnErrorCallback(OnErrorCallback&& _on_error_callback)
    {
        on_error_callback_ = std::move(_on_error_callback);
    }

    using LogCallback = std::function<void(EslContext*, LogLevel level, const std::string& tag,
                                           const std::string&    message)>;

    void SetLogHandler(LogCallback _log_callback)
    {
        log_callback_ = std::move(_log_callback);
    }

    void Feed(const uint8_t* data, size_t len) override
    {
        Context::Feed(data, len);
        TryLoadEvent();
    }

    void CommitReceivedData(size_t actual_len) override
    {
        Context::CommitReceivedData(actual_len);
        TryLoadEvent();
    }

private:
    OnLoggingCallback on_logging_callback_{};
    OnLoggedCallback  on_logged_callback_{};
    OnEventCallback   on_event_callback_{};
    OnErrorCallback   on_error_callback_{};
    LogCallback       log_callback_{};

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

        return ApiCallback{[this] { this->RealApi(); }, entry.api_callback.get()};
    }

    template <typename T>
    void SendMsg(const std::string& uuid, T&& headers, const std::string& body = "")
    {
        std::stringstream ss;
        if (uuid.empty())
        {
            ss << "sendmsg" << "\n";
        }
        else
        {
            ss << "sendmsg " << uuid << "\n";
        }

        auto body_len = body.size();

        for (auto it = headers.begin(); it != headers.end(); ++it)
        {
            if (it->first == "Content-Length" && body_len != 0)
            {
                ss << it->first << ": " << body_len << "\n";
                body_len = 0;
            }
            else
            {
                ss << it->first << ": " << it->second << "\n";
            }
        }

        if (!body.empty())
        {
            if (body_len != 0)
            {
                ss << "Content-Length: " << body_len << "\n";
            }

            ss << "\n";
            ss << body;
            ss << "\n\n";
        }
        else
        {
            ss << "\n";
        }

        auto str = ss.str();
        send_callback_(str.c_str(), str.size());
    }

    void Execute(const std::string& app, const std::string& args, const std::string& uuid, bool async = true)
    {
        std::map<std::string, std::string> params;

        params.insert({"call-command", "execute"});
        params.insert({"execute-app-name", app});
        if (!args.empty())
        {
            params.insert({"execute-app-arg", args});
        }
        if (async)
        {
            params.insert({"async", "true"});
        }

        this->SendMsg(uuid, params);
    }

    void SubscribeEvents(const SubscribeEventHelper& param)
    {
        this->Send(param.GetEslCommand());
    }

    void Exit()
    {
        this->send_callback_("exit", 4);
    }

private:
    void RealApi()
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

    void TryLoadEvent()
    {
        if (receive_data_size_ == 0)
        {
            return;
        }

        if (parse_header_ && !ParseHeaders())
        {
            return;
        }
        if (!ParseBody())
        {
            return;
        }
        parse_header_ = true;

        // call back
        this->on_event_callback_(this, current_event_);
        current_event_ = std::make_shared<EslEvent>();
        this->TryLoadEvent();
    }

    /**
     * Check Header Over Positon
     * x x x x x x \n\n
     *               |
     * 0 1 2 3 4 5 6 7
     * @return end of the second len: 8
     */
    size_t CheckHeaderOverPosition()
    {
        size_t pos = 1;
        while (pos < receive_data_size_)
        {
            if (receive_buffer_[pos] == '\n')
            {
                if (receive_buffer_[pos - 1] == '\r' || receive_buffer_[pos - 1] == '\n')
                {
                    // found
                    return pos + 1;
                }
            }
            ++pos;
        }

        return std::string::npos;
    }

    /**
     *
     * @return parse events header over
     */
    bool ParseHeaders()
    {
        auto len = CheckHeaderOverPosition();
        if (len == std::string::npos)
        {
            if (log_callback_)
                log_callback_(this, LogLevel::Debug, "parse", "parse headers, need more data.");
            return false;
        }

        std::vector<std::pair<std::string, std::string>> headers;
        if (!RealParseHeaders(headers, reinterpret_cast<const char*>(receive_buffer_.get()), len - 2))
        {
            return false;
        }

        for (auto&& kv : headers)
        {
            assert(!kv.first.empty());
            assert(!kv.second.empty());
            auto it = current_event_->headers.find(kv.first);
            if (it == current_event_->headers.end())
            {
                current_event_->headers.insert(std::make_pair(kv.first, kv.second));
            }
            else
            {
                if (log_callback_)
                    log_callback_(this, LogLevel::Warn, "parse",
                                  "header " + kv.first + " already exists, " + it->second + " -> " + kv.second);
                it->second = kv.second;
            }
        }

        this->ConsumeReceivedData(len);
        return true;
    }

    bool ParseBody()
    {
        auto it = current_event_->headers.find(ESL_EVENT_HEADER_CONTENT_LENGTH);
        if (it == current_event_->headers.end())
        {
            return true;
        }

        auto   len_str = it->second;
        size_t len     = 0;
        try
        {
            size_t index = 0;
            len          = std::stoull(len_str, &index);
            if (index != len_str.size())
            {
                if (log_callback_)
                    log_callback_(this, LogLevel::Error, "parse", "parse " + len_str + " to int failed.");
                on_error_callback_(this, Error::ParseError);
                return false;
            }
        }
        catch (std::exception& e)
        {
            if (log_callback_)
                log_callback_(this, LogLevel::Error, "parse", "parse " + len_str + " to int failed.");
            on_error_callback_(this, Error::ParseError);
            return false;
        }

        if (len > receive_data_size_)
        {
            // need more data
            if (log_callback_)
            {
                log_callback_(this, LogLevel::Debug, "parse", "parse headers, need more data.");
            }
            parse_header_ = false;
            return false;
        }

        current_event_->body.assign(receive_buffer_.get(), receive_buffer_.get() + len);
        this->ConsumeReceivedData(len);

        return true;
    }

    bool RealParseHeader(std::vector<std::pair<std::string, std::string>>& vec, const char* data, size_t len)
    {
        auto str = std::string(data, len);
        auto p   = std::find(data, data + len, ':');
        if (p == data + len)
        {
            // not found
            if (log_callback_)
                log_callback_(this, LogLevel::Error, "parse", std::string(data, len) + " not found ':'");
            on_error_callback_(this, Error::ParseError);
            return false;
        }

        std::pair<std::string, std::string> pp;
        pp.first  = std::string(data, std::distance(data, p));
        pp.second = std::string(p + 1, std::distance(p + 1, data + len));

        if (pp.first.empty() || pp.second.empty())
        {
            // not found
            if (log_callback_)
                log_callback_(this, LogLevel::Error, "parse", "key " + pp.first + " or value " + pp.second + " empty");
            on_error_callback_(this, Error::ParseError);
            return false;
        }

        tools::trim(pp.first);
        tools::trim(pp.second);

        pp.second = tools::url_decode(pp.second);
        tools::to_lower(pp.first);
        tools::to_lower(pp.second);

        assert(!pp.first.empty() && !pp.second.empty());

        vec.push_back(pp);
        return true;
    }

    bool RealParseHeaders(std::vector<std::pair<std::string, std::string>>& vec, const char* data, size_t len)
    {
        auto str = std::string(data, len);
        auto p   = std::find(data, data + len, '\n');
        if (p == data + len)
        {
            return RealParseHeader(vec, data, len);
        }

        return RealParseHeader(vec, data, std::distance(data, p)) &&
               RealParseHeaders(vec, p + 1, std::distance(p + 1, data + len));
    }

    struct ApiInfo
    {
        std::string                      cmd;
        std::shared_ptr<ApiCallbackImpl> api_callback;
    };

    std::queue<ApiInfo> api_queue_;
    ApiInfo             current_api_;

    std::shared_ptr<EslEvent> current_event_;
    bool                      parse_header_{true};
};
} // namespace eslpp

#endif // ESLPP__ESL_CONTEXT_HPP


