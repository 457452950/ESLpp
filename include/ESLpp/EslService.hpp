#pragma once
#ifndef ESLPP__ESL_SERVICE_H
#define ESLPP__ESL_SERVICE_H
#include <iostream>

#if defined(USE_STANDALONE_ASIO)
#include <asio.hpp>
namespace asio = ::asio;
#else
#include <boost/asio.hpp>

namespace asio
{
using namespace boost::asio;
using error_code = boost::system::error_code;
}
#endif

#include "EslContext.hpp"

static void DumpEslEvent(const eslpp::EslEvent& ev)
{
    using namespace std;
    cout << "============ event start ============\n";
    for (auto&& it : ev.headers)
    {
        cout << "[" << it.first << "]:[" << it.second << "]\n";
    }
    if (!ev.body.empty())
    {
        cout << "\n[" << ev.body << "]" << endl;
    }
    cout << "============ event end ============\n";
}

namespace eslpp
{
class EslService : public std::enable_shared_from_this<EslService>
{
    /*******************
     *      Service
     *******************/
public:
    explicit EslService(asio::any_io_executor io_executor) : socket_(std::move(io_executor))
    {
        // esl_context_.SetOnEventCallback([=] (std::shared_ptr<EslEvent> ev)
        // {
        //     DumpEslEvent(*ev);
        // });
        esl_context_.SetDisconnectCallback([]()
        {
            std::cout << "Disconnect" << std::endl;
        });
        esl_context_.SetOnLoggedCallback([] (bool success)
        {
            std::cout << "OnLogged " << success << std::endl;
        });
        esl_context_.SetOnErrorCallback([=] (Error err)
        {
            std::cout << "OnError" << std::endl;
        });
        esl_context_.SetLogHandler([] (LogLevel level, const std::string& tag, const std::string& message)
        {
            std::cout << tag << " " << message << std::endl;
        });
        esl_context_.SetSendHandler([=] (const char* data, size_t len)
        {
            std::cout << "send " << data << std::endl;
            asio::error_code ec;
            socket_.send(asio::buffer(data, len), 0, ec);
            if (ec)
            {
                std::cout << "Send failed, err: " << ec << std::endl;
            }
        });
    }

    ~EslService()
    {
    }

    bool Listen(asio::ip::tcp::endpoint endpoint);

    void Connect(asio::ip::tcp::endpoint endpoint, const std::string& user, const std::string& password)
    {
        esl_context_.SetLoggingCallback([=] (auto& _user, auto& _passwd)
        {
            _user   = user;
            _passwd = password;
        });

        socket_.async_connect(endpoint, [_this = shared_from_this()] (auto err) { _this->OnConnected(err); });
    }

    using OnConnectedCallback = std::function<void()>;

    void SetOnConnectedCallback(OnConnectedCallback&& _on_connected_callback)
    {
        on_connected_callback_ = std::move(_on_connected_callback);
    }

    using ErrorCallback = std::function<void(asio::error_code)>;

    void SetErrorCallback(ErrorCallback&& _disconnect_callback)
    {
        this->error_callback_ = std::move(_disconnect_callback);
    }

private:
    void OnConnected(asio::error_code err)
    {
        if (err)
        {
            std::cout << "OnConnected " << err << " " << err.message() << std::endl;
            return;
        }
        this->ReadSome();
    }

    void ReadSome()
    {
        auto buf = esl_context_.GetReceiveBuffer();
        std::cout << buf.capacity << std::endl;
        socket_.async_read_some(asio::buffer(buf.data, buf.capacity),
                                [_this = shared_from_this()] (auto err, auto bytes_read)
                                {
                                    _this->OnReadSome(err, bytes_read);
                                });
    }

    void OnReadSome(asio::error_code err, size_t bytes_transferred)
    {
        if (err)
        {
            std::cout << "OnReadSome " << err << " " << err.message() << std::endl;
            return;
        }
        std::cout << bytes_transferred << std::endl;
        this->esl_context_.CommitReceivedData(bytes_transferred);
        this->ReadSome();
    }

private:
    asio::ip::tcp::socket socket_;
    ErrorCallback         error_callback_;
    OnConnectedCallback   on_connected_callback_;

    /*******************
     *      Esl
     *******************/
public:
    void SetOnEventCallback(EslContext::OnEventCallback _on_event_callback)
    {
        esl_context_.SetOnEventCallback(std::move(_on_event_callback));
    }

    void SetOnEslLoggedCallback(EslContext::OnLoggedCallback&& _on_connected_callback)
    {
        esl_context_.SetOnLoggedCallback(std::move(_on_connected_callback));
    }

    using EslDisconnectCallback = EslContext::DisconnectCallback;

    void SetEslDisconnectCallback(EslDisconnectCallback&& _disconnect_callback)
    {
        esl_context_.SetDisconnectCallback(std::move(_disconnect_callback));
    }

    using Callback = EslContext::Callback;

    Callback Api(const std::string& cmd)
    {
        assert(!cmd.empty());
        return esl_context_.Api(cmd);
    }

    Callback BgApi(const std::string& cmd, const std::string& job_uuid = "")
    {
        assert(!cmd.empty());
        return esl_context_.BgApi(cmd);
    }

    Callback Send(const std::string& cmd, Callback::Type type)
    {
        assert(!cmd.empty());
        return esl_context_.Send(cmd, type);
    }

    Callback SubscribeEvents(const SubscribeEventHelper& param)
    {
        return esl_context_.SubscribeEvents(param);
    }

    // send message
    template <typename T>
    void SendMsg(const std::string& uuid, T&& headers, const std::string& body = "")
    {
        esl_context_.SendMsg(uuid, std::forward<T>(headers), body);
    }

    void Execute(const std::string& app, const std::string& args, const std::string& uuid, bool async = true)
    {
        esl_context_.Execute(app, args, uuid, async);
    }

    void Exit()
    {
        esl_context_.Exit();
    }

    void RegisterBgJob(const std::string& job_uuid, EslContext::OnEventCallback&& _on_event_callback)
    {
        esl_context_.RegisterBgJob(job_uuid, std::move(_on_event_callback));
    }

private:
    EslContext esl_context_;
};
}

#endif // ESLPP__ESL_SERVICE_H
