#include <cassert>

#include <iostream>
#include <set>

#include <ESLpp/EslContext.hpp>

void test_url_decode()
{
    using namespace std;
    using tools = eslpp::tools;

    // 基本解码
    assert(tools::url_decode("hello%20world") == "hello world");
    assert(tools::url_decode("a%2Bb%3Dc") == "a+b=c");

    // 特殊字符处理
    assert(tools::url_decode("%21%40%23") == "!@#");
    assert(tools::url_decode("%5C%25%2F") == "\\%/");

    // + 号处理
    assert(tools::url_decode("a+b+c") == "a b c");
    assert(tools::url_decode("%2B+%2B") == "+ +");

    // 边界和无效编码
    assert(tools::url_decode("%") == "%");
    assert(tools::url_decode("%1") == "%1");
    assert(tools::url_decode("%g0") == "%g0");
    assert(tools::url_decode("100%25") == "100%");

    // 中文字符 (UTF-8)
    assert(tools::url_decode("%E4%BD%A0%E5%A5%BD") == "\xE4\xBD\xA0\xE5\xA5\xBD");
    // 中文字符 (UTF-8)
    assert(tools::url_decode("%E4%BD%A0%E5%A5%BD") == "你好");

    // 保留无效编码
    assert(tools::url_decode("a%xxb") == "a%xxb");
    // 空字符串
    assert(tools::url_decode("") == "");

    cout << "All tests passed!" << endl;
}

void test_execute()
{
    using namespace std;
    using namespace eslpp;

    EslContext context;
    string     result;
    context.SetSendHandler([&] (const char* data, size_t len)
    {
        result = data;
    });

    context.Execute("playback", "/tmp/test.wav", "<uuid>");
    cout << result << endl;

    context.SendMsg("<uuid>", std::map<string, string>(), "abc");
    cout << result << endl;
}

void test_events()
{
    using namespace std;
    using namespace eslpp;
    {
        SubscribeEventHelper param;
        param.Event("all");
        cout << param.GetEslCommand() << endl;
        assert(param.GetEslCommand() == "event JSON all");
    }
    {
        //event plain CHANNEL_CREATE CHANNEL_DESTROY CUSTOM conference::maintenance sofia::register sofia::expire
        SubscribeEventHelper param;
        param.Events(std::set<std::string>{"CHANNEL_CREATE", "CHANNEL_DESTROY"});
        param.CustomEvents(std::unordered_set<std::string>{
                               "conference::maintenance", "sofia::register", "sofia::expire"
                           });

        cout << param.GetEslCommand() << endl;
    }
    {
        SubscribeEventHelper param;
        param.Event("DTMF");
        cout << param.GetEslCommand() << endl;
        assert(param.GetEslCommand() == "event PLAIN DTMF");
    }
}

void test_parse_event()
{
    using namespace std;
    using namespace eslpp;
    auto dump_event_cb = [] (EslContext* ctx, std::shared_ptr<EslEvent> event)
    {
        for (auto&& it : event->headers)
        {
            printf("\t[%s]:[%s]\n", it.first.c_str(), it.second.c_str());
        }
        if (!event->body.empty())
        {
            printf("[\n%s\n]\n", event->body.c_str());
        }
    };
    auto log_cb = [] (EslContext*, LogLevel level, const std::string& tag,
                      const std::string&    message)
    {
        printf("[%s]:[%s]\n", tag.c_str(), message.c_str());
    };

    EslContext context;
    context.SetOnEventCallback(dump_event_cb);
    context.SetLogHandler(log_cb);
    {
        static char data[] = "Speech-Type: detected-speech\n"
                "Event-Name: DETECTED_SPEECH\n"
                "Core-UUID: aac0f73e-b822-e54c-a02a-06a839ca3e5a\n"
                "FreeSWITCH-Hostname: AMONROY\n"
                "FreeSWITCH-IPv4: 192.168.1.220\n"
                "FreeSWITCH-IPv6: ::1\n"
                "Event-Date-Local: 2009-01-26 16:07:24\n"
                "Event-Date-GMT: Mon, 26 Jan 2009 22:07:24 GMT\n"
                "Event-Date-Timestamp: 1233007644906250\n"
                "Event-Calling-File: switch_ivr_async.c\n"
                "Event-Calling-Function: speech_thread\n"
                "Event-Calling-Line-Number: 1758\n"
                "Content-Length: 404\n"
                "\n"
                "<result grammar=\"<request1@form-level.store>#nombres\">\n"
                "	<interpretation grammar=\"<request1@form-level.store>#nombres\" confidence=\"0.494643\">\n"
                "		<instance confidence=\"0.494643\">\n"
                "			arturo monroy\n"
                "		</instance>\n"
                "		<input mode=\"speech\" confidence=\"0.494643\">\n"
                "			<input confidence=\"0.313102\">\n"
                "				arturo\n"
                "			</input>\n"
                "			<input confidence=\"0.618854\">\n"
                "				monroy\n"
                "			</input>\n"
                "		</input>\n"
                "	</interpretation>\n"
                "</result>";

        context.Feed(reinterpret_cast<const uint8_t*>(data), sizeof(data) - 1);
    }
    {
        static char data[] =
                "Job-UUID: 7f4db78a-17d7-11dd-b7a0-db4edd065621\n"
                "Job-Command: originate\n"
                "Job-Command-Arg: sofia/default/1005%20'%26park'\n"
                "Event-Name: BACKGROUND_JOB\n"
                "Core-UUID: 42bdf272-16e6-11dd-b7a0-db4edd065621\n"
                "FreeSWITCH-Hostname: ser\n"
                "FreeSWITCH-IPv4: 192.168.1.104\n"
                "FreeSWITCH-IPv6: 127.0.0.1\n"
                "Event-Date-Local: 2008-05-02%2007%3A37%3A03\n"
                "Event-Date-GMT: Thu,%2001%20May%202008%2023%3A37%3A03%20GMT\n"
                "Event-Date-timestamp: 1209685023894968\n"
                "Event-Calling-File: mod_event_socket.c\n"
                "Event-Calling-Function: api_exec\n"
                "Event-Calling-Line-Number: 609\n"
                "Content-Length: 41\n"
                "\n"
                "+OK 7f4de4bc-17d7-11dd-b7a0-db4edd065621\n";

        context.Feed(reinterpret_cast<const uint8_t*>(data), sizeof(data) - 1);
    }
}

int main()
{
    test_url_decode();
    test_execute();
    test_events();
    test_parse_event();
    return 0;
}
