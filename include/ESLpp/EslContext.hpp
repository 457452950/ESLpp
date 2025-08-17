#pragma once
#ifndef ESLPP__ESL_CONTEXT_HPP
#define ESLPP__ESL_CONTEXT_HPP

#include <cassert>
#include <cstdarg>

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
#define ESL_EVENT_HEADER_CONTENT_TYPE   "content-type"

#define ESL_EVENT_CONTENT_TYPE_AUTH             "auth/request"
#define ESL_EVENT_CONTENT_TYPE_COMMAND_REPLY    "command/reply"
#define ESL_EVENT_CONTENT_TYPE_API_RESPONSE     "api/response"
#define ESL_EVENT_CONTENT_TYPE_TEXT_PLAIN       "text/event-plain"
#define ESL_EVENT_CONTENT_TYPE_DISCONNECT       "text/disconnect-notice"


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

class Parser
{
public:
    Parser() = default;

    explicit Parser(size_t buffer_len) : receive_buffer_size_(buffer_len)
    {
        receive_buffer_ = std::make_unique<uint8_t[]>(buffer_len);
    }

    virtual ~Parser() = default;

    // noncopyable
    Parser(const Parser&)            = delete;
    Parser& operator=(const Parser&) = delete;

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
};

class EslEventParser : public Parser
{
public:
    EslEventParser()
    {
        parsing_event_ = std::make_unique<EslEvent>();
    }

    explicit EslEventParser(size_t buffer_len) : Parser(buffer_len)
    {
        parsing_event_ = std::make_unique<EslEvent>();
    }

    void SetDecode(bool decode)
    {
        decode_ = decode;
    }

    ~EslEventParser() override
    {
    }

    /************* from Parser start ****************/
    void Feed(const uint8_t* data, size_t len) override
    {
        Parser::Feed(data, len);
        Parse();
    }

    void CommitReceivedData(size_t actual_len) override
    {
        Parser::CommitReceivedData(actual_len);
        Parse();
    }

    /************* from Parser end ****************/

    const std::string& GetError()
    {
        return err_;
    }

    std::shared_ptr<EslEvent> NextEvent()
    {
        if (events_.empty())
        {
            return nullptr;
        }

        auto ev = events_.front();
        events_.pop();
        return ev;
    }

private:
    void Parse()
    {
        if (receive_data_size_ == 0 || !err_.empty())
        {
            // Log(LogLevel::Debug, "parse", "parse headers, need more data.");
            return;
        }

        if (parse_header_ && !ParseHeaders())
        {
            return;
        }
        if (err_.empty() && !ParseBody())
        {
            return;
        }
        parse_header_ = true;

        if (!err_.empty())
        {
            return;
        }

        // call back
        this->events_.push(parsing_event_);
        parsing_event_ = std::make_unique<EslEvent>();
        Parse();
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
            // Log(LogLevel::Debug, "parse", "parse headers, need more data.");
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
            auto it = parsing_event_->headers.find(kv.first);
            if (it == parsing_event_->headers.end())
            {
                parsing_event_->headers.insert(std::make_pair(kv.first, kv.second));
            }
            else
            {
                // Log(LogLevel::Warn, "parse",
                //     "header %s already exists, %s -> %s",
                //     kv.first.c_str(), it->second.c_str(), kv.second.c_str());
                it->second = kv.second;
            }
        }

