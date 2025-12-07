#include <coral/nursery.hpp>
#include <coral/sync_wait.hpp>
#include <coral/task.hpp>
#include <coral/when_any.hpp>
#include <coral/when_signal.hpp>
#include <coral/when_stopped.hpp>

#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

coral::task<> client(int i, std::stop_token stop_token)
{
    std::cout << "Client " << i << " started\n";
    co_await coral::when_stopped(stop_token);
    std::cout << "Client " << i << " finished\n";
}

coral::nursery_task<bool> server(std::stop_token stop_token)
{
    std::stop_source server_stop_source;
    std::stop_callback callback(
        stop_token, [&server_stop_source] { server_stop_source.request_stop(); });

    auto nursery = co_await coral::get_nursery();

    std::cout << "server started\n";

    int i = 0;
    while (!stop_token.stop_requested()) {
        std::cout << "server is waiting for a connection\n";
        i += 1;
        if (i == 5) { // as if we got an error
            std::cout << "server got an network error\n";
            server_stop_source.request_stop();
            co_return false;
        }
        std::this_thread::sleep_for(1s);
        std::cout << "server got the connection " << i << "\n";
        // Add client coroutine to the nursery collection
        nursery.start(client(i, server_stop_source.get_token()));
    }
    std::cout << "user requested stop\n";
    co_return true;
}

int main()
{
    std::stop_source stop_source;
    auto [index, success] = coral::sync_wait(coral::when_any(stop_source,
        coral::when_signal(stop_source.get_token(), SIGINT),
        server(stop_source.get_token())));

    if (index == 1 && !std::get<bool>(success)) {
        std::cout << "Some network error occured\n";
        return 1;
    }
    return 0;
}
