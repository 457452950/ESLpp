#include "Eslpp/EslService.hpp"

using namespace std;

class EslText : public eslpp::EslService
{
public:
    explicit EslText(asio::any_io_executor io_executor)
        : EslService(io_executor)
          , timer(io_executor)
    {
        this->SetOnEslLoggedCallback([=] (bool success)
        {
            cout << "OnEslLoggedCallback " << success << endl;
            if (success)
            {
                eslpp::SubscribeEventHelper h;
                h.Event("all");
                this->SubscribeEvents(h).OnSuccess([] (shared_ptr<eslpp::EslEvent> ev)
                {
                    cout << "OnSubscribeEvents.OnSuccess" << endl;
                    DumpEslEvent(*ev);
                });

                timer.expires_after(3000ms);
                timer.async_wait([&] (asio::error_code ec)
                {
                    this->OnTimeout(ec);
                });
            }
        });
        this->SetOnEventCallback([=] (std::shared_ptr<eslpp::EslEvent> ev)
        {
            cout << "OnEventCallback " << endl;
            DumpEslEvent(*ev);
        });
    }

    void OnTimeout(asio::error_code ec)
    {
        if (ec)
        {
            cout << "OnTimeout err: " << ec.message() << endl;
            return;
        }
        cout << "OnTimeout" << endl;
        this->Api("list_users").OnSuccess([] (std::shared_ptr<eslpp::EslEvent> ev)
        {
            cout << "list_users.OnSuccess" << endl;
            DumpEslEvent(*ev);
        });
        this->BgApi("list_users").OnBgJob([] (std::shared_ptr<eslpp::EslEvent> ev)
        {
            cout << "list_users.OnBgJob" << endl;
            DumpEslEvent(*ev);
        });

        timer.expires_after(3000ms);
        timer.async_wait([&] (asio::error_code ec)
        {
            this->OnTimeout(ec);
        });
    }

    asio::system_timer timer;
};

int main()
{
    asio::io_context context;

    auto service = make_shared<EslText>(context.get_executor());
    service->Connect(asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 8021), "", "ClueCon");


    context.run();
    return 0;
}