        this->ConsumeReceivedData(len);
        return true;
    }

    bool ParseBody()
    {
        auto it = parsing_event_->headers.find(ESL_EVENT_HEADER_CONTENT_LENGTH);
        if (it == parsing_event_->headers.end())
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
                // Log(LogLevel::Error, "parse", "parse %s -> %d to int failed.", len_str.c_str(), len);
                // on_error_callback_(this, Error::ParseError);
                err_ = "parse" + len_str + " to int failed.";
                return false;
            }
        }
        catch (std::exception& e)
        {
            // Log(LogLevel::Error, "parse", "parse %s to int failed.");
            // on_error_callback_(this, Error::ParseError);
            err_ = "parse" + len_str + " to int failed: " + e.what();
            return false;
        }

        if (len > receive_data_size_)
        {
            // need more data
            // Log(LogLevel::Debug, "parse", "parse headers, need more data.");
            parse_header_ = false;
            return false;
        }

        parsing_event_->body.assign(receive_buffer_.get(), receive_buffer_.get() + len);
        this->ConsumeReceivedData(len);

        return true;
    }

    bool RealParseHeader(std::vector<std::pair<std::string, std::string>>& vec, const char* data, size_t len)
    {
        // std::cout << "[" << std::string(data, len).c_str() << "]" << std::endl;
        auto str = std::string(data, len);
        auto p   = std::find(data, data + len, ':');
        if (p == data + len)
        {
            // not found
            err_ = std::string(data, len) + " not found ':'";
            return false;
        }

        std::pair<std::string, std::string> pp;
        pp.first  = std::string(data, std::distance(data, p));
        pp.second = std::string(p + 1, std::distance(p + 1, data + len));

        if (pp.first.empty() || pp.second.empty())
        {
            // not found
            err_ = "key " + pp.first + " or value " + pp.second + " empty";
            return false;
        }

        tools::trim(pp.first);
        tools::trim(pp.second);

        if (decode_)
            pp.second = tools::url_decode(pp.second);

        tools::to_lower(pp.first);
        // tools::to_lower(pp.second);

        // std::cout << "[" << pp.first.c_str() << "][" << pp.second.c_str() << "]" << std::endl;

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

private:
    std::queue<std::shared_ptr<EslEvent>> events_;

    std::shared_ptr<EslEvent> parsing_event_;
    bool                      parse_header_{true};
    bool                      decode_{false};
    std::string               err_;
};

class Context
{
public:
    Context()          = default;
    virtual ~Context() = default;

    // noncopyable
    Context(const Context&)            = delete;
    Context& operator=(const Context&) = delete;

    // 发送接口 (统一接口)
    using SendCallback = std::function<void(const char* data, size_t len)>;

    virtual void SetSendHandler(SendCallback _handler)
    {
        send_callback_ = std::move(_handler);
    }

protected:
    SendCallback send_callback_{};
};

enum class Error
{
    ParseError,
    EventError,
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
    explicit SubscribeEventHelper()
    {
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
        ss << "event PLAIN";
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
    std::unordered_set<std::string> events_;
    std::unordered_set<std::string> custom_events_;
};

class EslContext : public Context
{
public:
    EslContext()
    {
        text_plain_parser_.SetDecode(true);
    }

    ~EslContext() override
    {
    }


    // 数据接收接口 (两种模式)
    // Reactor 模式：主动喂数据
    virtual void Feed(const uint8_t* data, size_t len)
    {
        event_parser_.Feed(data, len);
        auto&& err = event_parser_.GetError();
        if (!err.empty())
        {
            Log(LogLevel::Error, "parse", err.c_str());
            return;
        }
        while (auto ev = event_parser_.NextEvent())
        {
            this->HandleEslEvent(ev);
        }
    }

    // Proactor 模式：获取接收缓冲区
    virtual BufferView GetReceiveBuffer()
    {
        return event_parser_.GetReceiveBuffer();
    }

    virtual void CommitReceivedData(size_t actual_len)
    {
        event_parser_.CommitReceivedData(actual_len);
        auto&& err = event_parser_.GetError();
        if (!err.empty())
        {
            Log(LogLevel::Error, "parse", err.c_str());
            return;
        }
        while (auto ev = event_parser_.NextEvent())
        {
            this->HandleEslEvent(ev);
        }
    }

public:
    using OnLoggingCallback = std::function<void(std::string&, std::string&)>;

