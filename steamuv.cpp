#include <cassert>
#include <chrono>
#include <iostream>
#include <functional>
#include <memory>
#include <set>
#include <string>

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

#include "steam++.h"

using namespace Steam;

QNetworkAccessManager *nam;
QTcpSocket *sock;
QTimer *timer;

QMetaObject::Connection m_connection;

std::string read_buffer;
std::string write_buffer;

std::string::size_type read_offset = 0;

void http_request(
	const char* uri,
	const char* method,
	std::vector<std::pair<const char*, const char*>> headers,
	std::vector<std::tuple<const char*, const unsigned char*, std::size_t>> query,
	std::function<void(int status_code, std::string &body)> callback
) {
	QUrlQuery uq;

	for (auto &tuple : query) {
		QByteArray ba(reinterpret_cast<const char*>(std::get<1>(tuple)), std::get<2>(tuple));
		uq.addQueryItem(std::get<0>(tuple), ba.toPercentEncoding());
	}

	auto byteArray = new QByteArray;
	byteArray->append(uq.query(QUrl::FullyEncoded));
	auto buffer = new QBuffer(byteArray);

	QNetworkRequest request;
	auto url = QUrl(uri);
	request.setUrl(QUrl(uri));
	for (auto &pair : headers)
		request.setRawHeader(pair.first, pair.second);

	QNetworkReply *reply = nam->sendCustomRequest(request, method, buffer);
	QObject::connect(reply, &QNetworkReply::finished, [url, callback, byteArray, buffer, reply] {
		auto e = reply->error();
		std::string body;
		auto ready_read = reply->bytesAvailable();
		body.resize(ready_read);
		reply->read(&body[0], ready_read);
		callback(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), body);

		reply->deleteLater();
		delete buffer;
		delete byteArray;
	});
}

extern SteamClient client;
CSClientPool* pool;

SteamClient client(
	// write callback
	[](std::size_t length, std::function<void(unsigned char* buffer)> fill) {
		// TODO: check if previous write has finished
		write_buffer.resize(length);
		fill(reinterpret_cast<unsigned char*>(&write_buffer[0]));
		sock->write(&write_buffer[0], write_buffer.size());
	},
	// set_inverval callback
	[](std::function<void()> callback, int timeout) {
		timer = new QTimer;
		QObject::connect(timer, &QTimer::timeout, callback);
		timer->start(timeout * 1000);
	}
);

std::map<std::uint32_t, std::string> app_tickets;
std::map<std::uint32_t, std::string> depot_keys;
std::map<std::uint32_t, std::uint64_t> depot_manifests;

std::vector<std::string> CSs; // in case app ticket arrives later than server list

std::uint64_t download_total;
std::uint64_t download_done;

// in case a chunk is used in more than one file, avoid downloading it more than once
std::map<std::string, std::set<std::pair<std::string, std::uint64_t>>> chunk_usage;
//       chunk ID                 path          offset

