
#include <list>
#include <thread>
#include <iostream>
#include <systemd/sd-daemon.h>

#include <boost/program_options.hpp>
namespace po = boost::program_options;

#include "http_server.hpp"


static void terminator(boost::asio::io_service& ios, http::http_server& http_serv)
{
	http_serv.stop();
	ios.stop();
}

int main(int argc, char** argv)
{

	sd_notify(0, "STATUS=checking parameters");

	try
	{
		unsigned short http_port = 0;
		po::options_description desc("options");
		desc.add_options()
			("help,h", "help message")
			("version", "current avrouter version")
			("httpport", po::value<unsigned short>(&http_port)->default_value(4001), "http listen port")
			;

		po::variables_map vm;
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);

		if (vm.count("help"))
		{
			std::cout << desc << "\n";
			return 0;
		}

		sd_notify(0, "STATUS=creating http daemon");

		boost::asio::io_service io_service;
		// 创建 http 服务器.
		http::http_server http_serv(io_service);

		auto systemd_fds = sd_listen_fds(0);

		if (systemd_fds > 0)
		{
			sd_journal_print(LOG_DEBUG, "sspay httpd: socket activated, using socket fd: %d", systemd_fds-1);
			auto proto = boost::asio::ip::tcp::v6();
			for (int i=0; i < systemd_fds; i++)
				http_serv.add_external_accepter(boost::asio::ip::tcp::acceptor(io_service, proto, SD_LISTEN_FDS_START+i));
		}
		else
		{
			http_serv.add_listen(http_port);
		}

		// 愚弄傻逼, 让他们以为服务器是 jsp 的.
		/*
		 * TODO, code here
		 http_serv.add_uri_handler("/api/create.jsp", boost::bind(&sspay::handle_create_request, &sspay_module, _1, _2, _3));
		http_serv.add_uri_handler("/api/alipay/show.jsp", boost::bind(&sspay::handle_alipay_callback, &sspay_module, _1, _2, _3));
		*/
		// 启动 HTTPD.
		http_serv.start();

		// Ctrl+c异步处理退出.
		boost::asio::signal_set terminator_signal(io_service);
		terminator_signal.add(SIGINT);
		terminator_signal.add(SIGTERM);
#if defined(SIGQUIT)
		terminator_signal.add(SIGQUIT);
#endif // defined(SIGQUIT)
		terminator_signal.async_wait(boost::bind(&terminator, boost::ref(io_service), boost::ref(http_serv)));

		// 开始启动整个系统事件循环.
		std::list<std::thread> io_threads;
		for (int i=0; i < 8; i++)
		{
			io_threads.push_back(
				std::thread(boost::bind(&boost::asio::io_service::run, &io_service))
			);
		}

		sd_notify(0, "READY=1\nSTATUS=SSPay Ready.");

		std::for_each(std::begin(io_threads), std::end(io_threads), std::bind(&std::thread::join, std::placeholders::_1));
	}
	catch (std::exception& e)
	{
		sd_journal_print(LOG_EMERG, "main exception: %s", e.what());
		std::cerr << "main exception: " << e.what();
		return -1;
	}
	return 0;

}