    void SetLoggingCallback(OnLoggingCallback&& _logging_callback)
    {
        on_logging_callback_ = std::move(_logging_callback);
    }

    using OnLoggedCallback = std::function<void(bool success)>;

    void SetOnLoggedCallback(OnLoggedCallback&& _on_logged_callback)
    {
        on_logged_callback_ = std::move(_on_logged_callback);
    }

    using OnEventCallback = std::function<void(std::shared_ptr<EslEvent>)>;

    void SetOnEventCallback(OnEventCallback&& _on_event_callback)
    {
        on_event_callback_ = std::move(_on_event_callback);
    }

    using OnErrorCallback = std::function<void(Error err)>;

    void SetOnErrorCallback(OnErrorCallback&& _on_error_callback)
    {
        on_error_callback_ = std::move(_on_error_callback);
    }

    using LogCallback = std::function<void(LogLevel           level, const std::string& tag,
                                           const std::string& message)>;

    void SetLogHandler(LogCallback _log_callback)
    {
        log_callback_ = std::move(_log_callback);
    }

    using DisconnectCallback = std::function<void()>;

    void SetDisconnectCallback(DisconnectCallback&& _disconnect_callback)
    {
        this->disconnect_callback_ = std::move(_disconnect_callback);
    }

private:
    void Log(LogLevel level, const std::string& tag, const char* fmt, ...)
    {
        if (!log_callback_)
        {
            return;
        }

        char buffer[2048] = {};

        va_list args;
        va_start(args, fmt);
        size_t len = std::vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        log_callback_(level, tag, {buffer, len});
    }

    void Log(LogLevel level, const std::string& tag, const std::string& message)
    {
        if (!log_callback_)
        {
            return;
        }

        log_callback_(level, tag, message);
    }

private:
    OnLoggingCallback  on_logging_callback_{};
    OnLoggedCallback   on_logged_callback_{};
    OnEventCallback    on_event_callback_{};
    OnErrorCallback    on_error_callback_{};
    LogCallback        log_callback_{};
    DisconnectCallback disconnect_callback_{};

private:
    struct ApiCallbackImpl
    {
        enum Type
        {
            Command,
            Api,
            BgApi,
        } type_;

        explicit ApiCallbackImpl(Type type) : type_(type)
        {
        }

        OnEventCallback on_ok_{};
        OnEventCallback on_fail_{};
        OnEventCallback on_bg_job_{};
    };

public:
    struct Callback
    {
    public:
        explicit Callback(std::function<void()>&& _private_cb, ApiCallbackImpl* _on_event_callback) :
            private_cb_(std::move(_private_cb)), api_callback_(_on_event_callback)
        {
            // std::cout << "Callback" << std::endl;
        }

        using Type = ApiCallbackImpl::Type;

        // noncopyable
        Callback(const Callback&)            = delete;
        Callback& operator=(const Callback&) = delete;

        // moveable
        Callback(Callback&& other) noexcept
        {
            std::swap(other.private_cb_, private_cb_);

            api_callback_       = other.api_callback_;
            other.api_callback_ = nullptr;
        }

        Callback& operator=(Callback&& other) noexcept
        {
            std::swap(other.private_cb_, private_cb_);
            std::swap(other.api_callback_, api_callback_);
            return *this;
        }

        ~Callback()
        {
            if (private_cb_)
            {
                private_cb_();
            }
        }

        Callback& OnSuccess(OnEventCallback&& _on_event_callback)
        {
            api_callback_->on_ok_ = std::move(_on_event_callback);
            return *this;
        }

        Callback& OnFail(OnEventCallback&& _on_event_callback)
        {
            api_callback_->on_fail_ = std::move(_on_event_callback);
            return *this;
        }

        Callback& OnBgJob(OnEventCallback&& _on_event_callback)
        {
            api_callback_->on_bg_job_ = std::move(_on_event_callback);
            return *this;
        }