void get_manifests(std::uint32_t depotid) {
	assert(pool);
	std::cout << "Have key, pool and ticket, getting manifests for " + std::to_string(depotid) << std::endl;
	pool->DownloadDepotManifest(depotid, depot_manifests[depotid], app_tickets[depotid], depot_keys[depotid], [=](std::vector<CSClientPool::FileData> &files) {
		qDebug() << QDateTime::currentDateTime() << "recieved manifest with " << files.size() << " fails\n";
		std::sort(files.begin(), files.end(), [](const CSClientPool::FileData &a, const CSClientPool::FileData &b) {
			return a.filename < b.filename;
		});
		QDir().mkdir("dltest");
		auto root = QDir("dltest");
		for (auto &file : files) {
			if (file.flags == EDepotFileFlag::Directory) {
				continue; // completely useless, maybe remove them completely
			}

			auto filename = file.filename;
			auto dir = QFileInfo(root, filename.c_str()).dir();
			if (!dir.exists()) {
				QDir().mkpath(dir.absolutePath());
			}

			for (auto &chunk : file.chunks) {
				auto &usages = chunk_usage[chunk.chunk_ID];
				usages.emplace(file.filename, chunk.offset);
				if (usages.size() > 1) {
					// was already queued, no need to download more than once
					continue;
				}

				download_total += chunk.size;
				
				pool->DownloadDepotChunk(depotid, chunk.chunk_ID, app_tickets[depotid], depot_keys[depotid], [root, chunk, filename](std::string data) {
					assert(chunk_usage.count(chunk.chunk_ID));
					auto &usages = chunk_usage[chunk.chunk_ID];
					assert(usages.size() > 0);
					for (auto &usage : usages) {
						QFile f;
						f.setFileName(root.filePath(usage.first.c_str()));
						f.open(QIODevice::ReadWrite);
						if (!f.isOpen()) {
							qWarning() << f.errorString();
						}
						f.seek(usage.second);
						auto written = f.write(data.data(), data.size());
						//assert(written == data.size());
					}
					chunk_usage.erase(chunk.chunk_ID);
					download_done += chunk.size;
					std::cout << "Downloaded " << download_done << " out of " << download_total << std::endl;
					if (download_done == download_total) {
						QCoreApplication::quit();
					}
				});
			}
		}
	});
}

void connect() {
	auto &endpoint = servers[rand() % (sizeof(servers) / sizeof(servers[0]))];
	sock = new QTcpSocket;
	sock->connectToHost(endpoint.host, endpoint.port);
	QObject::connect(sock, &QTcpSocket::connected, [] {
		auto length = client.connected();
		read_buffer.resize(length);
	});
	m_connection = QObject::connect(sock, &QTcpSocket::readyRead, [] {
		qint64 bytes_read;
		while (bytes_read = sock->read(&read_buffer[read_offset], read_buffer.size() - read_offset)) {
			if (bytes_read < 0) {
				return;
			}
			read_offset += bytes_read;
			if (read_offset == read_buffer.size()) {
				auto next_length = client.readable(reinterpret_cast<unsigned char*>(&read_buffer[0]));
				read_offset = 0;
				read_buffer.resize(next_length);
			}
		}
	});
}

