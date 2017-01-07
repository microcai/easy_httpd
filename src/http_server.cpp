
#include <systemd/sd-journal.h>
#include <boost/bind.hpp>
#include <boost/date_time.hpp>
using namespace boost::posix_time;

#include "http_server.hpp"

namespace http {

	http_server::http_server(boost::asio::io_service& ios)
		: m_io_service(ios)
		, m_listening(false)
		, m_timer(m_io_service)
	{
		m_listening = true;
		m_timer.expires_from_now(seconds(1));
		m_timer.async_wait(boost::bind(&http_server::on_tick, this, boost::asio::placeholders::error));
	}

	bool http_server::add_external_accepter(tcp::acceptor&& external_accepter)
	{
		boost::system::error_code ignore_ec;
		external_accepter.listen(boost::asio::socket_base::max_connections, ignore_ec);
		if (ignore_ec)
		{
			sd_journal_print(LOG_ERR, "HTTP Server listen failed: %s", ignore_ec.message());
			return false;
		}
		std::shared_ptr<tcp::acceptor> a(new tcp::acceptor(std::move(external_accepter)));
		m_acceptors.push_back(a);
		m_listening = true;
		return true;
	}


	bool http_server::add_listen(short unsigned int port, std::__cxx11::string address)
	{
		boost::asio::ip::tcp::resolver resolver(m_io_service);
		std::ostringstream port_string;
		port_string.imbue(std::locale("C"));
		port_string << port;
		boost::system::error_code ignore_ec;
		boost::asio::ip::tcp::resolver::query query(address, port_string.str());
		boost::asio::ip::tcp::resolver::iterator endpoint_iterator = resolver.resolve(query, ignore_ec);
		if (ignore_ec)
		{
			sd_journal_print(LOG_ERR, "HTTP Server bind address, DNS resolve failed: %s , address: %s", ignore_ec.message(), address.c_str());
			return false;
		}
		boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;

		auto acceptor = std::make_shared<boost::asio::ip::tcp::acceptor>(std::ref(m_io_service));
		acceptor->open(endpoint.protocol(), ignore_ec);
		if (ignore_ec)
		{
			sd_journal_print(LOG_ERR, "HTTP Server open protocol failed: %s", ignore_ec.message());
			return false;
		}
		acceptor->set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), ignore_ec);
		if (ignore_ec)
		{
			sd_journal_print(LOG_ERR, "HTTP Server set option failed: %s", ignore_ec.message());
			return false;
		}
		acceptor->bind(endpoint, ignore_ec);
		if (ignore_ec)
		{
			sd_journal_print(LOG_ERR, "HTTP Server bind failed: %s , address: %s", ignore_ec.message(), address.c_str());
			return false;
		}
		acceptor->listen(boost::asio::socket_base::max_connections, ignore_ec);
		if (ignore_ec)
		{
			sd_journal_print(LOG_ERR, "HTTP Server listen failed: %s", ignore_ec.message());
			return false;
		}
		m_acceptors.push_back(acceptor);
		m_listening = true;
		return true;
	}

	http_server::~http_server()
	{}

	void http_server::start()
	{
		if (!m_listening) return;

		for (int i =0; i < m_acceptors.size(); i++)
		{
			auto con = boost::make_shared<http_connection>(boost::ref(m_io_service), boost::ref(*this), &m_connection_manager);
			m_connections.push_back(con);
			m_acceptors[i]->async_accept(con->socket(), boost::bind(&http_server::handle_accept, this, boost::asio::placeholders::error, i));
		}
	}

	void http_server::stop()
	{
		for (auto& a : m_acceptors)
			a->close();
		m_connection_manager.stop_all();
		boost::system::error_code ignore_ec;
		m_timer.cancel(ignore_ec);
	}

	void http_server::handle_accept(const boost::system::error_code& error, int idx)
	{
		auto a = m_acceptors[idx];
		if (!a->is_open() || error)
		{
			if (error)
			{
				sd_journal_print(LOG_ERR, "http_server::handle_accept, error: %s", error.message());
				return;
			}
		}

		m_connection_manager.start(m_connections[idx]);

		m_connections[idx] = boost::make_shared<http_connection>(boost::ref(m_io_service), boost::ref(*this), &m_connection_manager);
		a->async_accept(m_connections[idx]->socket(), boost::bind(&http_server::handle_accept, this, boost::asio::placeholders::error, idx));
	}

	void http_server::on_tick(const boost::system::error_code& error)
	{
		if (error) return;

		m_connection_manager.tick();

		m_timer.expires_from_now(seconds(1));
		m_timer.async_wait(boost::bind(&http_server::on_tick, this, boost::asio::placeholders::error));
	}

	bool http_server::handle_request(const request& req, http_connection_ptr conn)
	{
		// 根据 URI 调用不同的处理.
		const std::string& uri = req.uri;
		boost::shared_lock<boost::shared_mutex> l(m_request_callback_mtx);
		auto iter = m_http_request_callbacks.find(uri);
		if (iter == m_http_request_callbacks.end())
			return false;
		iter->second(req, conn, boost::ref(m_connection_manager));
		return true;
	}

	bool http_server::add_uri_handler(const std::string& uri, http_request_callback cb)
	{
		boost::unique_lock<boost::shared_mutex> l(m_request_callback_mtx);
		if (m_http_request_callbacks.find(uri) != m_http_request_callbacks.end())
		{
			BOOST_ASSERT("module already exist!" && false);
			return false;
		}
		m_http_request_callbacks[uri] = cb;
		return true;
	}

}