    private:
        std::function<void()> private_cb_{};
        ApiCallbackImpl*      api_callback_{nullptr};
    };

    Callback Api(const std::string& cmd)
    {
        assert(!cmd.empty());
        return Send("api " + cmd, Callback::Type::Api);
    }

    Callback BgApi(const std::string& cmd, const std::string& job_uuid = "")
    {
        assert(!cmd.empty());
        auto _cmd = "bgapi " + cmd;
        if (!job_uuid.empty())
        {
            _cmd += ("\n"
                     "Job-UUID: " + job_uuid);
        }
        return std::move(Send(_cmd, Callback::Type::BgApi));
    }

    Callback Send(const std::string& cmd, Callback::Type type)
    {
        assert(!cmd.empty());
        static std::string cmd_suffix = "\n\n";
        std::string        _cmd       = cmd;

        // check end
        if (cmd.compare(cmd.size() - 2, 2, "\n\n") == 0)
        {
            // end with "\n\n"
        }
        else if (cmd.compare(cmd.size() - 1, 1, "\n") == 0)
        {
            _cmd += "\n";
        }
        else
        {
            _cmd += "\n\n";
        }

        // insert into api queue
        ApiInfo entry{
            _cmd, std::make_shared<ApiCallbackImpl>(type)
        };

        this->api_queue_.push(entry);
        std::cout << "api queue size: " << this->api_queue_.size() << std::endl;

        return Callback{[this] { this->RealApi(); }, entry.api_callback.get()};
    }

    Callback SubscribeEvents(const SubscribeEventHelper& param)
    {
        return this->Send(param.GetEslCommand(), Callback::Type::Command);
    }

    // send message
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

    void Exit()
    {
        this->send_callback_("exit", 4);
    }

    void RegisterBgJob(const std::string& job_uuid, OnEventCallback&& _on_event_callback)
    {
        this->bg_jobs_.insert({job_uuid, std::move(_on_event_callback)});
        std::cout << "bg jobs size: " << this->bg_jobs_.size() << std::endl;
    }

private:
    void RealApi()
    {
        if (!current_api_.cmd.empty() || api_queue_.empty())
        {
            // has active event, next turn.
            // no api waiting, skip.
            std::cout << "RealApi return, " << !current_api_.cmd.empty() << " " << api_queue_.empty() << std::endl;
            return;
        }
        std::cout << "RealApi" << std::endl;

        current_api_ = api_queue_.front();
        api_queue_.pop();
        std::cout << "api queue size: " << this->api_queue_.size() << std::endl;

        assert(!current_api_.cmd.empty());
        assert(this->send_callback_);

        this->send_callback_(current_api_.cmd.c_str(), current_api_.cmd.size());
    }


    struct ApiInfo
    {
        std::string                      cmd;
        std::shared_ptr<ApiCallbackImpl> api_callback;
    };

    void HandleEslEvent(std::shared_ptr<EslEvent> ev)
    {
        auto content_type = ev->headers.find(ESL_EVENT_HEADER_CONTENT_TYPE);
        if (content_type == ev->headers.end())
        {
            Log(LogLevel::Error, "event", "event header not found" ESL_EVENT_HEADER_CONTENT_TYPE);
            this->on_error_callback_(Error::EventError);
            return;
        }

        auto str_content_type = content_type->second;


        if (str_content_type == ESL_EVENT_CONTENT_TYPE_AUTH)
        {
            this->HandleAuth(ev);
            return;
        }

        if (str_content_type == ESL_EVENT_CONTENT_TYPE_DISCONNECT)
        {
            if (this->disconnect_callback_)
            {
                this->disconnect_callback_();
            }
            return;
        }

        if (str_content_type == ESL_EVENT_CONTENT_TYPE_COMMAND_REPLY)
        {
            this->HandleCommandReply(ev);
            if (current_api_.cmd.empty())
            {
                this->RealApi();
            }
            return;
        }

        if (str_content_type == ESL_EVENT_CONTENT_TYPE_API_RESPONSE)
        {
            this->HandleApi(ev);
            if (current_api_.cmd.empty())
            {
                this->RealApi();
            }
            return;
        }

        if (str_content_type == ESL_EVENT_CONTENT_TYPE_TEXT_PLAIN)
        {
            this->HandleTextPlain(ev);
            return;
        }

        Log(LogLevel::Error, "event", "unknown content type : %s", str_content_type.c_str());
    }

