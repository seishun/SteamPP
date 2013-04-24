#include <iostream>
#include <functional>
#include <uv.h>
#include "steam++.h"

using namespace Steam;

uv_tcp_t sock;
uv_timer_t timer;

std::string read_buffer;
std::string write_buffer;

std::string::size_type read_offset = 0;

extern SteamClient client;

SteamClient client(
	// write callback
	[](std::size_t length, std::function<void(unsigned char* buffer)> fill) {
		auto write = new uv_write_t;
		// TODO: check if previous write has finished
		write_buffer.resize(length);
		fill(reinterpret_cast<unsigned char*>(&write_buffer[0]));
		auto buf = uv_buf_init(&write_buffer[0], write_buffer.size());
		uv_write(write, (uv_stream_t*)&sock, &buf, 1, [](uv_write_t* req, int status) {
			delete req;
		});
	},
	// set_inverval callback
	[](std::function<void()> callback, int timeout) {
		auto callback_heap = new std::function<void()>(std::move(callback));
		timer.data = callback_heap;
		uv_timer_start(&timer, [](uv_timer_t* handle, int status) {
			auto callback = reinterpret_cast<std::function<void()>*>(handle->data);
			(*callback)();
			// TODO: delete it somewhere
		}, timeout * 1000, timeout * 1000);
	}
);

int main() {
	uv_tcp_init(uv_default_loop(), &sock);
	uv_timer_init(uv_default_loop(), &timer);
	
	auto &endpoint = servers[rand() % (sizeof(servers) / sizeof(servers[0]))];
	auto connect = new uv_connect_t;
	uv_tcp_connect(connect, &sock, uv_ip4_addr(endpoint.host, endpoint.port), [](uv_connect_t* req, int status) {
		auto length = client.connected();
		read_buffer.resize(length);
		uv_read_start(req->handle, [](uv_handle_t* handle, size_t suggested_size) {
			return uv_buf_init(&read_buffer[read_offset], read_buffer.size() - read_offset);
		}, [](uv_stream_t* stream, ssize_t nread, uv_buf_t buf) {
			if (nread < 1) {
				auto result = uv_last_error(uv_default_loop());
				auto str = uv_strerror(result);
			}
			read_offset += nread;
			if (read_offset == read_buffer.size()) {
				auto next_length = client.readable(reinterpret_cast<unsigned char*>(&read_buffer[0]));
				read_offset = 0;
				read_buffer.resize(next_length);
			}
		});
		delete req;
	});
	
	client.onHandshake = [] {
		client.LogOn("username", "password", "optional code");
	};
	
	client.onLogOn = [](EResult result) {
		if (result == EResult::OK) {
			std::cout << "logged on!" << std::endl;
			client.SetPersonaState(EPersonaState::Online);
			client.JoinChat(103582791432594962);
		}
	};
	
	client.onChatMsg = [](SteamID room, SteamID chatter, std::string message) {
		if (message == "ping") {
			client.SendChatMessage(room, "pong");
		}
	};
	
	return uv_run(uv_default_loop(), uv_run_mode::UV_RUN_DEFAULT);
}