int main(int argc, char *argv[]) {
	QCoreApplication a(argc, argv);

	std::vector<std::string> args(argv, argv + argc);

	QByteArray sentry;
	if (QDir().exists("sentry")) {
		QFile f("sentry");
		f.open(QIODevice::ReadOnly);
		sentry = f.readAll();
	}

	if (argc < 4) {
		std::cout << "Need login, password, depot to download and optional steam guard code" << std::endl;
		return 1;
	}

	connect();
	
	client.onHandshake = [&args, &sentry] {
		if (args.size() == 5) {
			if (sentry.size())
				client.LogOn(args[1].c_str(), args[2].c_str(), (const unsigned char*)sentry.constData(), args[4].c_str());
			else 
				client.LogOn(args[1].c_str(), args[2].c_str(), nullptr, args[4].c_str());
		}
		else {
			if (sentry.size())
				client.LogOn(args[1].c_str(), args[2].c_str(), (const unsigned char*)sentry.constData());
			else
				client.LogOn(args[1].c_str(), args[2].c_str());
		}
	};
	
	client.onLogOn = [&args](EResult result, SteamID steamID) {
		if (result == EResult::OK) {
			std::cout << "logged on!" << std::endl;
			client.SetPersonaState(EPersonaState::Online);
			//client.JoinChat(103582791432594962);
			client.GetAppOwnershipTicket(7);
			client.PICSGetProductInfo(std::stoul(args[3]));

			CSClientPool::FetchServerList("cs.steampowered.com", 80, 66, http_request, [](std::vector<std::string> &server_list) {
				std::cout << "Got servers list" << std::endl;
				if (app_tickets.count(7)) {
					std::cout << "Connecting..." << std::endl;
					//server_list.erase(server_list.begin());
					pool = new CSClientPool(server_list, reinterpret_cast<const unsigned char*>(app_tickets[7].data()), app_tickets[7].length(), http_request);
					if (depot_keys.size()) {
						std::cout << "server list arrived late depot keys already waiting" << std::endl;
						for (auto &depotid : depot_keys) {
							if (app_tickets.count(depotid.first)) {
								get_manifests(depotid.first);
							} // else we'll get manifests when we get app ticket
						}
					}
				} else {
					CSs = std::move(server_list);
				}
			});
		}
		else {
			if (result == EResult::AccountLogonDenied) {
				std::cout << "Provide Steam Guard code on your command line.";
			}
			else {
				std::cout << "Logon error.";
			}
			QCoreApplication::quit();
		}
	};

	client.onAppOwnershipTicket = [](EResult result, std::uint32_t appID, std::string ticket) {
		std::cout << "Got app ownership ticket for " + std::to_string(appID) << std::endl;
		assert(result == EResult::OK);
		app_tickets[appID] = ticket;
		if (appID == 7 && CSs.size()) {
			// app ticket arrived later than server list
			assert(!pool);
			std::cout << "Connecting late..." << std::endl;
			pool = new CSClientPool(CSs, reinterpret_cast<const unsigned char*>(ticket.data()), ticket.length(), http_request);
		} else if (depot_keys.count(appID) && pool) {
			// we were waiting for this - depot key arrived first
			get_manifests(appID);
		}
	};

	client.onDepotKey = [](EResult result, std::uint32_t depotID, std::string depot_key) {
		std::cout << "Got depot key for " + std::to_string(depotID) << std::endl;
		assert(result == EResult::OK);
		depot_keys[depotID] = depot_key;
		assert(depot_manifests.count(depotID));
		if (app_tickets.count(depotID) && pool) {
			// we were waiting for this - app ticket arrived first
			get_manifests(depotID);
		}
		
	};

	client.onPICSProductInfo = [](std::uint32_t appID, std::string &key_values) {
		std::cout << "Got info about " + std::to_string(appID) << std::endl;
		auto depots = *(*(*key_values)["appinfo"])["depots"];
		/*
		"overridescddb"		"1"
		"markdlcdepots"		"1"
		"571"
		{
			"name"		"dota 2 beta content"
			"systemdefined"		"1"
			"manifests"
			{
				"public"		"7651229287868284304"
			}
			"maxsize"		"6656695963"
		}
		"572"
		{
			"name"		"dota 2 beta client"
			"systemdefined"		"1"
			"manifests"
			{
				"public"		"7497473213787319611"
			}
		}
		*/
		for (auto &depot : depots) {
			char *str_end;
			auto depotid = std::strtoul(depot.first.c_str(), &str_end, 10);
			if (str_end != depot.first.c_str() + depot.first.length()) {
				continue;
			}
			auto data = *depot.second;

			if (data.count("config")) {
				auto config = *data["config"];
				if (config.count("language") && config["language"] != "english") {
					std::cout << "skipping " << config["language"] << " depot (" << depot.first << ")" << std::endl;
					continue;
				}
				if (config.count("oslist") && config["oslist"] != "windows") {
					std::cout << "skipping " << config["oslist"] << " depot (" << depot.first << ")" << std::endl;
					continue;
				}
			}
			// save manifest id
			depot_manifests[depotid] = std::stoull((*data["manifests"])["public"]);
			client.GetAppOwnershipTicket(depotid);
			client.GetDepotDecryptionKey(depotid);
			std::cout << "Requesting app ticket and depot key for " + depot.first << std::endl;
		}
	};

	client.onSentry = [](const unsigned char hash[20]) {
		QFile sentry("sentry");
		sentry.open(QIODevice::WriteOnly);
		sentry.write((const char*)hash, 20);
	};

	nam = new QNetworkAccessManager;
	
    return a.exec();
}