    void TryLogin(const std::string& user, const std::string& passwd)
    {
        auto login_cb_ok = [=] (std::shared_ptr<EslEvent> ev)
        {
            auto content_type = ev->headers.find(ESL_EVENT_HEADER_CONTENT_TYPE);
            if (content_type == ev->headers.end())
            {
                this->Log(LogLevel::Error, "event", "message header not found" ESL_EVENT_HEADER_CONTENT_TYPE);
                this->on_error_callback_(Error::EventError);
                return;
            }

            if (content_type->second != ESL_EVENT_CONTENT_TYPE_COMMAND_REPLY)
            {
                this->Log(LogLevel::Error, "event", ("content type error: " + content_type->second));
                this->on_error_callback_(Error::EventError);
                return;
            }

            auto reply = ev->headers.find("reply-text");
            if (content_type == ev->headers.end())
            {
                this->Log(LogLevel::Error, "event", "message header reply-text not found");
                this->on_error_callback_(Error::EventError);
                return;
            }
            auto reply_text = reply->second;
            bool logged_in  = false;
            if (reply_text.compare(0, 3, "+OK") == 0)
            {
                // start with +ok
                logged_in = true;
            }
            assert(logged_in == true);
            if (this->on_logged_callback_)
            {
                this->on_logged_callback_(logged_in);
            }
        };
        auto login_cb_fail = [=] (std::shared_ptr<EslEvent> ev)
        {
            auto content_type = ev->headers.find(ESL_EVENT_HEADER_CONTENT_TYPE);
            if (content_type == ev->headers.end())
            {
                this->Log(LogLevel::Error, "event", "message header not found" ESL_EVENT_HEADER_CONTENT_TYPE);
                this->on_error_callback_(Error::EventError);
                return;
            }

            if (content_type->second != ESL_EVENT_CONTENT_TYPE_COMMAND_REPLY)
            {
                this->Log(LogLevel::Error, "event", "content type error: " + content_type->second);
                this->on_error_callback_(Error::EventError);
                return;
            }

            auto reply = ev->headers.find("reply-text");
            if (content_type == ev->headers.end())
            {
                this->Log(LogLevel::Error, "event", "message header reply-text not found");
                this->on_error_callback_(Error::EventError);
                return;
            }
            auto reply_text = reply->second;
            bool logged_in  = false;
            if (reply_text.compare(0, 3, "+OK") == 0)
            {
                // start with +ok
                logged_in = true;
            }
            assert(logged_in == false);
            if (this->on_logged_callback_)
            {
                this->on_logged_callback_(logged_in);
            }
        };

        if (user.empty())
        {
            this->Send("auth " + passwd, Callback::Type::Command).OnSuccess(login_cb_ok).OnFail(login_cb_fail);
        }
        else
        {
            this->Send("userauth " + user + ":" + passwd, Callback::Type::Command).OnSuccess(login_cb_ok).
                  OnFail(login_cb_fail);
        }
    }

    void HandleAuth(std::shared_ptr<EslEvent> ev)
    {
        // try login
        std::string user, passwd;
        if (on_logging_callback_)
        {
            on_logging_callback_(user, passwd);
        }
        if (user.empty() || passwd.empty())
        {
            Log(LogLevel::Warn, "auth", "user or passwd empty");
        }
        TryLogin(user, passwd);
    }

    void HandleApi(std::shared_ptr<EslEvent> ev)
    {
        if (this->current_api_.cmd.empty())
        {
            Log(LogLevel::Error, "api", "api not found");
            return;
        }
        if (this->current_api_.api_callback->type_ != Callback::Type::Api)
        {
            Log(LogLevel::Error, "api", "callback type is not api");
            return;
        }

        auto&& body = ev->body;
        if (body.compare(0, 4, "-ERR") == 0)
        {
            // fail
            if (this->current_api_.api_callback->on_fail_)
            {
                this->current_api_.api_callback->on_fail_(ev);
            }
        }
        else
        {
            // success
            if (this->current_api_.api_callback->on_ok_)
            {
                this->current_api_.api_callback->on_ok_(ev);
            }
        }

        // take next api
        this->current_api_ = {};
    }

    void HandleCommandReply(std::shared_ptr<EslEvent> ev)
    {
        if (this->current_api_.cmd.empty())
        {
            Log(LogLevel::Error, "command", "command not found");
            return;
        }
        if (this->current_api_.api_callback->type_ == Callback::Type::Api)
        {
            Log(LogLevel::Error, "command", "callback type is Api");
            return;
        }

        auto reply_text = ev->headers.find("reply-text");
        if (reply_text == ev->headers.end())
        {
            Log(LogLevel::Error, "command", "reply-text not found");
            return;
        }
        if (reply_text->second.compare(0, 4, "-ERR") == 0)
        {
            if (current_api_.api_callback && current_api_.api_callback->on_fail_)
                current_api_.api_callback->on_fail_(ev);
            // take next api
            this->current_api_ = {};
            return;
        }

        auto job_uuid = ev->headers.find("job-uuid");
        bool has_job  = job_uuid != ev->headers.end();
        bool is_job   = current_api_.api_callback->type_ == Callback::Type::BgApi;
        if (has_job != is_job)
        {
            Log(LogLevel::Error, "command", "has job %d, is job %d", has_job, is_job);
            return;
        }
        if (has_job && job_uuid->second.empty())
        {
            Log(LogLevel::Error, "command", "job uuid empty");
            return;
        }

        if (current_api_.api_callback->on_ok_)
        {
            current_api_.api_callback->on_ok_(ev);
        }
        else if (has_job && current_api_.api_callback->on_bg_job_)
        {
            RegisterBgJob(job_uuid->second, std::move(current_api_.api_callback->on_bg_job_));
        }

        // take next api
        this->current_api_ = {};
    }

    void HandleTextPlain(std::shared_ptr<EslEvent> ev)
    {
        text_plain_parser_.Feed(reinterpret_cast<const uint8_t*>(ev->body.c_str()), ev->body.size());
        auto&& err = text_plain_parser_.GetError();
        if (!err.empty())
        {
            Log(LogLevel::Error, "text-plain", err);
            return;
        }

        ev = text_plain_parser_.NextEvent();
        if (!ev)
        {
            Log(LogLevel::Error, "text-plain", "parse event fail.");
            return;
        }

        auto job_uuid = ev->headers.find("job-uuid");
        if (job_uuid == ev->headers.end())
        {
            if (on_event_callback_)
            {
                on_event_callback_(ev);
            }
            return;
        }
        else
        {
            auto job_uuid_str = job_uuid->second;
            auto job_it       = this->bg_jobs_.find(job_uuid_str);
            if (job_it != this->bg_jobs_.end() && job_it->second)
            {
                job_it->second(ev);
            }
            this->bg_jobs_.erase(job_it);
            std::cout << "bg jobs size: " << this->bg_jobs_.size() << std::endl;
            return;
        }
    }

    EslEventParser event_parser_{10240};
    EslEventParser text_plain_parser_;

    std::queue<ApiInfo> api_queue_;
    ApiInfo             current_api_;

    std::unordered_map<std::string, OnEventCallback> bg_jobs_;
};
} // namespace eslpp

#endif // ESLPP__ESL_CONTEXT_HPP


